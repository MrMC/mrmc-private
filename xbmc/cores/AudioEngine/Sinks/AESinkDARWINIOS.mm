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

#include "cores/AudioEngine/Sinks/AESinkDARWINIOS.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "cores/AudioEngine/Sinks/osx/CoreAudioHelpers.h"
#include "platform/darwin/DarwinUtils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "threads/Condition.h"
#include "windowing/WindowingFactory.h"

#include <AudioToolbox/AudioToolbox.h>
#import  <AVFoundation/AVAudioSession.h>

enum CAChannelIndex {
  CAChannel_PCM_2CHAN = 0,
  CAChannel_PCM_5CHAN = 1,
  CAChannel_PCM_6CHAN = 2,
  CAChannel_PCM_8CHAN = 3,
  CAChannel_PCM_DD5_0 = 4,
  CAChannel_PCM_DD5_1 = 5,
};

// Stereo (L R) (Stereo eanbled)
// 5.1 (L C R Ls Rs LFE) (DD5.1 enabled)
// 5.1 (L R LFE C Ls Rs) (DD5.1 disabled
// 7.1 (L R LFE C Ls Rs Rls Rrs) (auto)
static enum AEChannel CAChannelMap[6][9] = {
  { AE_CH_FL , AE_CH_FR , AE_CH_NULL },
  { AE_CH_FL,  AE_CH_FR,  AE_CH_FC , AE_CH_BL , AE_CH_BR , AE_CH_NULL },
  { AE_CH_FL , AE_CH_FR , AE_CH_LFE, AE_CH_FC , AE_CH_BL , AE_CH_BR , AE_CH_NULL },
  { AE_CH_FL , AE_CH_FR , AE_CH_LFE, AE_CH_FC , AE_CH_SL , AE_CH_SR , AE_CH_BL , AE_CH_BR , AE_CH_NULL },
  { AE_CH_FL , AE_CH_FC , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_NULL},
  { AE_CH_FL , AE_CH_FC , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_LFE, AE_CH_NULL },
};

enum NativeAudioSettings {
  kAudioStereo    = 0,
  kAudioDD5_1     = 1,
  kAudioAuto      = 2,
  kAudioAutoAtmos = 3,
  kAudioUnknown   = 4,
};

static NativeAudioSettings getNativeAudioSettings()
{
  NativeAudioSettings nativeAudioSettings = kAudioUnknown;

  AudioComponentDescription description = {0};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_RemoteIO;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;

  // Get component
  AudioUnit audioUnit;
  AudioComponent component;
  component = AudioComponentFindNext(NULL, &description);
  OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
  if (status == noErr)
  {
    // must initialize for AudioUnitGetPropertyXXXX to work
    status = AudioUnitInitialize(audioUnit);

    UInt32 layoutSize = 0;
    status = AudioUnitGetPropertyInfo(audioUnit,
      kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, 0, &layoutSize, nullptr);
    if (status == noErr)
    {
      AudioChannelLayout *layout = nullptr;
      layout = (AudioChannelLayout*)malloc(layoutSize);
      status = AudioUnitGetProperty(audioUnit,
        kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, 0, layout, &layoutSize);

      /*
      CFStringRef layoutName = nullptr;
      UInt32 propertySize = sizeof(layoutName);
      status = AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutName, layoutSize, layout, &propertySize, &layoutName);
      if (layoutName)
      {
        NSLog(@"ChannelLayoutName: %@", layoutName);
        CFRelease(layoutName);
      }

      status = AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutSimpleName, layoutSize, layout, &propertySize, &layoutName);
      // later
      if (layoutName)
      {
        NSLog(@"ChannelLayoutSimpleName: %@", layoutName);
        CFRelease(layoutName);
      }
      */

      // function returns noErr only if audio set to Stereo or DD5.1
      AudioChannelLayoutTag layoutTag = kAudioChannelLayoutTag_Unknown;
      UInt32 layoutTagSize = sizeof(layoutTagSize);
      status = AudioFormatGetProperty(kAudioFormatProperty_TagForChannelLayout, layoutSize, layout, &layoutTagSize, &layoutTag);
      //NSLog(@"mChannelLayoutTag: %u", layoutTag);
      if (layoutTag == kAudioChannelLayoutTag_Stereo)
      {
        CLog::Log(LOGNOTICE, "getNativeAudioSettings:Stereo");
        nativeAudioSettings = kAudioStereo;
      }
      else if (layoutTag == kAudioChannelLayoutTag_MPEG_5_1_C)
      {
        CLog::Log(LOGNOTICE, "getNativeAudioSettings:DD5.1");
        nativeAudioSettings = kAudioDD5_1;
      }
      else if (layoutTag == kAudioChannelLayoutTag_Unknown)
      {
        long maxChannels = [[AVAudioSession sharedInstance] maximumOutputNumberOfChannels];
        //NSLog(@"maxChannels: %ld", maxChannels);
        if (maxChannels > 8)
        {
          CLog::Log(LOGNOTICE, "getNativeAudioSettings:Auto, Atmos Enabled");
          nativeAudioSettings = kAudioAutoAtmos;
        }
        else
        {
          CLog::Log(LOGNOTICE, "getNativeAudioSettings:Auto, Atmos Disabled");
          nativeAudioSettings = kAudioAuto;
        }
      }
      else
      {
        CLog::Log(LOGNOTICE, "getNativeAudioSettings:Unknown. layoutTag: %u", layoutTag);
        nativeAudioSettings = kAudioAuto;
        CFStringRef layoutName = nullptr;
        UInt32 propertySize = sizeof(layoutName);
        status = AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutName, layoutSize, layout, &propertySize, &layoutName);
        if (layoutName)
        {
          const char *layoutNameCString =  [(__bridge NSString *)layoutName UTF8String];
          CLog::Log(LOGNOTICE, "getNativeAudioSettings:ChannelLayoutName: %s", layoutNameCString);
          CFRelease(layoutName);
        }
      }

      free(layout);
    }

  }

  AudioUnitUninitialize(audioUnit);
  AudioComponentInstanceDispose(audioUnit);

  return nativeAudioSettings;
}

