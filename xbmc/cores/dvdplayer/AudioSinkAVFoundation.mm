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

#pragma mark - AudioResourceLoader
@interface AudioResourceLoader : NSObject <AVAssetResourceLoaderDelegate>
@property (nonatomic) bool abortflag;
@property (nonatomic) unsigned int frameBytes;
@property (nonatomic) char *readbuffer;
@property (nonatomic) size_t readbufferSize;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) unsigned int transferCount;
@property (nonatomic) NSData *dataAtOffsetZero;
- (id)initWithFrameBytes:(unsigned int)frameBytes;
- (void)abort;
- (int)write:(uint8_t*)data size:(unsigned int)size;
- (double)bufferSeconds;
- (double)transferSeconds;
@end

@implementation AudioResourceLoader
- (id)initWithFrameBytes:(unsigned int)frameBytes
{
  self = [super init];
  if (self)
  {
    _abortflag = false;
    _frameBytes = frameBytes;
    _readbufferSize = _frameBytes * 64;
    _avbuffer = new AERingBuffer(_readbufferSize);
    // make readbuffer one frame larger
    _readbufferSize += _frameBytes;
    _readbuffer = new char[_readbufferSize];
    _transferCount = 0;
  }
  return self;
}

- (void)dealloc
{
  SAFE_DELETE(_avbuffer);
  SAFE_DELETE(_readbuffer);
}

- (NSError *)loaderCancelledError
{
  NSError *error = [[NSError alloc] initWithDomain:@"AudioResourceLoaderErrorDomain"
    code:-1 userInfo:@{NSLocalizedDescriptionKey:@"avloader cancelled"}];
  _abortflag = true;
  return error;
}

#define logDataRequestEndOf 0
#define logDataRequestBgnEnd 0
#define logDataRequestSending 0
- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
  AVAssetResourceLoadingContentInformationRequest* contentRequest = loadingRequest.contentInformationRequest;

  if (_abortflag)
  {
    [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
    return YES;
  }

  if (contentRequest)
  {
    // only handles eac3
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
      size_t requestedBytes = _frameBytes * 2;
      if (dataRequest.requestedOffset == 0)
      {
        // 2) above. make sure avplayer has enough frame blocks at startup
        requestedBytes = _frameBytes * 36;
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
        if (_abortflag)
        {
          [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
#if logDataRequestBgnEnd
          CLog::Log(LOGDEBUG, "avloader dataRequest end");
#endif
          return YES;
        }
        usleep(10 * 1000);
      }
      if (dataRequest.requestsAllDataToEndOfResource == YES && dataRequest.requestedLength > (long)requestedBytes)
      {
        // calc how many complete frames are present
        size_t maxFrameBytes = _frameBytes * (_avbuffer->GetReadSize() / _frameBytes);
        // limit to size of _readbuffer
        if (maxFrameBytes > (_readbufferSize))
          maxFrameBytes = _frameBytes * ((_readbufferSize) / _frameBytes);
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

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  _abortflag = true;
  CLog::Log(LOGDEBUG, "avloader didCancelLoadingRequest");
}

- (void)abort
{
  _abortflag = true;
}

- (int)write:(uint8_t*)data size:(unsigned int)size
{
  if (data && size > 0)
  {
    int ok = AE_RING_BUFFER_OK;
    if (_avbuffer->Write(data, size) == ok)
      return size;
  }

  return 0;
}

- (double)bufferSeconds
{
  return ((double)_avbuffer->GetReadSize() / _frameBytes) * 0.032;
}

- (double)transferSeconds
{
  return ((double)_transferCount / _frameBytes) * 0.032;;
}
@end

#pragma mark - AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AVPlayerSink : NSObject
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)close;
- (int)addPackets:(uint8_t*)data size:(unsigned int)size;
- (void)play:(bool) state;
- (void)start;
- (bool)loaded;
- (void)flush;
- (double)clockSeconds;
- (double)bufferSeconds;
- (double)sinkBufferSeconds;
@end

@interface AVPlayerSink ()
@property (nonatomic) bool loadedFlag;
@property (nonatomic) dispatch_queue_t serialQueue;
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) NSString *contentType;
@property (nonatomic) AudioResourceLoader *avLoader;
@end

@implementation AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _loadedFlag = false;
    _frameSize = frameSize;
    _serialQueue = dispatch_queue_create("com.mrmc.loaderqueue", DISPATCH_QUEUE_SERIAL);

    _avplayer = [[AVPlayer alloc] init];
    // this little gem primes avplayer so next
    // load with a playerItem is fast... go figure.
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *filepath = [bundle pathForResource:@"point1sec" ofType:@"mp3"];
    AVPlayerItem *playerItem = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:filepath]];
    [_avplayer replaceCurrentItemWithPlayerItem:playerItem];
    [_avplayer play];
  }
  if (![[[NSBundle mainBundle] bundleIdentifier] hasPrefix:[NSString stringWithUTF8String:"tv.mrmc.mrmc"]])
    return nil;

  return self;
}

