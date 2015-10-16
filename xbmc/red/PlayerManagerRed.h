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

#include "PlayBackManagerRed.h"
#include "RedMedia.h"

typedef void (*PlayerCallBackFn)(const void *ctx, bool status);

class CMediaManagerRed;
class CReportManagerRed;
class CUpdateManagerRed;

class CPlayerManagerRed : public CThread
{
public:
  CPlayerManagerRed();
  virtual ~CPlayerManagerRed();

  static CPlayerManagerRed* GetPlayerManager();
  
  void          Startup();
  void          FullUpdate();
  void          SendReport();
  void          GetStats(CDateTime &NextUpdateTime, CDateTime &NextDownloadTime, CDateTimeSpan &NextDownloadDuration);
  void          PlayPause();
  void          PausePlaying();
  void          StopPlaying();
  void          PlayNext();
  
  std::string   GetPlayerStatus();
  bool          SendPlayerStatus(const std::string status);

  void          RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn);
  void          RegisterPlayBackCallBack(const void *ctx, PlayBackCallBackFn fn);
  void          UpdatePlayerInfo(const std::string strPlayerID, const std::string strApiKey,const std::string strSecretKey, const std::string strApiURL );
  void          ForceLocalPlayerUpdate();
  void          CheckForUpdate(PlayerInfo &player);

protected:
  virtual void  Process();
  void          GetPlayerInfo();
  bool          GetProgamInfo();
  void          NotifyAssetDownload(RedMediaAsset &asset);
  void          SendFilesDownloaded();
  void          SendPlayerHealth();
  void          SendNetworkInfo();
  void          SendPlayerLog();
  void          GetActions();
  void          ClearAction(std::string action);
  static void   AssetUpdateCallBack(const void *ctx, RedMediaAsset &asset, bool wasDownloaded);
  static void   ReportManagerCallBack(const void *ctx, bool status);

  std::string   m_strHome;
  bool          m_Startup;
  bool          m_HasNetwork;
  bool          m_FullUpdate;
  CDateTime     m_NextUpdateTime;
  CDateTimeSpan m_NextUpdateInterval;
  CDateTimeSpan m_NextReportInterval;
  CDateTime     m_NextDownloadTime;
  CDateTimeSpan m_NextDownloadDuration;
  CDateTime     m_PlaybackTime;
  CDateTimeSpan m_PlaybackDuration;
  

  PlayerInfo    m_PlayerInfo;
  ProgramInfo   m_ProgramInfo;
  CDateTime     m_StartUpdateTime;
  CDateTime     m_EndUpdateTime;
  CDateTime     m_ProgramDateStamp;
  
  CMediaManagerRed  *m_MediaManager;
  CReportManagerRed *m_ReportManager;
  CUpdateManagerRed *m_UpdateManager;
  CPlayBackManagerRed *m_PlaybackManager;

  static CCriticalSection m_player_lock;
  static CPlayerManagerRed *m_PlayerManager;

  PlayerCallBackFn m_PlayerCallBackFn;
  const void    *m_PlayerCallBackCtx;
};
