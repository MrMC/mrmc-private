/*
 *      Copyright (C) 2019 Team MrMC
 *      http://mrmc.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "AudioSinkAVFoundation.h"

#include "DVDClock.h"
#include "DVDCodecs/Audio/DVDAudioCodec.h"
#include "cores/AudioEngine/AEFactory.h"
#include "cores/AudioEngine/Interfaces/AEStream.h"
#include "cores/AudioEngine/Utils/AEAudioFormat.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "platform/darwin/DarwinUtils.h"
#include "threads/SingleLock.h"
#include "utils/BitstreamReader.h"
#include "utils/log.h"

#define AVMediaType AVMediaType_fooo
#import <AVFoundation/AVFoundation.h>
#undef AVMediaType
#import <AVFoundation/AVAudioSession.h>

#pragma mark - AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AVPlayerSink : NSObject <AVAssetResourceLoaderDelegate>
- (id)initWithFrameSize:(unsigned int)frameSize;
- (int)addPackets:(uint8_t*)data size:(unsigned int)size;
- (void)play:(bool) state;
- (void)drain;
- (void)flush;
- (void)stopBuffering;
- (void)startBuffering:(AVCodecID) type;
- (double)getClockSeconds;
- (double)getBufferSeconds;
- (double)getSinkBufferSeconds;
- (double)getLatencySeconds;
@end

@interface AVPlayerSink ()
@property (atomic) bool loaded;
@property (atomic) bool canceled;
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) char *readbuffer;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) NSString *contentType;
@property (nonatomic) unsigned int transferCount;
@property (nonatomic) NSData *dataAtOffsetZero;
@end

@implementation AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _loaded = false;
    _canceled = false;
    _avplayer = nullptr;
    _avbuffer = new AERingBuffer(frameSize * 64);
    _readbuffer = new char[65536 * 2];
    _frameSize = frameSize;
    _transferCount = 0;
  }
  return self;
}

- (void)dealloc
{
  _canceled = true;
  _avplayer = nullptr;
  _playerItem = nullptr;
  SAFE_DELETE(_avbuffer);
  SAFE_DELETE(_readbuffer);
}

#pragma mark - ResourceLoader
//-----------------------------------------------------------------------------------
- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  _canceled = true;
  CLog::Log(LOGDEBUG, "avloader didCancelLoadingRequest");
}

- (NSError *)loaderCancelledError
{
  NSError *error = [[NSError alloc] initWithDomain:@"ResourceLoaderErrorDomain"
    code:-1 userInfo:@{NSLocalizedDescriptionKey:@"Resource loader cancelled"}];

  return error;
}

#define logDataRequestEndOf 0
#define logDataRequestBgnEnd 0
#define logDataRequestSending 0
- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
  AVAssetResourceLoadingContentInformationRequest* contentRequest = loadingRequest.contentInformationRequest;

  if (contentRequest)
  {
    // figure out if this is ac3 or eac3
    NSURL *resourceURL = [loadingRequest.request URL];
    if ([resourceURL.pathExtension isEqualToString:@"ac3"])
      contentRequest.contentType = @"public.ac3-audio";
    else if ([resourceURL.pathExtension isEqualToString:@"eac3"])
      contentRequest.contentType = @"public.enhanced-ac3-audio";
    else if ([resourceURL.pathExtension isEqualToString:@"ec3"])
      contentRequest.contentType = @"public.enhanced-ac3-audio";
    // public.mpeg-2-transport-stream
    // (2147483647) or 639.132037797619048 hours at 0.032 secs pre 1792 byte frame
    contentRequest.contentLength = INT_MAX;
    // must be 'NO' to get player to start playing immediately
    contentRequest.byteRangeAccessSupported = NO;
    NSLog(@"avloader contentRequest %@", contentRequest);
  }

  AVAssetResourceLoadingDataRequest* dataRequest = loadingRequest.dataRequest;
  if (dataRequest)
  {
    //There where always 3 initial requests
    // 1) one for the first two bytes of the file
    // 2) one from the beginning of the file
    // 3) one from the end of the file
    //NSLog(@"resourceLoader dataRequest %@", dataRequest);
#if logDataRequestBgnEnd
    CLog::Log(LOGDEBUG, "avloader dataRequest bgn");
#endif
    NSInteger reqLen = dataRequest.requestedLength;
    if (reqLen == 2)
    {
      // 1) from above.
      // avplayer always 1st read two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      CLog::Log(LOGDEBUG, "avloader check content tag, %u in buffer", _avbuffer->GetReadSize());
    }
    else
    {
      size_t requestedBytes = _frameSize * 2;
      if (dataRequest.requestedOffset == 0)
      {
        // 2) above. make sure avplayer has enough frame blocks at startup
        requestedBytes = _frameSize * 36;
      }

      if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset != 0)
      {
        // 3) from above.
        // we have already hit 2) and saved it.
        // just shove it back to make avplayer happy.
        [dataRequest respondWithData:_dataAtOffsetZero];
#if logDataRequestEndOf
        CLog::Log(LOGDEBUG, "avloader check endof, %u in buffer", _avbuffer->GetReadSize());
#endif
        //CLog::Log(LOGDEBUG, "avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, bufferbytes:%u",
        //  dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _avbuffer->GetReadSize());
        [loadingRequest finishLoading];
#if logDataRequestBgnEnd
        CLog::Log(LOGDEBUG, "avloader dataRequest end");
#endif
        return YES;
      }

      // 2) from above and any other type of transfer
      while (_avbuffer->GetReadSize() < requestedBytes)
      {
        usleep(1 * 1000);
        if (_canceled)
        {
          [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
#if logDataRequestBgnEnd
          CLog::Log(LOGDEBUG, "avloader dataRequest end");
#endif
          return YES;
        }
      }
      if (dataRequest.requestsAllDataToEndOfResource == YES && dataRequest.requestedLength > (long)requestedBytes)
      {
        // calc how many complete frames are present
        size_t maxFrameBytes = _frameSize * (_avbuffer->GetReadSize() / _frameSize);
        // limit to size of _readbuffer
        if (maxFrameBytes > (65536 * 2))
          maxFrameBytes = _frameSize * ((65536 * 2) / _frameSize);
        //CLog::Log(LOGDEBUG, "avloader maxframebytes %lu", maxFrameBytes);
        requestedBytes = maxFrameBytes;
      }

      _avbuffer->Read((unsigned char*)_readbuffer, requestedBytes);

      // check if we have enough data
      if (requestedBytes)
      {
        NSData *data = [NSData dataWithBytes:_readbuffer length:requestedBytes];
        if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset == 0)
          _dataAtOffsetZero = [NSData dataWithBytes:_readbuffer length:requestedBytes];
        [dataRequest respondWithData:data];

#if logDataRequestSending
        // log the transfer
        size_t bufferbytes = _avbuffer->GetReadSize();
        if (bufferbytes > 0)
          CLog::Log(LOGDEBUG, "avloader sending %lu bytes, %zu in buffer",
            (unsigned long)[data length], bufferbytes);
        else
          CLog::Log(LOGDEBUG, "avloader sending %lu bytes", (unsigned long)[data length]);
#endif
        _transferCount += requestedBytes;
        if (_transferCount != dataRequest.currentOffset)
        {
          CLog::Log(LOGWARNING, "avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, _transferCount:%u, bufferbytes:%u",
            dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _transferCount, _avbuffer->GetReadSize());
        }
        [loadingRequest finishLoading];
      }
      else
      {
        CLog::Log(LOGDEBUG, "avloader loaderCancelledError");
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }
#if logDataRequestBgnEnd
  CLog::Log(LOGDEBUG, "avloader dataRequest end");
#endif

  return YES;
}

#pragma mark - ResourcePlayer
//-----------------------------------------------------------------------------------
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _playerItem && [keyPath isEqualToString:@"status"])
  {
      if (_avplayer.currentItem.status == AVPlayerItemStatusReadyToPlay)
      {
        CLog::Log(LOGDEBUG, "avloader AVPlayerItemStatusReadyToPlay");
        [_avplayer prerollAtRate:2.0 completionHandler:^(BOOL finished)
        {
            if (finished)
              _loaded = true;
        }];
      }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

- (int)addPackets:(uint8_t*)data size:(unsigned int)size
{
  if (data && size > 0)
  {
    int ok = AE_RING_BUFFER_OK;
    if (_avbuffer->Write(data, size) == ok)
      return size;
  }

  return 0;
}

- (void)play:(bool) state
{
  if (state)
    [_avplayer play];
    //[_avplayer playImmediatelyAtRate:1.0];
  else
  {
    if (_loaded)
      [_avplayer pause];
  }
}

- (void)drain
{
  // let audio play out
}

- (void)flush
{
}

- (void)startBuffering:(AVCodecID) type
{
/*
  for (NSString *mime in [AVURLAsset audiovisualTypes])
    NSLog(@"AVURLAsset audiovisualTypes:%@", mime);

  for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
*/
  // run on our own serial queue, keeps main thread from stalling us
  // and lets us do long sleeps without stalling main thread.
  dispatch_queue_t serialQueue = dispatch_queue_create("com.mrmc.loaderqueue", DISPATCH_QUEUE_SERIAL);
    dispatch_async(serialQueue, ^{
    NSString *extension = @"ac3";
    if (type == AV_CODEC_ID_EAC3)
      extension = @"ec3";
    // needs leading dir ('fake') or pathExtension in resourceLoader will fail
    NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
    [url appendString:extension];
    NSURL *streamURL = [NSURL URLWithString: url];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:streamURL options:nil];
    [asset.resourceLoader setDelegate:self queue:serialQueue];

    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    [_playerItem addObserver:self forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nil];

    _avplayer = [[AVPlayer alloc] initWithPlayerItem:_playerItem];
    _avplayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
    _avplayer.automaticallyWaitsToMinimizeStalling = NO;
    _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
  });
}