static void dumpAVAudioSessionProperties()
{
  std::string route = CDarwinUtils::GetAudioRoute();
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:audio route = %s", route.empty() ? "NONE" : route.c_str());

  AVAudioSession *mySession = [AVAudioSession sharedInstance];

  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:sampleRate %f", [mySession sampleRate]);
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:outputLatency %f", [mySession outputLatency]);
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:IOBufferDuration %f", [mySession IOBufferDuration]);
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:outputNumberOfChannels %ld", (long)[mySession outputNumberOfChannels]);
  // maximumOutputNumberOfChannels provides hints to tvOS audio settings
  // if 2, then audio is set to two channel stereo. iOS return this unless hdmi connected
  // if 6, then audio is set to Digial Dolby 5.1 OR hdmi path detected sink can only handle 6 channels.
  // if 8, then audio is set to Best Quality AND hdmi path detected sink can handle 8 channels.
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:maximumOutputNumberOfChannels %ld", (long)[mySession maximumOutputNumberOfChannels]);
  int portChannelCount =  CDarwinUtils::GetAudioSessionOutputChannels();
  CLog::Log(LOGNOTICE, "dumpAVAudioSessionProperties:SessionPortOutputNumberOfChannels %d", portChannelCount);

  CDarwinUtils::DumpAudioDescriptions("dumpAVAudioSessionProperties");
}

static bool deactivateAudioSession(int count)
{
  if (--count < 0)
    return false;

  bool rtn = false;
  NSError *err = nullptr;
  // deactvivate the session
  AVAudioSession *mySession = [AVAudioSession sharedInstance];
  if (![mySession setActive: NO error: &err])
  {
    CLog::Log(LOGWARNING, "AVAudioSession setActive NO failed, count %d", count);
    usleep(10 * 1000);
    rtn = deactivateAudioSession(count);
  }
  else
  {
    rtn = true;
  }
  return rtn;
}

