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
#include "utils/log.h"

#define AVMediaType AVMediaType_fooo
#import <AVFoundation/AVFoundation.h>
#undef AVMediaType
#import <AVFoundation/AVAudioSession.h>

static void *playbackLikelyToKeepUp = &playbackLikelyToKeepUp;
static void *playbackBufferEmpty = &playbackBufferEmpty;
static void *playbackBufferFull = &playbackBufferFull;

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
- (double)getBufferedSeconds;
- (double)getLatencySeconds;
@end

@interface AVPlayerSink ()
@property (atomic) bool started;
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

#define logDataRequestBgnEnd 0
#define logDataRequestEndOf 0
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

        // log the transfer
        size_t bufferbytes = _avbuffer->GetReadSize();
#if logDataRequestSending
        if (_avbuffer->GetReadSize() > 0)
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
  if (object == _avplayer.currentItem && [keyPath isEqualToString:@"loadedTimeRanges"])
  {
    NSArray *timeRanges = (NSArray*)[change objectForKey:NSKeyValueChangeNewKey];
    if (timeRanges && [timeRanges count])
    {
      CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
      if (CMTimeGetSeconds(timerange.duration) > 3.0)
      {
        if ([_avplayer rate] == 0.0)
        {
          CLog::Log(LOGDEBUG, "avloader timerange.start %f", CMTimeGetSeconds(timerange.start));
          CLog::Log(LOGDEBUG, "avloader timerange.duration %f", CMTimeGetSeconds(timerange.duration));
        }
      }
    }
  }
  else if (object == _avplayer.currentItem && [keyPath isEqualToString:@"status"])
  {
      if (_avplayer.currentItem.status == AVPlayerItemStatusReadyToPlay)
      {
        CLog::Log(LOGDEBUG, "avloader AVPlayerItemStatusReadyToPlay");
      }
  }
  else if ([keyPath isEqualToString:@"playbackBufferFull"] )
  {
    CLog::Log(LOGDEBUG, "avloader playbackBufferFull");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackBufferEmpty"] )
  {
    CLog::Log(LOGDEBUG, "avloader playbackBufferEmpty");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackLikelyToKeepUp"])
  {
    CLog::Log(LOGDEBUG, "avloader playbackLikelyToKeepUp");
    if (_avplayer.currentItem.playbackLikelyToKeepUp)
    {
    }
  }
  else if (object == _avplayer && [keyPath isEqualToString:@"status"])
  {
    if (_avplayer.status == AVPlayerStatusReadyToPlay)
    {
      CLog::Log(LOGDEBUG, "avloader AVPlayerStatusReadyToPlay");
      [_avplayer prerollAtRate:1.0 completionHandler:^(BOOL finished){
        if (finished)
        {
          //[self.videoPlayer play];
        }
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
    //[_avplayer play];
    [_avplayer playImmediatelyAtRate:1.0];
  else
    [_avplayer pause];
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
    dispatch_sync(serialQueue, ^{
    NSString *extension = @"ac3";
    if (type == AV_CODEC_ID_EAC3)
      extension = @"ec3";
    // needs leading dir ('fake') or pathExtension in resourceLoader will fail
    NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
    [url appendString:extension];
    NSURL *ac3URL = [NSURL URLWithString: url];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:ac3URL options:nil];
    [asset.resourceLoader setDelegate:self queue:serialQueue];

    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    //[_playerItem addObserver:self forKeyPath:@"playbackBufferFull" options:NSKeyValueObservingOptionNew context:playbackBufferFull];
    //[_playerItem addObserver:self forKeyPath:@"playbackBufferEmpty" options:NSKeyValueObservingOptionNew context:playbackBufferEmpty];
    //[_playerItem addObserver:self forKeyPath:@"playbackLikelyToKeepUp" options:NSKeyValueObservingOptionNew context:playbackLikelyToKeepUp];
    //[_playerItem addObserver:self forKeyPath:@"loadedTimeRanges" options:NSKeyValueObservingOptionNew context:nil];
    //[_playerItem addObserver:self forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nil];

    _avplayer = [[AVPlayer alloc] initWithPlayerItem:_playerItem];
    _avplayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
    [_avplayer addObserver:self forKeyPath:@"status" options:0 context:nil];
    _avplayer.automaticallyWaitsToMinimizeStalling = NO;
    _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
  });
  usleep(100 * 1000);
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

- (double)getBufferedSeconds
{
  double avbufferSeconds = (_avbuffer->GetReadSize() / _frameSize) * 0.032;
  double transferSeconds = (_transferCount / _frameSize) * 0.032;
  double playerSeconds = [self getClockSeconds];
  double seconds = (transferSeconds - playerSeconds) + avbufferSeconds;
  //CLog::Log(LOGDEBUG, "avloader getBufferedSeconds %f", seconds);

  return seconds;
}

- (double)getLatencySeconds
{
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

#pragma mark - CAudioSinkAVFoundation
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CAudioSinkAVFoundation::CAudioSinkAVFoundation(volatile bool &bStop, CDVDClock *clock)
: CThread("CAudioSinkAVFoundation")
, m_bStop(bStop)
, m_pClock(clock)
, m_startPtsSeconds(0)
, m_sync(false)
, m_start(false)
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
  // EAC3 can have 1,2,3 or 6 audio blocks per sync frame
  m_frameSize = audioframe.format.m_streamInfo.m_ac3FrameSize;
  m_sync = false;
  m_start = false;
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

  m_bAbort = false;
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

  if (!m_start && audioframe.nb_frames)
  {
    m_start = true;
    m_startPtsSeconds = DVD_TIME_TO_SEC(audioframe.pts);
  }
  unsigned int written = 0;
  while (!m_bAbort && !m_bStop && written < audioframe.nb_frames)
  {
    written = [m_avsink addPackets:audioframe.data[0] size:audioframe.nb_frames];
    double buffer_s = [m_avsink getBufferedSeconds];
    if (buffer_s > 3.0)
    {
      lock.Leave();
      Sleep(50);
      if (m_bAbort)
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
  m_sync = true;
}

double CAudioSinkAVFoundation::GetDelay()
{
  // Returns the time (dvd timebase) that it will take
  // for the next added packet to be heard from the speakers.
  // 1) used as audio cachetime in player during startup
  // 2) in DVDPlayerAudio during RESYNC
  // 3) and internally to offset passed pts in AddPackets
  CSingleLock lock (m_critSection);
  double buffered_s = [m_avsink getBufferedSeconds] + [m_avsink getLatencySeconds];
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
  m_bAbort = true;

  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Flush");
  m_sync = false;
  m_start = false;
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
  m_bAbort = true;
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
  double buffered_s = [m_avsink getBufferedSeconds];
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

  double absolute;
  double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
  double sink_s = 0.0;
  if (m_avsink)
    sink_s = [m_avsink getClockSeconds] + m_startPtsSeconds + [m_avsink getLatencySeconds];
  if (player_s > 0.0 && sink_s > 0.0)
    syncError = sink_s - player_s;

  return syncError;
}

double CAudioSinkAVFoundation::GetSyncError()
{
  return m_syncErrorDVDTime;
}

void CAudioSinkAVFoundation::SetSyncErrorCorrection(double correction)
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::SetSyncErrorCorrection m_syncError (%f), correction(%f)",
    m_syncErrorDVDTime, correction);

  m_syncErrorDVDTime += correction;
}

double CAudioSinkAVFoundation::GetClock()
{
  // return clock time in milliseconds (corrected for starting pts)
  return ([m_avsink getClockSeconds] + m_startPtsSeconds) * 1000;
}

void CAudioSinkAVFoundation::Process()
{
  // debug monitoring thread
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Started");

  while (!m_bStop)
  {
    double sink_s = 0.0;
    double buffered_s = 0.0;
    double absolute;
    double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
    if (m_start && m_avsink)
    {
      sink_s = [m_avsink getClockSeconds] + m_startPtsSeconds;
      buffered_s = [m_avsink getBufferedSeconds];
    }
    CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process buffered_s (%f), player_s(%f), sink_s(%f), delta(%f)",
      buffered_s, player_s, sink_s, player_s - sink_s);

    Sleep(100);
  }

  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Stopped");
}
