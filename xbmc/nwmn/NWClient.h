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
#include "XBDateTime.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"

#include "NWTVAPI.h"

// ---------------------------------------------
// ---------------------------------------------
typedef struct NWPlayerSettings {
  std::string strLocation_id;
  std::string strMachine_id;
  std::string strMachine_description;
  std::string strMachine_ethernet_id;
  std::string strMachine_hw_version;
  std::string strMachine_name;
  std::string strMachine_purchase_date;
  std::string strMachine_sn;
  std::string strMachine_vendor;
  std::string strMachine_warrenty_nr;
  std::string strMachine_wireless_id;
  std::string strSettings_cf_bundle_version;
  std::string strSettings_update_interval;
  std::string strSettings_update_time;
  std::string strSettings_software_version;
  std::string strSettings_software_url;
  std::string strUrl_feed;
  std::string strTimeZone;
  bool        allowUpdate;
  std::vector <std::string> intCategories_order;
} NWPlayerSettings;

typedef struct NWPlayerInfo {
  std::string id;
  std::string name;
  std::string member;
  std::string timezone;
  std::string playlist_id;
  std::string video_format;
  std::string update_time;
  std::string update_interval;
  std::string status;
  std::string apiKey;
  std::string apiSecret;
  int         intSettingsVersion;
 
  std::string strUpdateUrl;
  std::string strUpdateKey;
  std::string strUpdateMD5;
  std::string strUpdateSize;
  std::string strUpdateName;
  std::string strUpdateDate;
  std::string strUpdateVersion;
} NWPlayerInfo;

typedef struct NWAsset {
  int id;
  int group_id;
  std::string name;
  std::string type;
  std::string video_url;
  std::string video_md5;
  int         video_size;
  std::string video_basename;
  std::string video_localpath;
  std::string thumb_url;
  std::string thumb_md5;
  int         thumb_size;
  std::string thumb_basename;
  std::string thumb_localpath;
  CDateTime   available_to;
  CDateTime   available_from;
  
  std::string uuid;       // changes all the time, not to be used as identifier
  std::string time_played;// ditto
  bool        valid;
} NWAsset;

typedef struct NWGroup {
  int id;
  std::string name;
  int next_asset_index;
  std::vector<NWAsset> assets;
} NWGroup;

typedef struct NWGroupPlaylist {
  int id;
  std::string name;
  std::string type;
  std::string video_format;
  std::string updated_date;
  std::vector<int> play_order;
  std::vector<NWGroup> groups;
} NWGroupPlaylist;

// ---------------------------------------------
// ---------------------------------------------
typedef void (*ClientCallBackFn)(const void *ctx, bool status);
typedef void (*PlayerCallBackFn)(const void *ctx, int msg, struct NWAsset &asset);


class CNWPlayer;
class CNWMediaManager;
class CNWReportManager;
class CNWUpdateManager;

class CNWClient
: public CThread
{
public:
  CNWClient();
  virtual ~CNWClient();

  static CNWClient* GetClient();
  
  void          Startup();
  void          SetSettings(NWPlayerSettings settings);
  NWPlayerSettings GetSettings();
  void          FullUpdate();
  void          SendReport();
  void          GetStats(CDateTime &NextUpdateTime, CDateTime &NextDownloadTime, CDateTimeSpan &NextDownloadDuration);
  void          PlayPause();
  void          PausePlaying();
  void          StopPlaying();
  void          PlayNext();
  
  void          GetProgamInfo(NWGroupPlaylist &groupPlayList)
                {
                  groupPlayList = m_ProgramInfo;
                };
  bool          SendPlayerStatus(const std::string status);

  void          RegisterClientCallBack(const void *ctx, ClientCallBackFn fn);
  void          RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn);
  void          UpdatePlayerInfo(const std::string strPlayerID, const std::string strApiKey,const std::string strSecretKey);
  void          ForceLocalPlayerUpdate();
  void          CheckForUpdate(NWPlayerInfo &player);

protected:
  virtual void  Process();
  void          GetPlayerInfo();
  bool          GetPlayerStatus();
  bool          GetProgamInfo();
  void          NotifyAssetDownload(NWAsset &asset);
  void          SendFilesDownloaded();
  void          SendPlayerHealth();
  void          SendNetworkInfo();
  void          SendPlayerLog();
  void          GetActions();
  void          ClearAction(std::string action);
  bool          CreateGroupPlaylist(std::string home, NWGroupPlaylist &groupPlayList,
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
  NWGroupPlaylist m_ProgramInfo;
  CDateTime     m_StartUpdateTime;
  CDateTime     m_EndUpdateTime;
  CDateTime     m_ProgramDateStamp;
  
  CNWPlayer     *m_Player;
  CNWMediaManager  *m_MediaManager;
  CNWReportManager *m_ReportManager;
  CNWUpdateManager *m_UpdateManager;

  static CCriticalSection m_playerLock;
  static CNWClient *m_this;

  ClientCallBackFn m_ClientCallBackFn;
  const void      *m_ClientCallBackCtx;
};