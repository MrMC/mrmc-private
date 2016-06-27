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

#include "URL.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "filesystem/CurlFile.h"

#include <string>
#include <sstream>


CPlexClient::CPlexClient(std::string data, std::string ip)
{
  m_local = true;
  ParseData(data, ip);
}

CPlexClient::CPlexClient(const TiXmlElement* ServerNode)
{
  m_local = false;
  m_uuid = XMLUtils::GetAttribute(ServerNode, "machineIdentifier");
  m_serverName = XMLUtils::GetAttribute(ServerNode, "name");
  m_updated = atol(XMLUtils::GetAttribute(ServerNode, "updatedAt").c_str());
  m_version = XMLUtils::GetAttribute(ServerNode, "updatedAt");
  m_authToken = XMLUtils::GetAttribute(ServerNode, "accessToken");
  m_scheme = XMLUtils::GetAttribute(ServerNode, "scheme");
  if (m_scheme.empty())
    m_scheme = "http";

  CURL url;
  url.SetHostName(XMLUtils::GetAttribute(ServerNode, "address"));
  int port = atoi(XMLUtils::GetAttribute(ServerNode, "port").c_str());
  url.SetPort(port);
  url.SetProtocol(m_scheme);
  url.SetProtocolOptions("&X-Plex-Token=" + m_authToken);

  m_url = url.Get();
  GetIdentity();
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
      else if (name == "Updated-At")
        m_updated = atol(val.c_str());
      else if (name == "Version")
        m_version = val;
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

void CPlexClient::GetIdentity()
{
  XFILE::CCurlFile plex;
  CURL curl(m_url);
  curl.SetFileName(curl.GetFileName() + "identity");
  std::string strResponse;
  if (plex.Get(curl.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexClient::GetIdentity() %s", strResponse.c_str());
  }
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
        content.type = XMLUtils::GetAttribute(DirectoryNode, "type");
        content.title = XMLUtils::GetAttribute(DirectoryNode, "title");
        content.path = XMLUtils::GetAttribute(DirectoryNode, "path");
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

