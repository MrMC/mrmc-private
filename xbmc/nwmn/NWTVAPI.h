#pragma once

/*
 *  Copyright (C) 2016 Team MN
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
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "string"
#include "vector"

#include "XBDateTime.h"

const std::string kTVAPI_Status_On = "On";
const std::string kTVAPI_Status_Off = "Off";
const std::string kTVAPI_Status_Restarting = "Restarting";

const std::string kTVAPI_BASE = "test.nationwidemember.com";
const std::string kTVAPI_URLBASE = "http://test.nationwidemember.com/tv-api/1/";

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_Activate {
  std::string code;
  std::string application_id;
  // reply
  std::string apiKey;
  std::string apiSecret;
} TVAPI_Activate;

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_Status {
  std::string apiKey;
  std::string apiSecret;
  // reply
  std::string key;
  std::string unique_id;
  std::string machine_id;
  std::string status;
  std::string status_text;
  std::string activation_date;
} TVAPI_Status;

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_MachineLocation {
    std::string id;
    std::string name;
    std::string address;
    std::string address2;
    std::string city;
    std::string state;
    std::string phone;
    std::string fax;
} TVAPI_MachineLocation;

typedef struct TVAPI_MachineNetwork {
    std::string macaddress;
    std::string macaddress_wireless;
    std::string dhcp;
    std::string ipaddress;
    std::string subnet;
    std::string router;
    std::string dns_1;
    std::string dns_2;
} TVAPI_MachineNetwork;

typedef struct TVAPI_MachineSettings {
  std::string network;
  std::string pairing;
  std::string about;
  std::string hdmibrightness;
  std::string tvresolution;
  std::string updatesoftware;
  std::string language;
  std::string legal;
} TVAPI_MachineSettings;

typedef struct TVAPI_MachineMenu {
  std::string membernettv;
  std::string vendorcommercials;
  std::string hdcontent;
  std::string membercommercials;
  std::string movietrailers;
  std::string promotionalcampaigns;
  std::string nationwidebroadcasts;
  std::string imaginationwidehd;
  std::string primemediacommercialfactory;
} TVAPI_MachineMenu;

typedef struct TVAPI_MachineMembernet_software {
  std::string id;
  std::string version;
  std::string cfbundleversion;
  std::string url;
} TVAPI_MachineMembernet_software;

typedef struct TVAPI_MachineApple_software {
  std::string id;
  std::string version;
  std::string url;
} TVAPI_MachineApple_software;

typedef struct TVAPI_Machine {
  std::string apiKey;
  std::string apiSecret;
  // reply
  std::string id;
  std::string member;
  std::string machine_name;
  std::string description;
  std::string playlist_id;
  std::string status;
  std::string vendor;
  std::string hardware;
  std::string timezone;
  std::string serial_number;
  std::string warranty_number;
  std::string video_format;
  std::string allow_new_content;
  std::string allow_software_update;
  std::string update_time;
  std::string update_interval;
  TVAPI_MachineLocation location;
  TVAPI_MachineNetwork  network;
  TVAPI_MachineSettings settings;
  TVAPI_MachineMenu     menu;
  TVAPI_MachineMembernet_software  membernet_software;
  TVAPI_MachineApple_software apple_software;
} TVAPI_Machine;

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_PlaylistFile {
  std::string type;
  std::string path;
  std::string size;
  std::string width;
  std::string height;
  std::string etag;
  std::string mime_type;
  std::string created_date;
  std::string updated_date;
} TVAPI_PlaylistFile;

typedef struct TVAPI_PlaylistItem {
  std::string id;
  std::string name;
  std::string tv_category_id;
  std::string description;
  std::string created_date;
  std::string updated_date;
  std::string completion_date;
  std::string theatricalrelease;
  std::string dvdrelease;
  std::string download;
  std::string availability_to;
  std::string availability_from;
  TVAPI_PlaylistFile thumb;
  TVAPI_PlaylistFile poster;
  std::vector<TVAPI_PlaylistFile> files;
} TVAPI_PlaylistItem;

typedef struct TVAPI_PlaylistItems {
  std::string apiKey;
  std::string apiSecret;
  // reply
  std::string id;
  std::string name;
  std::string type;
  std::vector<TVAPI_PlaylistItem> items;
} TVAPI_PlaylistItems;

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_PlaylistInfo {
  std::string id;
  std::string name;
  std::string type;
  std::string updated_date;
  std::string layout;
  std::string member_id;
  std::string member_name;
  std::string nmg_managed;
} TVAPI_PlaylistInfo;

typedef struct TVAPI_Playlists {
  std::string apiKey;
  std::string apiSecret;
  // reply
  //std::string page;
  //std::string perPage;
  //std::string total;
  //std::string total_pages;
  std::vector<TVAPI_PlaylistInfo> playlists;
} TVAPI_Playlists;

// ---------------------------------------------
// ---------------------------------------------
typedef struct TVAPI_CategoryId {
  std::string id;
  std::string name;
} TVAPI_CategoryId;

typedef struct TVAPI_FileId {
  std::string id;
} TVAPI_FileId;

typedef struct TVAPI_Playlist {
  std::string apiKey;
  std::string apiSecret;
  // reply
  std::string id;
  std::string name;
  std::string type;
  std::string layout;
  std::string member_id;
  std::string nmg_managed;
  std::string updated_date;
  std::vector<TVAPI_FileId> files;
  std::vector<TVAPI_CategoryId> categories;
} TVAPI_Playlist;

// ---------------------------------------------
// ---------------------------------------------
bool TVAPI_DoActivate(TVAPI_Activate &activate);
bool TVAPI_GetStatus(TVAPI_Status &status);
bool TVAPI_GetMachine(TVAPI_Machine &machine);
bool TVAPI_GetPlaylists(TVAPI_Playlists &playlists);

bool TVAPI_GetPlaylist(TVAPI_Playlist &playlist, std::string playlist_id);
bool TVAPI_GetPlaylistItems(TVAPI_PlaylistItems &playlistItems, std::string playlist_id);