- (void)stopBuffering
{
  _canceled = true;
  _avplayer = nullptr;
  _playerItem = nullptr;
  usleep(100 * 1000);
}

- (double)getClockSeconds
{
  CMTime currentTime = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(currentTime);
  if (sink_s > 0.0)
    return sink_s;
  else
    return 0.0;
}

- (double)getBufferSeconds
{
  double avbufferSeconds = (_avbuffer->GetReadSize() / _frameSize) * 0.032;
  double transferSeconds = (_transferCount / _frameSize) * 0.032;
  double playerSeconds = [self getClockSeconds];
  double seconds = (transferSeconds - playerSeconds) + avbufferSeconds;
  if (seconds < 0.0)
    seconds = 0.0;

  return seconds;
}

- (double)getSinkBufferSeconds
{
  double buffered_s = 0.0;
  if (_playerItem)
  {
    NSArray *timeRanges = [_playerItem loadedTimeRanges];
    if (timeRanges && [timeRanges count])
    {
      CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
      //CLog::Log(LOGDEBUG, "avloader timerange.start %f", CMTimeGetSeconds(timerange.start));
      double duration = CMTimeGetSeconds(timerange.duration);
      //CLog::Log(LOGDEBUG, "avloader timerange.duration %f", duration);
      buffered_s = duration - [self getClockSeconds];
      if (buffered_s < 0.0)
        buffered_s = 0.0;
    }
  }
  return buffered_s;
}

