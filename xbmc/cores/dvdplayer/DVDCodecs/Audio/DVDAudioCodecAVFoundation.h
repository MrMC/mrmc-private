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

#include "DVDAudioCodecPassthrough.h"
#include "cores/AudioEngine/Utils/AEAudioFormat.h"
#include "threads/Thread.h"

#ifdef __OBJC__
  @class AVPlayerSink;
#else
  class AVPlayerSink;
#endif

class CDVDAudioCodecAVFoundation : public CDVDAudioCodecPassthrough, CThread
{
public:
  CDVDAudioCodecAVFoundation();
  virtual ~CDVDAudioCodecAVFoundation();

  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int  Decode(uint8_t* pData, int iSize, double dts, double pts);
  virtual void GetData(DVDAudioFrame &frame);
  virtual void Reset();
  virtual const char* GetName() { return "passthrough-afv"; }
  virtual void  SetClock(CDVDClock *clock);
  virtual void  SetSpeed(int iSpeed);
protected:
  virtual void  Process();
private:
  double        GetPlayerClockSeconds();

  AEAudioFormat       m_format;
  AVPlayerSink       *m_avsink;
  CDVDClock          *m_clock = nullptr;
  double              m_dts;
  double              m_pts;
  int                 m_speed;
  AVCodecID           m_codec;
};

