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

#include <atomic>
#include <memory>
#include <algorithm>

#include "EmbyClient.h"
#include "EmbyUtils.h"

#include "Application.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "filesystem/StackDirectory.h"
#include "network/Network.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Base64.h"

#include <string>
#include <sstream>

CEmbyClient::CEmbyClient()
{
  m_local = true;
  m_owned = true;
  m_presence = true;
  m_protocol = "http";
  m_needUpdate = false;
}

CEmbyClient::~CEmbyClient()
{
}

bool CEmbyClient::Init(const CVariant &data, std::string ip)
{
  static const std::string ServerPropertyId = "Id";
  static const std::string ServerPropertyName = "Name";
  static const std::string ServerPropertyAddress = "Address";

  if (!data.isObject() ||
      !data.isMember(ServerPropertyId) ||
      !data.isMember(ServerPropertyName) ||
      !data.isMember(ServerPropertyAddress))
    return false;

  std::string id = data[ServerPropertyId].asString();
  std::string name = data[ServerPropertyName].asString();
  std::string address = data[ServerPropertyAddress].asString();
  if (id.empty() || name.empty() || address.empty())
    return false;

  CURL url(address);

  m_url = url.Get();
  m_protocol = url.GetProtocol();
  m_uuid = id;
  m_serverName = name;

  return !m_url.empty();
}

bool CEmbyClient::Init(const TiXmlElement* DeviceNode)
{
  m_url = "";
  m_presence = XMLUtils::GetAttribute(DeviceNode, "presence") == "1";
  //if (!m_presence)
  //  return false;

  m_uuid = XMLUtils::GetAttribute(DeviceNode, "clientIdentifier");
  m_owned = XMLUtils::GetAttribute(DeviceNode, "owned");
  m_serverName = XMLUtils::GetAttribute(DeviceNode, "name");
  m_accessToken = XMLUtils::GetAttribute(DeviceNode, "accessToken");
  m_httpsRequired = XMLUtils::GetAttribute(DeviceNode, "httpsRequired");
  m_platform = XMLUtils::GetAttribute(DeviceNode, "platform");

  std::vector<EmbyConnection> connections;
  const TiXmlElement* ConnectionNode = DeviceNode->FirstChildElement("Connection");
  while (ConnectionNode)
  {
    EmbyConnection connection;
    connection.port = XMLUtils::GetAttribute(ConnectionNode, "port");
    connection.address = XMLUtils::GetAttribute(ConnectionNode, "address");
    connection.protocol = XMLUtils::GetAttribute(ConnectionNode, "protocol");
    connection.external = XMLUtils::GetAttribute(ConnectionNode, "local") == "0" ? 1 : 0;
    connections.push_back(connection);

    ConnectionNode = ConnectionNode->NextSiblingElement("Connection");
  }

  CURL url;
  if (!connections.empty())
  {
    // sort so that all external=0 are first. These are the local connections.
    std::sort(connections.begin(), connections.end(),
      [] (EmbyConnection const& a, EmbyConnection const& b) { return a.external < b.external; });

    for (const auto &connection : connections)
    {
      url.SetHostName(connection.address);
      url.SetPort(atoi(connection.port.c_str()));
      url.SetProtocol(connection.protocol);
      url.SetProtocolOptions("&X-MediaBrowser-Token=" + m_accessToken);
      int timeout = connection.external ? 5 : 1;
      //if (CEmbyUtils::GetIdentity(url, timeout))
      {
        CLog::Log(LOGDEBUG, "CEmbyClient::Init "
          "serverName(%s), ipAddress(%s), protocol(%s)",
          m_serverName.c_str(), connection.address.c_str(), connection.protocol.c_str());

        m_url = url.Get();
        m_protocol = url.GetProtocol();
        m_local = (connection.external == 0);
        break;
      }
    }
  }

  return !m_url.empty();
}

std::string CEmbyClient::GetUrl()
{
  return m_url;
}

std::string CEmbyClient::GetHost()
{
  CURL url(m_url);
  return url.GetHostName();
}

int CEmbyClient::GetPort()
{
  CURL url(m_url);
  return url.GetPort();
}

const EmbySectionsContentVector CEmbyClient::GetTvContent() const
{
  CSingleLock lock(m_criticalTVShow);
  return m_showSectionsContents;
}