static void setAVAudioSessionProperties(NSTimeInterval bufferseconds, double samplerate, int channels)
{
  // darwin docs and technotes say,
  // deavtivate the session before changing the values
  AVAudioSession *mySession = [AVAudioSession sharedInstance];

  // need to fetch maximumOutputNumberOfChannels when active
  int maxchannels = [mySession maximumOutputNumberOfChannels];

  NSError *err = nullptr;
  // deactvivate the session
  if (!deactivateAudioSession(10))
    CLog::Log(LOGWARNING, "AVAudioSession setActive NO failed: %ld", (long)err.code);

  // change the number of channels
  if (channels > maxchannels)
    channels = maxchannels;
  err = nullptr;
  [mySession setPreferredOutputNumberOfChannels: channels error: &err];
  if (err != nullptr)
    CLog::Log(LOGWARNING, "setAVAudioSessionProperties:setPreferredOutputNumberOfChannels failed");

  // change the sameple rate
  err = nullptr;
  [mySession setPreferredSampleRate: samplerate error: &err];
  if (err != nullptr)
    CLog::Log(LOGWARNING, "setAVAudioSessionProperties:setPreferredSampleRate failed");

  // change the i/o buffer duration
  err = nullptr;
  [mySession setPreferredIOBufferDuration: bufferseconds error: &err];
  if (err != nullptr)
    CLog::Log(LOGWARNING, "setAVAudioSessionProperties:setPreferredIOBufferDuration failed");

  // reactivate the session
  if (![mySession setActive: YES error: &err])
    CLog::Log(LOGWARNING, "AVAudioSession setActive YES failed: %ld", (long)err.code);

  // check that we got the samperate what we asked for
  if (samplerate != [mySession sampleRate])
    CLog::Log(LOGWARNING, "sampleRate does not match: asked %f, is %f", samplerate, [mySession sampleRate]);

  // check that we got the number of channels what we asked for
  if (channels != [mySession outputNumberOfChannels])
    CLog::Log(LOGWARNING, "number of channels do not match: asked %d, is %ld", channels, (long)[mySession outputNumberOfChannels]);

}

#pragma mark - SineWaveGenerator
/***************************************************************************************/
/***************************************************************************************/
#if DO_440HZ_TONE_TEST
static void SineWaveGeneratorInitWithFrequency(SineWaveGenerator *ctx, double frequency, double samplerate)
{
  // Given:
  //   frequency in cycles per second
  //   2*PI radians per sine wave cycle
  //   sample rate in samples per second
  //
  // Then:
  //   cycles     radians     seconds     radians
  //   ------  *  -------  *  -------  =  -------
  //   second      cycle      sample      sample
  ctx->currentPhase = 0.0;
  ctx->phaseIncrement = frequency * 2*M_PI / samplerate;
}

static int16_t SineWaveGeneratorNextSampleInt16(SineWaveGenerator *ctx)
{
  int16_t sample = INT16_MAX * sinf(ctx->currentPhase);

  ctx->currentPhase += ctx->phaseIncrement;
  // Keep the value between 0 and 2*M_PI
  while (ctx->currentPhase > 2*M_PI)
    ctx->currentPhase -= 2*M_PI;

  return sample / 4;
}
static float SineWaveGeneratorNextSampleFloat(SineWaveGenerator *ctx)
{
  float sample = MAXFLOAT * sinf(ctx->currentPhase);
  
  ctx->currentPhase += ctx->phaseIncrement;
  // Keep the value between 0 and 2*M_PI
  while (ctx->currentPhase > 2*M_PI)
    ctx->currentPhase -= 2*M_PI;
  
  return sample / 4;
}
#endif

#pragma mark - CAAudioUnitSink
/***************************************************************************************/
/***************************************************************************************/
class CAAudioUnitSink
{
  public:
    CAAudioUnitSink();
   ~CAAudioUnitSink();

    bool         open(AudioStreamBasicDescription outputFormat, size_t buffer_size);
    bool         close();
    bool         activate();
    bool         deactivate();
    void         updatedelay(AEDelayStatus& status);
    double       buffertime();
    unsigned int sampletrate() { return m_outputFormat.mSampleRate; };
    unsigned int write(uint8_t *data, unsigned int frames, unsigned int framesize);
    void         drain();

  private:
    bool         setupAudio();
 
    // callbacks
    static OSStatus renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                  const AudioTimeStamp *inTimeStamp, UInt32 inOutputBusNumber, UInt32 inNumberFrames,
                  AudioBufferList *ioData);

    bool                m_setup;
    bool                m_activated;
    id                  m_observer;
    AudioUnit           m_audioUnit;
    AudioStreamBasicDescription m_outputFormat;
    AERingBuffer       *m_buffer;

    Float32             m_totalLatency;
    Float32             m_inputLatency;
    Float32             m_outputLatency;
    Float32             m_bufferDuration;

    unsigned int        m_sampleRate;
    unsigned int        m_frameSize;
    unsigned int        m_frames;

    std::atomic<bool>   m_started;

    CAESpinSection      m_render_section;
    std::atomic<int64_t>  m_render_timestamp;
};

CAAudioUnitSink::CAAudioUnitSink()
: m_activated(false)
, m_buffer(nullptr)
, m_started(false)
, m_render_timestamp(0)
{
}

CAAudioUnitSink::~CAAudioUnitSink()
{
  close();
}

