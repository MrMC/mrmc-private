/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "cores/AudioEngine/Sinks/AESinkDARWINAVF.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "cores/AudioEngine/Sinks/osx/CoreAudioHelpers.h"
#include "platform/darwin/DarwinUtils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "threads/Condition.h"
#include "windowing/WindowingFactory.h"
#include "platform/darwin/DarwinUtils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AVMediaType AVMediaType_fooo
#import <AVFoundation/AVFoundation.h>
#undef AVMediaType

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVAudioSession.h>

#pragma mark - ResourceLoader


static void *playbackLikelyToKeepUp = &playbackLikelyToKeepUp;
static void *playbackBufferEmpty = &playbackBufferEmpty;
static void *playbackBufferFull = &playbackBufferFull;

@interface ResourceLoader : NSObject <AVAssetResourceLoaderDelegate>
@property (nonatomic) FILE *fp;
@property (nonatomic) bool canceled;
@property (nonatomic) NSString *contentType;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) unsigned int transferCount;
@property (nonatomic) NSData *dataAtOffsetZero;
@property (nonatomic) AERingBuffer *avbuffer;
- (id)initWithBuffer:(AERingBuffer*)buffer;
- (void)cancel;
@end

@implementation ResourceLoader
- (id)initWithBuffer:(AERingBuffer*)buffer
{
  self = [super init];
  if (self)
  {
    _canceled = false;
    _avbuffer = buffer;
    _transferCount = 0;
    std::string temppath(CDarwinUtils::GetUserTempDirectory());
    temppath += "CDVDAudioCodecAVFoundation.bin";
    _fp = fopen(temppath.c_str(), "rb");
  }
  return self;
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
    NSLog(@"resourceLoader contentRequest %@", contentRequest);
  }

  AVAssetResourceLoadingDataRequest* dataRequest = loadingRequest.dataRequest;
  if (dataRequest)
  {
    //There where always 3 initial requests
    // 1) one for the first two bytes of the file
    // 2) one from the beginning of the file
    // 3) one from the end of the file
    //NSLog(@"resourceLoader dataRequest %@", dataRequest);
    CLog::Log(LOGDEBUG, "resourceLoader dataRequest bgn");
    NSInteger reqLen = dataRequest.requestedLength;
    if (reqLen == 2)
    {
      // 1) from above.
      // avplayer always 1st read two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      usleep(250 * 1000);
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      CLog::Log(LOGDEBUG, "resourceLoader check content tag");
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
        CLog::Log(LOGDEBUG, "resourceLoader check endof");
        [loadingRequest finishLoading];
        CLog::Log(LOGDEBUG, "resourceLoader dataRequest end");
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
            CLog::Log(LOGDEBUG, "resourceLoader dataRequest end");
            return YES;
          }
          struct stat buf = {0};
          fstat(fileno(_fp), &buf);
          filesize = buf.st_size;
          if (dataRequest.requestedOffset + buffersize < (long long)filesize)
            break;
        }
        CLog::Log(LOGDEBUG, "resourceLoader dataRequest _fp size(%zu)", filesize);
      }
      else
      {
        while (_avbuffer->GetReadSize() < buffersize)
        {
          usleep(10 * 1000);
          if (_canceled)
          {
            [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
            CLog::Log(LOGDEBUG, "resourceLoader dataRequest end");
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
            CLog::Log(LOGDEBUG, "resourceLoader sending %lu bytes", (unsigned long)[data length]);
        }
        _transferCount += bytesToCopy;
        CLog::Log(LOGDEBUG, "resourceLoader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, _transferCount:%u, bufferbytes:%u",
          dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _transferCount, _avbuffer->GetReadSize());
        [loadingRequest finishLoading];
      }
      else
      {
        CLog::Log(LOGDEBUG, "resourceLoader availableBytes %lu bytes", availableBytes);
        //availableBytes = buffersize;
        //memset(&buffer, 0x00, buffersize);
        // maybe return an empty buffer so silence is played until we have data
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }
  CLog::Log(LOGDEBUG, "resourceLoader dataRequest end");

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
  CLog::Log(LOGDEBUG, "resourceLoader didCancelLoadingRequest");
}

@end

CCriticalSection g_avfmutex;
XbmcThreads::ConditionVariable g_avfcondVar;

#pragma mark - AVPlayerSink
@interface AVPlayerSink : NSObject
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)stopPlayback;
- (void)startPlayback:(CAEStreamInfo::DataType) type;
- (unsigned int)addPackets:(uint8_t*)buffer withBytes:(unsigned int)bytes;
@end