const EmbySectionsContentVector CEmbyClient::GetMovieContent() const
{
  CSingleLock lock(m_criticalMovies);
  return m_movieSectionsContents;
}

const EmbySectionsContentVector CEmbyClient::GetArtistContent() const
{
  CSingleLock lock(m_criticalArtist);
  return m_artistSectionsContents;
}

const EmbySectionsContentVector CEmbyClient::GetPhotoContent() const
{
  CSingleLock lock(m_criticalPhoto);
  return m_photoSectionsContents;
}

const std::string CEmbyClient::FormatContentTitle(const std::string contentTitle) const
{
  std::string owned = (GetOwned() == "1") ? "O":"S";
  std::string title = StringUtils::Format("Emby(%s) - %s - %s %s",
              owned.c_str(), GetServerName().c_str(), contentTitle.c_str(), GetPresence()? "":"(off-line)");
  return title;
}

std::string CEmbyClient::FindSectionTitle(const std::string &path)
{
  CURL real_url(path);
  if (real_url.GetProtocol() == "plex")
    real_url = CURL(Base64::Decode(URIUtils::GetFileName(real_url)));

  if (!real_url.GetFileName().empty())
  {
    {
      CSingleLock lock(m_criticalMovies);
      for (const auto &contents : m_movieSectionsContents)
      {
        if (real_url.GetFileName().find(contents.section) != std::string::npos)
          return contents.title;
      }
    }
    {
      CSingleLock lock(m_criticalTVShow);
      for (const auto &contents : m_showSectionsContents)
      {
        if (real_url.GetFileName().find(contents.section) != std::string::npos)
          return contents.title;
      }
    }
  }

  return "";
}

bool CEmbyClient::IsSameClientHostName(const CURL& url)
{
  CURL real_url(url);
  if (real_url.GetProtocol() == "plex")
    real_url = CURL(Base64::Decode(URIUtils::GetFileName(real_url)));

  if (URIUtils::IsStack(real_url.Get()))
    real_url = CURL(XFILE::CStackDirectory::GetFirstStackedFile(real_url.Get()));
  
  return GetHost() == real_url.GetHostName();
}

std::string CEmbyClient::LookUpUuid(const std::string path) const
{
  std::string uuid;

  CURL url(path);
  {
    CSingleLock lock(m_criticalMovies);
    for (const auto &contents : m_movieSectionsContents)
    {
      if (contents.section == url.GetFileName())
        return m_uuid;
    }
  }
  {
    CSingleLock lock(m_criticalTVShow);
    for (const auto &contents : m_showSectionsContents)
    {
      if (contents.section == url.GetFileName())
        return m_uuid;
    }
  }

  return uuid;
}