- (void)close
{
  [_avplayer.currentItem.asset cancelLoading];
  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  _avplayer = nullptr;
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
}

- (void)dealloc
{
  [self close];
}

//-----------------------------------------------------------------------------------
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _playerItem && [keyPath isEqualToString:@"status"])
  {
      if (_avplayer.currentItem.status == AVPlayerItemStatusReadyToPlay)
      {
        if (abs([_avplayer rate]) > 0.0)
          _loadedFlag = true;
        else
        {
          // we can only preroll if not playing (rate == 0.0)
          [_avplayer prerollAtRate:2.0 completionHandler:^(BOOL finished)
          {
            // set loaded regardless of finished or not.
            _loadedFlag = true;
          }];
        }
        CLog::Log(LOGDEBUG, "avloader AVPlayerItemStatusReadyToPlay loaded %d", _loadedFlag);
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
    return [_avLoader write:data size:size];

  return size;
}

- (void)play:(bool)state
{
  if (state)
    [_avplayer playImmediatelyAtRate:1.0];
  else
    [_avplayer pause];
}

- (void)start
{
/*
  for (NSString *mime in [AVURLAsset audiovisualTypes])
    NSLog(@"AVURLAsset audiovisualTypes:%@", mime);

  for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
*/
  // run on our own serial queue, keeps main thread from stalling us
  // and lets us do long sleeps waiting for data without stalling main thread.
  dispatch_sync(_serialQueue, ^{
    NSString *extension = @"ec3";
    // needs leading dir ('fake') or pathExtension in resourceLoader will fail
    NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
    [url appendString:extension];
    NSURL *streamURL = [NSURL URLWithString: url];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:streamURL options:nil];
    _avLoader = [[AudioResourceLoader alloc] initWithFrameBytes:_frameSize];
    [asset.resourceLoader setDelegate:_avLoader queue:_serialQueue];

    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    [_playerItem addObserver:self forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nil];

    [_avplayer replaceCurrentItemWithPlayerItem:_playerItem];
    _avplayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
    _avplayer.automaticallyWaitsToMinimizeStalling = NO;
    _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
  });
}

- (bool)loaded
{
  return _loadedFlag;
}

- (void)flush
{
  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
  [self start];
}

- (double)clockSeconds
{
  CMTime currentTime = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(currentTime);
  if (sink_s > 0.0)
    return sink_s;
  else
    return 0.0;
}

- (double)bufferSeconds
{
  double avbufferSeconds = [_avLoader bufferSeconds];
  double transferSeconds = [_avLoader transferSeconds];
  double playerSeconds = [self clockSeconds];
  double seconds = (transferSeconds - playerSeconds) + avbufferSeconds;
  if (seconds < 0.0)
    seconds = 0.0;

  return seconds;
}

- (double)sinkBufferSeconds
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
      buffered_s = duration - [self clockSeconds];
      if (buffered_s < 0.0)
        buffered_s = 0.0;
    }
  }
  return buffered_s;
}

@end

