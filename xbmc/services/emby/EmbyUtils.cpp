/*
 *      Copyright (C) 2017 Team MrMC
 *      https://github.com/MrMC
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

#include "EmbyUtils.h"
#include "EmbyServices.h"
#include "Application.h"
#include "ContextMenuManager.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/StackDirectory.h"
#include "network/Network.h"
#include "utils/Base64.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/ZipFile.h"
#include "settings/Settings.h"

#include "video/VideoInfoTag.h"
#include "video/windows/GUIWindowVideoBase.h"

#include "music/tags/MusicInfoTag.h"
#include "music/dialogs/GUIDialogSongInfo.h"
#include "music/dialogs/GUIDialogMusicInfo.h"
#include "guilib/GUIWindowManager.h"

static int  g_progressSec = 0;
static CFileItem m_curItem;
static EmbyUtilsPlayerState g_playbackState = EmbyUtilsPlayerState::stopped;

bool CEmbyUtils::HasClients()
{
  return CEmbyServices::GetInstance().HasClients();
}

void CEmbyUtils::GetDefaultHeaders(const std::string& userId, XFILE::CCurlFile &curl)
{
/*
  curl.SetRequestHeader("Content-Type", "application/xml; charset=utf-8");
  curl.SetRequestHeader("Content-Length", "0");
  curl.SetRequestHeader("Connection", "Keep-Alive");
  curl.SetUserAgent(CSysInfo::GetUserAgent());
  curl.SetRequestHeader("X-Plex-Client-Identifier", CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID));
  curl.SetRequestHeader("X-Plex-Product", "MrMC");
  curl.SetRequestHeader("X-Plex-Version", CSysInfo::GetVersionShort());
  std::string hostname;
  g_application.getNetwork().GetHostName(hostname);
  StringUtils::TrimRight(hostname, ".local");
  curl.SetRequestHeader("X-Plex-Model", CSysInfo::GetModelName());
  curl.SetRequestHeader("X-Plex-Device", CSysInfo::GetModelName());
  curl.SetRequestHeader("X-Plex-Device-Name", hostname);
  curl.SetRequestHeader("X-Plex-Platform", CSysInfo::GetOsName());
  curl.SetRequestHeader("X-Plex-Platform-Version", CSysInfo::GetOsVersion());
  curl.SetRequestHeader("Cache-Control", "no-cache");
  curl.SetRequestHeader("Pragma", "no-cache");
  curl.SetRequestHeader("Expires", "Sat, 26 Jul 1997 05:00:00 GMT");
*/
  curl.SetRequestHeader("Accept", "application/json");
  curl.SetRequestHeader("X-Emby-Authorization",
    StringUtils::Format("MediaBrowser Client=\"%s\", Device=\"%s\", DeviceId=\"%s\", Version=\"%s\", UserId=\"%s\"",
      CSysInfo::GetAppName().c_str(), CSysInfo::GetDeviceName().c_str(),
      CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID).c_str(),
      CSysInfo::GetVersionShort().c_str(), userId.c_str()));
}

void CEmbyUtils::SetEmbyItemProperties(CFileItem &item)
{
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(item.GetPath());
  SetEmbyItemProperties(item, client);
}

void CEmbyUtils::SetEmbyItemProperties(CFileItem &item, const CEmbyClientPtr &client)
{
  item.SetProperty("EmbyItem", true);
  item.SetProperty("MediaServicesItem", true);
  if (!client)
    return;
  if (client->IsCloud())
    item.SetProperty("MediaServicesCloudItem", true);
  item.SetProperty("MediaServicesClientID", client->GetUuid());
}

void CEmbyUtils::SetEmbyItemsProperties(CFileItemList &items)
{
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(items.GetPath());
  SetEmbyItemsProperties(items, client);
}

void CEmbyUtils::SetEmbyItemsProperties(CFileItemList &items, const CEmbyClientPtr &client)
{
  items.SetProperty("EmbyItem", true);
  items.SetProperty("MediaServicesItem", true);
  if (!client)
    return;
  if (client->IsCloud())
    items.SetProperty("MediaServicesCloudItem", true);
  items.SetProperty("MediaServicesClientID", client->GetUuid());
}

