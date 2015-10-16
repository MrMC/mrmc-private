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
#include <string>
#include <vector>
#include "XBDateTime.h"

const float kRedPlayerFloatVersion = 1.2;
const std::string kRedDownloadPath = "downloads/";
const std::string kRedDownloadUpdatePath = "downloads/updates/";
const std::string kRedDownloadMusicPath = "downloads/music/";
const std::string kRedDownloadMusicThumbNailsPath = "downloads/music_thumbnails/";

const std::string kRedStatus_On = "On";
const std::string kRedStatus_Off = "Off";
const std::string kRedStatus_Restarting = "Restarting";

typedef struct RedMediaAsset {
  std::string id;
  std::string url;
  std::string md5;
  std::string size;
  std::string name;
  std::string basename;
  std::string thumbnail_basename;

  std::string artist;
  std::string year;
  std::string genre;
  std::string composer;
  std::string album;
  std::string tracknumber;
  std::string thumbnail_url;
  
  bool        valid;
  std::string type;
  std::string localpath;
  std::string thumbnail_localpath;
  std::string mediagroup_id;
  std::string last_played_id;
  std::string uuid;      // changes all the time, not to be used as identifier
  std::string timePlayed;// ditto
} RedMediaAsset;

typedef struct RedMediaGroup {
  std::string id;
  std::string name;
  std::string playbackType;
  CDateTime   startDate;
  CDateTime   endDate;
  std::vector<std::string> lastPlayedId;
  std::string playlistId;
  std::string assetIndex;
  std::vector<RedMediaAsset> assets;
} RedMediaGroup;

typedef struct RedMediaUpdate {
  std::string url;
  std::string key;
  std::string md5;
  std::string size;
  std::string name;
  CDateTime   date;
  float       version;
  std::string localpath;
} RedMediaUpdate;

typedef struct PlayerInfo {
  std::string strPlayerClientID;
  std::string strPlayerClientName;
  std::string strPlayerID;
  std::string strPlayerName;
  std::string strPlayerTimeZone;
  std::string strProgramID;
  std::string strPlaylistID; // obsolete
  std::string strStatus;
  std::string strUpdateInterval;
  std::string strReportInterval;
  std::string strDownloadStartTime;
  std::string strDownloadDuration;
  std::string strApiKey;
  std::string strSecretKey;
  std::string strApiURL;
  std::string strPlayStartTime;
  std::string strPlayDuration;
  int         intSettingsVersion;
 
  std::string strUpdateUrl;
  std::string strUpdateKey;
  std::string strUpdateMD5;
  std::string strUpdateSize;
  std::string strUpdateName;
  std::string strUpdateDate;
  std::string strUpdateVersion;
} PlayerInfo;

typedef struct RedMediaPlaylist {
  std::string strID;
  std::string strName;
  std::string strLastUpdated;
  int         intMediaGroupsCount;
  std::string strZoneID;
  std::vector<RedMediaGroup> MediaGroups;
} RedMediaPlaylist;

typedef struct RedMediaZone {
  std::string strId;
  std::string strLeft;
  std::string strTop;
  std::string strWidth;
  std::string strHeight;
  std::string strPlaylistID;
  std::string strName;
  std::string strLastUpdated;
  std::vector<RedMediaPlaylist> playlists;
} RedMediaZone;

typedef struct ProgramInfo {
  std::string strProgramID;
  std::string strDate;
  std::string strScreenW;
  std::string strScreenH;
  std::vector<RedMediaZone> zones;
} ProgramInfo;


