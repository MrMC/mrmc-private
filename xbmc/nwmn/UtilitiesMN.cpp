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
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "nwmn/UtilitiesMN.h"
#include "nwmn/MNMedia.h"

#include "URL.h"
#include "utils/md5.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "storage/MediaManager.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/XMLUtils.h"

#if defined(TARGET_LINUX)
  #include "linux/LinuxTimezone.h"
#endif

bool PingMNServer(const std::string& apiURL)
{
  CURL url(apiURL.c_str());
  std::string http_path = url.GetProtocol().c_str();
  http_path += "://" + url.GetHostName();

  XFILE::CCurlFile http;
  CURL http_url(http_path.c_str());
  bool found = http.Open(http_url);
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
  
  // local media xml
  std::string localMediaXML = "special://MN/media_xml.xml";
  
  CXBMCTinyXML xmlDoc;
  TiXmlElement *rootXmlNode;
  if (PingMNServer(url))
  {
    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);
    
    xmlDoc.Parse(strXML.c_str());
    rootXmlNode = xmlDoc.RootElement();
    xmlDoc.InsertEndChild(*rootXmlNode);
    xmlDoc.SaveFile(localMediaXML);
  }
  else if (XFILE::CFile::Exists(localMediaXML))// No internet
  {
    xmlDoc.LoadFile(localMediaXML);
    rootXmlNode = xmlDoc.RootElement();
  }
  
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
  
  // Local settings xml
  std::string localSettingsXML = "special://MN/settings_xml.xml";
  
  CXBMCTinyXML xmlDoc;
  TiXmlElement *rootXmlNode;
  
  if (PingMNServer(url))
  {
    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);
    
    xmlDoc.Parse(strXML.c_str());
    rootXmlNode = xmlDoc.RootElement();
    xmlDoc.InsertEndChild(*rootXmlNode);
    xmlDoc.SaveFile(localSettingsXML);
  }
  else if (XFILE::CFile::Exists(localSettingsXML))// No internet
  {
    xmlDoc.LoadFile(localSettingsXML);
    rootXmlNode = xmlDoc.RootElement();
  }
  

  
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
