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

#include "PlexDiscovery.h"
#include "PlexServer.h"

#include "Application.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/JSONVariantParser.h"
#include "utils/XMLUtils.h"


CPlexDiscovery::CPlexDiscovery()
: CThread("PlexDiscovery")
{
  m_clientIdentifier = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID);
  m_clientProduct = "MrMC";
  m_clientVersion = "2.3.0";

  m_myPlexEnabled = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXMYPLEX);
  m_myPlexUser = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER);
  m_myPlexPass = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS);
}

CPlexDiscovery::~CPlexDiscovery()
{
  if (IsRunning())
    Stop();
}

void CPlexDiscovery::Start()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    StopThread();
  CThread::Create();
}

void CPlexDiscovery::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    StopThread();
}

bool CPlexDiscovery::IsActive()
{
  return IsRunning();
}

void CPlexDiscovery::Process()
{

  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  if (iface)
  {
    if (m_myPlexEnabled)
    {
      FetchPlexToken();
      FetchMyPlexServers();
    }

    SOCKETS::CAddress my_addr;
    my_addr.SetAddress(iface->GetCurrentIPAddress().c_str(), 32414);

    SOCKETS::CUDPSocket *socket = SOCKETS::CSocketFactory::CreateUDPSocket();
    if (!socket)
    {
      CLog::Log(LOGERROR, "CPlexDiscovery: Could not create socket, aborting!");
      return;
    }

    if (!socket->Bind(my_addr, 32414, 0))
    {
      CLog::Log(LOGERROR, "CPlexDiscovery: Could not listen on port %d", 32414);
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

      try
      {
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
      }
      catch (...)
      {
        CLog::Log(LOGERROR, "CPlexDiscovery: Exception caught while listening for socket");
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
          CLog::Log(LOGNOTICE, "CPlexDiscovery: Server found via GDM %s", host.c_str());
          //CLog::Log(LOGNOTICE, "CPlexDiscovery: New server found via GDM %s", data.c_str());
        }
        else if (GetServer(newServer.m_uuid))
        {
          GetServer(newServer.m_uuid)->ParseData(data, host);
          CLog::Log(LOGDEBUG, "CPlexDiscovery: Server updated via GDM %s", host.c_str());
        }
      }

      usleep(50 * 1000);
    }
  }
}

void CPlexDiscovery::FetchPlexToken()
{
  XFILE::CCurlFile plex;
  plex.SetRequestHeader("Content-Type", "application/xml; charset=utf-8");
  plex.SetRequestHeader("Content-Length", "0");
  plex.SetRequestHeader("X-Plex-Client-Identifier", "8334-8A72-4C28-FDAF-29AB-479E-4069-C3A3");
  //plex.SetRequestHeader("X-Plex-Client-Identifier", m_clientIdentifier);
  plex.SetRequestHeader("X-Plex-Product", m_clientProduct);
  plex.SetRequestHeader("X-Plex-Version", m_clientVersion);

  CURL url("https://plex.tv/users/sign_in.json");
  url.SetUserName(m_myPlexUser);
  url.SetPassword(m_myPlexPass);

  std::string strResponse;
  std::string strPostData;
  if (plex.Post(url.Get(), strPostData, strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexDiscovery: myPlex %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    CVariant user = reply["user"];
    m_clientToken = user["authentication_token"].asString();
  }
}

void CPlexDiscovery::FetchMyPlexServers()
{
  XFILE::CCurlFile plex;
  if (!m_clientToken.empty())
    plex.SetRequestHeader("X-Plex-Token", m_clientToken);

  std::string strResponse;
  CURL url("https://plex.tv/pms/servers");
  if (plex.Get(url.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexDiscovery: servers %s", strResponse.c_str());
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
    CLog::Log(LOGDEBUG, "CPlexDiscovery: sections %s", strResponse.c_str());
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

void CPlexDiscovery::SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket)
{
  SOCKETS::CAddress discoverAddress;
  discoverAddress.SetAddress("239.0.0.250", 32414);
  std::string discoverMessage = "M-SEARCH * HTTP/1.1\r\n\r\n";
  int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
  if (packetSize < 0)
    CLog::Log(LOGERROR, "CPlexDiscovery: discover send failed");
}

PlexServer* CPlexDiscovery::GetServer(std::string uuid)
{
  for (std::vector<PlexServer>::iterator s_it = m_servers.begin(); s_it != m_servers.end(); ++s_it)
  {
    if (s_it->GetUuid() == uuid)
        return &(*s_it);
  }
  return nullptr;
}

bool CPlexDiscovery::AddServer(PlexServer server)
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
