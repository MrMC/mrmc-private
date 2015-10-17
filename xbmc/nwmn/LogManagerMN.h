#pragma once
/*
 *  Copyright (C) 2015 Team MN
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

#include <string>
#include "threads/Thread.h"

struct PlayerSettings;

class CLogManagerMN : public CThread
{
public:
  CLogManagerMN(const std::string &home);
  virtual ~CLogManagerMN();

  void TriggerLogUpload();
  void LogPlayback(PlayerSettings settings, std::string assetID);
  void LogSettings(PlayerSettings settings);

protected:
  virtual void  Process();

  std::string m_strHome;
  CEvent      m_wait_event;

};