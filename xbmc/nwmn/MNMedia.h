#pragma once

/*
 *  Copyright (C) 2014 Team MN
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
#include "vector"

#include "XBDateTime.h"

const float kMNPlayerFloatVersion = 1.0;
const std::string kMNDownloadPath = "downloads/";
const std::string kMNDownloadLogPath = "log/";
const std::string kMNDownloadUpdatePath = "update/";

const std::string kMNStatus_On = "On";
const std::string kMNStatus_Off = "Off";
const std::string kMNStatus_Restarting = "Restarting";

typedef struct MNMediaAsset {
  std::string id;
  std::string category_id;
  std::string title;
  std::string video_url;
  std::string video_md5;
  std::string video_fileSize;
  std::string video_localpath;
  std::string video_height;
  std::string video_width;
  std::string video_duration;
  std::string thumb_url;
  std::string thumb_md5;
  std::string thumb_fileSize;
  std::string thumb_localpath;
  std::string thumb_width;
  std::string thumb_height;
  std::string available_start;
  std::string available_stop;
  
  std::string mediagroup_id;
  std::string last_played_id;
  std::string uuid;      // changes all the time, not to be used as identifier
  std::string timePlayed;// ditto
  bool        valid;
} MNMediaAsset;

typedef struct MNCategory {
  std::string id;
  std::string name;
  std::string icon;
  std::vector<MNMediaAsset> items;
} MNCategory;

typedef struct MNMediaUpdate {
  std::string url;
  std::string key;
  std::string md5;
  std::string size;
  std::string name;
  CDateTime   date;
  float       version;
  std::string localpath;
} MNMediaUpdate;

typedef struct PlayerSettings {
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
} PlayerSettings;

typedef struct MNMediaPlaylist {
  std::string strID;
  std::string strName;
  std::string strLastUpdated;
  int         intMediaGroupsCount;
  std::string strZoneID;
  std::vector<MNCategory> MediaGroups;
} MNMediaPlaylist;

typedef struct MNMediaZone {
  std::string strId;
  std::string strLeft;
  std::string strTop;
  std::string strWidth;
  std::string strHeight;
  std::string strPlaylistID;
  std::string strName;
  std::string strLastUpdated;
  std::vector<MNMediaPlaylist> playlists;
} MNMediaZone;