bool CAAudioUnitSink::open(AudioStreamBasicDescription outputFormat, size_t buffer_size)
{
  m_setup         = false;
  m_outputFormat  = outputFormat;
  m_totalLatency = 0.0;
  m_inputLatency = 0.0;
  m_outputLatency = 0.0;
  m_bufferDuration= 0.0;
  m_sampleRate    = (unsigned int)outputFormat.mSampleRate;
  m_frameSize     = outputFormat.mChannelsPerFrame * outputFormat.mBitsPerChannel / 8;

  m_buffer = new AERingBuffer(buffer_size);

  return setupAudio();
}

bool CAAudioUnitSink::close()
{
  deactivate();
  SAFE_DELETE(m_buffer);
  m_started = false;

  return true;
}

bool CAAudioUnitSink::activate()
{
  if (!m_activated)
  {
    if (setupAudio())
    {
      AudioOutputUnitStart(m_audioUnit);
      m_activated = true;
    }
  }

  return m_activated;
}

bool CAAudioUnitSink::deactivate()
{
  if (m_activated)
  {
    AudioUnitReset(m_audioUnit, kAudioUnitScope_Global, 0);

    // this is a delayed call, the OS will block here
    // until the autio unit actually is stopped.
    AudioOutputUnitStop(m_audioUnit);

    // detach the render callback on the unit
    AURenderCallbackStruct callbackStruct = {0};
    AudioUnitSetProperty(m_audioUnit,
      kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
      0, &callbackStruct, sizeof(callbackStruct));

    AudioUnitUninitialize(m_audioUnit);
    AudioComponentInstanceDispose(m_audioUnit), m_audioUnit = nullptr;

    m_setup = false;
    m_activated = false;
  }

  return m_activated;
}

void CAAudioUnitSink::updatedelay(AEDelayStatus &status)
{
  // return the number of audio frames in buffer, in seconds
  // use internal framesize, once written,
  // bytes in buffer are owned by CAAudioUnitSink.
  unsigned int size;
  CAESpinLock lock(m_render_section);
  do
  {
    status.tick = m_render_timestamp;
    status.delay = 0;
    if(m_buffer)
      size = m_buffer->GetReadSize();
    else
      size = 0;
  } while(lock.retry());

  // bytes to seconds
  status.delay += (double)size / (double)m_frameSize / (double)m_sampleRate;
  // add in hw delay and total latency (in seconds)
  status.delay += m_totalLatency;
  //CLog::Log(LOGDEBUG, "%s size %f sec", __FUNCTION__, status.delay);
}

double CAAudioUnitSink::buffertime()
{
  // return the number of audio frames for the total buffer size, in seconds
  // use internal framesize, buffer is owned by CAAudioUnitSink.
  double buffertime;
  buffertime = (double)m_buffer->GetMaxSize() / (double)(m_frameSize * m_sampleRate);
  //CLog::Log(LOGDEBUG, "%s total %f bytes", __FUNCTION__, buffertime);
  return buffertime;
}

CCriticalSection mutex;
XbmcThreads::ConditionVariable condVar;

unsigned int CAAudioUnitSink::write(uint8_t *data, unsigned int frames, unsigned int framesize)
{
  // use the passed in framesize instead of internal,
  // writes are relative to AE formats. once written,
  // CAAudioUnitSink owns them.
  if (m_buffer->GetWriteSize() < frames * framesize)
  { // no space to write - wait for a bit
    CSingleLock lock(mutex);
    unsigned int timeout = 900 * frames / m_sampleRate;
    if (!m_started)
      timeout = 4500;

    // we are using a timer here for beeing sure for timeouts
    // condvar can be woken spuriously as signaled
    XbmcThreads::EndTime timer(timeout);
    condVar.wait(mutex, timeout);
    if (!m_started && timer.IsTimePast())
    {
      CLog::Log(LOGERROR, "%s engine didn't start in %d ms!", __FUNCTION__, timeout);
      return INT_MAX;
    }
  }

  unsigned int write_frames = std::min(frames, m_buffer->GetWriteSize() / framesize);
  if (write_frames)
    m_buffer->Write(data, write_frames * framesize);
  
  return write_frames;
}

void CAAudioUnitSink::drain()
{
  unsigned int bytes = m_buffer->GetReadSize();
  unsigned int totalBytes = bytes;
  int maxNumTimeouts = 3;
  unsigned int timeout = buffertime();

  while (bytes && maxNumTimeouts > 0)
  {
    CSingleLock lock(mutex);
    XbmcThreads::EndTime timer(timeout);
    condVar.wait(mutex, timeout);

    bytes = m_buffer->GetReadSize();
    // if we timeout and do not consume bytes,
    // decrease maxNumTimeouts and try again.
    if (timer.IsTimePast() && bytes == totalBytes)
      maxNumTimeouts--;
    totalBytes = bytes;
  }
}