#pragma mark - DolbyFrameParser
// Undefined AC3 stream type.
constexpr static int STREAM_TYPE_UNDEFINED = -1;
// Type 0 AC3 stream type. See ETSI TS 102 366 E.1.3.1.1.
// These frames comprise an independent stream or substream.
// The programme may be decoded independently of any other
// substreams that might exist in the bit stream
constexpr static int STREAM_TYPE_TYPE0 = 0;
// Type 1 AC3 stream type. See ETSI TS 102 366 E.1.3.1.1.
// These frames comprise a dependent substream. The programme
// shall be decoded in conjunction with the independent
// substream with which it is associated
constexpr static int STREAM_TYPE_TYPE1 = 1;
// Type 2 AC3 stream type. See ETSI TS 102 366 E.1.3.1.1.
// These frames comprise an independent stream or substream
// that was previously coded in AC-3. Type 2 streams shall be
// independently decodable, and may not have any dependent
// streams associated with them.
constexpr static int STREAM_TYPE_TYPE2 = 2;
// The number of new samples per (E-)AC-3 audio block.
constexpr static int AUDIO_SAMPLES_PER_AUDIO_BLOCK = 256;
// Each syncframe has 6 blocks that provide 256 new audio samples. See ETSI TS 102 366 4.1.
constexpr static int AC3_SYNCFRAME_AUDIO_SAMPLE_COUNT = 6 * AUDIO_SAMPLES_PER_AUDIO_BLOCK;
// Sample rates, indexed by fscod.
constexpr static int SAMPLE_RATE_BY_FSCOD[] = {48000, 44100, 32000};
// Sample rates, indexed by fscod2 (E-AC-3).
constexpr static int SAMPLE_RATE_BY_FSCOD2[] = {24000, 22050, 16000};
// Channel counts, indexed by acmod.
constexpr static int CHANNEL_COUNT_BY_ACMOD[] = {2, 1, 2, 3, 3, 4, 4, 5};
// Number of audio blocks per E-AC-3 syncframe, indexed by numblkscod.
constexpr static int BLOCKS_PER_SYNCFRAME_BY_NUMBLKSCOD[] = {1, 2, 3, 6};
constexpr static int BLOCKS_PER_SYNCFRAME_BY_NUMBLKSCOD_LENGTH = 4;
// Nominal bitrates in kbps, indexed by frmsizecod / 2. (See ETSI TS 102 366 table 4.13.)
constexpr static int BITRATE_BY_HALF_FRMSIZECOD[] = {32, 40, 48, 56, 64, 80, 96,
      112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640};
// 16-bit words per syncframe, indexed by frmsizecod / 2. (See ETSI TS 102 366 table 4.13.)
constexpr static int SYNCFRAME_SIZE_WORDS_BY_HALF_FRMSIZECOD_44_1[] = {69, 87, 104,
      121, 139, 174, 208, 243, 278, 348, 417, 487, 557, 696, 835, 975, 1114, 1253, 1393};
constexpr static int SYNCFRAME_SIZE_WORDS_BY_HALF_FRMSIZECOD_44_1_LENGTH = 19;

class CDolbyFrameParser
{
  public:
   ~CDolbyFrameParser()
    {
      SAFE_DELETE(m_bs);
    };

    std::string parse(const uint8_t *buf, int len);
 private:
    void parse_ac3();
    int  ac3SyncframeSize(int fscod, int frmsizecod);
    void parse_ec3();
    void parse_eac3_bsi();
    void analyze_ac3_skipfld();
    bool parse_ac3_emdf();

    CBitstreamReader *m_bs = nullptr;
    std::string mimeType;
    uint16_t syncword = 0;
    uint16_t crc1 = 0;
    int streamType = STREAM_TYPE_UNDEFINED;
    int substreamid;
    int sampleRate;
    int acmod;
    int frameSize;
    int sampleCount;
    bool lfeon;
    int channelCount;
    int bsid;
    int fscod;
    int frmsizecod;
    int audioBlocks;
    int numblkscod;
};

std::string CDolbyFrameParser::parse(const uint8_t *buf, int len)
{
  // assume correct endian for arch
  if (buf[0] != 0x0b || buf[1] != 0x77)
    return "";

  bsid = buf[5] >> 3;

  mimeType = "";
  SAFE_DELETE(m_bs);
  m_bs = new CBitstreamReader(buf, len);

  if (bsid <= 10)
    parse_ac3();
  else
    parse_ec3();

  return mimeType;
}