@interface AVPlayerSink ()
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) ResourceLoader *resourceloader;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) unsigned int frameSize;
@property (atomic) bool started;
@end

@implementation AVPlayerSink
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
      CLog::Log(LOGDEBUG, "resourceLoader timerange.start %f", CMTimeGetSeconds(timerange.start));
      CLog::Log(LOGDEBUG, "resourceLoader timerange.duration %f", CMTimeGetSeconds(timerange.duration));
      if (CMTimeGetSeconds(timerange.duration) > 2.0)
        [_avplayer play];
    }
  }
  else if ([keyPath isEqualToString:@"playbackBufferFull"] )
  {
    CLog::Log(LOGDEBUG, "resourceLoader playbackBufferFull");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackBufferEmpty"] )
  {
    CLog::Log(LOGDEBUG, "resourceLoader playbackBufferEmpty");
    if (_avplayer.currentItem.playbackBufferEmpty)
    {
    }
  }
  else if ([keyPath isEqualToString:@"playbackLikelyToKeepUp"])
  {
    CLog::Log(LOGDEBUG, "resourceLoader playbackLikelyToKeepUp");
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
  // needs leading dir ('fake') or pathExtension in ResourceLoader will fail
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
  _resourceloader = [[ResourceLoader alloc] initWithBuffer:_avbuffer];
  _resourceloader.frameSize = _frameSize;
  [asset.resourceLoader setDelegate:_resourceloader queue:dispatch_get_main_queue()];

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
  _started = true;

  NSLog(@"AVURLAsset preferredForwardBufferDuration:%f", [_avplayer.currentItem preferredForwardBufferDuration]);
}

- (void)stopPlayback
{
  [_resourceloader cancel];
  usleep(100 * 1000);
  _avplayer = nullptr;
  _playerItem = nullptr;
  SAFE_DELETE(_avbuffer);
  _started = false;
}

- (unsigned int)addPackets:(uint8_t*)buffer withBytes:(unsigned int)bytes
{
  AVPlayerStatus status = [_avplayer status];
  float rate = [_avplayer rate];
  bool started = status != AVPlayerStatusUnknown;
  CMTime currentTime = [_playerItem currentTime];
  CLog::Log(LOGDEBUG, "status: %d, rate(%f), currentTime: %f, writebytes: %u", (int)status, rate, CMTimeGetSeconds(currentTime), _avbuffer->GetWriteSize());

  // use the passed in framesize instead of internal,
  // writes are relative to AE formats. once written,
  // CAAudioUnitSink owns them.
  if (_avbuffer->GetWriteSize() < bytes)
  { // no space to write - wait for a bit
    CSingleLock lock(g_avfmutex);
    unsigned int timeout = 900 * bytes / 48000;
    if (!started)
      timeout = 4500;

    // we are using a timer here for beeing sure for timeouts
    // g_avfcondVar can be woken spuriously as signaled
    XbmcThreads::EndTime timer(timeout);
    g_avfcondVar.wait(g_avfmutex, timeout);
    if (!started && timer.IsTimePast())
    {
      CLog::Log(LOGERROR, "%s engine didn't start in %d ms!", __FUNCTION__, timeout);
      return INT_MAX;
    }
  }

  unsigned int write_bytes = std::min(bytes, _avbuffer->GetWriteSize());
  if (write_bytes)
    _avbuffer->Write(buffer, write_bytes);

  return write_bytes;
}

@end

#pragma mark - EnumerateDevices
/***************************************************************************************/
/***************************************************************************************/
static void EnumerateDevices(AEDeviceInfoList &list)
{
  CAEDeviceInfo device;

  device.m_deviceName = "default";
  device.m_displayName = "Default";
  device.m_displayNameExtra = "";
  device.m_deviceType = AE_DEVTYPE_HDMI;

  device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_AC3);
  device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_EAC3);
  device.m_sampleRates.push_back(48000);
  //device.m_sampleRates.push_back(192000);

  device.m_dataFormats.push_back(AE_FMT_RAW);
  //device.m_dataFormats.push_back(AE_FMT_S16LE);

  // add channel info
  UInt32 maxChannels = [[AVAudioSession sharedInstance] maximumOutputNumberOfChannels];
  if (maxChannels > 6)
    device.m_channels = AE_CH_LAYOUT_7_1;
  else
    device.m_channels = AE_CH_LAYOUT_5_1;

  CLog::Log(LOGDEBUG, "EnumerateDevices:Device(%s)" , device.m_deviceName.c_str());

  list.push_back(device);
}