bool CAAudioUnitSink::setupAudio()
{
  if (m_setup && m_audioUnit)
    return true;

  // Audio Unit Setup
  // Describe a default output unit.
  AudioComponentDescription description = {};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_RemoteIO;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;

  // Get component
  AudioComponent component;
  component = AudioComponentFindNext(NULL, &description);
  OSStatus status = AudioComponentInstanceNew(component, &m_audioUnit);
  if (status != noErr)
  {
    CLog::Log(LOGERROR, "CAAudioUnitSink::setupAudio:error creating audioUnit (error: %d)", (int)status);
    return false;
  }

  // set the hw buffer size (in seconds), this affects the number of samples
  // that get rendered every time the audio callback is fired.
  double samplerate = m_outputFormat.mSampleRate;
  int channels = m_outputFormat.mChannelsPerFrame;
  NSTimeInterval bufferseconds = 1024 * m_outputFormat.mChannelsPerFrame / m_outputFormat.mSampleRate;
  CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:setting channels %d", channels);
  CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:setting samplerate %f", samplerate);
  CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:setting buffer duration to %f", bufferseconds);
  setAVAudioSessionProperties(bufferseconds, samplerate, channels);

  // Get the real output samplerate, the requested might not avaliable
  Float64 realisedSampleRate = [[AVAudioSession sharedInstance] sampleRate];
  if (m_outputFormat.mSampleRate != realisedSampleRate)
  {
    CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:couldn't set requested samplerate %d, AudioUnit will resample to %d instead", (int)m_outputFormat.mSampleRate, (int)realisedSampleRate);
    // if we don't want AudioUnit to resample - but instead let activeae resample -
    // reflect the realised samplerate to the output format here
    // well maybe it is handy in the future - as of writing this
    // AudioUnit was about 6 times faster then activeae ;)
    //m_outputFormat.mSampleRate = realisedSampleRate;
    //m_sampleRate = realisedSampleRate;
  }

  // Set the output stream format
  UInt32 ioDataSize = sizeof(AudioStreamBasicDescription);
  status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
    kAudioUnitScope_Input, 0, &m_outputFormat, ioDataSize);
  if (status != noErr)
  {
    CLog::Log(LOGERROR, "CAAudioUnitSink::setupAudio:error setting stream format on audioUnit (error: %d)", (int)status);
    return false;
  }


  // Attach a render callback on the unit
  AURenderCallbackStruct callbackStruct = {0};
  callbackStruct.inputProc = renderCallback;
  callbackStruct.inputProcRefCon = this;
  status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback,
    kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct));
  if (status != noErr)
  {
    CLog::Log(LOGERROR, "CAAudioUnitSink::setupAudio:error setting render callback for AudioUnit (error: %d)", (int)status);
    return false;
  }

  status = AudioUnitInitialize(m_audioUnit);
	if (status != noErr)
  {
    CLog::Log(LOGERROR, "CAAudioUnitSink::setupAudio:error initializing AudioUnit (error: %d)", (int)status);
    return false;
  }

  AVAudioSession *mySession = [AVAudioSession sharedInstance];
  m_inputLatency  = [mySession inputLatency];
  m_outputLatency  = [mySession outputLatency];
  m_bufferDuration = [mySession IOBufferDuration];
  //m_totalLatency   = (m_inputLatency + m_bufferDuration) + (m_outputLatency + m_bufferDuration);
  m_totalLatency   = m_outputLatency + m_bufferDuration;
  CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:total latency = %f", m_totalLatency);

  m_setup = true;
  std::string formatString;
  CLog::Log(LOGNOTICE, "CAAudioUnitSink::setupAudio:setup audio format: %s",
    StreamDescriptionToString(m_outputFormat, formatString));

  dumpAVAudioSessionProperties();

  return m_setup;
}

inline void LogLevel(unsigned int got, unsigned int wanted)
{
  static unsigned int lastReported = INT_MAX;
  if (got != wanted)
  {
    if (got != lastReported)
    {
      CLog::Log(LOGWARNING, "DARWINIOS: %sflow (%u vs %u bytes)", got > wanted ? "over" : "under", got, wanted);
      lastReported = got;
    }    
  }
  else
    lastReported = INT_MAX; // indicate we were good at least once
}

