/*
 *      Copyright (C) 2016 Team MrMC
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

#include "PlexServices.h"

#include "Application.h"
#include "URL.h"
#include "GUIUserMessages.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/CurlFile.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "profiles/dialogs/GUIDialogLockSettings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include "utils/JSONVariantParser.h"
#include "utils/Variant.h"
#include "utils/XMLUtils.h"

#include "PlexUtils.h"
#include "PlexClient.h"

using namespace ANNOUNCEMENT;

#define NS_PLEX_MEDIA_SERVER_PORT 32414
#define NS_BROADCAST_ADDR "239.0.0.250"
#define NS_SEARCH_MSG "M-SEARCH * HTTP/1.1\r\n"

CPlexServices::CPlexServices()
: CThread("PlexServices")
{
}

CPlexServices::~CPlexServices()
{
  if (IsRunning())
    Stop();
}

CPlexServices& CPlexServices::GetInstance()
{
  static CPlexServices sPlexServices;
  return sPlexServices;
}

void CPlexServices::Start()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    StopThread();
  CThread::Create();
}

void CPlexServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    StopThread();
  m_clients.clear();
}

bool CPlexServices::IsActive()
{
  return IsRunning();
}

void CPlexServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
}

void CPlexServices::OnSettingAction(const CSetting *setting)
{
  if (setting == nullptr)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXSIGNIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNIN) == g_localizeStrings.Get(1240))
    {
      // prompt is 'sign-in'
      std::string user;
      std::string pass;
      std::string module;
      bool saveDetails = false;
      if (CGUIDialogLockSettings::ShowAndGetUserAndPassword(user, pass, module, &saveDetails, true))
      {
        if (!user.empty() && !pass.empty())
        {
          m_myPlexUser = user;
          m_myPlexPass = pass;
          if (FetchPlexToken())
          {
            // change prompt to 'sign-out'
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, g_localizeStrings.Get(1241));
            // have to call this directly as we are changing a label and it will not trigger a OnSettingChanged
            OnSettingChanged(setting);
          }
        }
        else
        {
          // opps, nuke'em all
          m_myPlexUser.clear();
          m_myPlexPass.clear();
          m_myPlexToken.clear();
        }
      }
    }
    else
    {
      // prompt is 'sign-out'
      // clear user/pass/auth and change prompt to 'sign-in'
      m_myPlexUser.clear();
      m_myPlexPass.clear();
      m_myPlexToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, g_localizeStrings.Get(1240));
      // have to call this directly as we are changing a label and it will not trigger a OnSettingChanged
      OnSettingChanged(setting);
    }
    // save changes
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER, m_myPlexUser);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS, m_myPlexPass);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH, m_myPlexToken);
  }
}

void CPlexServices::OnSettingChanged(const CSetting *setting)
{
  // All Plex settings so far
  /*
   static const std::string SETTING_SERVICES_PLEXSIGNIN;
   static const std::string SETTING_SERVICES_PLEXGDMSERVER;
   static const std::string SETTING_SERVICES_PLEXMYPLEXUSER;
   static const std::string SETTING_SERVICES_PLEXMYPLEXPASS;
   static const std::string SETTING_SERVICES_PLEXMYPLEXAUTH;
   */

  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (!m_myPlexToken.empty() ||
      settingId == CSettings::SETTING_SERVICES_PLEXGDMSERVER)
  {
    ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "UpdateRecentlyAdded");
    ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "UpdateRecentlyAdded");
    // start or stop the service
    if (static_cast<const CSettingBool*>(setting)->GetValue())
      Start();
    else
      Stop();
  }

  CSettings::GetInstance().Save();
}

void CPlexServices::ApplyUserSettings()
{
  m_myPlexUser = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER);
  m_myPlexPass = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS);
  m_myPlexToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH);
  // end of Plex settings

  // 0 is disabled, 1 is auto
  m_useGDMServer = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER);
}

