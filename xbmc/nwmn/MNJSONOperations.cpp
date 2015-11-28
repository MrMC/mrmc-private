/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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
#include "utils/SortUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "music/tags/MusicInfoTag.h"
#include "music/Artist.h"
#include "music/Album.h"
#include "music/Song.h"
#include "music/Artist.h"
#include "messaging/ApplicationMessenger.h"
#include "filesystem/Directory.h"
#include "settings/Settings.h"

using namespace JSONRPC;
using namespace KODI::MESSAGING;

JSONRPC_STATUS CMNJSONOperations::SetPlayerSettings(const std::string &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  std::string argv1;
  CVariant params = parameterObject["params"];
  std::string xmlStr = parameterObject["set"].asString();
  
  if (params.isObject())
  {
    for (CVariant::const_iterator_map it = params.begin_map(); it != params.end_map(); it++)
    {
      if (it->first == "values")
      {
        argv1 = it->second.asString();
        StringUtils::Replace(argv1, ";", ",");
        StringUtils::Replace(argv1, "values=", "");
        StringUtils::Replace(argv1, "\\", "");
        StringUtils::Replace(argv1, "},",";");
      }
    }
  }
  
  // we have to parse  above and extract values for url, machine and location ID
  
  return OK;
}