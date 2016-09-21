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
#include <atomic>
#include <string>

#include "NWTVAPI.h"
#include "NWClientUtilities.h"

#include "interfaces/IAnnouncer.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "dialogs/GUIDialogProgress.h"

enum AssetDownloadState
{
  IsPresent,
  willDownload,
  wasDownloaded,
};

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
  void          FullUpdate();
  void          PlayPause();
  void          PausePlaying();
  void          StopPlaying();
  void          PlayNext();

  void          UpdateFromJson(std::string url, std::string machineID, std::string locationID);

  void          GetProgamInfo(NWPlaylist &playList) { playList = m_ProgramInfo; };
  void          GetPlayerInfo(NWPlayerInfo &playerInfo) { playerInfo = m_PlayerInfo; };
  
  void          RegisterClientCallBack(const void *ctx, ClientCallBackFn fn);
  void          RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn);
  bool          DoAuthorize();
  bool          IsAuthorized();

protected:
  virtual void  Process();

  void          ShowStartUpDialog();
  void          CloseStartUpDialog();
  bool          ManageStartupDialog();

  void          GetPlayerInfo();
  bool          GetProgamInfo();
  void          GetActions();
  void          ClearAction(TVAPI_Actions &actions, std::string id);

  void          SendFilesPlayed();
  void          LogFilesPlayed(std::string assetID);
  void          SendFilesDownloaded();
  void          LogFilesDownLoaded(std::string assetID);
  bool          SendPlayerStatus(const std::string status);

  void          InitializeInternalsFromPlayer();
  bool          CreatePlaylist(std::string home, NWPlaylist &playList,
                  const TVAPI_Playlist &playlist, const TVAPI_PlaylistItems &playlistItems);

  static void   AssetUpdateCallBack(const void *ctx, NWAsset &asset, AssetDownloadState downloadState);

  std::string   m_strHome;
  std::atomic<bool> m_Startup;
  bool          m_HasNetwork;
  bool          m_FullUpdate;
  int           m_totalAssets;
  CDateTime     m_NextUpdateTime;
  CDateTimeSpan m_NextUpdateInterval;
  
  NWPlayerInfo  m_PlayerInfo;
  NWPlaylist    m_ProgramInfo;
  CNWPlayer     *m_Player;
  CNWMediaManager *m_MediaManager;
  CGUIDialogProgress *m_dlgProgress;

  CCriticalSection m_reportLock;
  static CCriticalSection m_clientLock;
  static CNWClient *m_this;

  ClientCallBackFn m_ClientCallBackFn;
  const void      *m_ClientCallBackCtx;
};
