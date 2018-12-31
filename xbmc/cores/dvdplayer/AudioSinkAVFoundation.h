#pragma once

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

#include "IAudioSink.h"
#include "threads/SystemClock.h"
#include "threads/CriticalSection.h"
#include "cores/AudioEngine/Interfaces/AEStream.h"

#include <atomic>

class CAudioSinkAVFoundation : public IAudioSink, IAEClockCallback
{
public:
  CAudioSinkAVFoundation(volatile bool& bStop, CDVDClock *clock);
 ~CAudioSinkAVFoundation();

  void SetVolume(float fVolume);
  void SetDynamicRangeCompression(long drc);
  float GetCurrentAttenuation();
  void Pause();
  void Resume();
  bool Create(const DVDAudioFrame &audioframe, AVCodecID codec, bool needresampler);
  bool IsValidFormat(const DVDAudioFrame &audioframe);
  void Destroy();
  unsigned int AddPackets(const DVDAudioFrame &audioframe);
  double GetPlayingPts();
  double GetCacheTime();
  double GetCacheTotal(); // returns total time a stream can buffer
  double GetMaxDelay(); // returns total time of audio in AE for the stream
  double GetDelay(); // returns the time it takes to play a packet if we add one at this time
  double GetSyncError();
  void SetSyncErrorCorrection(double correction);
  double GetResampleRatio();
  void SetResampleMode(int mode);
  void Flush();
  void Drain();
  void AbortAddPackets();

  void SetSpeed(int iSpeed);
  void SetResampleRatio(double ratio);

  double GetClock();
  double GetClockSpeed();

protected:
  volatile bool& m_bStop;
  CDVDClock *m_pClock;

  int m_speed;
  bool m_start;
  double m_startDelaySeconds;
  bool m_bPaused;
  bool m_bPassthrough;
  double m_playingPts;
  double m_timeOfPts;
  double m_syncError;
  unsigned int m_syncErrorTime;
  CCriticalSection m_critSection;
  std::atomic_bool m_bAbort;

  XbmcThreads::EndTime m_timer;
};
