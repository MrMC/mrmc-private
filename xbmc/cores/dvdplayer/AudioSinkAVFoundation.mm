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
- (void)addPackets:(uint8_t*)data size:(int)size dts:(double)dts pts:(double)pts;
- (void)play:(bool) state;
- (void)drain;
- (void)flush;
- (void)stopPlayback;
- (void)startPlayback:(AVCodecID) type;
- (double)getClockSeconds;
- (double)getBufferedSeconds;
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
    _avbuffer = new AERingBuffer(frameSize * 256);
    _readbuffer = new char[65536];
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
        //usleep(250 * 1000);
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
        if (maxFrameBytes > 65536)
          maxFrameBytes = 65536;
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
        CLog::Log(LOGDEBUG, "avloader sending %lu bytes, %u in buffer",
          (unsigned long)[data length], _avbuffer->GetReadSize());

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
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

- (void)addPackets:(uint8_t*)data size:(int)size dts:(double)dts pts:(double)pts
{
  if (data)
  {
    int written = 0;
    int retries = 10;
    while (!_canceled && written < size && --retries)
    {
      int max_bytes = _avbuffer->GetWriteSize() / 2;
      if (size > max_bytes)
      {
        int sleep_time_ms = 0.032;
        usleep(sleep_time_ms * 1000);
        continue;
      }
      _avbuffer->Write(data, size);
      written = size;
    }
  }
}

- (void)play:(bool) state
{
  if (state)
    [_avplayer play];
  else
    [_avplayer pause];
}

- (void)drain
{
  // let audio play out
}

- (void)flush
{
  // flush audio to silence
}

- (void)startPlayback:(AVCodecID) type
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
      NSURL *ac3URL = [NSURL URLWithString: url];
      AVURLAsset *asset = [AVURLAsset URLAssetWithURL:ac3URL options:nil];
      [asset.resourceLoader setDelegate:self queue:serialQueue];

      _playerItem = [AVPlayerItem playerItemWithAsset:asset];
      [_playerItem addObserver:self forKeyPath:@"playbackBufferFull" options:NSKeyValueObservingOptionNew context:playbackBufferFull];
      [_playerItem addObserver:self forKeyPath:@"playbackBufferEmpty" options:NSKeyValueObservingOptionNew context:playbackBufferEmpty];
      [_playerItem addObserver:self forKeyPath:@"playbackLikelyToKeepUp" options:NSKeyValueObservingOptionNew context:playbackLikelyToKeepUp];
      [_playerItem addObserver:self forKeyPath:@"loadedTimeRanges" options:NSKeyValueObservingOptionNew context:nil];

      _avplayer = [[AVPlayer alloc] initWithPlayerItem:_playerItem];
      _avplayer.automaticallyWaitsToMinimizeStalling = NO;
      //_avplayer.currentItem.preferredForwardBufferDuration = 4.0;
      _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
      [_avplayer pause];
    });

  //NSLog(@"AVURLAsset preferredForwardBufferDuration:%f", [_avplayer.currentItem preferredForwardBufferDuration]);
}

- (void)stopPlayback
{
  _canceled = true;
  _avplayer = nullptr;
  _playerItem = nullptr;
  SAFE_DELETE(_avbuffer);
}

- (double)getClockSeconds
{
  CMTime currentTime = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(currentTime);
  return sink_s;
}

- (double)getBufferedSeconds
{
  double buffered_s = 0.0;
  NSArray *timeRanges = [_playerItem loadedTimeRanges];
  if (timeRanges && [timeRanges count])
  {
    CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
    //CLog::Log(LOGDEBUG, "avloader timerange.start %f", CMTimeGetSeconds(timerange.start));
    double duration = CMTimeGetSeconds(timerange.duration);
    //CLog::Log(LOGDEBUG, "avloader timerange.duration %f", duration);
    buffered_s = duration - [self getClockSeconds];
  }
  return buffered_s;
}

@end

#pragma mark - CAudioSinkAVFoundation
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CAudioSinkAVFoundation::CAudioSinkAVFoundation(volatile bool &bStop, CDVDClock *clock)
: CThread("CAudioSinkAVFoundation")
, m_bStop(bStop)
, m_pClock(clock)
, m_speed(DVD_PLAYSPEED_NORMAL)
, m_start(false)
, m_startPts(DVD_NOPTS_VALUE)
, m_startDelaySeconds(0.5)
, m_avsink(nullptr)
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::CAudioSinkAVFoundation");
  m_bPassthrough = false;
  m_bPaused = true;
  m_playingPts = DVD_NOPTS_VALUE;
  m_timeOfPts = 0.0;
  m_syncError = 0.0;
  m_syncErrorTime = 0;
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
  m_bPassthrough = audioframe.passthrough;

  switch (codec)
  {
    case AV_CODEC_ID_AC3:
      //framesize;
      break;

    case AV_CODEC_ID_EAC3:
      break;

    default:
      return false;
      break;
  }

  CAEFactory::Suspend();

  m_codec = codec;
  m_frameSize = audioframe.format.m_streamInfo.m_ac3FrameSize;
  m_start = false;
  m_startPts = DVD_NOPTS_VALUE;
  m_avsink = [[AVPlayerSink alloc] initWithFrameSize:m_frameSize];
  [m_avsink startPlayback:m_codec];

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

  [m_avsink stopPlayback];
  usleep(100 * 1000);
  m_avsink = nullptr;

  m_bPassthrough = false;
  m_bPaused = true;
  m_startPts = DVD_NOPTS_VALUE;
  m_playingPts = DVD_NOPTS_VALUE;

  CAEFactory::Resume();
}

