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

#include "PlexClient.h"
#include "PlexUtils.h"

#include "Application.h"
#include "URL.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "filesystem/CurlFile.h"
#include "network/Network.h"

#include <string>
#include <sstream>


static bool IsInSubNet(std::string address, std::string port)
{
  bool rtn = false;
  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  in_addr_t localMask = ntohl(inet_addr(iface->GetCurrentNetmask().c_str()));
  in_addr_t testAddress = ntohl(inet_addr(address.c_str()));
  in_addr_t localAddress = ntohl(inet_addr(iface->GetCurrentIPAddress().c_str()));

  in_addr_t temp1 = testAddress & localMask;
  in_addr_t temp2 = localAddress & localMask;
  if (temp1 == temp2)
  {
    // we are on the same subnet
    // now make sure it is a plex server
    std::string url = "http://" + address + ":" + port;
    rtn = CPlexUtils::GetIdentity(url);
  }
  return rtn;
}

CPlexClient::CPlexClient(std::string data, std::string ip)
{
  m_local = true;
  ParseData(data, ip);
}

CPlexClient::CPlexClient(const TiXmlElement* DeviceNode)
{
  m_local = false;
  m_uuid = XMLUtils::GetAttribute(DeviceNode, "clientIdentifier");
  m_serverName = XMLUtils::GetAttribute(DeviceNode, "name");
  m_authToken = XMLUtils::GetAttribute(DeviceNode, "accessToken");

  std::string port;
  std::string address;
  const TiXmlElement* ConnectionNode = DeviceNode->FirstChildElement("Connection");
  while (ConnectionNode)
  {
    port = XMLUtils::GetAttribute(ConnectionNode, "port");
    address = XMLUtils::GetAttribute(ConnectionNode, "address");
    m_scheme = XMLUtils::GetAttribute(ConnectionNode, "protocol");
    if (XMLUtils::GetAttribute(ConnectionNode, "local") == "1" && IsInSubNet(address, port))
      break;

    ConnectionNode = ConnectionNode->NextSiblingElement("Connection");
  }

  CURL url;
  url.SetHostName(address);
  url.SetPort(atoi(port.c_str()));
  url.SetProtocol(m_scheme);
  url.SetProtocolOptions("&X-Plex-Token=" + m_authToken);

  m_url = url.Get();
}

void CPlexClient::ParseData(std::string data, std::string ip)
{
  int port = 0;
  std::istringstream f(data);
  std::string s;
  while (std::getline(f, s))
  {
    int pos = s.find(':');
    if (pos > 0)
    {
      std::string substr = s.substr(0, pos);
      std::string name = StringUtils::Trim(substr);
      substr = s.substr(pos + 1);
      std::string val = StringUtils::Trim(substr);
      if (name == "Content-Type")
        m_contentType = val;
      else if (name == "Resource-Identifier")
        m_uuid = val;
      else if (name == "Name")
        m_serverName = val;
      else if (name == "Port")
        port = atoi(val.c_str());
    }
  }

  CURL url;
  url.SetHostName(ip);
  url.SetPort(port);
  url.SetProtocol("http");

  m_url = url.Get();
}

std::string CPlexClient::GetUrl()
{
  return m_url;
}

std::string CPlexClient::GetHost()
{
  CURL url(m_url);
  return url.GetHostName();
}

int CPlexClient::GetPort()
{
  CURL url(m_url);
  return url.GetPort();
}

void CPlexClient::ParseSections()
{
  XFILE::CCurlFile plex;

  CURL curl(m_url);
  curl.SetFileName(curl.GetFileName() + "library/sections");
  std::string strResponse;
  if (plex.Get(curl.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexClient::ParseSections() %s", strResponse.c_str());
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());
    
    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* DirectoryNode = MediaContainer->FirstChildElement("Directory");
      while (DirectoryNode)
      {
        SectionsContent content;
        content.path = XMLUtils::GetAttribute(DirectoryNode, "path");
        content.type = XMLUtils::GetAttribute(DirectoryNode, "type");
        content.title = XMLUtils::GetAttribute(DirectoryNode, "title");
        content.updatedAt = XMLUtils::GetAttribute(DirectoryNode, "updatedAt");
        std::string key = XMLUtils::GetAttribute(DirectoryNode, "key");
        content.section = "library/sections/" + key;
        std::string art = XMLUtils::GetAttribute(DirectoryNode, "art");
        if (m_local)
          content.art = art;
        else
          content.art = content.section + "/resources/" + URIUtils::GetFileName(art);
        if (content.type == "movie")
          m_movieSectionsContents.push_back(content);
        else
          m_showSectionsContents.push_back(content);
        DirectoryNode = DirectoryNode->NextSiblingElement("Directory");
      }
    }
  }
}

