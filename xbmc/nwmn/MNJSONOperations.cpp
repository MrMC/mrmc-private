/*
 *  Copyright (C) 2005-2013 Team MN
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

#include "MNJSONOperations.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/Variant.h"
#include "powermanagement/PowerManager.h"
#include "JSONRPCUtils.h"
#include "utils/JSONVariantWriter.h"
#include "utils/JSONVariantParser.h"

#include "AudioLibrary.h"
#include "music/MusicDatabase.h"
#include "FileItem.h"
#include "Util.h"
#include "filesystem/File.h"
#include "utils/SortUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "music/tags/MusicInfoTag.h"
#include "music/Artist.h"
#include "music/Album.h"
#include "music/Song.h"
#include "music/Artist.h"
#include "messaging/ApplicationMessenger.h"
#include "filesystem/Directory.h"
#include "settings/Settings.h"
#include "PlayerManagerMN.h"
#include "dialogs/GUIDialogKaiToast.h"

using namespace JSONRPC;
using namespace KODI::MESSAGING;

JSONRPC_STATUS CMNJSONOperations::SetPlayerSettings(const std::string &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  std::string argv;
  CVariant params = parameterObject["params"];
  
  if (params.isObject())
  {
    for (CVariant::const_iterator_map it = params.begin_map(); it != params.end_map(); it++)
    {
      if (it->first == "values")
      {
        argv = it->second.asString();
        StringUtils::Replace(argv, ";", ",");
      }
    }
  }
  
  std::string settingsFile = "special://home/MN/settings.txt";
  
  XFILE::CFile file;
  if (!file.OpenForWrite(settingsFile, true))
  {
    CLog::Log(LOGERROR, "MN Save settings.txt - Unable to open file");
  }
  else
  {
    file.Write(argv.c_str(), argv.size());
  }
  file.Close();
  
  CVariant obj = CJSONVariantParser::Parse((const unsigned char *)argv.c_str(), argv.size());
  
  std::string url;
  std::string machineID;
  std::string locationID;
  
  if (obj.isObject())
  {
    for (CVariant::const_iterator_map it = obj.begin_map(); it != obj.end_map(); it++)
    {
      if (it->first == "url")
      {
        url = it->second["feed"].asString();
      }
      else if (it->first == "location")
      {
        locationID = it->second["id"].asString();
      }
      else if (it->first == "machine")
      {
        machineID = it->second["id"].asString();
      }
    }
  }
  
  if (url.empty() || machineID.empty() || locationID.empty())
  {
    CLog::Log(LOGERROR, "MN ERROR parsing settings, url - %s, Machine ID - %s , location ID - %s", url.c_str(), machineID.c_str(), locationID.c_str());
  }
  else
  {
    CLog::Log(LOGERROR, "MN Updated settings, url - %s, Machine ID - %s , location ID - %s", url.c_str(), machineID.c_str(), locationID.c_str());
    
    CSettings::GetInstance().SetString(CSettings::MN_LOCATION_ID ,locationID);
    CSettings::GetInstance().SetString(CSettings::MN_MACHINE_ID  ,machineID);
    CSettings::GetInstance().SetString(CSettings::MN_URL         ,url);
    // Notify that we have changed settings
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info,
                                          "MemberNet",
                                          "Player details updated",
                                          TOAST_DISPLAY_TIME, false);
    CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
    if (MNPlayerManager)
      MNPlayerManager->Startup();
    
  }
  return OK;
}