#pragma mark - AEDeviceInfoList
/***************************************************************************************/
/***************************************************************************************/
AEDeviceInfoList CAESinkDARWINAVF::m_devices;

CAESinkDARWINAVF::CAESinkDARWINAVF()
  : CThread("CAESinkDARWINAVF"),
    m_avsink(nullptr),
    m_started(false),
    m_draining(false),
    m_sink_frameSize(0),
    m_sinkbuffer_size(0),
    m_sinkbuffer_level(0),
    m_sinkbuffer_sec_per_byte(0)
{
}

CAESinkDARWINAVF::~CAESinkDARWINAVF()
{
  m_avsink = nullptr;
}

bool CAESinkDARWINAVF::Initialize(AEAudioFormat &format, std::string &device)
{
  m_started = false;
  std::string route = CDarwinUtils::GetAudioRoute();
  // no route, no audio. bail and let AE kick back to NULL device
  if (route.empty())
    return false;

  // no device, bail and let AE kick back to NULL device
  bool found = false;
  std::string devicelower = device;
  StringUtils::ToLower(devicelower);
  for (size_t i = 0; i < m_devices.size(); i++)
  {
    if (devicelower.find(m_devices[i].m_deviceName) != std::string::npos)
    {
      m_info = m_devices[i];
      found = true;
      break;
    }
  }
  if (!found)
    return false;

  bool passthrough = false;
  if (format.m_dataFormat == AE_FMT_RAW)
  {
    format.m_dataFormat = AE_FMT_S16LE;
    //format.m_channelLayout = AE_CH_LAYOUT_2_0;
    if (route.find("HDMI") != std::string::npos)
    {
      passthrough = true;
      switch (format.m_streamInfo.m_type)
      {
        case CAEStreamInfo::STREAM_TYPE_AC3:
          if (!format.m_streamInfo.m_ac3FrameSize)
            format.m_streamInfo.m_ac3FrameSize = 1536;
          format.m_frames = format.m_streamInfo.m_ac3FrameSize;
          format.m_frameSize = 1;
        break;
        case CAEStreamInfo::STREAM_TYPE_EAC3:
          if (!format.m_streamInfo.m_ac3FrameSize)
            format.m_streamInfo.m_ac3FrameSize = 1792;
          format.m_frames = format.m_streamInfo.m_ac3FrameSize;
          format.m_frames *= 4;
          format.m_frameSize = 1;
        break;
        default:
          passthrough = false;
        break;
      }
    }
  }
  m_format = format;

  if (passthrough)
  {
    // setup a pretend 500ms internal buffer
    m_sink_frameSize = format.m_streamInfo.m_ac3FrameSize;
    m_sinkbuffer_size = m_sink_frameSize * 40;
    m_sinkbuffer_sec_per_byte = 0.032 / m_sink_frameSize;

    m_draining = false;
    m_wake.Reset();
    m_inited.Reset();
    Create();
    if (!m_inited.WaitMSec(100))
    {
      while(!m_inited.WaitMSec(1))
        Sleep(10);
    }

    m_avsink = [[AVPlayerSink alloc] initWithFrameSize:m_sink_frameSize];
    [m_avsink startPlayback:format.m_streamInfo.m_type];
  }

  return passthrough == true;
}