- (double)getLatencySeconds
{
  return 0.0;
  if ([self getClockSeconds] > 0.0)
  {
    AVAudioSession *mySession = [AVAudioSession sharedInstance];
    double outputLatency = [mySession outputLatency];
    double ioBufferDuration = [mySession IOBufferDuration];
    double latency = outputLatency + ioBufferDuration;
    //CLog::Log(LOGNOTICE, "avloader latency %f", latency);
    return latency;
  }
  return 0.0;
}


@end
#pragma mark - DolbyFrameParser
/*
class CDolbyFrameParser
{
  public:
    CDolbyFrameParser() {};
   ~CDolbyFrameParser() {};

    void open(const uint8_t *buf, int len);
    void close();
    bool parse();
 private:
    bool parse_ac3();
    bool parse_ec3();
    CBitstreamReader *m_bs;
};

void CDolbyFrameParser::open(const uint8_t *buf, int len)
{
  m_bs = new CBitstreamReader(buf, len);
}
void CDolbyFrameParser::close()
{
  SAFE_DELETE(m_bs);
}
bool CDolbyFrameParser::parse()
{
  //peek at sync word
  if (m_bs->GetBits(16) != 0x0B77)
    return false;

  //---- byte 0
  m_bs->SkipBits(16);
  //peek at bsid
  int bsid = m_bs->GetBits(29) & 0x1F;
  if (bsid >= 17)
    return false;
  if (bsid <= 10)
    return parse_ac3();
  else
    return parse_ec3();
}
bool CDolbyFrameParser::parse_ac3()
{
  //---- byte 2
  uint crc1         = m_bs->ReadBits(16);
  //---- byte 4
  uint fscod        = m_bs->ReadBits( 2);
  uint frmsizecod   = m_bs->ReadBits( 6);
  //---- byte 5
  uint bsid         = m_bs->ReadBits( 5);
  uint bsmod        = m_bs->ReadBits( 3);
  uint acmod        = m_bs->ReadBits( 3);

  if ((acmod & 1) && acmod != 1)
    int cmixlev     = m_bs->ReadBits(2);
  if (acmod & 4)
    int surmixlev   = m_bs->ReadBits(2);
  if (acmod == 2)
    int dsurmod     = m_bs->ReadBits(2);
  int lfeon         = m_bs->ReadBits(1);

  if (fscod == 3 || frmsizecod >= 48)
    return(false);

  static int channels[] = {2, 1, 2, 3, 3, 4, 4, 5};
  int nChannels = channels[acmod] + lfeon;

  static int freq[] = {48000, 44100, 32000, 0};
  int nSamplesPerSec = freq[fscod];

  switch(bsid)
  {
    case 9:  nSamplesPerSec >>= 1; break;
    case 10: nSamplesPerSec >>= 2; break;
    case 11: nSamplesPerSec >>= 3; break;
    default: break;
  }

  static int rate[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640, 768, 896, 1024, 1152, 1280};
  int nBytesPerSec = ( rate[ frmsizecod >> 1 ] * 1000 ) / 8;

  return true;
}
bool CDolbyFrameParser::parse_ec3()
{
  //---- byte 2
  uint strmtyp      = m_bs->ReadBits( 2);
  if (strmtyp == 3)
    return false;

  uint substreamid  = m_bs->ReadBits( 3);
  uint frmsize      = m_bs->ReadBits(11) + 1;
  //---- byte 4
  uint fscod        = m_bs->ReadBits( 2);
  uint fscod2       = m_bs->ReadBits( 2); // only valid if fscod == 3
  uint numblckscod  = fscod2;
  uint acmod        = m_bs->ReadBits( 3);
  uint lfeon        = m_bs->ReadBits( 1);
  //---- byte 5
  uint bsid         = m_bs->ReadBits( 5);

  static int channels[] = {2, 1, 2, 3, 3, 4, 4, 5};
  uint nChannels = channels[acmod] + lfeon;

  static int freq[] = {48000, 44100, 32000, 0};
  int nSamplesPerSec;
  if (fscod == 3)
    nSamplesPerSec = freq[fscod2]/2;
  else
    nSamplesPerSec = freq[fscod];

  int nBytesPerSec = 1000 *((frmsize * nSamplesPerSec) / (16 * 48000));

  uint dialnorm     = m_bs->ReadBits( 5);
  uint compre       = m_bs->ReadBits( 1);
  if (compre)         m_bs->SkipBits( 8);

  return true;
}
*/

