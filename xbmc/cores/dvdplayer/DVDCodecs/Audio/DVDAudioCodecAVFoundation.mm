/*
 *      Copyright (C) 2018 Team MrMC
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

#include "DVDAudioCodecAVFoundation.h"
#include "DVDCodecs/DVDCodecs.h"
#include "DVDStreamInfo.h"
#include "cores/AudioEngine/AEFactory.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "platform/darwin/DarwinUtils.h"
#include "utils/log.h"

#include <sys/stat.h>

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
- (void)speed:(bool) state;
- (void)stopPlayback;
- (void)startPlayback:(AVCodecID) type;
- (double)playbackStatus;
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
    _started = false;
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
    CLog::Log(LOGDEBUG, "avloader dataRequest bgn");
    NSInteger reqLen = dataRequest.requestedLength;
    if (reqLen == 2)
    {
      // 1) from above.
      // avplayer always 1st read two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      CLog::Log(LOGDEBUG, "avloader check content tag");
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
        CLog::Log(LOGDEBUG, "avloader check endof");
        [loadingRequest finishLoading];
        CLog::Log(LOGDEBUG, "avloader dataRequest end");
        return YES;
      }

      // 2) from above and any other type of transfer
      while (_avbuffer->GetReadSize() < requestedBytes)
      {
        usleep(1 * 1000);
        if (_canceled)
        {
          [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
          CLog::Log(LOGDEBUG, "avloader dataRequest end");
          return YES;
        }
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
          CLog::Log(LOGDEBUG, "avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, _transferCount:%u, bufferbytes:%u",
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
  CLog::Log(LOGDEBUG, "avloader dataRequest end");

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
          //[_avplayer play];
          _started = true;
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
      _started = true;
    }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

- (void)addPackets:(uint8_t*)data size:(int)size dts:(double)dts pts:(double)pts;
{
  if (data)
  {
    unsigned int write_bytes = std::min(size, (int)_avbuffer->GetWriteSize());
    if (write_bytes)
      _avbuffer->Write(data, write_bytes);
  }
}

- (void)speed:(bool) state;
{
  if (state)
    [_avplayer play];
  else
    [_avplayer pause];
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
  _started = false;
}

- (double)playbackStatus
{
  //AVPlayerStatus status = [_avplayer status];
  //float rate = [_avplayer rate];
  CMTime currentTime = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(currentTime);
  //CLog::Log(LOGDEBUG, "status: %d, rate(%f), currentTime: %f", (int)status, rate, sink_s);

  return sink_s;
}

@end


#pragma mark - CDVDAudioCodecAVFoundation
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CDVDAudioCodecAVFoundation::CDVDAudioCodecAVFoundation(void)
: CDVDAudioCodecPassthrough()
, CThread("CDVDAudioCodecAVFoundation")
, m_avsink(nullptr)
, m_speed(DVD_PLAYSPEED_NORMAL)
{
  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::CDVDAudioCodecAVFoundation");
}

CDVDAudioCodecAVFoundation::~CDVDAudioCodecAVFoundation(void)
{
  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::~CDVDAudioCodecAVFoundation");
  m_avsink = nullptr;
}

bool CDVDAudioCodecAVFoundation::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  bool rtn = CDVDAudioCodecPassthrough::Open(hints, options);
  if (rtn)
  {
    AEAudioFormat format;
    format.m_dataFormat = AE_FMT_RAW;
    format.m_sampleRate = hints.samplerate;
    switch (hints.codec)
    {
      case AV_CODEC_ID_AC3:
        format.m_streamInfo.m_type = CAEStreamInfo::STREAM_TYPE_AC3;
        format.m_streamInfo.m_sampleRate = hints.samplerate;
        break;

      case AV_CODEC_ID_EAC3:
        format.m_streamInfo.m_type = CAEStreamInfo::STREAM_TYPE_EAC3;
        format.m_streamInfo.m_sampleRate = hints.samplerate;
        break;

      case AV_CODEC_ID_TRUEHD:
        format.m_streamInfo.m_type = CAEStreamInfo::STREAM_TYPE_TRUEHD;
        format.m_streamInfo.m_sampleRate = hints.samplerate;
        break;

      default:
        format.m_streamInfo.m_type = CAEStreamInfo::STREAM_TYPE_NULL;
    }
    m_format = format;
    // fixme: move to decode so we know real frame size
    m_avsink = [[AVPlayerSink alloc] initWithFrameSize:1792];
    [m_avsink startPlayback:hints.codec];

    Create();
    CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::Open");
  }

  return rtn;
}

void CDVDAudioCodecAVFoundation::Dispose()
{
  m_bStop = true;
  StopThread();

  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::Dispose");
  [m_avsink stopPlayback];
  usleep(100 * 1000);
  m_avsink = nullptr;

  CDVDAudioCodecPassthrough::Dispose();
}

int CDVDAudioCodecAVFoundation::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  int rtn = CDVDAudioCodecPassthrough::Decode(pData, iSize, dts, pts);

  if (iSize > 0 && (iSize != rtn))
    CLog::Log(LOGDEBUG, "%s iSize(%d), rtn(%d)", __FUNCTION__, iSize, rtn);
  else
  {
    if (m_avsink)
    {
      [m_avsink addPackets:pData size:iSize dts:dts pts:pts];
    }
  }
  return rtn;
}

void CDVDAudioCodecAVFoundation::GetData(DVDAudioFrame &frame)
{
  CDVDAudioCodecPassthrough::GetData(frame);
  /*
  if (m_avsink)
  {
    double sink_s = [m_avsink playbackStatus];
    if (sink_s == 0.0 && GetPlayerClockSeconds() < 2.0)
      frame.nb_frames = 0;
  }
  */
}

void CDVDAudioCodecAVFoundation::Reset()
{
  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::Reset");
  CDVDAudioCodecPassthrough::Reset();
}

void CDVDAudioCodecAVFoundation::SetClock(CDVDClock *clock)
{
  m_clock = clock;
}

void CDVDAudioCodecAVFoundation::SetSpeed(int iSpeed)
{
  if (iSpeed == m_speed)
    return;

  switch(iSpeed)
  {
    case DVD_PLAYSPEED_PAUSE:
      CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::SetSpeed Pause");
      [m_avsink speed:false];
      //m_messages->enqueue(PAUSE);
      break;
    default:
    case DVD_PLAYSPEED_NORMAL:
      CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::SetSpeed Play");
      [m_avsink speed:true];
      //m_messages->enqueue(PLAY);
      break;
  }
  m_speed = iSpeed;
}

double CDVDAudioCodecAVFoundation::GetPlayerClockSeconds()
{
  if (!m_clock)
    return 0.0;

  double seconds = m_clock->GetClock() / DVD_TIME_BASE;
  if (seconds > 0.0)
    return seconds;
  else
    return 0.0;
}

void CDVDAudioCodecAVFoundation::Process()
{
  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::Process Started");

  // bump our priority to be level with the krAEken (ActiveAE)
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);

  while (!m_bStop)
  {
    double player_s = GetPlayerClockSeconds();
    if (player_s > 0.0)
    {
      double sink_s = 0.0;
      if (m_avsink)
      {
        sink_s = [m_avsink playbackStatus];
      }
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAVFoundation::Process player_s(%f), sink_s(%f)", player_s, sink_s);
    }
    Sleep(100);
  }

  SetPriority(THREAD_PRIORITY_NORMAL);
  CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::Process Stopped");
}