void CDolbyFrameParser::parse_ac3()
{
  //---- byte 0 -> byte 1
  syncword = m_bs->ReadBits(16);
  //---- byte 2 -> byte 3
  crc1 = m_bs->ReadBits(16);
  //---- byte 4
  fscod = m_bs->ReadBits(2);
  int frmsizecod = m_bs->ReadBits(6);
  frameSize = ac3SyncframeSize(fscod, frmsizecod);
  if (frameSize <= 0)
    return;

  //---- byte 5
  m_bs->SkipBits(5 + 3);  // bsid, bsmod
  acmod = m_bs->ReadBits(3);
  if ((acmod & 0x01) != 0 && acmod != 1)
    m_bs->SkipBits(2);    // cmixlev
  if ((acmod & 0x04) != 0)
    m_bs->SkipBits(2);    // surmixlev
  if (acmod == 2)
    m_bs->SkipBits(2);    // dsurmod

  sampleRate = SAMPLE_RATE_BY_FSCOD[fscod];
  sampleCount = AC3_SYNCFRAME_AUDIO_SAMPLE_COUNT;
  lfeon = m_bs->ReadBits(1);
  channelCount = CHANNEL_COUNT_BY_ACMOD[acmod] + (lfeon ? 1 : 0);

  mimeType = "AC3";
}
int CDolbyFrameParser::ac3SyncframeSize(int fscod, int frmsizecod)
{
  int halfFrmsizecod = frmsizecod / 2;
  if (fscod < 0 || fscod >= BLOCKS_PER_SYNCFRAME_BY_NUMBLKSCOD_LENGTH || frmsizecod < 0
      || halfFrmsizecod >= SYNCFRAME_SIZE_WORDS_BY_HALF_FRMSIZECOD_44_1_LENGTH) {
    // Invalid values provided.
    return 0;
  }
  int sampleRate = SAMPLE_RATE_BY_FSCOD[fscod];
  if (sampleRate == 44100) {
    return 2 * (SYNCFRAME_SIZE_WORDS_BY_HALF_FRMSIZECOD_44_1[halfFrmsizecod] + (frmsizecod % 2));
  }
  int bitrate = BITRATE_BY_HALF_FRMSIZECOD[halfFrmsizecod];
  if (sampleRate == 32000) {
    return 6 * bitrate;
  } else { // sampleRate == 48000
    return 4 * bitrate;
  }
}

void CDolbyFrameParser::parse_ec3()
{
  parse_eac3_bsi();
}

