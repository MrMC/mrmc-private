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

const float kTVAPI_PlayerFloatVersion = 1.0;
const std::string kTVAPI_NDownloadPath = "downloads/";
const std::string kTVAPI_DownloadLogPath = "log/";
const std::string kTVAPI_DownloadUpdatePath = "update/";

const std::string kTVAPI_Status_On = "On";
const std::string kTVAPI_Status_Off = "Off";
const std::string kTVAPI_Status_Restarting = "Restarting";

const std::string kTVAPI_URLBASE = "http://test.nationwidemember.com/tv-api/1/";

typedef struct NWActivate {
  std::string code;
  std::string application_id;
  // reply
  std::string apiKey;
  std::string apiSecret;
} NWActivate;

typedef struct NWStatus {
  std::string apiKey;
  std::string apiSecret;
  // reply
  std::string key;
  std::string unique_id;
  std::string machine_id;
  std::string status;
  std::string status_text;
  std::string activation_date;
} NWStatus;

typedef struct NWMachineLocation {
    std::string id;
    std::string name;
    std::string address;
    std::string address2;
    std::string city;
    std::string state;
    std::string phone;
    std::string fax;
} NWMachineLocation;

typedef struct NWMachineNetwork {
    std::string macaddress;
    std::string macaddress_wireless;
    std::string dhcp;
    std::string ipaddress;
    std::string subnet;
    std::string router;
    std::string dns_1;
    std::string dns_2;
} NWMachineNetwork;

typedef struct NWMachineSettings {
  std::string network;
  std::string pairing;
  std::string about;
  std::string hdmibrightness;
  std::string tvresolution;
  std::string updatesoftware;
  std::string language;
  std::string legal;
} NWMachineSettings;

typedef struct NWMachineMenu {
  std::string membernettv;
  std::string vendorcommercials;
  std::string hdcontent;
  std::string membercommercials;
  std::string movietrailers;
  std::string promotionalcampaigns;
  std::string nationwidebroadcasts;
  std::string imaginationwidehd;
  std::string primemediacommercialfactory;
} NWMachineMenu;

typedef struct NWMachineMembernet_software {
  std::string id;
  std::string version;
  std::string cfbundleversion;
  std::string url;
} NWMachineMembernet_software;

typedef struct NWMachineApple_software {
  std::string id;
  std::string version;
  std::string url;
} NWMachineApple_software;

typedef struct NWMachine {
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
  NWMachineLocation location;
  NWMachineNetwork network;
  NWMachineSettings settings;
  NWMachineMenu menu;
  NWMachineMembernet_software  membernet_software;
  NWMachineApple_software apple_software;
} NWMachine;




bool TVAPI_DoActivate(NWActivate &activate);
bool TVAPI_GetStatus(NWStatus &status);
bool TVAPI_GetMachine(NWMachine &machine);