OSStatus CAAudioUnitSink::renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
  const AudioTimeStamp *inTimeStamp, UInt32 inOutputBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
  CAAudioUnitSink *sink = (CAAudioUnitSink*)inRefCon;

  sink->m_render_section.enter();
  sink->m_started = true;

  for (unsigned int i = 0; i < ioData->mNumberBuffers; i++)
  {
    unsigned int wanted = ioData->mBuffers[i].mDataByteSize;
    unsigned int bytes = std::min(sink->m_buffer->GetReadSize(), wanted);
    sink->m_buffer->Read((unsigned char*)ioData->mBuffers[i].mData, bytes);
    //LogLevel(bytes, wanted);

    if (bytes == 0)
    {
      // Apple iOS docs say kAudioUnitRenderAction_OutputIsSilence provides a hint to
      // the audio unit that there is no audio to process. and you must also explicitly
      // set the buffers contents pointed at by the ioData parameter to 0.
      memset(ioData->mBuffers[i].mData, 0x00, ioData->mBuffers[i].mDataByteSize);
      *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    else if (bytes < wanted)
    {
      // zero out what we did not copy over (underflow)
      uint8_t *empty = (uint8_t*)ioData->mBuffers[i].mData + bytes;
      memset(empty, 0x00, wanted - bytes);
    }
  }

  sink->m_render_timestamp = inTimeStamp->mHostTime;
  sink->m_render_section.leave();
  // tell the sink we're good for more data
  condVar.notifyAll();

  return noErr;
}

#pragma mark - EnumerateDevices
/***************************************************************************************/
/***************************************************************************************/
static void EnumerateDevices(AEDeviceInfoList &list)
{
  CAEDeviceInfo device;

  device.m_deviceName = "default";
  device.m_displayName = "Default";
  device.m_displayNameExtra = "";

  // if not hdmi,  CAESinkDARWINIOS::Initialize will kick back to 2 channel PCM
  device.m_deviceType = AE_DEVTYPE_HDMI;

  device.m_sampleRates.push_back(44100);
  device.m_sampleRates.push_back(48000);

  device.m_dataFormats.push_back(AE_FMT_S16LE);
  device.m_dataFormats.push_back(AE_FMT_FLOAT);

  // allow passthrough if iOS/tvOS is 11.2 or below
  if (CDarwinUtils::GetIOSVersion() <= 11.2)
  {
    device.m_dataFormats.push_back(AE_FMT_RAW);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_AC3);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_EAC3);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_512);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_1024);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_2048);
    device.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTSHD_CORE);
  }
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
AEDeviceInfoList CAESinkDARWINIOS::m_devices;

CAESinkDARWINIOS::CAESinkDARWINIOS()
: m_audioSink(nullptr)
{
}

CAESinkDARWINIOS::~CAESinkDARWINIOS()
{
}