void CDolbyFrameParser::parse_eac3_bsi()
{
}
/*
  // entry with stream pointing to syncword
  // Syntax from ETSI TS 102 366 V1.2.1 subsections E.1.2.1 and E.1.2.2.
  m_bs->SkipBits(16);          //skip syncword
  m_bs->SkipBits(2+3+11+2+2+3+1);      //strmtyp,substreamid,frmsiz,fscod,numblkscod,acmod,lfeon
  if(m_bs->GetBits(1))            //compre ..................................................................................... 1
    m_bs->SkipBits(8);          //{compr} ......................................................................... 8
  if(hdr->channel_mode == 0x0)       // if 1+1 mode (dual mono, so some items need a second value)
  {
    m_bs->SkipBits(5);          //dialnorm2 ............................................................................... 5
    if(m_bs->GetBits(1))          //compr2e ................................................................................. 1
      m_bs->SkipBits(8);        //{compr2} .................................................................... 8
  }
  if(hdr->frame_type == 0x1)        // if dependent stream
  {
    if(m_bs->GetBits(1))          //chanmape ................................................................................ 1
      m_bs->SkipBits(16);        // {chanmap} ................................................................. 16
  }
  // mixing metadata
  if(m_bs->GetBits(1))            //mixmdate ................................................................................... 1
  {
    if(hdr->channel_mode > 0x2)     // if more than 2 channels
      m_bs->SkipBits(2);        //{dmixmod} ................................. 2
    if((hdr->channel_mode & 0x1) && (hdr->channel_mode > 0x2)) // if three front channels exist
      m_bs->SkipBits(6);        //ltrtcmixlev,lorocmixlev .......................................................................... 3
    if(hdr->channel_mode & 0x4)     // if a surround channel exists
      m_bs->SkipBits(6+3);        //ltrtsurmixlev,lorosurmixlev
    if(hdr->lfe_on)           // if the LFE channel exists
    {
      if(m_bs->GetBits(1))         //lfemixlevcode
        m_bs->SkipBits(5);      //lfemixlevcod
    }
    if(hdr->frame_type == 0x0)       // if independent stream
    {
      if(m_bs->GetBits(1))         //pgmscle
        m_bs->SkipBits(6);      //pgmscl
      if(hdr->channel_mode == 0x0)   // if 1+1 mode (dual mono, so some items need a second value)
      {
        if(m_bs->GetBits(1))       //pgmscl2e
          m_bs->SkipBits(6);    //pgmscl2
      }
      if(m_bs->GetBits(1))         //extpgmscle
        m_bs->SkipBits(6);      //extpgmscl
      uint8_t mixdef = m_bs->GetBits(2);
      if(mixdef == 0x1)         // mixing option 2
        m_bs->SkipBits(1+1+3);    //premixcmpsel, drcsrc, premixcmpscl
      else if(mixdef == 0x2)       // mixing option 3 {
        m_bs->SkipBits(12);
      }
      else if(mixdef == 0x3)       // mixing option 4
      {
        uint8_t mixdeflen = m_bs->GetBits(5);  //mixdeflen
        if (m_bs->GetBits(1))      //mixdata2e
        {
          m_bs->SkipBits(1+1+3);  //premixcmpsel,drcsrc,premixcmpscl
          if(m_bs->GetBits(1))     //extpgmlscle
            m_bs->SkipBits(4);  //extpgmlscl
          if(m_bs->GetBits(1))     //extpgmcscle
            m_bs->SkipBits(4);  //extpgmcscl
          if(m_bs->GetBits(1))     //extpgmrscle
            m_bs->SkipBits(4);  //extpgmrscl
          if(m_bs->GetBits(1))     //extpgmlsscle
            m_bs->SkipBits(4);  //extpgmlsscl
          if(m_bs->GetBits(1))     //extpgmrsscle
            m_bs->SkipBits(4);  //extpgmrsscl
          if(m_bs->GetBits(1))     //extpgmlfescle
            m_bs->SkipBits(4);  //extpgmlfescl
          if(m_bs->GetBits(1))     //dmixscle
            m_bs->SkipBits(4);  //dmixscl
          if (m_bs->GetBits(1))    //addche
          {
            if(m_bs->GetBits(1))  //extpgmaux1scle
              m_bs->SkipBits(4);//extpgmaux1scl
            if(m_bs->GetBits(1))  //extpgmaux2scle
              m_bs->SkipBits(4);//extpgmaux2scl
          }
        }
        if(m_bs->GetBits(1))      //mixdata3e
        {
          m_bs->SkipBits(5);    //spchdat
          if(m_bs->GetBits(1))    //addspchdate
          {
            m_bs->SkipBits(5+2);  //spchdat1,spchan1att
            if(m_bs->GetBits(1))  //addspchdat1e
              m_bs->SkipBits(5+3);  //spchdat2,spchan2att
          }
        }
        //mixdata ........................................ (8*(mixdeflen+2)) - no. mixdata bits
        m_bs->SkipBytes(mixdeflen + 2);
        //mixdatafill ................................................................... 0 - 7
        //used to round up the size of the mixdata field to the nearest byte
        m_bs->ByteAlign();
      }
      if(hdr->channel_mode < 0x2) // if mono or dual mono source
      {
        if(m_bs->GetBits(1))      //paninfoe
          m_bs->SkipBits(8+6);    //panmean,paninfo
        if(hdr->channel_mode == 0x0) // if 1+1 mode (dual mono - some items need a second value)
        {
          if(m_bs->GetBits(1))    //paninfo2e
            m_bs->SkipBits(8+6);  //panmean2,paninfo2
        }
      }
      // mixing configuration information
      if(m_bs->GetBits(1))        //frmmixcfginfoe
      {
        if(hdr->num_blocks == 1) {  //if(numblkscod == 0x0)
          m_bs->SkipBits(5);    //blkmixcfginfo[0]
        }
        else
        {
          for(int blk = 0; blk < hdr->num_blocks; blk++)
          {
            if(m_bs->GetBits(1))  //blkmixcfginfoe[blk]
              m_bs->SkipBits(5);//blkmixcfginfo[blk]
          }
        }
      }
    }
  }
  // informational metadata
  if(m_bs->GetBits(1))            //infomdate
  {
    m_bs->SkipBits(3+1+1);        //bsmod,copyrightb,origbs
    if(hdr->channel_mode == 0x2)    // if in 2/0 mode
      m_bs->SkipBits(2+2);        //dsurmod,dheadphonmod
    if(hdr->channel_mode >= 0x6)     // if both surround channels exist
      m_bs->SkipBits(2);        //dsurexmod
    if(m_bs->GetBits(1))          //audprodie
      m_bs->SkipBits(5+2+1);      //mixlevel,roomtyp,adconvtyp
    if(hdr->channel_mode == 0x0)    // if 1+1 mode (dual mono, so some items need a second value)
    {
      if(m_bs->GetBits(1))        //audprodi2e
        m_bs->SkipBits(5+2+1);    //mixlevel2,roomtyp2,adconvtyp2
    }
    if(hdr->sr_code < 0x3)         // if not half sample rate
      m_bs->SkipBits(1);        //sourcefscod
  }
  if(hdr->frame_type == 0x0 && hdr->num_blocks != 6)  //(numblkscod != 0x3)
    m_bs->SkipBits(1);          //convsync
  if(hdr->frame_type == 0x2)         // if bit stream converted from AC-3
  {
    uint8_t blkid = 0;
    if(hdr->num_blocks == 6)       // 6 blocks per syncframe
      blkid = 1;
    else
      blkid = m_bs->GetBits(1);
    if(blkid)
      m_bs->SkipBits(6);        //frmsizecod
  }
  if(m_bs->GetBits(1))            //addbsie
  {
    uint8_t addbsil = m_bs->GetBits(6) + 1;//addbsil
    m_bs->SkipBytes(addbsil);        //addbsi
  }
}
*/
//Atmos in E-AC-3 is detected by looking for OAMD payload (see ETSI TS 103 420)
// in EMDF container in skipfld inside audblk() of E-AC-3 frame (see ETSI TS 102 366)
void CDolbyFrameParser::analyze_ac3_skipfld()
{
  // take a look at frame's auxdata()
  if (bsid != 16)
    return;

  uint16_t emdf_syncword;
  while (m_bs->GetRemainingBits() > 16)
  {
    emdf_syncword = m_bs->PeekBits(16);
    if (emdf_syncword == 0x5838) //NB! m_bs->head still points to emdf syncword because we peeked and did not get the bits!
    {
      if (parse_ac3_emdf())
        break;
    }
    else
    {
      //EMDF syncword may start anywhere in stream, also mid-byte!
      m_bs->SkipBits(1);
    }
  }
}
bool CDolbyFrameParser::parse_ac3_emdf()
{
  return false;
}