void CAESinkDARWINAVF::Deinitialize()
{
  m_started = false;
  [m_avsink stopPlayback];
  usleep(100 * 1000);
  m_avsink = nullptr;

  // force m_bStop and set m_wake, if might be sleeping.
  m_bStop = true;
  StopThread();
}

void CAESinkDARWINAVF::AddPause(unsigned int millis)
{
  //CLog::Log(LOGDEBUG, "%s %u", __FUNCTION__, millis);
  usleep(millis * 1000);

  uint8_t buffer[m_sink_frameSize];
  memset(buffer, 0x00, m_sink_frameSize);
  uint8_t *bufferptr = buffer;
  AddPackets(&bufferptr, m_sink_frameSize, 0, 0);
}

void CAESinkDARWINAVF::GetDelay(AEDelayStatus &status)
{
  double sinkbuffer_seconds_to_empty = m_sinkbuffer_sec_per_byte * (double)m_sinkbuffer_level;
  status.SetDelay(sinkbuffer_seconds_to_empty);
}

double CAESinkDARWINAVF::GetCacheTotal()
{
  return m_sinkbuffer_sec_per_byte * (double)m_sinkbuffer_size;
}

unsigned int CAESinkDARWINAVF::AddPackets(uint8_t **data, unsigned int frames, unsigned int offset, int64_t timestamp)
{
  uint8_t *buffer = data[0] + (offset * m_format.m_frameSize);
  uint16_t firstWord = buffer[0] << 8 | buffer[1];
  CLog::Log(LOGDEBUG, "%s firstWord(0x%4.4X), frames(%u), offset(%u)", __FUNCTION__, firstWord, frames, offset);

  //if (m_avsink && firstWord == 0x0B77)
  //  [m_avsink addPackets:buffer withBytes:frames];

  int written = 0;
  int retries = 10;
  int size = frames;
  while (!m_bStop && written < size && --retries)
  {
    unsigned int max_bytes = m_sinkbuffer_size - m_sinkbuffer_level;
    if (frames > max_bytes)
    {
      int sleep_time_ms = m_format.m_streamInfo.GetDuration();
      usleep(sleep_time_ms * 1000);
      continue;
    }

    m_sinkbuffer_level += frames;
    written = frames;
    m_wake.Set();
  }

  return written;
}

void CAESinkDARWINAVF::Drain()
{
  m_draining = true;
  m_wake.Set();
}

bool CAESinkDARWINAVF::HasVolume()
{
  return false;
}

void CAESinkDARWINAVF::EnumerateDevicesEx(AEDeviceInfoList &list, bool force)
{
  m_devices.clear();
  EnumerateDevices(m_devices);
  list = m_devices;
}

void CAESinkDARWINAVF::Process()
{
  CLog::Log(LOGDEBUG, "CAESinkDARWINAVF::Process");

  // The object has been created and waiting to play,
  m_inited.Set();
  // yield to give other threads a chance to do some work.
  Sleep(0);

  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);
  while (!m_bStop)
  {
    if (m_draining)
    {
      // TODO: is it correct to not take data at the appropriate rate while draining?
      m_sinkbuffer_level = 0;
      m_draining = false;
    }

    // pretend we have a 64k audio buffer
    unsigned int min_buffer_size = m_sink_frameSize * 20;
    unsigned int read_bytes = m_sinkbuffer_level;
    if (read_bytes > min_buffer_size)
      read_bytes = min_buffer_size;

    if (read_bytes > 0)
    {
      // drain it
      m_sinkbuffer_level -= read_bytes;

      // we MUST drain at the correct audio sample rate
      // or the NULL sink will not work right. So calc
      // an approximate sleep time.
      double seconds = (double)read_bytes * m_sinkbuffer_sec_per_byte;
      usleep(seconds * 1000000);
    }

    if (m_sinkbuffer_level == 0)
    {
      // sleep this audio thread, we will get woken when we have audio data.
      m_wake.WaitMSec(250);
    }
  }
  SetPriority(THREAD_PRIORITY_NORMAL);
}