#pragma mark - CAudioSinkAVFoundation
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CAudioSinkAVFoundation::CAudioSinkAVFoundation(volatile bool &bStop, CDVDClock *clock)
: CThread("CAudioSinkAVFoundation")
, m_bStop(bStop)
, m_pClock(clock)
, m_startPtsSeconds(0)
, m_startPtsFlag(false)
, m_avsink(nullptr)
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::CAudioSinkAVFoundation");
  m_syncErrorDVDTime = 0.0;
  m_syncErrorDVDTimeSecondsOld = 0.0;
}

CAudioSinkAVFoundation::~CAudioSinkAVFoundation()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::~CAudioSinkAVFoundation");
  m_avsink = nullptr;
}

bool CAudioSinkAVFoundation::Create(const DVDAudioFrame &audioframe, AVCodecID codec, bool needresampler)
{
  CLog::Log(LOGNOTICE,
    "Creating audio stream (codec id: %i, channels: %i, sample rate: %i, %s)",
    codec,
    audioframe.format.m_channelLayout.Count(),
    audioframe.format.m_sampleRate,
    audioframe.passthrough ? "pass-through" : "no pass-through"
  );

  CSingleLock lock(m_critSection);

  CAEFactory::Suspend();

  m_codec = codec;
  // EAC3 can have 1,2,3 or 6 audio blocks of 256 bytes per sync frame
  m_frameSize = audioframe.format.m_streamInfo.m_ac3FrameSize;
  m_startPtsFlag = false;
  m_startPtsSeconds = 0;
  m_avsink = [[AVPlayerSink alloc] initWithFrameSize:m_frameSize];
  [m_avsink startBuffering:m_codec];

  CThread::Create();
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Create");

  return true;
}