#pragma mark - CAVSink
class CAVSink
{
  public:
   ~CAVSink()
    {
      close();
    };

    bool open(const int framebytes)
    {
      m_frameSize = framebytes;
      m_avsink = [[AVPlayerSink alloc] initWithFrameSize:m_frameSize];
      if (!m_avsink)
        return false;
      [m_avsink start];
      return true;
    };
    // close, going away
    void close()
    {
      if (m_avsink)
      {
        [m_avsink close];
        m_avsink = nullptr;
      }
    };
    // add frame packets to sink buffers
    int write(const uint8_t *buf, int len)
    {
      int written = 0;
      if (m_avsink)
        written = [m_avsink addPackets:(uint8_t*)buf size:len];
      return written;
    };
    // start/stop audio playback
    void play(const bool playpause)
    {
      if (m_avsink)
        [m_avsink play:playpause];
    };
    // flush audio playback buffers
    void flush()
    {
      if (m_avsink)
        [m_avsink flush];
    };
    // return true when sink is ready to output audio after filling
    bool ready()
    {
      if (m_avsink)
        return [m_avsink loaded];
      return false;
    };
    // time in seconds of when sound hits your ears
    double time_s()
    {
      if (m_avsink)
        return [m_avsink clockSeconds];
      return 0.0;
    };
    // delay in seconds of adding data before it hits your ears
    double delay_s()
    {
      if (m_avsink)
        return [m_avsink bufferSeconds];
      return 0.0;
    };
    // alternative delay_s method
    double delay2_s()
    {
      if (m_avsink)
        return [m_avsink sinkBufferSeconds];
      return 0.0;
    };
    double mindelay_s()
    {
      return 2.5;
    };
    double maxdelay_s()
    {
      return 3.0;
    };
private:
  int m_frameSize = 0;
  AVPlayerSink *m_avsink = nullptr;
};

