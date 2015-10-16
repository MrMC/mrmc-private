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
#include "XBDateTime.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"

#include "RedMedia.h"
typedef void (*ReportManagerCallBackFn)(const void *ctx, bool status);

class  CPlayerManagerRed;
struct PlayerInfo;

class CReportManagerRed : public CThread
{
public:
  CReportManagerRed(const std::string &home, PlayerInfo *player_info);
  virtual ~CReportManagerRed();

  void          SendReport();
  void          SetReportInterval(const CDateTimeSpan &interval);
  void          RegisterReportManagerCallBack(const void *ctx, ReportManagerCallBackFn fn);

protected:
  virtual void  Process();
  void          SendDailyAssetReport();
  bool          SendAssetReport(std::string xmlEncoded);

  std::string             m_strHome;
  CEvent                  m_SendReport;
  CDateTime               m_NextReportTime;
  CDateTimeSpan           m_NextReportInterval;
  PlayerInfo             *m_PlayerInfo;
  ReportManagerCallBackFn m_ReportManagerCallBackFn;
  const void             *m_ReportManagerCallBackCtx;
};
