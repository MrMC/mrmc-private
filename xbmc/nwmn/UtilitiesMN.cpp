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

#include "nwmn/UtilitiesMN.h"
#include "nwmn/NWTVAPI.h"

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

void GetLocalPlayerInfo(NWPlayerInfo &player, std::string home)
{
/*
  CLog::Log(LOGDEBUG, "**NW** - GetLocalPlayerInfo");
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
*/
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

bool SetDownloadedAsset(int assetID, bool downloaded)
{
//  // simplify calling MN db SetDownloadedAsset
//  CDBManagerMN database;
//  database.Open();
//  bool result = database.SetDownloadedAsset(AssetID,downloaded);
//  database.Close();
//  return result;
  return false;
}