#pragma mark - CAudioSinkAVFoundation
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CAudioSinkAVFoundation::CAudioSinkAVFoundation(volatile bool &bStop, CDVDClock *clock)
: CThread("CAudioSinkAVFoundation")
, m_bStop(bStop)
, m_pClock(clock)
, m_startPtsSeconds(0)
, m_startPtsFlag(false)
, m_sink(nullptr)
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::CAudioSinkAVFoundation");
  m_syncErrorDVDTime = 0.0;
  m_syncErrorDVDTimeSecondsOld = 0.0;
}

CAudioSinkAVFoundation::~CAudioSinkAVFoundation()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::~CAudioSinkAVFoundation");
  SAFE_DELETE(m_parser);
  SAFE_DELETE(m_sink);
}

bool CAudioSinkAVFoundation::Create(const DVDAudioFrame &audioframe, AVCodecID codec, bool needresampler)
{
  if (codec != AV_CODEC_ID_EAC3)
    return false;

  CLog::Log(LOGNOTICE,
    "Creating audio stream (codec id: %i, channels: %i, sample rate: %i, %s)",
    codec,
    audioframe.format.m_channelLayout.Count(),
    audioframe.format.m_sampleRate,
    audioframe.passthrough ? "pass-through" : "no pass-through"
  );

  CSingleLock lock(m_critSection);

  CAEFactory::Suspend();
  // wait for AE to suspend
  XbmcThreads::EndTime timer(250);
  while (!CAEFactory::IsSuspended() && !timer.IsTimePast())
    usleep(1 * 1000);

  // EAC3 can have 1,2,3 or 6 audio blocks of 256 bytes per sync frame
  m_frameSize = audioframe.format.m_streamInfo.m_ac3FrameSize;
  m_startPtsFlag = false;
  m_startPtsSeconds = 0;
  if (m_sink)
  {
    m_sink->close();
    SAFE_DELETE(m_sink);
  }
  m_sink = new CAVSink();
  if (!m_sink->open(m_frameSize))
    return false;

  //m_parser = new CDolbyFrameParser();

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

  if (m_sink)
    m_sink->close();
  SAFE_DELETE(m_sink);

  CAEFactory::Resume();
  // wait for AE to wake
  XbmcThreads::EndTime timer(250);
  while (CAEFactory::IsSuspended() && !timer.IsTimePast())
    usleep(1 * 1000);
}