bool CAESinkDARWINIOS::Initialize(AEAudioFormat &format, std::string &device)
{
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

  AudioStreamBasicDescription audioFormat = {0};
  audioFormat.mFormatID = kAudioFormatLinearPCM;

  // check if are we dealing with raw formats or pcm
  bool passthrough = false;
  switch (format.m_dataFormat)
  {
    case AE_FMT_RAW:
      // this will be selected when AE wants AC3 or DTS or anything other then float
      format.m_dataFormat = AE_FMT_S16LE;
      audioFormat.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
      if (route.find("HDMI") != std::string::npos)
      {
        passthrough = true;
      }
      else
      {
        // this should never happen but we cover it just in case
        // for iOS/tvOS, if we are not hdmi, we cannot do raw
        // so kick back to pcm.
        format.m_dataFormat = AE_FMT_FLOAT;
        audioFormat.mFormatFlags |= kLinearPCMFormatFlagIsFloat;
      }
    break;
    default:
      // AE lies, even when we register formats we can handle,
      // it shoves everything down and it is up to the sink
      // to check/verify and kick back to what the sink supports
      format.m_dataFormat = AE_FMT_FLOAT;
      audioFormat.mFormatFlags |= kLinearPCMFormatFlagIsFloat;
    break;
  }

  // check and correct sample rates to what we support,
  // remember, AE is a lier and we need to check/verify
  // and kick back to what the sink supports
  switch(format.m_sampleRate)
  {
    case 11025:
    case 22050:
    case 44100:
    case 88200:
    case 176400:
#if defined(TARGET_DARWIN_TVOS)
      if (route.find("HDMI") != std::string::npos)
        audioFormat.mSampleRate = 48000;
      else
#endif
        audioFormat.mSampleRate = 44100;
      break;
    default:
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 32000:
    case 48000:
    case 96000:
    case 192000:
    case 384000:
      audioFormat.mSampleRate = 48000;
      break;
  }

  if (passthrough)
  {
    // passthrough is special, PCM encapsulated IEC61937 packets.
    // make sure input and output samplerate match for preventing resampling
    audioFormat.mSampleRate = [[AVAudioSession sharedInstance] sampleRate];
    audioFormat.mFramesPerPacket = 1; // must be 1
    audioFormat.mChannelsPerFrame= 2; // passthrough needs 2 channels
    audioFormat.mBitsPerChannel  = 16;
    audioFormat.mBytesPerFrame   = audioFormat.mChannelsPerFrame * (audioFormat.mBitsPerChannel >> 3);
    audioFormat.mBytesPerPacket  = audioFormat.mBytesPerFrame * audioFormat.mFramesPerPacket;
    audioFormat.mFormatFlags    |= kLinearPCMFormatFlagIsPacked;
  }
  else
  {
    UInt32 maxChannels = [[AVAudioSession sharedInstance] maximumOutputNumberOfChannels];
#if defined(TARGET_DARWIN_TVOS)
    // tvos supports up to 8 channels of pcm
    // clamp number of channels to what tvOS reports
    // maxChannels == 2 generally indicates Stereo
    // maxChannels == 6 could be 6 channel receiver or tvOS set to DD5.1
    if (maxChannels == 2)
      audioFormat.mChannelsPerFrame = maxChannels;
    else if (maxChannels == 6 && format.m_channelLayout.Count() >= 6)
      audioFormat.mChannelsPerFrame = maxChannels;
    else
      audioFormat.mChannelsPerFrame= format.m_channelLayout.Count();

#else
    // ios supports up to 2 channels (unless we are hdmi connected ? )
    audioFormat.mChannelsPerFrame= maxChannels;
#endif
    audioFormat.mFramesPerPacket = 1; // must be 1
    audioFormat.mBitsPerChannel  = CAEUtil::DataFormatToBits(format.m_dataFormat);
    audioFormat.mBytesPerFrame   = audioFormat.mChannelsPerFrame * (audioFormat.mBitsPerChannel >> 3);
    audioFormat.mBytesPerPacket  = audioFormat.mBytesPerFrame * audioFormat.mFramesPerPacket;
    audioFormat.mFormatFlags    |= kLinearPCMFormatFlagIsPacked;
  }

  std::string formatString;
  CLog::Log(LOGDEBUG, "CAESinkDARWINIOS::Initialize: AudioStreamBasicDescription: %s %s",
    StreamDescriptionToString(audioFormat, formatString), passthrough ? "passthrough" : "pcm");

#if DO_440HZ_TONE_TEST
  SineWaveGeneratorInitWithFrequency(&m_SineWaveGenerator, 440.0, audioFormat.mSampleRate);
#endif

  size_t buffer_size;
  switch (format.m_streamInfo.m_type)
  {
    case CAEStreamInfo::STREAM_TYPE_AC3:
      if (!format.m_streamInfo.m_frameSize)
        format.m_streamInfo.m_frameSize = 1536;
      format.m_frames = format.m_streamInfo.m_frameSize;
      buffer_size = format.m_frames * 8;
    break;
    case CAEStreamInfo::STREAM_TYPE_EAC3:
      if (!format.m_streamInfo.m_frameSize)
        format.m_streamInfo.m_frameSize = 1536;
      format.m_frames = format.m_streamInfo.m_frameSize;
      buffer_size = format.m_frames * 8;
    break;
    case CAEStreamInfo::STREAM_TYPE_DTS_512:
    case CAEStreamInfo::STREAM_TYPE_DTSHD_CORE:
      format.m_frames = 512;
      buffer_size = 16384;
    break;
    case CAEStreamInfo::STREAM_TYPE_DTS_1024:
      format.m_frames = 1024;
      buffer_size = 16384;
    break;
    case CAEStreamInfo::STREAM_TYPE_DTS_2048:
      format.m_frames = 2048;
      buffer_size = 16384;
    break;
    default:
      format.m_frames = 1024;
      buffer_size = (512 * audioFormat.mBytesPerFrame) * 8;
    break;
  }
  m_audioSink = new CAAudioUnitSink;
  m_audioSink->open(audioFormat, buffer_size);
  // reset to the realised samplerate
  format.m_sampleRate = m_audioSink->sampletrate();

  // channel setup has to happen after CAAudioUnitSink creation as
  // they might be change.
  if (!passthrough)
  {
    UInt32 maxChannels = [[AVAudioSession sharedInstance] maximumOutputNumberOfChannels];
    NativeAudioSettings nativeAudioSettings = getNativeAudioSettings();
    UInt32 portChannelCount =  CDarwinUtils::GetAudioSessionOutputChannels();
    CLog::Log(LOGNOTICE, "CAESinkDARWINIOS::Initialize: maxChannels %d, portChannelCount %d",
      maxChannels, portChannelCount);

    CAChannelIndex channel_index = CAChannel_PCM_6CHAN;
#if defined(TARGET_DARWIN_TVOS)
    if (maxChannels == 2 && format.m_channelLayout.Count() > 2)
    {
      // if 2, then audio is set to 2 channel PCM
      // aimed at handling airplay
      channel_index = CAChannel_PCM_2CHAN;
    }
    else if (maxChannels == 6 && format.m_channelLayout.Count() == 6)
    {
      // if audio is set to Digial Dolby 5.1, need to use DD mapping
      if (nativeAudioSettings == kAudioDD5_1)
        channel_index = CAChannel_PCM_DD5_1;
      else
        channel_index = CAChannel_PCM_6CHAN;
      if (maxChannels > 6)
        maxChannels = 6;
    }
    else if (format.m_channelLayout.Count() == 5)
    {
      // if audio is set to Digial Dolby 5.1, need to use DD mapping
      if (nativeAudioSettings == kAudioDD5_1)
        channel_index = CAChannel_PCM_DD5_0;
      else
        channel_index = CAChannel_PCM_5CHAN;
      if (maxChannels > 5)
        maxChannels = 5;
    }
    else
#endif
    {
      if (format.m_channelLayout.Count() > 6)
        channel_index = CAChannel_PCM_8CHAN;
    }

    CAEChannelInfo channel_info;
    for (size_t chan = 0; chan < format.m_channelLayout.Count(); ++chan)
    {
      if (chan < maxChannels)
        channel_info += CAChannelMap[channel_index][chan];
    }
    format.m_channelLayout = channel_info;
  }
  format.m_frameSize = format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(format.m_dataFormat) >> 3);

  m_format = format;

  if (!m_audioSink->activate())
    return false;

  return true;
}