unsigned int CAudioSinkAVFoundation::AddPackets(const DVDAudioFrame &audioframe)
{
  m_bAbort = false;
  if (!m_start && audioframe.nb_frames)
  {
    m_start = true;
    m_startPts = audioframe.pts;
    m_timer.Set(m_startDelaySeconds * 1000);
  }

  CSingleLock lock (m_critSection);
  m_syncErrorTime = 0;
  m_syncError = 0.0;

  m_playingPts = audioframe.pts + audioframe.duration - GetDelay();
  m_timeOfPts = m_pClock->GetAbsoluteClock();

  if (m_avsink)
  {
    [m_avsink addPackets:audioframe.data[0] size:audioframe.nb_frames dts:0 pts:audioframe.pts];
  }

  return audioframe.nb_frames;
}

void CAudioSinkAVFoundation::Drain()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Drain");
  [m_avsink drain];
}

float CAudioSinkAVFoundation::GetCurrentAttenuation()
{
  CSingleLock lock (m_critSection);
  return 1.0f;
}

void CAudioSinkAVFoundation::Pause()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Pause");
  m_playingPts = DVD_NOPTS_VALUE;
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
  // Returns the time in seconds that it will take
  // for the next added packet to be heard from the speakers.
  // used as audio cachetime in player during startup,
  // in DVDPlayerAudio during RESYNC,
  // and internally to offset passed pts in AddPackets
  CSingleLock lock (m_critSection);

  double delay = 0.3;
  return delay * DVD_TIME_BASE;
}

void CAudioSinkAVFoundation::Flush()
{
  m_bAbort = true;

  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Flush");
  m_playingPts = DVD_NOPTS_VALUE;
  m_syncError = 0.0;
  m_syncErrorTime = 0;
  [m_avsink flush];
}

void CAudioSinkAVFoundation::AbortAddPackets()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::AbortAddPackets");
  m_bAbort = true;
}

bool CAudioSinkAVFoundation::IsValidFormat(const DVDAudioFrame &audioframe)
{
  if (audioframe.passthrough != m_bPassthrough)
    return false;

  return true;
}

double CAudioSinkAVFoundation::GetCacheTime()
{
  // Returns the time in seconds that it will take
  // to underrun the cache if no sample is added.
  // ie. time of current cache in seconds.
  CSingleLock lock (m_critSection);
  if (m_timer.IsTimePast())
    return 0.3;

  return (double)m_timer.MillisLeft() / 1000.0;
}

double CAudioSinkAVFoundation::GetCacheTotal()
{
  // total cache time of stream in seconds
  // returns total time a stream can buffer
  CSingleLock lock (m_critSection);
  return m_startDelaySeconds;
}

double CAudioSinkAVFoundation::GetMaxDelay()
{
  // returns total time of audio in AE for the stream
  // used as audio cachetotal in player during startu
  CSingleLock lock (m_critSection);
  return 1.0;
}

double CAudioSinkAVFoundation::GetPlayingPts()
{
  // passed to CDVDPlayerAudio and accessed by CDVDPlayerAudio::GetCurrentPts()
  // which is used by CDVDPlayer to ONLY report a/v sync.
  // Is not used for correcting a/v sync.
  if (m_playingPts == DVD_NOPTS_VALUE)
    return 0.0;

  double now = m_pClock->GetAbsoluteClock();
  double diff = now - m_timeOfPts;
  double cache = GetCacheTime();
  double played = 0.0;

  if (diff < cache)
    played = diff;
  else
    played = cache;

  m_timeOfPts = now;
  m_playingPts += played;
  return m_playingPts;
}

double CAudioSinkAVFoundation::GetSyncError()
{
  // ErrorAdjust
  return m_syncError;
}

void CAudioSinkAVFoundation::SetSyncErrorCorrection(double correction)
{
  // lasts until next AddPacket/INSYNC/m_syncErrorTime != m_syncError
  m_syncError += correction;
}

void CAudioSinkAVFoundation::SetSpeed(int iSpeed)
{
  if (iSpeed == m_speed)
    return;

  switch(iSpeed)
  {
    case DVD_PLAYSPEED_PAUSE:
      CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::SetSpeed Pause");
      break;
    default:
    case DVD_PLAYSPEED_NORMAL:
      CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::SetSpeed Play");
      break;
  }
  m_speed = iSpeed;
}

double CAudioSinkAVFoundation::GetClock()
{
  // return clock time in milliseconds
  double absolute;
  // absolute is not used in GetClock :)
  return m_pClock->GetClock(absolute) / DVD_TIME_BASE * 1000;
}

double CAudioSinkAVFoundation::GetClockSpeed()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::GetClockSpeed");
  if (m_pClock)
    return m_pClock->GetClockSpeed();
  else
    return 1.0;
}

void CAudioSinkAVFoundation::Process()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Started");

  // bump our priority to be level with the krAEken (ActiveAE)
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);

  while (!m_bStop)
  {
    double player_s = GetClock() / 1000.0;
    if (player_s > 0.0)
    {
      double sink_s = 0.0;
      double buffered_s = 0.0;
      if (m_avsink)
      {
        sink_s = [m_avsink getClockSeconds] + m_startPts;
        buffered_s = [m_avsink getBufferedSeconds];
      }
      CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process buffered_s (%f), player_s(%f), sink_s(%f), delta(%f)",
        buffered_s, player_s, sink_s, player_s - sink_s);
    }
    Sleep(100);
  }

  SetPriority(THREAD_PRIORITY_NORMAL);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Stopped");
}