unsigned int CAudioSinkAVFoundation::AddPackets(const DVDAudioFrame &audioframe)
{
  CSingleLock lock (m_critSection);
  if (m_parser)
  {
    std::string dolbyformat = m_parser->parse(audioframe.data[0], audioframe.nb_frames);
    CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::AddPackets dolbyformat %s", dolbyformat.c_str());
  }

  m_abortAddPacketWait = false;
  double syncErrorSeconds = CalcSyncErrorSeconds();
  if (abs(syncErrorSeconds) > 0.020 ||
      abs(syncErrorSeconds - m_syncErrorDVDTimeSecondsOld) > 0.020)
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
    double buffer_s = 0.0;
    double minbuffer_s = 1.0;
    if (m_sink)
    {
      written = m_sink->write(audioframe.data[0], audioframe.nb_frames);
      buffer_s = m_sink->delay_s();
      minbuffer_s = m_sink->mindelay_s();
    }
    // native sink needs about 2.5 seconds of buffer
    // it will get stuttery below 2 seconds as it pauses to
    // wait for internal buffers to fill.
    if (buffer_s > minbuffer_s)
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
  // let audio play out and wait for it to end.
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Drain");
  double delay_s = m_sink->delay_s();
  XbmcThreads::EndTime timer(delay_s * 1000);
  while (!m_bStop && !timer.IsTimePast())
  {
    delay_s = m_sink->delay_s();
    if (delay_s <= 0.0)
      return;

    sleep(50);
  }
}

void CAudioSinkAVFoundation::Flush()
{
  m_abortAddPacketWait = true;

  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Flush");
  m_startPtsFlag = false;
  m_startPtsSeconds = 0;
  if (m_sink)
    m_sink->flush();
}

void CAudioSinkAVFoundation::Pause()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Pause");
  if (m_sink)
    m_sink->play(false);
}

void CAudioSinkAVFoundation::Resume()
{
  CSingleLock lock(m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Resume");
  if (m_sink)
    m_sink->play(true);
}

double CAudioSinkAVFoundation::GetDelay()
{
  // Returns the time (dvd timebase) that it will take
  // for the next added packet to be heard from the speakers.
  // 1) used as audio cachetime in player during startup
  // 2) in DVDPlayerAudio during RESYNC
  // 3) and internally to offset passed pts in AddPackets
  CSingleLock lock (m_critSection);
  double delay_s = 0.3;
  if (m_sink)
    delay_s = m_sink->delay_s();
  return delay_s * DVD_TIME_BASE;
}

double CAudioSinkAVFoundation::GetMaxDelay()
{
  // returns total time (seconds) of audio in AE for the stream
  // used as audio cachetotal in player during startup
  CSingleLock lock (m_critSection);
  double maxdelay_s = 0.3;
  if (m_sink)
    maxdelay_s = m_sink->maxdelay_s();
  return maxdelay_s;
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
  double delay_s = 0.3;
  if (m_sink)
    delay_s = m_sink->delay_s();
  return delay_s;
}

double CAudioSinkAVFoundation::GetCacheTotal()
{
  // total cache time of stream in seconds
  // returns total time a stream can buffer
  // only used to signal start (cachetime >= cachetotal * 0.75)
  CSingleLock lock (m_critSection);
  double maxdelay_s = 0.3;
  if (m_sink)
    maxdelay_s = m_sink->maxdelay_s();
  return maxdelay_s;
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

  if (m_sink && m_sink->ready())
  {
    double absolute;
    double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
    double sink_s = m_sink->time_s() + m_startPtsSeconds;
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
  m_syncErrorDVDTime += correction;
}

double CAudioSinkAVFoundation::GetClock()
{
  // return clock time in milliseconds (corrected for starting pts)
  if (m_sink)
    return (m_sink->time_s() + m_startPtsSeconds) * 1000.0;
  return m_startPtsSeconds * 1000.0;
}

void CAudioSinkAVFoundation::Process()
{
  // debug monitoring thread
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Started");

  while (!m_bStop)
  {
    double sink_s = 0.0;
    double buffer_s = 0.0;
    double buffer2_s = 0.0;
    double absolute;
    double player_s = m_pClock->GetClock(absolute) / DVD_TIME_BASE;
    if (m_startPtsFlag && m_sink)
    {
      sink_s = m_sink->time_s() + m_startPtsSeconds;
      buffer_s = m_sink->delay_s();
      buffer2_s = m_sink->delay2_s();
    }
    CLog::Log(LOGDEBUG, "avloader buffer2_s (%f), buffer_s (%f), player_s(%f), sink_s(%f), delta(%f)",
      buffer2_s, buffer_s, player_s, sink_s, player_s - sink_s);

    Sleep(250);
  }

  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Process Stopped");
}
