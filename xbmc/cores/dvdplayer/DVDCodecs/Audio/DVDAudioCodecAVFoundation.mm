/*
 *      Copyright (C) 2010-2013 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
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
@interface AVPlayerSink : NSObject <AVAssetResourceLoaderDelegate>
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)stopPlayback;
- (void)startPlayback:(AVCodecID) type;
- (void)playbackStatus;
@end

@interface AVPlayerSink ()
@property (atomic) bool started;
@property (atomic) bool canceled;
@property (nonatomic) FILE *fp;
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) NSString *contentType;
@property (nonatomic) unsigned int transferCount;
@property (nonatomic) NSData *dataAtOffsetZero;
@end

@implementation AVPlayerSink
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _started = false;
    _canceled = false;
    _avplayer = nullptr;
    _avbuffer = new AERingBuffer(frameSize * 256);
    _frameSize = frameSize;
    _transferCount = 0;
    std::string temppath(CDarwinUtils::GetUserTempDirectory());
    temppath += "CDVDAudioCodecAVFoundation.bin";
    _fp = fopen(temppath.c_str(), "rb");
  }
  return self;
}

- (void)dealloc
{
  _canceled = true;
  _avplayer = nullptr;
  _playerItem = nullptr;
  SAFE_DELETE(_avbuffer);
}

#pragma mark - ResourceLoader
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
      usleep(250 * 1000);
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      CLog::Log(LOGDEBUG, "avloader check content tag");
    }
    else
    {
      size_t bytesRequested = (int)reqLen;
      // Pull audio from buffer
      unsigned int buffersize = 65536;
      //if (bytesRequested > 65536)
        buffersize = _frameSize * 20;
      char buffer[buffersize];
      size_t availableBytes = 0;
      size_t requestedBytes = buffersize;

      if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset != 0)
      {
        // 3) from above.
        // we have already hit 2) and saved it.
        // just shove it back to make avplayer happy.
        usleep(250 * 1000);
        [dataRequest respondWithData:_dataAtOffsetZero];
        CLog::Log(LOGDEBUG, "avloader check endof");
        [loadingRequest finishLoading];
        CLog::Log(LOGDEBUG, "avloader dataRequest end");
        return YES;
      }

      // 2) from above and any other type of transfer
      if (_fp)
      {
        size_t filesize = 0;
        while (1)
        {
          usleep(10 * 1000);
          if (_canceled)
          {
            [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
            CLog::Log(LOGDEBUG, "avloader dataRequest end");
            return YES;
          }
          struct stat buf = {0};
          fstat(fileno(_fp), &buf);
          filesize = buf.st_size;
          if (dataRequest.requestedOffset + buffersize < (long long)filesize)
            break;
        }
        //CLog::Log(LOGDEBUG, "avloader dataRequest _fp size(%zu)", filesize);
      }
      else
      {
        while (_avbuffer->GetReadSize() < buffersize)
        {
          usleep(10 * 1000);
          if (_canceled)
          {
            [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
            CLog::Log(LOGDEBUG, "avloader dataRequest end");
            return YES;
          }
        }
      }

      if (_fp)
      {
        availableBytes = fread(&buffer, 1, requestedBytes, _fp);
      }
      else
      {
        unsigned int wanted = requestedBytes;
        unsigned int bytes = std::min(_avbuffer->GetReadSize(), wanted);
        _avbuffer->Read((unsigned char*)&buffer, bytes);
        availableBytes = bytes;
      }

      // check if we have enough data
      if (availableBytes)
      {
        size_t bytesToCopy = bytesRequested > availableBytes ? availableBytes : bytesRequested;
        if (bytesToCopy > 0)
        {
            NSData *data = [NSData dataWithBytes:buffer length:bytesToCopy];
            if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset == 0)
              _dataAtOffsetZero = [NSData dataWithBytes:buffer length:bytesToCopy];
            [dataRequest respondWithData:data];
            CLog::Log(LOGDEBUG, "avloader sending %lu bytes", (unsigned long)[data length]);
            [self playbackStatus];
        }
        _transferCount += bytesToCopy;
        if (_transferCount != dataRequest.currentOffset)
        {
          CLog::Log(LOGDEBUG, "avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, _transferCount:%u, bufferbytes:%u",
            dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _transferCount, _avbuffer->GetReadSize());
        }
        [loadingRequest finishLoading];
      }
      else
      {
        CLog::Log(LOGDEBUG, "avloader availableBytes %lu bytes", availableBytes);
        //availableBytes = buffersize;
        //memset(&buffer, 0x00, buffersize);
        // maybe return an empty buffer so silence is played until we have data
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }
  CLog::Log(LOGDEBUG, "avloader dataRequest end");

  return YES;
}

#pragma mark - ResourcePlayer
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _avplayer.currentItem && [keyPath isEqualToString:@"loadedTimeRanges"])
  {
    NSArray *timeRanges = (NSArray*)[change objectForKey:NSKeyValueChangeNewKey];
    if (timeRanges && [timeRanges count])
    {
      CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
      if (CMTimeGetSeconds(timerange.duration) > 4.0)
      {
        if ([_avplayer rate] == 0.0)
        {
          CLog::Log(LOGDEBUG, "avloader timerange.start %f", CMTimeGetSeconds(timerange.start));
          CLog::Log(LOGDEBUG, "avloader timerange.duration %f", CMTimeGetSeconds(timerange.duration));
          [_avplayer play];
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
    }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
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

- (void)playbackStatus
{
  AVPlayerStatus status = [_avplayer status];
  float rate = [_avplayer rate];
  CMTime currentTime = [_playerItem currentTime];
  CLog::Log(LOGDEBUG, "status: %d, rate(%f), currentTime: %f", (int)status, rate, CMTimeGetSeconds(currentTime));
}

@end
CDVDAudioCodecAVFoundation::CDVDAudioCodecAVFoundation(void)
  : CDVDAudioCodecPassthrough()
  , m_avsink(nullptr)
{
}

CDVDAudioCodecAVFoundation::~CDVDAudioCodecAVFoundation(void)
{
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
    std::string temppath(CDarwinUtils::GetUserTempDirectory());
    temppath += "CDVDAudioCodecAVFoundation.bin";
    remove(temppath.c_str());
    m_fp = fopen(temppath.c_str(), "wb");

    // fixme: move to decode so we know real frame size
    m_avsink = [[AVPlayerSink alloc] initWithFrameSize:1792];
    [m_avsink startPlayback:hints.codec];
  }
  CLog::Log(LOGDEBUG, "%s", __FUNCTION__);

  return rtn;
}

void CDVDAudioCodecAVFoundation::Dispose()
{
  [m_avsink stopPlayback];
  usleep(100 * 1000);
  m_avsink = nullptr;

  if (m_fp)
    fclose(m_fp);

  CDVDAudioCodecPassthrough::Dispose();
  CLog::Log(LOGDEBUG, "%s", __FUNCTION__);
}

int CDVDAudioCodecAVFoundation::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  int rtn = CDVDAudioCodecPassthrough::Decode(pData, iSize, dts, pts);

  if (iSize > 0 && (iSize != rtn))
    CLog::Log(LOGDEBUG, "%s iSize(%d), rtn(%d)", __FUNCTION__, iSize, rtn);
  else
  {
    if (m_fp)
    {
      size_t bytes_written;
      bytes_written = fwrite(pData, 1, iSize, m_fp);
    }
  }
  return rtn;
}

void CDVDAudioCodecAVFoundation::Reset()
{
  CDVDAudioCodecPassthrough::Reset();
}
