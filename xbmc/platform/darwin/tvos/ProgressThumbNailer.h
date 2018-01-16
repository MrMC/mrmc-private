#pragma once
/*
 *      Copyright (C) 2018 Team MrMC
 *      https://github.com/MrMC
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

#import <string>
#import "FileItem.h"
#import "threads/Thread.h"
#import "threads/CriticalSection.h"

class CDVDInputStream;
class CDVDDemux;
class CDVDVideoCodec;
typedef struct CGImage* CGImageRef;

class CProgressThumbNailer
: public CThread
{
public:
  CProgressThumbNailer(const CFileItem& item);
 ~CProgressThumbNailer();

  bool IsInitialized() { return m_videoCodec != nullptr; };
  void RequestThumbsAsTime(int seekTime);
  void RequestThumbAsPercentage(float percentage);
  CGImageRef GetThumb();

private:
  void Process();
  CGImageRef ExtractThumb(int seekTime);

  std::string m_path;
  std::string m_redactPath;
  float m_aspect;
  bool m_forced_aspect;
  int m_seekTime = -1;
  int m_seekTimeOld = -1;
  int m_seekPercentage = -1;
  int m_seekPercentageOld = -1;
  CEvent m_processSleep;
  CGImageRef m_thumbImage = nullptr;;
  int m_videoStream = -1;
  CCriticalSection m_critical;
  CDVDInputStream *m_inputStream = nullptr;
  CDVDDemux *m_videoDemuxer = nullptr;
  CDVDVideoCodec *m_videoCodec = nullptr;
};
