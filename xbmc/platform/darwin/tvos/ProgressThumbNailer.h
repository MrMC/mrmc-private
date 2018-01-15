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

class CDVDInputStream;
class CDVDDemux;
class CDVDVideoCodec;
typedef struct CGImage* CGImageRef;

class CProgressThumbNailer
{
public:
  CProgressThumbNailer(const CFileItem& item);
 ~CProgressThumbNailer();

  bool Initialize();
  bool IsInitialized() { return m_videoCodec != nullptr; };
  CGImageRef ExtractThumb(float percentage);

private:
  std::string m_path;
  std::string m_redactPath;
  float m_aspect;
  bool  m_forced_aspect;
  CDVDInputStream *m_inputStream = nullptr;
  CDVDDemux *m_demuxer = nullptr;
  CDVDVideoCodec *m_videoCodec = nullptr;
  int m_videoStream = -1;
};