bool CEmbyClient::ParseSections(enum EmbySectionParsing parser)
{
  bool rtn = false;
  XFILE::CCurlFile plex;
  //plex.SetBufferSize(32768*10);
  plex.SetTimeout(10);

  CURL curl(m_url);
  curl.SetFileName(curl.GetFileName() + "library/sections");
  std::string strResponse;
  if (plex.Get(curl.Get(), strResponse))
  {
#if defined(PLEX_DEBUG_VERBOSE)
    if (parser == EmbySectionParsing::newSection || parser == EmbySectionParsing::checkSection)
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %d, %s", parser, strResponse.c_str());
#endif
    if (parser == EmbySectionParsing::updateSection)
    {
      {
        CSingleLock lock(m_criticalMovies);
        m_movieSectionsContents.clear();
      }
      {
        CSingleLock lock(m_criticalTVShow);
        m_showSectionsContents.clear();
      }
      m_needUpdate = false;
    }

    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* DirectoryNode = MediaContainer->FirstChildElement("Directory");
      while (DirectoryNode)
      {
        EmbySectionsContent content;
        content.uuid = XMLUtils::GetAttribute(DirectoryNode, "uuid");
        content.path = XMLUtils::GetAttribute(DirectoryNode, "path");
        content.type = XMLUtils::GetAttribute(DirectoryNode, "type");
        content.title = XMLUtils::GetAttribute(DirectoryNode, "title");
        content.updatedAt = XMLUtils::GetAttribute(DirectoryNode, "updatedAt");
        std::string key = XMLUtils::GetAttribute(DirectoryNode, "key");
        content.section = "library/sections/" + key;
        content.thumb = XMLUtils::GetAttribute(DirectoryNode, "composite");
        std::string art = XMLUtils::GetAttribute(DirectoryNode, "art");
        if (m_local)
          content.art = art;
        else
          content.art = content.section + "/resources/" + URIUtils::GetFileName(art);
        if (content.type == "movie")
        {
          if (parser == EmbySectionParsing::checkSection)
          {
            CSingleLock lock(m_criticalMovies);
            for (const auto &contents : m_movieSectionsContents)
            {
              if (contents.uuid == content.uuid)
              {
                if (contents.updatedAt != content.updatedAt)
                {
#if defined(PLEX_DEBUG_VERBOSE)
                  CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections need update on %s:%s",
                    m_serverName.c_str(), content.title.c_str());
#endif
                  m_needUpdate = true;
                }
              }
            }
          }
          else
          {
            CSingleLock lock(m_criticalMovies);
            m_movieSectionsContents.push_back(content);
          }
        }
        else if (content.type == "show")
        {
          if (parser == EmbySectionParsing::checkSection)
          {
            CSingleLock lock(m_criticalTVShow);
            for (const auto &contents : m_showSectionsContents)
            {
              if (contents.uuid == content.uuid)
              {
                if (contents.updatedAt != content.updatedAt)
                {
#if defined(PLEX_DEBUG_VERBOSE)
                  CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections need update on %s:%s",
                    m_serverName.c_str(), content.title.c_str());
#endif
                  m_needUpdate = true;
                }
              }
            }
          }
          else
          {
            CSingleLock lock(m_criticalTVShow);
            m_showSectionsContents.push_back(content);
          }
        }
        else if (content.type == "artist")
        {
          if (parser == EmbySectionParsing::checkSection)
          {
            CSingleLock lock(m_criticalArtist);
            for (const auto &contents : m_artistSectionsContents)
            {
              if (contents.uuid == content.uuid)
              {
                if (contents.updatedAt != content.updatedAt)
                {
#if defined(PLEX_DEBUG_VERBOSE)
                  CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections need update on %s:%s",
                            m_serverName.c_str(), content.title.c_str());
#endif
                  m_needUpdate = true;
                }
              }
            }
          }
          else
          {
            CSingleLock lock(m_criticalArtist);
            m_artistSectionsContents.push_back(content);
          }
        }
        else if (content.type == "photo")
        {
          if (parser == EmbySectionParsing::checkSection)
          {
            CSingleLock lock(m_criticalPhoto);
            for (const auto &contents : m_photoSectionsContents)
            {
              if (contents.uuid == content.uuid)
              {
                if (contents.updatedAt != content.updatedAt)
                {
#if defined(PLEX_DEBUG_VERBOSE)
                  CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections need update on %s:%s",
                            m_serverName.c_str(), content.title.c_str());
#endif
                  m_needUpdate = true;
                }
              }
            }
          }
          else
          {
            CSingleLock lock(m_criticalPhoto);
            m_photoSectionsContents.push_back(content);
          }
        }
        else
        {
          CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %s found unhandled content type %s",
            m_serverName.c_str(), content.type.c_str());
        }
        DirectoryNode = DirectoryNode->NextSiblingElement("Directory");
      }

      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %s found %d movie sections",
        m_serverName.c_str(), (int)m_movieSectionsContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %s found %d shows sections",
        m_serverName.c_str(), (int)m_showSectionsContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %s found %d artist sections",
                m_serverName.c_str(), (int)m_artistSectionsContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections %s found %d photo sections",
                m_serverName.c_str(), (int)m_photoSectionsContents.size());

      rtn = true;
    }
    else
    {
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections no MediaContainer found");
    }
  }
  else
  {
    // 401's are attempts to access a local server that is also in PMS
    // and these require an access token. Only local servers that are
    // not is PMS can be accessed via GDM.
    if (plex.GetResponseCode() != 401)
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseSections failed %s", strResponse.c_str());
    rtn = false;
  }

  return rtn;
}

void CEmbyClient::SetPresence(bool presence)
{
  if (m_presence != presence)
  {
    m_presence = presence;
  }
}
