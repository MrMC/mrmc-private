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

#include <vector>
#include "XBDateTime.h"

class  TiXmlElement;
class  NWPlayerInfo;

bool        PingMNServer(const std::string& strURL);
std::string Encode(const std::string& strURLData);
std::string EncodeExtra(const std::string& strURLData);
//std::string FormatUrl(const PlayerInfo &PlayerInfo, const std::string &function, const std::string extrashit = "");
//void        SetDefaultPlayerInfo(PlayerInfo &player);
//bool        ParsePlayerInfo(PlayerInfo &player, TiXmlElement *settingsNode);

void        GetLocalPlayerInfo(NWPlayerInfo &player,std::string home);
bool        SaveLocalPlayerInfo(const TiXmlElement settingsNode, std::string home);
std::string GetMD5(const std::string strText);
void        OpenAndroidSettings();
std::string GetDiskUsed(std::string path);
std::string GetDiskFree(std::string path);
std::string GetDiskTotal(std::string path);
std::string GetSystemUpTime();
bool        SetDownloadedAsset(int AssetID, bool downloaded=true);
