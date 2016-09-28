#pragma once

/*
 *  Copyright (C) 2016 RootCoder, LLC.
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
 *  along with this app; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <queue>
#include <string>

#include "NWTVAPI.h"
#include "NWClientUtilities.h"

#include "interfaces/IAnnouncer.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"

// ---------------------------------------------
// ---------------------------------------------
typedef void (*ClientCallBackFn)(const void *ctx, bool status);
typedef void (*PlayerCallBackFn)(const void *ctx, int msg, struct NWAsset &asset);


class CNWPlayer;
class CNWMediaManager;

class CNWClient
: public CThread
, public ANNOUNCEMENT::IAnnouncer
{
public:
  CNWClient();
  virtual ~CNWClient();

  static CNWClient* GetClient();
  
  virtual void  Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data);

  void          Startup();
  void          SetSettings(NWPlayerSettings settings);
  NWPlayerSettings GetSettings();
  void          FullUpdate();
  void          GetStats(CDateTime &NextUpdateTime, CDateTime &NextDownloadTime, CDateTimeSpan &NextDownloadDuration);
  void          PlayPause();
  void          PausePlaying();
  void          StopPlaying();
  void          PlayNext();
  
  void          GetProgamInfo(NWPlaylist &playList) { playList = m_ProgramInfo; };
  void          GetPlayerInfo(NWPlayerInfo &playerInfo) { playerInfo = m_PlayerInfo; };
  
  bool          SendPlayerStatus(const std::string status);

  void          RegisterClientCallBack(const void *ctx, ClientCallBackFn fn);
  void          RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn);
  void          UpdatePlayerInfo(const std::string strPlayerID, const std::string strApiKey,const std::string strSecretKey);
  bool          DoAuthorize();
  bool          IsAuthorized();

protected:
  virtual void  Process();
  void          GetPlayerInfo();
  bool          GetPlayerStatus();
  bool          GetProgamInfo();
  void          SendFilesDownloaded();
  void          SendPlayerHealth();
  void          SendNetworkInfo();
  void          SendPlayerLog();
  void          GetActions();
  void          ClearAction(TVAPI_Actions &actions, std::string id);
  bool          CreatePlaylist(std::string home, NWPlaylist &playList,
                  const TVAPI_Playlist &playlist, const TVAPI_PlaylistItems &playlistItems);

  static void   AssetUpdateCallBack(const void *ctx, NWAsset &asset, bool wasDownloaded);
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
  
  TVAPI_Activate m_activate;
  TVAPI_Status   m_status;

  NWPlayerInfo  m_PlayerInfo;
  NWPlaylist    m_ProgramInfo;
  CDateTime     m_StartUpdateTime;
  CDateTime     m_EndUpdateTime;
  CDateTime     m_ProgramDateStamp;
  
  CNWPlayer     *m_Player;
  CNWMediaManager *m_MediaManager;

  static CCriticalSection m_playerLock;
  static CNWClient *m_this;

  ClientCallBackFn m_ClientCallBackFn;
  const void      *m_ClientCallBackCtx;
};
