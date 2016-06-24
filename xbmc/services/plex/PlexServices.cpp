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
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/CurlFile.h"
#include "guilib/LocalizeStrings.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include "utils/JSONVariantParser.h"
#include "utils/XMLUtils.h"

#include "PlexClient.h"
#include "PlexServer.h"

using namespace ANNOUNCEMENT;

#define NS_PLEX_MEDIA_SERVER_PORT 32414
#define NS_BROADCAST_ADDR "239.0.0.250"
#define NS_SEARCH_MSG "M-SEARCH * HTTP/1.1\r\n"

CPlexServices::CPlexServices()
: CThread("PlexServices")
, m_myPlexEnabled(false)
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

void CPlexServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
}

void CPlexServices::Start()
{
  CSingleLock lock(m_critical);
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXENABLE) && !IsRunning())
  {
    if (IsRunning())
      StopThread();
    CThread::Create();
  }
}

void CPlexServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
  {
    StopThread();
  }
}

bool CPlexServices::IsActive()
{
  return IsRunning();
}

void CPlexServices::OnSettingChanged(const CSetting *setting)
{
  // All Plex settings so far
  /*
   static const std::string SETTING_SERVICES_PLEXENABLE;
   static const std::string SETTING_SERVICES_PLEXMYPLEX;
   static const std::string SETTING_SERVICES_PLEXMYPLEXUSER;
   static const std::string SETTING_SERVICES_PLEXMYPLEXPASS;
   static const std::string SETTING_SERVICES_PLEXSERVER;
   static const std::string SETTING_SERVICES_PLEXSERVERPORT;
   static const std::string SETTING_SERVICES_PLEXSERVERHOST;
   */

  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXENABLE)
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
  m_client_uuid = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID);

  // myPlex on/off and user/pass below
  m_myPlexEnabled = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXMYPLEX);
  m_myPlexUser = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER);
  m_myPlexPass = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS);
  // end of Plex settings

  // 1 is auto, 2 is manual and port/host below
  int server = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_PLEXSERVER);
  std::string port = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSERVERPORT);
  std::string host = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSERVERHOST);
}

void CPlexServices::Process()
{
  ApplyUserSettings();

  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  if (iface)
  {
    if (m_myPlexEnabled)
    {
      FetchPlexToken();
      FetchMyPlexServers();
    }

    SOCKETS::CUDPSocket *socket = SOCKETS::CSocketFactory::CreateUDPSocket();
    if (!socket)
    {
      CLog::Log(LOGERROR, "CPlexServices: Could not create socket, aborting!");
      return;
    }

    SOCKETS::CAddress my_addr;
    my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
    if (!socket->Bind(my_addr, NS_PLEX_MEDIA_SERVER_PORT, 0))
    {
      CLog::Log(LOGERROR, "CPlexServices: Could not listen on port %d", NS_PLEX_MEDIA_SERVER_PORT);
      SAFE_DELETE(socket);
      return;
    }
    socket->SetBroadCast(true);

    SOCKETS::CSocketListener listener;
    // add our socket to the 'select' listener
    listener.AddSocket(socket);

    // do an initial broadcast to get things rolling
    SendDiscoverBroadcast(socket);

    CStopWatch idleTimer;
    idleTimer.StartZero();
    while (!m_bStop)
    {
      std::map<std::string, std::string> vBuffer;

      // send a discover broadcast every N seconds
      if (idleTimer.GetElapsedMilliseconds() > 5000)
      {
        SendDiscoverBroadcast(socket);
        idleTimer.Reset();
      }
      // start listening until we timeout
      if (listener.Listen(250))
      {
        char buffer[1024] = {0};
        SOCKETS::CAddress sender;
        int packetSize = socket->Read(sender, 1024, buffer);
        if (packetSize > -1)
        {
          std::string buf(buffer, packetSize);
          if (buf.find("200 OK") != std::string::npos)
            vBuffer[sender.Address()] = buf;
        }
      }

      for (std::map<std::string, std::string>::iterator it = vBuffer.begin(); it != vBuffer.end(); ++it)
      {
        std::string host = it->first;
        std::string data = it->second;

        PlexServer newServer(data, host);
/*
        // Set token for local servers
        if (Config::GetInstance().UsePlexAccount)
          newServer.SetAuthToken(Plexservice::GetMyPlexToken());
*/
        if (AddServer(newServer))
        {
          CLog::Log(LOGNOTICE, "CPlexServices: Server found via GDM %s", host.c_str());
          //CLog::Log(LOGNOTICE, "CPlexServices: New server found via GDM %s", data.c_str());
        }
        else if (GetServer(newServer.m_uuid))
        {
          GetServer(newServer.m_uuid)->ParseData(data, host);
          CLog::Log(LOGDEBUG, "CPlexServices: Server updated via GDM %s", host.c_str());
        }
      }

      usleep(50 * 1000);
    }

    if (socket)
      SAFE_DELETE(socket);
  }
}

void CPlexServices::FetchPlexToken()
{
  XFILE::CCurlFile plex;
  plex.SetRequestHeader("Content-Type", "application/xml; charset=utf-8");
  plex.SetRequestHeader("Content-Length", "0");
  plex.SetRequestHeader("X-Plex-Client-Identifier", m_client_uuid);
  plex.SetRequestHeader("X-Plex-Product", "MrMC");
  plex.SetRequestHeader("X-Plex-Version", "2.3.0");

  CURL url("https://plex.tv/users/sign_in.json");
  url.SetUserName(m_myPlexUser);
  url.SetPassword(m_myPlexPass);

  std::string strResponse;
  std::string strPostData;
  if (plex.Post(url.Get(), strPostData, strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexServices: myPlex %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    CVariant user = reply["user"];
    m_myPlexToken = user["authentication_token"].asString();
  }
}

void CPlexServices::FetchMyPlexServers()
{
  XFILE::CCurlFile plex;
  if (!m_myPlexToken.empty())
    plex.SetRequestHeader("X-Plex-Token", m_myPlexToken);

  std::string strResponse;
  CURL url("https://plex.tv/pms/servers");
  if (plex.Get(url.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexServices: servers %s", strResponse.c_str());
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* ServerNode = MediaContainer->FirstChildElement("Server");
      while (ServerNode)
      {
        ServerNode = ServerNode->NextSiblingElement("Server");
      }
    }
  }

  strResponse = "";
  url.Parse("https://plex.tv/pms/system/library/sections");
  if (plex.Get(url.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexServices: sections %s", strResponse.c_str());
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* DirectoryNode = MediaContainer->FirstChildElement("Directory");
      while (DirectoryNode)
      {
        DirectoryNode = DirectoryNode->NextSiblingElement("Directory");
      }
    }
  }
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

PlexServer* CPlexServices::GetServer(std::string uuid)
{
  for (std::vector<PlexServer>::iterator s_it = m_servers.begin(); s_it != m_servers.end(); ++s_it)
  {
    if (s_it->GetUuid() == uuid)
        return &(*s_it);
  }
  return nullptr;
}

bool CPlexServices::AddServer(PlexServer server)
{
  // do not add existing servers
  for (std::vector<PlexServer>::iterator s_it = m_servers.begin(); s_it != m_servers.end(); ++s_it)
  {
    if (s_it->GetUuid() == server.GetUuid())
    return false;
  }

  m_servers.push_back(server);
  return true;
}
