#pragma once

/*
 *  Copyright (C) 2014 Team RED
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

#include <queue>
#include <string>
#include "RedMedia.h"
#include "XBDateTime.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "filesystem/CurlFile.h"

class CUpdateManagerRed : public CThread
{
public:
  CUpdateManagerRed(const std::string &home);
  virtual ~CUpdateManagerRed();

  void          OverrideDownloadWindow();
  void          SetDownloadTime(const CDateTime &time, const CDateTimeSpan &duration);
  void          QueueUpdateForDownload(RedMediaUpdate &update);


protected:
  virtual void  Process();
  void          DoUpdate(RedMediaUpdate &update);
  
  std::string           m_strHome;

  bool                  m_Override;
  CDateTime             m_NextDownloadTime;
  CDateTimeSpan         m_NextDownloadDuration;
  
  bool                  m_update_fired;

  XFILE::CCurlFile      m_http;
  CCriticalSection      m_download_lock;
  std::queue<RedMediaUpdate> m_download;
};
