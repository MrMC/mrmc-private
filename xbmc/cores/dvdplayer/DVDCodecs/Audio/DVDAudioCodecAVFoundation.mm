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

#include <algorithm>


#define AVMediaType AVMediaType_fooo
#import <AVFoundation/AVFoundation.h>
#undef AVMediaType

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVAudioSession.h>

#pragma mark - AudioResourceLoader
static void *playbackLikelyToKeepUp = &playbackLikelyToKeepUp;
static void *playbackBufferEmpty = &playbackBufferEmpty;
static void *playbackBufferFull = &playbackBufferFull;

@interface AudioResourceLoader : NSObject <AVAssetResourceLoaderDelegate>
@property (nonatomic) bool canceled;
@property (nonatomic) NSString *contentType;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) AERingBuffer *avbuffer;
- (id)initWithBuffer:(AERingBuffer*)buffer;
- (void)cancel;
@end

@implementation AudioResourceLoader
- (id)initWithBuffer:(AERingBuffer*)buffer
{
  self = [super init];
  if (self)
  {
    _canceled = false;
    _avbuffer = buffer;
  }
  return self;
}

- (NSError *)loaderCancelledError
{
  NSError *error = [[NSError alloc] initWithDomain:@"AudioResourceLoaderErrorDomain"
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
    contentRequest.contentLength = INT_MAX;
    // must be 'NO' to get player to start playing immediately
    contentRequest.byteRangeAccessSupported = NO;
    NSLog(@"AudioResourceLoader contentRequest %@", contentRequest);
  }

  AVAssetResourceLoadingDataRequest* dataRequest = loadingRequest.dataRequest;
  if (dataRequest)
  {
/*
    while (_avbuffer->GetReadSize() < 65536)
    {
      if (_canceled)
        break;
      usleep(10 * 1000);
    }
*/
    NSLog(@"AudioResourceLoader dataRequest %@", dataRequest);
    NSInteger reqLen = dataRequest.requestedLength;
    if (reqLen == 2)
    {
      // avplayer always 1st reads two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      CLog::Log(LOGDEBUG, "AudioResourceLoader check content tag");
    }
    else
    {
      size_t bytesRequested = (int)reqLen;
      // Pull audio from buffer
      int const buffersize = 65536;
      char buffer[buffersize];
      size_t availableBytes = 0;
      size_t requestedBytes = buffersize;
      //CLog::Log(LOGDEBUG, "AudioResourceLoader GetReadSize %u bytes", _avbuffer->GetReadSize());

      unsigned int wanted = requestedBytes;
      unsigned int bytes = std::min(_avbuffer->GetReadSize(), wanted);
      _avbuffer->Read((unsigned char*)&buffer, bytes);
      availableBytes = bytes;

      useconds_t wait = (availableBytes/_frameSize) * 32; // each ac3/eac3 frame is 32ms
      if (wait > 0)
        usleep(wait * 1000);

      // check if we have enough data
      if (availableBytes)
      {
        size_t bytesToCopy = bytesRequested > availableBytes ? availableBytes : bytesRequested;
        if (bytesToCopy > 0)
        {
            NSData *data = [NSData dataWithBytes:buffer length:bytesToCopy];
            [dataRequest respondWithData:data];
            CLog::Log(LOGDEBUG, "AudioResourceLoader sending %lu bytes", (unsigned long)[data length]);
        }
        [loadingRequest finishLoading];
      }
      else
      {
        CLog::Log(LOGDEBUG, "AudioResourceLoader availableBytes %lu bytes", availableBytes);
        //availableBytes = buffersize;
        //memset(&buffer, 0x00, buffersize);
        // maybe return an empty buffer so silence is played until we have data
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }

      if (availableBytes)
      {
        [loadingRequest finishLoading];
      }
      else
      {
        //CLog::Log(LOGDEBUG, "AudioResourceLoader loading finished");
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }

  return YES;
}

- (void)cancel
{
  _canceled = true;
}

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  _canceled = true;
  CLog::Log(LOGDEBUG, "AudioResourceLoader didCancelLoadingRequest");
}

@end

#pragma mark - AVPlayerAudio
@interface AVPlayerAudio : NSObject
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)stopPlayback;
- (void)startPlayback:(CAEStreamInfo::DataType) type;
- (unsigned int)addPackets:(uint8_t*)buffer withBytes:(unsigned int)bytes;
@end