void CAudioSinkAVFoundation::Destroy()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Destroy");

  m_bStop = true;
  StopThread();

  [m_avsink stopBuffering];
  m_avsink = nullptr;
  m_startPtsSeconds = 0;

  CAEFactory::Resume();
}

unsigned int CAudioSinkAVFoundation::AddPackets(const DVDAudioFrame &audioframe)
{
  CSingleLock lock (m_critSection);

  m_abortAddPacketWait = false;
  double syncErrorSeconds = CalcSyncErrorSeconds();
  if (abs(syncErrorSeconds - m_syncErrorDVDTimeSecondsOld) > 0.050)
  {
    m_syncErrorDVDTimeSecondsOld = syncErrorSeconds;
    m_syncErrorDVDTime = DVD_SEC_TO_TIME(syncErrorSeconds);
  }
  else
  {
    m_syncErrorDVDTime = 0.0;
    m_syncErrorDVDTimeSecondsOld = 0.0;
  }

  if (!m_startPtsFlag && audioframe.nb_frames)
  {
    m_startPtsFlag = true;
    m_startPtsSeconds = (double)audioframe.pts / DVD_TIME_BASE;
  }
  unsigned int written = 0;
  while (!m_abortAddPacketWait && !m_bStop && written < audioframe.nb_frames)
  {
    written = [m_avsink addPackets:audioframe.data[0] size:audioframe.nb_frames];
    double buffer_s = [m_avsink getBufferSeconds];
    if (buffer_s > 3.0)
    {
      lock.Leave();
      Sleep(50);
      if (m_abortAddPacketWait)
        break;
      lock.Enter();
    }
  }

  return audioframe.nb_frames;
}

void CAudioSinkAVFoundation::Drain()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Drain");
  [m_avsink drain];
}

void CAudioSinkAVFoundation::Pause()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Pause");
  [m_avsink play:false];
}

