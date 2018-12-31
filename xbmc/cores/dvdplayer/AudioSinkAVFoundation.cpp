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

#include "AudioSinkAVFoundation.h"

#include "DVDClock.h"
#include "DVDCodecs/Audio/DVDAudioCodec.h"
#include "cores/AudioEngine/AEFactory.h"
#include "cores/AudioEngine/Interfaces/AEStream.h"
#include "cores/AudioEngine/Utils/AEAudioFormat.h"
#include "settings/MediaSettings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"

CAudioSinkAVFoundation::CAudioSinkAVFoundation(volatile bool &bStop, CDVDClock *clock)
: m_bStop(bStop)
, m_pClock(clock)
, m_speed(DVD_PLAYSPEED_NORMAL)
, m_start(false)
, m_startDelaySeconds(0.5)
{
  m_bPassthrough = false;
  m_bPaused = true;
  m_playingPts = DVD_NOPTS_VALUE;
  m_timeOfPts = 0.0;
  m_syncError = 0.0;
  m_syncErrorTime = 0;
}

CAudioSinkAVFoundation::~CAudioSinkAVFoundation()
{
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

  //CAEFactory::Suspend();

  return true;
}

void CAudioSinkAVFoundation::Destroy()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Destroy");
  m_bPassthrough = false;
  m_bPaused = true;
  m_playingPts = DVD_NOPTS_VALUE;

  //CAEFactory::Resume();
}

unsigned int CAudioSinkAVFoundation::AddPackets(const DVDAudioFrame &audioframe)
{
  m_bAbort = false;
  if (!m_start && audioframe.nb_frames)
  {
    m_start = true;
    m_timer.Set(m_startDelaySeconds * 1000);
  }

  CSingleLock lock (m_critSection);
  m_syncErrorTime = 0;
  m_syncError = 0.0;

  m_playingPts = audioframe.pts + audioframe.duration - GetDelay();
  m_timeOfPts = m_pClock->GetAbsoluteClock();

  return audioframe.nb_frames;
}

void CAudioSinkAVFoundation::Drain()
{
  CSingleLock lock (m_critSection);
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::Drain");
}

void CAudioSinkAVFoundation::SetVolume(float volume)
{
  CSingleLock lock (m_critSection);
}

void CAudioSinkAVFoundation::SetDynamicRangeCompression(long drc)
{
  CSingleLock lock (m_critSection);
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
}

void CAudioSinkAVFoundation::Resume()
{
  CSingleLock lock(m_critSection);
  CLog::Log(LOGDEBUG,"CAudioSinkAVFoundation::Resume");
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

void CAudioSinkAVFoundation::SetResampleRatio(double ratio)
{
  CSingleLock lock (m_critSection);
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
      CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::SetSpeed Pause");
      break;
    default:
    case DVD_PLAYSPEED_NORMAL:
      CLog::Log(LOGDEBUG, "CDVDAudioCodecAVFoundation::SetSpeed Play");
      break;
  }
  m_speed = iSpeed;
}

double CAudioSinkAVFoundation::GetResampleRatio()
{
  return 1.0;
}

void CAudioSinkAVFoundation::SetResampleMode(int mode)
{
}

double CAudioSinkAVFoundation::GetClock()
{
  double absolute;
  // absolute is not used in GetClock :)
  return m_pClock->GetClock(absolute) / DVD_TIME_BASE * 1000;
}

double CAudioSinkAVFoundation::GetClockSpeed()
{
  CLog::Log(LOGDEBUG, "CAudioSinkAVFoundation::AbortAddPackets");
  if (m_pClock)
    return m_pClock->GetClockSpeed();
  else
    return 1.0;
}
