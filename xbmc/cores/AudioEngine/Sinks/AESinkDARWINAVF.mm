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
#include "platform/darwin/DarwinUtils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#import <AVFoundation/AVAudioSession.h>

#pragma mark - AESinkDARWINAVF
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
  device.m_dataFormats.push_back(AE_FMT_RAW);

  // add channel info
  UInt32 maxChannels = [[AVAudioSession sharedInstance] maximumOutputNumberOfChannels];
  if (maxChannels > 6)
    device.m_channels = AE_CH_LAYOUT_7_1;
  else
    device.m_channels = AE_CH_LAYOUT_5_1;

  CLog::Log(LOGDEBUG, "EnumerateDevices:Device(%s)" , device.m_deviceName.c_str());

  list.push_back(device);
}

/***************************************************************************************/
/***************************************************************************************/
AEDeviceInfoList CAESinkDARWINAVF::m_devices;

CAESinkDARWINAVF::CAESinkDARWINAVF()
  : CThread("CAESinkDARWINAVF")
  , m_draining(false)
  , m_sink_frameSize(0)
  , m_sinkbuffer_size(0)
  , m_sinkbuffer_level(0)
  , m_sinkbuffer_sec_per_byte(0)
{
}

CAESinkDARWINAVF::~CAESinkDARWINAVF()
{
}

bool CAESinkDARWINAVF::Initialize(AEAudioFormat &format, std::string &device)
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

  bool passthrough = false;
  if (format.m_dataFormat == AE_FMT_RAW && route.find("HDMI") != std::string::npos)
  {
    passthrough = true;
    switch (format.m_streamInfo.m_type)
    {
      case CAEStreamInfo::STREAM_TYPE_AC3:
        if (!format.m_streamInfo.m_ac3FrameSize)
          format.m_streamInfo.m_ac3FrameSize = 1536;
        format.m_frames = format.m_streamInfo.m_ac3FrameSize;
      break;
      case CAEStreamInfo::STREAM_TYPE_EAC3:
        if (!format.m_streamInfo.m_ac3FrameSize)
          format.m_streamInfo.m_ac3FrameSize = 1792;
        // good for 1536 or 1792 packet size
        format.m_frames = 10752;
      break;
      default:
        passthrough = false;
      break;
    }
  }
  if (passthrough)
  {
    // passthrough is always two channels/raw
    CAEChannelInfo channel_info;
    for (unsigned int i = 0; i < 2; ++i)
      channel_info += AE_CH_RAW;
    format.m_channelLayout = channel_info;

    format.m_dataFormat = AE_FMT_S16LE;
    format.m_frameSize = 1;
    m_format = format;

    // setup a pretend 500ms internal buffer
    m_sink_frameSize = format.m_streamInfo.m_ac3FrameSize;
    m_sinkbuffer_size = m_sink_frameSize * 20;
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
  }

  return passthrough == true;
}

void CAESinkDARWINAVF::Deinitialize()
{
  // force m_bStop and set m_wake, if might be sleeping.
  m_bStop = true;
  StopThread();
}

void CAESinkDARWINAVF::AddPause(unsigned int millis)
{
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
/*
  uint8_t *buffer = data[0] + (offset * m_format.m_frameSize);
  uint16_t firstWord = buffer[0] << 8 | buffer[1];
  CLog::Log(LOGDEBUG, "%s firstWord(0x%4.4X), frames(%u), offset(%u)", __FUNCTION__, firstWord, frames, offset);
*/

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
    unsigned int min_buffer_size = m_sink_frameSize * 10;
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
      m_wake.WaitMSec(50);
    }
  }
  SetPriority(THREAD_PRIORITY_NORMAL);
}