void CPlexServices::Process()
{
  bool hasPlexServers = false;
  ApplyUserSettings();

  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  if (iface)
  {
    SOCKETS::CUDPSocket *socket = nullptr;
    SOCKETS::CSocketListener listener;
    if (m_useGDMServer)
    {
      socket = SOCKETS::CSocketFactory::CreateUDPSocket();
      if (socket)
      {
        SOCKETS::CAddress my_addr;
        my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
        if (!socket->Bind(my_addr, NS_PLEX_MEDIA_SERVER_PORT, 0))
        {
          CLog::Log(LOGERROR, "CPlexServices: Could not listen on port %d", NS_PLEX_MEDIA_SERVER_PORT);
          SAFE_DELETE(socket);
        }

        if (socket)
        {
          socket->SetBroadCast(true);
          // add our socket to the 'select' listener
          listener.AddSocket(socket);
          // do an initial broadcast to get things rolling
          SendDiscoverBroadcast(socket);
        }
      }
      else
        CLog::Log(LOGERROR, "CPlexServices: Could not create socket for GDM");
    }

    // try plex.tv
    if (!m_myPlexToken.empty() && !hasPlexServers)
      hasPlexServers = FetchMyPlexServers();

    CStopWatch idleTimer;
    idleTimer.StartZero();
    while (!m_bStop)
    {
      // recheck services every N seconds
      if (idleTimer.GetElapsedMilliseconds() > 5000)
      {
        // try plex.tv
        if (!m_myPlexToken.empty() && !hasPlexServers)
          hasPlexServers = FetchMyPlexServers();

        // check GDM
        if (socket)
          SendDiscoverBroadcast(socket);

        idleTimer.Reset();
      }

      // listen for GDM reply until we timeout
      if (socket && listener.Listen(250))
      {
        char buffer[1024] = {0};
        SOCKETS::CAddress sender;
        int packetSize = socket->Read(sender, 1024, buffer);
        if (packetSize > -1)
        {
          std::string buf(buffer, packetSize);
          if (buf.find("200 OK") != std::string::npos)
          {
            CPlexClient newClient(buf, sender.Address());
            if (AddClient(newClient))
            {
              CLog::Log(LOGNOTICE, "CPlexServices: Server found via GDM %s", sender.Address());
            }
            else if (GetClient(newClient.m_uuid))
            {
              GetClient(newClient.m_uuid)->ParseData(buf, sender.Address());
              CLog::Log(LOGDEBUG, "CPlexServices: Server updated via GDM %s", sender.Address());
            }
          }
        }
      }
      usleep(250 * 1000);
    }

    if (socket)
      SAFE_DELETE(socket);
  }
}

bool CPlexServices::FetchPlexToken()
{
  bool rtn = false;
  XFILE::CCurlFile plex;
  CPlexUtils::GetDefaultHeaders(plex);

  CURL url("https://plex.tv/users/sign_in.json");
  url.SetUserName(m_myPlexUser);
  url.SetPassword(m_myPlexPass);

  std::string strResponse;
  std::string strPostData;
  if (plex.Post(url.Get(), strPostData, strResponse))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: myPlex %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    CVariant user = reply["user"];
    m_myPlexToken = user["authentication_token"].asString();
    rtn = true;
  }

  return rtn;
}

bool CPlexServices::FetchMyPlexServers()
{
  bool rtn = false;
  XFILE::CCurlFile plex;
  CPlexUtils::GetDefaultHeaders(plex);
  if (!m_myPlexToken.empty())
    plex.SetRequestHeader("X-Plex-Token", m_myPlexToken);

  std::string strResponse;
  CURL url("https://plex.tv/api/resources");
  if (plex.Get(url.Get(), strResponse))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: servers %s", strResponse.c_str());
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* DeviceNode = MediaContainer->FirstChildElement("Device");
      while (DeviceNode)
      {
        std::string provides = XMLUtils::GetAttribute(DeviceNode, "provides");
        if (provides == "server")
        {
          CPlexClient newClient(DeviceNode);

          if (AddClient(newClient))
          {
            CLog::Log(LOGNOTICE, "CPlexServices: Server found via plex.tv %s", newClient.GetServerName().c_str());
            rtn = true;
          }
          else if (GetClient(newClient.m_uuid))
          {
            //GetClient(newClient.m_uuid)->ParseData(data, host);
            CLog::Log(LOGDEBUG, "CPlexServices: Server updated via plex.tv %s", newClient.GetServerName().c_str());
            rtn = true;
          }
        }
        DeviceNode = DeviceNode->NextSiblingElement("Device");
      }
      
    }
  }

  return rtn;
}

void CPlexServices::SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket)
{
  SOCKETS::CAddress discoverAddress;
  discoverAddress.SetAddress(NS_BROADCAST_ADDR, NS_PLEX_MEDIA_SERVER_PORT);
  std::string discoverMessage = NS_SEARCH_MSG;
  int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
  if (packetSize < 0)
    CLog::Log(LOGERROR, "CPlexServices: discover send failed");
}

CPlexClient* CPlexServices::GetClient(std::string uuid)
{
  for (std::vector<CPlexClient>::iterator s_it = m_clients.begin(); s_it != m_clients.end(); ++s_it)
  {
    if (s_it->GetUuid() == uuid)
        return &(*s_it);
  }
  return nullptr;
}

bool CPlexServices::AddClient(CPlexClient client)
{
  // do not add existing clients
  for (std::vector<CPlexClient>::iterator s_it = m_clients.begin(); s_it != m_clients.end(); ++s_it)
  {
    if (s_it->GetUuid() == client.GetUuid())
    return false;
  }
  client.ParseSections();
  m_clients.push_back(client);

  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
  g_windowManager.SendThreadMessage(msg);

  return true;
}
