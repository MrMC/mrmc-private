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

#include "RedMedia.h"
#include "UtilitiesRed.h"
#include "DBManagerRed.h"

#include "messaging/ApplicationMessenger.h"
#include "URL.h"
#include "utils/md5.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "storage/MediaManager.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"
#include "utils/URIUtils.h"

bool PingRedServer(const std::string& apiURL)
{
  CURL url(apiURL.c_str());
  XFILE::CCurlFile http;
  bool found = http.IsInternet();
//  found = http.Exists(url);
  http.Close();
  
  if (found)
    CLog::Log(LOGDEBUG, "**RED** - PingRedServer: network=yes");
  else
    CLog::Log(LOGDEBUG, "**RED** - PingRedServer: network=no");

  return found;
}

// this is needed as standard XBMC CURL::Encode() did not do it right, XBMC or server issue?
std::string Encode(const std::string& strURLData)
{
  std::string strResult;
  /* wonder what a good value is here is, depends on how often it occurs */
  strResult.reserve( strURLData.length() * 2 );
  
  for (size_t i = 0; i < strURLData.size(); ++i)
  {
    const char kar = strURLData[i];
    // Don't URL encode "-_.!()" according to RFC1738
    // TODO: Update it to "-_.~" after Gotham according to RFC3986
    if (StringUtils::isasciialphanum(kar) || kar == '-' || kar == '+' || kar == '.' || kar == '_' || kar == '!' || kar == '(' || kar == ')')
      strResult.push_back(kar);
    else
      strResult += StringUtils::Format("%%%02.2X", (unsigned int)((unsigned char)kar)); // TODO: Change to "%%%02.2X" after Gotham
  }
  
  return strResult;
}

// this is needed as standard XBMC CURL::Encode() did not do it right, XBMC or server issue?
std::string EncodeExtra(const std::string& strURLData)
{
  std::string strResult;
  /* wonder what a good value is here is, depends on how often it occurs */
  strResult.reserve( strURLData.length() * 2 );
  
  for (size_t i = 0; i < strURLData.size(); ++i)
  {
    const char kar = strURLData[i];
    // Don't URL encode "-_.!()" according to RFC1738
    // TODO: Update it to "-_.~" after Gotham according to RFC3986
    if (StringUtils::isasciialphanum(kar) || kar == '-' || kar == '+' || kar == '.' || kar == '_' || kar == '!' || kar == '(' || kar == ')' || kar == '&' || kar == '=')
      strResult.push_back(kar);
    else
      strResult += StringUtils::Format("%%%02.2X", (unsigned int)((unsigned char)kar)); // TODO: Change to "%%%02.2X" after Gotham
  }
  
  return strResult;
}

std::string FormatUrl(const PlayerInfo &PlayerInfo, const std::string &function, const std::string extrashit)
{
  std::string url = StringUtils::Format("%s?%s%s&security=%s&apiKey=%s",
    PlayerInfo.strApiURL.c_str(),
    function.c_str(),
    extrashit.c_str(),
    GetMD5(PlayerInfo.strSecretKey + function).c_str(),
    PlayerInfo.strApiKey.c_str()
    );

  return url;
}

void SetDefaultPlayerInfo(PlayerInfo &player)
{
  player.strPlayerID = "22";
  player.strSecretKey= "MsqTZFsBYXeNiTprJk3W";
  player.strApiKey   = "cFpN1RnsW9YulGb2Vhvy";
  player.strApiURL   = "http://ec2-54-235-246-85.compute-1.amazonaws.com/api/1/";
  player.strPlayerClientID = "1";
  player.strProgramID = "15";
  CLog::Log(LOGDEBUG, "**RED** - SetDefaultPlayerInfo: using player-id=%s", player.strPlayerID.c_str());
}

bool ParsePlayerInfo(PlayerInfo &player, TiXmlElement *settingsNode)
{
  if (settingsNode)
  {
    player.intSettingsVersion = 0;
    
    XMLUtils::GetInt(settingsNode, "settings_version", player.intSettingsVersion);
    CLog::Log(LOGDEBUG, "**RED** - ParsePlayerInfo: V%i found", player.intSettingsVersion);
    
    if (player.intSettingsVersion == 2)
    {
      CLog::Log(LOGDEBUG, "**RED** - ParsePlayer V2 info");
      TiXmlElement* playerNode = settingsNode->FirstChildElement("player");
      if (playerNode)
      {
        CLog::Log(LOGDEBUG, "**RED** - Parse player node");
        XMLUtils::GetString(playerNode, "player_client_id",      player.strPlayerClientID);
        XMLUtils::GetString(playerNode, "player_client_name",    player.strPlayerClientName);

        XMLUtils::GetString(playerNode, "player_id",             player.strPlayerID);
        XMLUtils::GetString(playerNode, "player_name",           player.strPlayerName);
        //we internally track the player status, so ignore the server player status.
        //XMLUtils::GetString(playerNode, "player_status",       player.strStatus);
        XMLUtils::GetString(playerNode, "player_update_interval",player.strUpdateInterval);
        XMLUtils::GetString(playerNode, "player_download_start_time", player.strDownloadStartTime);
        XMLUtils::GetString(playerNode, "player_download_duration", player.strDownloadDuration);
        XMLUtils::GetString(playerNode, "player_program_id",     player.strProgramID);
        XMLUtils::GetString(playerNode, "player_report_interval", player.strReportInterval);
        XMLUtils::GetString(playerNode, "player_time_zone",      player.strPlayerTimeZone);

        XMLUtils::GetString(playerNode, "player_api_key",        player.strApiKey);
        XMLUtils::GetString(playerNode, "player_api_secret_key", player.strSecretKey);
        XMLUtils::GetString(playerNode, "player_api_url",        player.strApiURL);

        XMLUtils::GetString(playerNode, "player_play_start_time",player.strPlayStartTime);
        XMLUtils::GetString(playerNode, "player_play_duration",  player.strPlayDuration);

        URIUtils::RemoveSlashAtEnd(player.strApiURL);
      }
      
      TiXmlElement* softwareNode = settingsNode->FirstChildElement("software");
      if (softwareNode)
      {
        CLog::Log(LOGDEBUG, "**RED** - Parse software node");
        XMLUtils::GetString(softwareNode, "software_key",            player.strUpdateKey);
        XMLUtils::GetString(softwareNode, "software_md5_hash",       player.strUpdateMD5);
        XMLUtils::GetString(softwareNode, "software_file_size",      player.strUpdateSize);
        XMLUtils::GetString(softwareNode, "software_name",           player.strUpdateName);
        XMLUtils::GetString(softwareNode, "software_release_date",   player.strUpdateDate);
        XMLUtils::GetString(softwareNode, "software_version_number", player.strUpdateVersion);
        XMLUtils::GetString(softwareNode, "software_download_url",   player.strUpdateUrl);
        
        URIUtils::RemoveSlashAtEnd(player.strUpdateUrl);
      }

      return true;
    }
  }
  
  return false;
}

