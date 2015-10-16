/*
 *  Copyright (C) 2014 Team MN
 *
 *  This Program is free software; you can MNistribute it and/or modify
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

#include "MNMedia.h"
#include "UtilitiesMN.h"
//#include "DBManagerMN.h"

#include "ApplicationMessenger.h"
#include "URL.h"
#include "Util.h"
#include "utils/md5.h"
#include "XFileUtils.h"
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
#include "LangInfo.h"

#if defined(TARGET_LINUX)
#include "linux/LinuxTimezone.h"
#endif

bool PingMNServer(const std::string& apiURL)
{
  CURL url(apiURL.c_str());
  XFILE::CCurlFile http;
  std::string http_path = url.GetProtocol().c_str();
  http_path += "://" + url.GetHostName();
  CURL http_url(http_path.c_str());
  bool found = http.Exists(http_url);
  if (!found && (errno == EACCES))
    found = true;
  http.Close();
  
  if (found)
    CLog::Log(LOGDEBUG, "**MN** - PingMNServer: network=yes");
  else
    CLog::Log(LOGDEBUG, "**MN** - PingMNServer: network=no");

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

//std::string FormatUrl(const PlayerInfo &PlayerInfo, const std::string &function, const std::string extrashit)
//{
//  std::string url = StringUtils::Format("%s?%s%s&security=%s&apiKey=%s",
//    PlayerInfo.strApiURL.c_str(),
//    function.c_str(),
//    extrashit.c_str(),
//    GetMD5(PlayerInfo.strSecretKey + function).c_str(),
//    PlayerInfo.strApiKey.c_str()
//    );
//
//  return url;
//}
//
//void SetDefaultPlayerInfo(PlayerInfo &player)
//{
//  player.strPlayerID = "1";
//  player.strSecretKey= "MsqTZFsBYXeNiTprJk3W";
//  player.strApiKey   = "cFpN1RnsW9YulGb2Vhvy";
//  player.strApiURL   = "http://ec2-54-235-246-85.compute-1.amazonaws.com/api/1";
//  CLog::Log(LOGDEBUG, "**MN** - SetDefaultPlayerInfo: using player-id=%s", player.strPlayerID.c_str());
//}
//
//bool ParsePlayerInfo(PlayerInfo &player, TiXmlElement *settingsNode)
//{
//  if (settingsNode)
//  {
//    player.intSettingsVersion = 0;
//    
//    XMLUtils::GetInt(settingsNode, "settings_version", player.intSettingsVersion);
//    CLog::Log(LOGDEBUG, "**MN** - ParsePlayerInfo: V%i found", player.intSettingsVersion);
//    
//    if (player.intSettingsVersion == 2)
//    {
//      CLog::Log(LOGDEBUG, "**MN** - ParsePlayer V2 info");
//      TiXmlElement* playerNode = settingsNode->FirstChildElement("player");
//      if (playerNode)
//      {
//        CLog::Log(LOGDEBUG, "**MN** - Parse player node");
//        XMLUtils::GetString(playerNode, "player_client_id",      player.strPlayerClientID);
//        XMLUtils::GetString(playerNode, "player_client_name",    player.strPlayerClientName);
//
//        XMLUtils::GetString(playerNode, "player_id",             player.strPlayerID);
//        XMLUtils::GetString(playerNode, "player_name",           player.strPlayerName);
//        //we internally track the player status, so ignore the server player status.
//        //XMLUtils::GetString(playerNode, "player_status",       player.strStatus);
//        XMLUtils::GetString(playerNode, "player_update_interval",player.strUpdateInterval);
//        XMLUtils::GetString(playerNode, "player_download_start_time", player.strDownloadStartTime);
//        XMLUtils::GetString(playerNode, "player_download_duration", player.strDownloadDuration);
//        XMLUtils::GetString(playerNode, "player_program_id",     player.strProgramID);
//        XMLUtils::GetString(playerNode, "player_report_interval", player.strReportInterval);
//        XMLUtils::GetString(playerNode, "player_time_zone",      player.strPlayerTimeZone);
//
//        XMLUtils::GetString(playerNode, "player_api_key",        player.strApiKey);
//        XMLUtils::GetString(playerNode, "player_api_secret_key", player.strSecretKey);
//        XMLUtils::GetString(playerNode, "player_api_url",        player.strApiURL);
//
//        XMLUtils::GetString(playerNode, "player_play_start_time",player.strPlayStartTime);
//        XMLUtils::GetString(playerNode, "player_play_duration",  player.strPlayDuration);
//
//        URIUtils::RemoveSlashAtEnd(player.strApiURL);
//      }
//      
//      TiXmlElement* softwareNode = settingsNode->FirstChildElement("software");
//      if (softwareNode)
//      {
//        CLog::Log(LOGDEBUG, "**MN** - Parse software node");
//        XMLUtils::GetString(softwareNode, "software_key",            player.strUpdateKey);
//        XMLUtils::GetString(softwareNode, "software_md5_hash",       player.strUpdateMD5);
//        XMLUtils::GetString(softwareNode, "software_file_size",      player.strUpdateSize);
//        XMLUtils::GetString(softwareNode, "software_name",           player.strUpdateName);
//        XMLUtils::GetString(softwareNode, "software_release_date",   player.strUpdateDate);
//        XMLUtils::GetString(softwareNode, "software_version_number", player.strUpdateVersion);
//        XMLUtils::GetString(softwareNode, "software_download_url",   player.strUpdateUrl);
//        
//        URIUtils::RemoveSlashAtEnd(player.strUpdateUrl);
//      }
//
//      return true;
//    }
//  }
//  
//  return false;
//}

//void GetLocalPlayerInfo(PlayerInfo &player, std::string home)
//{
//  CLog::Log(LOGDEBUG, "**MN** - GetLocalPlayerInfo");
//  std::string localPlayer = home + "webdata/PlayerSetup.xml";
//  CXBMCTinyXML XmlDoc;
//  
//  if (XFILE::CFile::Exists(localPlayer) && XmlDoc.LoadFile(localPlayer))
//  {
//    TiXmlElement *rootXmlNode = XmlDoc.RootElement();
//    if (!ParsePlayerInfo(player, rootXmlNode))
//      SetDefaultPlayerInfo(player);
//  }
//  else
//  {
//    SetDefaultPlayerInfo(player);
//  }
//}

bool SaveLocalPlayerInfo(const TiXmlElement settingsNode, std::string home)
{
  CLog::Log(LOGDEBUG, "**MN** - SaveLocalPlayerInfo - settings node");
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

bool IsMNVideo(MNMediaAsset asset)
{
//  return StringUtils::EqualsNoCase(asset.type, "video");
  return false;
}

void OpenAndroidSettings()
{
  CLog::Log(LOGDEBUG, "**MN** - OpenAndroidSettings");
  
#if defined(TARGET_ANDROID)
  std::vector<CStdString> params;
  CStdString param = "com.android.settings";
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

std::string GetDiskTotal(std::string path)
{
  std::string total;
  std::string home = CSpecialProtocol::TranslatePath(path);

  std::vector<std::string> diskUsage = g_mediaManager.GetDiskUsage();
  for (size_t d = 1; d < diskUsage.size(); d++)
  {
    StringUtils::RemoveCRLF(diskUsage[d]);
    StringUtils::RemoveDuplicatedSpacesAndTabs(diskUsage[d]);
    
    std::vector<std::string> items = StringUtils::Split(diskUsage[d], " ");
    std::string mountPoint = items[items.size() - 1];
    if (mountPoint == "/")
      total = items[1];
    if (mountPoint.find(path) != std::string::npos)
      total = items[1];
  }
  StringUtils::Replace(total, "Gi", "GB");
  
  return total;
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
  
  std::string uptime = StringUtils::Format("%d day%s %d hours %d minutes",
                                           iDays,
                                           iDays > 1 ? "s" :"",
                                           iHours,
                                           iMinutes);
  return uptime;
}

bool SetDownloadedAsset(const std::string AssetID, bool downloaded)
{
//  // simplify calling MN db SetDownloadedAsset
//  CDBManagerMN database;
//  database.Open();
//  bool result = database.SetDownloadedAsset(AssetID,downloaded);
//  database.Close();
//  return result;
  return false;
}

void ParseMediaXML(PlayerSettings settings,std::vector<MNCategory> &categories, MNCategory &OnDemand)
{
  
  std::string url = StringUtils::Format("%s/xml/tv/media.php?machine=%s-%s",
                                        settings.strUrl_feed.c_str(),
                                        settings.strLocation_id.c_str(),
                                        settings.strMachine_id.c_str()
                                        );
  categories.clear();
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  TiXmlElement* rootXmlNode = xml.RootElement();
  
  CXBMCTinyXML xmlDoc;
  TiXmlNode *pRoot = xmlDoc.InsertEndChild(*rootXmlNode);
  
  // save media xml
  std::string localMediaXML = "special://MN/media_xml.xml";
  xmlDoc.SaveFile(localMediaXML);
  
  if (rootXmlNode)
  {
    
    TiXmlNode *pCatNode = NULL;
    while ((pCatNode = rootXmlNode->IterateChildren(pCatNode)) != NULL)
    {
      if (pCatNode->ValueStr() == "category")
      {
        MNCategory category;
        category.id = ((TiXmlElement*) pCatNode)->Attribute("id");
        category.name = ((TiXmlElement*) pCatNode)->Attribute("name");
        category.icon = ((TiXmlElement*) pCatNode)->Attribute("icon");

        TiXmlNode *pAssetNode = NULL;
        while ((pAssetNode = pCatNode->IterateChildren(pAssetNode)) != NULL)
        {
          MNMediaAsset asset;
          asset.id = ((TiXmlElement*) pAssetNode)->Attribute("id");
          asset.category_id = category.id;
          XMLUtils::GetString(pAssetNode, "title", asset.title);
          // video details
          TiXmlElement* pMetadataNode = pAssetNode->FirstChildElement("media:highresolution_video");
          if (pMetadataNode)
          {
            asset.video_url      = ((TiXmlElement*) pMetadataNode)->Attribute("url");
            asset.video_md5      = ((TiXmlElement*) pMetadataNode)->Attribute("md5");
            asset.video_fileSize = ((TiXmlElement*) pMetadataNode)->Attribute("fileSize");
            asset.video_height   = ((TiXmlElement*) pMetadataNode)->Attribute("height");
            asset.video_width    = ((TiXmlElement*) pMetadataNode)->Attribute("width");
            asset.video_duration = ((TiXmlElement*) pMetadataNode)->Attribute("duration");
          }
          // thumb details
          pMetadataNode = pAssetNode->FirstChildElement("media:lowresolution_posterframe");
          if (pMetadataNode)
          {
            asset.thumb_url      = ((TiXmlElement*) pMetadataNode)->Attribute("thumb_url");
            asset.thumb_md5      = ((TiXmlElement*) pMetadataNode)->Attribute("thumb_md5");
            asset.thumb_fileSize = ((TiXmlElement*) pMetadataNode)->Attribute("thumb_fileSize");
            asset.thumb_width    = ((TiXmlElement*) pMetadataNode)->Attribute("thumb_width");
            asset.thumb_height   = ((TiXmlElement*) pMetadataNode)->Attribute("thumb_height");
          }
          // start/stop details
          pMetadataNode = pAssetNode->FirstChildElement("available")->FirstChildElement("publish");
          if (pMetadataNode)
          {
            asset.available_start     = ((TiXmlElement*) pMetadataNode)->Attribute("start");
            asset.available_stop      = ((TiXmlElement*) pMetadataNode)->Attribute("stop");
          }

          category.items.push_back(asset);
        }
        
        if (category.items.size() > 0)
        {
          if (category.id == "10")
            OnDemand = category;
          else
            categories.push_back(category);
        }
      }
    }
  }
}

void ParseSettingsXML(PlayerSettings &settings)
{
  
  std::string url = StringUtils::Format("%s/xml/tv/settings.php?machine=%s-%s",
                                        settings.strUrl_feed.c_str(),
                                        settings.strLocation_id.c_str(),
                                        settings.strMachine_id.c_str()
                                        );
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url, strXML);

  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  TiXmlElement* rootXmlNode = xml.RootElement();
  
  CXBMCTinyXML xmlDoc;
  TiXmlNode *pRoot = xmlDoc.InsertEndChild(*rootXmlNode);
  
  // save settings xml
  std::string localSettingsXML = "special://MN/settings_xml.xml";
  xmlDoc.SaveFile(localSettingsXML);
  

  
  if (rootXmlNode)
  {
    TiXmlElement* pSettingsNode = rootXmlNode->FirstChildElement("settings");
    if (pSettingsNode)
    {
      TiXmlElement* pplaylistsNode = pSettingsNode->FirstChildElement("playlist");
      settings.strSettings_update_interval = ((TiXmlElement*) pplaylistsNode)->Attribute("updateinterval");
      settings.strSettings_update_time     = ((TiXmlElement*) pplaylistsNode)->Attribute("time");
    }

    TiXmlElement *pPLNode = rootXmlNode->FirstChildElement("playlist");
    TiXmlElement* pCategoryNode = pPLNode->FirstChildElement("category");
    while (pCategoryNode)
    {
      settings.intCategories_order.push_back(((TiXmlElement*) pCategoryNode)->Attribute("id"));
      pCategoryNode = pCategoryNode->NextSiblingElement("category");
    }
    
    std::string TZ;
    XMLUtils::GetString(rootXmlNode, "timezone", TZ);
    
    if (TZ != settings.strTimeZone)
    {
      settings.strTimeZone = TZ;
#if defined(TARGET_LINUX)
      g_timezone.SetTimezone(settings.strTimeZone);
      CDateTime::ResetTimezoneBias();
#endif
    }
    
    TiXmlElement *pUpdateNode = rootXmlNode->FirstChildElement("allow_software_update");
    settings.allowUpdate = strncmp(((TiXmlElement*) pUpdateNode)->Attribute("available"), "1", strlen("1"))==0;
    
    TiXmlElement *pSWNode = rootXmlNode->FirstChildElement("mn_software");
    settings.strSettings_cf_bundle_version = ((TiXmlElement*) pSWNode)->Attribute("CFBundleVersion");
    settings.strSettings_software_version  = ((TiXmlElement*) pSWNode)->Attribute("version");
    settings.strSettings_software_url      = ((TiXmlElement*) pSWNode)->Attribute("url");
  }
}

void LogPlayback(std::string home,PlayerSettings settings,std::string assetID)
{
  
//  date,assetID
//  2015-02-05 12:01:40-0500,58350
//  2015-02-05 12:05:40-0500,57116
  
  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strFileName = StringUtils::Format("%slog/%s_%s_%s_%s_playback.log",
                                                home.c_str(),
                                                settings.strLocation_id.c_str(),
                                                settings.strMachine_id.c_str(),
                                                settings.strMachine_sn.c_str(),
                                                time.GetAsDBDate().c_str()
                                                );
                                                
                                                
  XFILE::CFile file;
  XFILE::auto_buffer buffer;
  
  if (XFILE::CFile::Exists(strFileName))
  {
    file.LoadFile(strFileName, buffer);
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    std::string header = "date,assetID\n";
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  CLangInfo langInfo;
  std::string strData = StringUtils::Format("%s%s,%s\n",
                                            time.GetAsDBDateTime().c_str(),
                                            langInfo.GetTimeZone().c_str(),
                                            assetID.c_str()
                                            );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

void LogSettings(std::string home,PlayerSettings settings)
{
  
  //  date,uptime,disk-used,disk-free,smart-status
  //  2015-03-04 18:08:21+0400,11 days 2 hours 12 minutes,118GB,24GB,Disks OK
  
  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strFileName = StringUtils::Format("%slog/%s_%s_%s_%s_settings.log",
                                                home.c_str(),
                                                settings.strLocation_id.c_str(),
                                                settings.strMachine_id.c_str(),
                                                settings.strMachine_sn.c_str(),
                                                time.GetAsDBDate().c_str()
                                                );
  
  
  XFILE::CFile file;
  XFILE::auto_buffer buffer;
  
  if (XFILE::CFile::Exists(strFileName))
  {
    file.LoadFile(strFileName, buffer);
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    std::string header = "date,uptime,disk-used,disk-free,smart-status\n";
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  CLangInfo langInfo;
  std::string strData = StringUtils::Format("%s%s,%s,%s,%s,Disks OK\n",
                                            time.GetAsDBDateTime().c_str(),
                                            langInfo.GetTimeZone().c_str(),
                                            GetSystemUpTime().c_str(),
                                            GetDiskUsed("/").c_str(),
                                            GetDiskFree("/").c_str()
                                            );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

void UploadLogs(PlayerSettings settings)
{
//  std::string cmd;
//  cmd = StringUtils::Format("RunAddon(script.nationwide_helper,upload,%s,%s,%s)",
//                            settings.strLocation_id.c_str(),
//                            settings.strMachine_id.c_str(),
//                            settings.strMachine_sn.c_str()
//                            );
//  CApplicationMessenger::GetInstance().ExecBuiltIn(cmd, false);
}