void CAudioSinkAVFoundation::Resume()
{
  CSingleLock lock(m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Resume");
  [m_avsink play:true];
}

double CAudioSinkAVFoundation::GetDelay()
{
  // Returns the time (dvd timebase) that it will take
  // for the next added packet to be heard from the speakers.
  // 1) used as audio cachetime in player during startup
  // 2) in DVDPlayerAudio during RESYNC
  // 3) and internally to offset passed pts in AddPackets
  CSingleLock lock (m_critSection);
  double buffered_s = [m_avsink getBufferSeconds] + [m_avsink getLatencySeconds];
  return buffered_s * DVD_TIME_BASE;
}

double CAudioSinkAVFoundation::GetMaxDelay()
{
  // returns total time (seconds) of audio in AE for the stream
  // used as audio cachetotal in player during startup
  CSingleLock lock (m_critSection);
  return 4.0;
}

void CAudioSinkAVFoundation::Flush(bool retain)
{
  m_abortAddPacketWait = true;

  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Flush");
  m_startPtsFlag = false;
  m_startPtsSeconds = 0;
  [m_avsink stopBuffering];
  if (retain)
  {
    m_avsink = [[AVPlayerSink alloc] initWithFrameSize:m_frameSize];
    [m_avsink startBuffering:m_codec];
  }
}

void CAudioSinkAVFoundation::AbortAddPackets()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::AbortAddPackets");
  m_abortAddPacketWait = true;
}

bool CAudioSinkAVFoundation::IsValidFormat(const DVDAudioFrame &audioframe)
{
  return audioframe.passthrough == true;
}

double CAudioSinkAVFoundation::GetCacheTime()
{
  // Returns the time in seconds that it will take
  // to underrun the cache if no sample is added.
  // ie. time of current cache in seconds.
  // 1) used in setting a timeout in CDVDPlayerAudio message queue
  // 2) used to signal start (cachetime >= cachetotal * 0.75)
  CSingleLock lock (m_critSection);
  double buffered_s = [m_avsink getBufferSeconds];
  return buffered_s;
}

double CAudioSinkAVFoundation::GetCacheTotal()
{
  // total cache time of stream in seconds
  // returns total time a stream can buffer
  // only used to signal start (cachetime >= cachetotal * 0.75)
  CSingleLock lock (m_critSection);
  return 4.0;
}

double CAudioSinkAVFoundation::GetPlayingPts()
{
  // passed to CDVDPlayerAudio and accessed by CDVDPlayerAudio::GetCurrentPts()
  // which is used by CDVDPlayer to ONLY report a/v sync.
  // Is not used for correcting a/v sync.
  return DVD_MSEC_TO_TIME(GetClock());
}

double CAudioSinkAVFoundation::CalcSyncErrorSeconds()
{
  double syncError = 0.0;

  if (m_avsink && [m_avsink loaded])
  {
    double absolute;
    double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
    double sink_s = [m_avsink getClockSeconds] + m_startPtsSeconds + [m_avsink getLatencySeconds];
    if (player_s > 0.0 && sink_s > 0.0)
      syncError = sink_s - player_s;
  }
  return syncError;
}

double CAudioSinkAVFoundation::GetSyncError()
{
  return m_syncErrorDVDTime;
}

void CAudioSinkAVFoundation::SetSyncErrorCorrection(double correction)
{
  //CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::SetSyncErrorCorrection m_syncError (%f), correction(%f)",
  //  m_syncErrorDVDTime, correction);

  m_syncErrorDVDTime += correction;
}

double CAudioSinkAVFoundation::GetClock()
{
  // return clock time in milliseconds (corrected for starting pts)
  return ([m_avsink getClockSeconds] + m_startPtsSeconds) * 1000.0;
}

void CAudioSinkAVFoundation::Process()
{
  // debug monitoring thread
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Started");

  while (!m_bStop)
  {
    double sink_s = 0.0;
    double buffer_s = 0.0;
    double sinkBuffer_s = 0.0;
    double absolute;
    double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
    if (m_startPtsFlag && m_avsink)
    {
      sink_s = [m_avsink getClockSeconds] + m_startPtsSeconds;
      buffer_s = [m_avsink getBufferSeconds];
      sinkBuffer_s = [m_avsink getSinkBufferSeconds];
    }
    CLog::Log(LOGDEBUG, "avloader playerBuffered_s (%f), buffered_s (%f), player_s(%f), sink_s(%f), delta(%f)",
      sinkBuffer_s, buffer_s, player_s, sink_s, player_s - sink_s);

    Sleep(250);
  }

  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Stopped");
}