void GetLocalPlayerInfo(PlayerInfo &player, std::string home)
{
  CLog::Log(LOGDEBUG, "**RED** - GetLocalPlayerInfo");
  std::string localPlayer = home + "webdata/PlayerSetup.xml";
  CXBMCTinyXML XmlDoc;
  
  if (XFILE::CFile::Exists(localPlayer) && XmlDoc.LoadFile(localPlayer))
  {
    TiXmlElement *rootXmlNode = XmlDoc.RootElement();
    if (!ParsePlayerInfo(player, rootXmlNode))
      SetDefaultPlayerInfo(player);
  }
  else
  {
    SetDefaultPlayerInfo(player);
  }
}

bool SaveLocalPlayerInfo(const TiXmlElement settingsNode, std::string home)
{
  CLog::Log(LOGDEBUG, "**RED** - SaveLocalPlayerInfo - settings node");
  CXBMCTinyXML xmlDoc;
  TiXmlNode *pRoot = xmlDoc.InsertEndChild(settingsNode);
  if (!pRoot) return false;
  
  std::string localPlayer = home + "webdata/PlayerSetup.xml";
  return xmlDoc.SaveFile(localPlayer);
}

std::string GetMD5(const std::string strText)
{
  std::string hash = XBMC::XBMC_MD5::GetMD5(strText);
  StringUtils::ToLower(hash);
  return hash;
}

bool IsRedVideo(RedMediaAsset asset)
{
  return StringUtils::EqualsNoCase(asset.type, "video");
}

void OpenAndroidSettings()
{
  CLog::Log(LOGDEBUG, "**RED** - OpenAndroidSettings");
  
#if defined(TARGET_ANDROID)
  std::vector<std::string> params;
  std::string param = "com.android.settings";
  params.push_back(param);
  CApplicationMessenger::Get().StartAndroidActivity(params);
#endif
}

std::string GetDiskUsed(std::string path)
{
  std::string used;
  std::string home = CSpecialProtocol::TranslatePath(path);
  
  std::vector<std::string> diskUsage = g_mediaManager.GetDiskUsage();
  for (size_t d = 1; d < diskUsage.size(); d++)
  {
    StringUtils::RemoveCRLF(diskUsage[d]);
    StringUtils::RemoveDuplicatedSpacesAndTabs(diskUsage[d]);
    
    std::vector<std::string> items = StringUtils::Split(diskUsage[d], " ");
    std::string mountPoint = items[items.size() - 1];
    if (mountPoint == "/")
      used = items[2];
    if (mountPoint.find(path) != std::string::npos)
      used = items[2];
  }

  StringUtils::Replace(used, "Gi", "GB");

  return used;
}

std::string GetDiskFree(std::string path)
{
  std::string free;
  std::string home = CSpecialProtocol::TranslatePath(path);

  std::vector<std::string> diskUsage = g_mediaManager.GetDiskUsage();
  for (size_t d = 1; d < diskUsage.size(); d++)
  {
    StringUtils::RemoveCRLF(diskUsage[d]);
    StringUtils::RemoveDuplicatedSpacesAndTabs(diskUsage[d]);

    std::vector<std::string> items = StringUtils::Split(diskUsage[d], " ");
    std::string mountPoint = items[items.size() - 1];
    if (mountPoint == "/")
      free = items[3];
    if (mountPoint.find(path) != std::string::npos)
      free = items[3];
  }
  
  StringUtils::Replace(free, "Gi", "GB");
  
  return free;
}

std::string GetSystemUpTime()
{
  // uptime=0 days 5 hours 22 minutes
  int iMinutes = 0; int iHours = 0; int iDays = 0;
  iMinutes = g_sysinfo.GetTotalUptime();
  if (iMinutes >= 60) // Hour's
  {
    iHours = iMinutes / 60;
    iMinutes = iMinutes - (iHours *60);
  }
  if (iHours >= 24) // Days
  {
    iDays = iHours / 24;
    iHours = iHours - (iDays * 24);
  }
  
  std::string uptime = StringUtils::Format("%d days %d hours %d minutes", iDays, iHours, iMinutes);
  return uptime;
}

bool SetDownloadedAsset(const std::string AssetID, bool downloaded)
{
  // simplify calling RED db SetDownloadedAsset
  CDBManagerRed database;
  database.Open();
  bool result = database.SetDownloadedAsset(AssetID,downloaded);
  database.Close();
  return result;
}

