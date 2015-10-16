/*
 *      Copyright (C) 2005-2013 Team RED
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

#include "RedJSONOperations.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/Variant.h"
#include "powermanagement/PowerManager.h"

#include "Application.h"
#include "PlayerManagerRed.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "UtilitiesRed.h"

using namespace JSONRPC;
using namespace KODI::MESSAGING;

JSONRPC_STATUS CRedJSONOperations::SendClick(const std::string &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  
  std::string function = parameterObject["click"].asString();
  CPlayerManagerRed* RedPlayerManager = CPlayerManagerRed::GetPlayerManager();
  if (function == "playPause")
  {
    if (RedPlayerManager)
      RedPlayerManager->PlayPause();
  }
  else if (function == "next")
  {
    if (RedPlayerManager)
      RedPlayerManager->PlayNext();
  }
  else if (function == "stop")
  {
    if (RedPlayerManager)
      RedPlayerManager->StopPlaying();
  }
  return OK;
}

JSONRPC_STATUS CRedJSONOperations::SaveXML(const std::string &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  std::string xmlStr = parameterObject["xml"].asString();
  std::string xml = "special://red/webdata/PlayerSetup.xml";
  
  CXBMCTinyXML xmlDoc;
  xmlDoc.Parse(xmlStr);
  if (xmlDoc.SaveFile(xml))
  {
    CPlayerManagerRed* RedPlayerManager = CPlayerManagerRed::GetPlayerManager();
    if (RedPlayerManager)
    {
      // Notify that we have changed settings
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info,
                                            "R.E.D",
                                            "Player details updated",
                                            TOAST_DISPLAY_TIME, false);
      RedPlayerManager->ForceLocalPlayerUpdate();
      RedPlayerManager->FullUpdate();
    }
  }
  return OK;
}