void CAESinkDARWINIOS::Deinitialize()
{
  SAFE_DELETE(m_audioSink);
}

void CAESinkDARWINIOS::GetDelay(AEDelayStatus &status)
{
  if (m_audioSink)
    m_audioSink->updatedelay(status);
  else
    status.SetDelay(0.0);
}

void CAESinkDARWINIOS::AddPause(unsigned int micros)
{
  uint8_t buffer[m_format.m_frameSize];
  memset(buffer, 0x00, m_format.m_frameSize);
  uint8_t *bufferptr = buffer;
  AddPackets(&bufferptr, m_format.m_frameSize, 0, 0);

  usleep(micros);
}

double CAESinkDARWINIOS::GetCacheTotal()
{
  if (m_audioSink)
    return m_audioSink->buffertime();
  return 0.0;
}

unsigned int CAESinkDARWINIOS::AddPackets(uint8_t **data, unsigned int frames, unsigned int offset, int64_t timestamp)
{
  uint8_t *buffer = data[0] + (offset * m_format.m_frameSize);
#if DO_440HZ_TONE_TEST
  if (m_format.m_dataFormat == AE_FMT_FLOAT)
  {
    float *samples = (float*)buffer;
    for (unsigned int j = 0; j < frames ; j++)
    {
      float sample = SineWaveGeneratorNextSampleFloat(&m_SineWaveGenerator);
      *samples++ = sample;
      *samples++ = sample;
    }
    
  }
  else
  {
    int16_t *samples = (int16_t*)buffer;
    for (unsigned int j = 0; j < frames ; j++)
    {
      int16_t sample = SineWaveGeneratorNextSampleInt16(&m_SineWaveGenerator);
      *samples++ = sample;
      *samples++ = sample;
    }
  }
#endif
  if (m_audioSink)
    return m_audioSink->write(buffer, frames, m_format.m_frameSize);
  return 0;
}

void CAESinkDARWINIOS::Drain()
{
  if (m_audioSink)
    m_audioSink->drain();
}

bool CAESinkDARWINIOS::HasVolume()
{
  return false;
}

void CAESinkDARWINIOS::EnumerateDevicesEx(AEDeviceInfoList &list, bool force)
{
  m_devices.clear();
  EnumerateDevices(m_devices);
  list = m_devices;
}
