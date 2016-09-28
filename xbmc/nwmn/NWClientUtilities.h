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

#include <string>
#include <vector>

#include "XBDateTime.h"

// ---------------------------------------------
// ---------------------------------------------
const float       kNWClient_PlayerFloatVersion = 1.0;
const std::string kNWClient_LogPath = "log/";
const std::string kNWClient_DownloadPath = "downloads/";
const std::string kNWClient_DownloadVideoPath = "downloads/video/";
const std::string kNWClient_DownloadVideoThumbNailsPath = "downloads/video_thumbnails/";
const std::string kNWClient_DownloadMusicPath = "downloads/music/";
const std::string kNWClient_DownloadMusicThumbNailsPath = "downloads/music_thumbnails/";
//   std::string localPlayer = home + "webdata/PlayerSetup.xml";
const std::string kNWClient_PlayerFileName = "player.xml";
const std::string kNWClient_PlaylistFileName = "playlist.xml";

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

// ---------------------------------------------
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

  // runtime
  bool        valid;
} NWAsset;

typedef struct NWGroup {
  int id;
  std::string name;
  int next_asset_index;
  std::vector<NWAsset> assets;
} NWGroup;

typedef struct NWPlaylist {
  int id;
  std::string name;
  std::string type;
  std::string video_format;
  std::string updated_date;
  std::vector<int> play_order;
  std::vector<NWGroup> groups;
} NWPlaylist;

// ---------------------------------------------
// ---------------------------------------------
    //if (HasLocalPlayer(m_strHome))
    //  LoadLocalPlayer(m_strHome, m_PlayerInfo);
bool HasLocalPlayer(std::string home);
bool LoadLocalPlayer(std::string home, NWPlayerInfo &playerInfo);
bool SaveLocalPlayer(std::string home, const NWPlayerInfo &playerInfo);

bool HasLocalPlaylist(std::string home);
bool LoadLocalPlaylist(std::string home, NWPlaylist &payList);
bool SaveLocalPlaylist(std::string home, const NWPlaylist &playList);

void LogPlayback(std::string home, std::string machineID, std::string assetID);
void LogDownLoad(std::string home, std::string machineID, std::string assetID);

std::string GetDiskUsed(std::string path);
std::string GetDiskFree(std::string path);
std::string GetDiskTotal(std::string path);
std::string GetSystemUpTime();