@interface AVPlayerAudio ()
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) AudioResourceLoader *resourceloader;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) unsigned int frameSize;
@property (atomic) bool started;
@end

@implementation AVPlayerAudio
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _avplayer = nullptr;
    _avbuffer = new AERingBuffer(frameSize * 256);
    _frameSize = frameSize;
    _started = false;
  }
  return self;
}

- (void)dealloc
{
  [_resourceloader cancel];
  _avplayer = nullptr;
  _playerItem = nullptr;
  SAFE_DELETE(_avbuffer);
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _avplayer.currentItem && [keyPath isEqualToString:@"loadedTimeRanges"])
  {
    NSArray *timeRanges = (NSArray*)[change objectForKey:NSKeyValueChangeNewKey];
    if (timeRanges && [timeRanges count])
    {
      CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
      CLog::Log(LOGDEBUG, "AudioResourceLoader timerange.start %f", CMTimeGetSeconds(timerange.start));
      CLog::Log(LOGDEBUG, "AudioResourceLoader timerange.duration %f", CMTimeGetSeconds(timerange.duration));
    }
  }
  else if ([keyPath isEqualToString:@"playbackBufferFull"] )
  {
    CLog::Log(LOGDEBUG, "AudioResourceLoader playbackBufferFull");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackBufferEmpty"] )
  {
    CLog::Log(LOGDEBUG, "AudioResourceLoader playbackBufferEmpty");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackLikelyToKeepUp"])
  {
    CLog::Log(LOGDEBUG, "AudioResourceLoader playbackLikelyToKeepUp");
    if (_avplayer.currentItem.playbackLikelyToKeepUp)
    {
    }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

- (void)startPlayback:(CAEStreamInfo::DataType) type
{
  NSString *extension = @"ac3";
  if (type == CAEStreamInfo::STREAM_TYPE_EAC3)
    extension = @"ec3";
  // needs leading dir ('fake') or pathExtension in AudioResourceLoader will fail
  NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
  [url appendString:extension];
  NSURL *ac3URL = [NSURL URLWithString: url];
  AVURLAsset *asset = [AVURLAsset URLAssetWithURL:ac3URL options:nil];
/*
  for (NSString *mime in [AVURLAsset audiovisualTypes])
    NSLog(@"AVURLAsset audiovisualTypes:%@", mime);

  for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
*/
  _resourceloader = [[AudioResourceLoader alloc] initWithBuffer:_avbuffer];
  _resourceloader.frameSize = _frameSize;
  [asset.resourceLoader setDelegate:_resourceloader queue:dispatch_get_main_queue()];

  _playerItem = [AVPlayerItem playerItemWithAsset:asset];
  [_playerItem addObserver:self forKeyPath:@"playbackBufferFull" options:NSKeyValueObservingOptionNew context:playbackBufferFull];
  [_playerItem addObserver:self forKeyPath:@"playbackBufferEmpty" options:NSKeyValueObservingOptionNew context:playbackBufferEmpty];
  [_playerItem addObserver:self forKeyPath:@"playbackLikelyToKeepUp" options:NSKeyValueObservingOptionNew context:playbackLikelyToKeepUp];
  [_playerItem addObserver:self forKeyPath:@"loadedTimeRanges" options:NSKeyValueObservingOptionNew context:nil];

  _avplayer = [[AVPlayer alloc] initWithPlayerItem:_playerItem];
  [_avplayer play];
  _started = true;
}

- (void)stopPlayback
{
  _started = false;
}

- (unsigned int)addPackets:(uint8_t*)buffer withBytes:(unsigned int)bytes
{
  AVPlayerStatus status = [_avplayer status];
  CMTime currentTime = [_playerItem currentTime];
  CLog::Log(LOGDEBUG, "status: %d, currentTime: %f, writesize: %u", (int)status, CMTimeGetSeconds(currentTime), _avbuffer->GetWriteSize());

  unsigned int write_bytes = std::min(bytes, _avbuffer->GetWriteSize());
  if (write_bytes)
    _avbuffer->Write(buffer, write_bytes);

  return write_bytes;
}

@end


CDVDAudioCodecAVFoundation::CDVDAudioCodecAVFoundation(void)
: CDVDAudioCodecPassthrough()
{
}

CDVDAudioCodecAVFoundation::~CDVDAudioCodecAVFoundation(void)
{
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

  }
  CLog::Log(LOGDEBUG, "%s", __FUNCTION__);

  return rtn;
}

void CDVDAudioCodecAVFoundation::Dispose()
{
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
