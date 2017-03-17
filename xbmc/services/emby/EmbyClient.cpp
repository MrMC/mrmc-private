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
#include "EmbyClientSync.h"
#include "EmbyUtils.h"

#include "Application.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "filesystem/StackDirectory.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Base64.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/Variant.h"
#include "video/VideoInfoTag.h"

#include <string>
#include <sstream>

CEmbyClient::CEmbyClient()
{
  m_local = true;
  m_owned = true;
  m_presence = true;
  m_protocol = "http";
  m_needUpdate = false;
  m_clientSync = nullptr;
}

CEmbyClient::~CEmbyClient()
{
  SAFE_DELETE(m_clientSync);
}

bool CEmbyClient::Init(const std::string &userId, const std::string &accessToken, const EmbyServerInfo &serverInfo)
{
  m_local = true;
  m_userId = userId;
  m_serverInfo = serverInfo;
  m_accessToken = accessToken;

  CURL url(m_serverInfo.LocalAddress);
  url.SetProtocolOptions("&X-MediaBrowser-Token=" + m_accessToken);
  m_url = url.Get();
  m_protocol = url.GetProtocol();

  m_clientSync = new CEmbyClientSync(this, m_serverInfo.ServerName,
    m_serverInfo.LocalAddress, CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID).c_str(), m_accessToken);
  m_clientSync->Start();

  return true;
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

const EmbyViewContentVector CEmbyClient::GetTvContent() const
{
  CSingleLock lock(m_viewTVshowContents_lock);
  return m_viewTVshowContents;
}

const EmbyViewContentVector CEmbyClient::GetMovieContent() const
{
  CSingleLock lock(m_viewMoviesContents_lock);
  return m_viewMoviesContents;
}

const EmbyViewContentVector CEmbyClient::GetArtistContent() const
{
  CSingleLock lock(m_viewArtistContents_lock);
  return m_viewArtistContents;
}

const EmbyViewContentVector CEmbyClient::GetPhotoContent() const
{
  CSingleLock lock(m_viewPhotosContents_lock);
  return m_viewPhotosContents;
}

const std::string CEmbyClient::FormatContentTitle(const std::string contentTitle) const
{
  std::string owned = (GetOwned() == "1") ? "O":"S";
  std::string title = StringUtils::Format("Emby(%s) - %s - %s %s",
              owned.c_str(), GetServerName().c_str(), contentTitle.c_str(), GetPresence()? "":"(off-line)");
  return title;
}

std::string CEmbyClient::FindViewName(const std::string &path)
{
  CURL real_url(path);
  if (real_url.GetProtocol() == "emby")
    real_url = CURL(Base64::Decode(URIUtils::GetFileName(real_url)));

  if (!real_url.GetFileName().empty())
  {
    {
      CSingleLock lock(m_viewMoviesContents_lock);
      for (const auto &contents : m_viewMoviesContents)
      {
        if (real_url.GetFileName().find(contents.viewprefix) != std::string::npos)
          return contents.name;
      }
    }
    {
      CSingleLock lock(m_viewTVshowContents_lock);
      for (const auto &contents : m_viewTVshowContents)
      {
        if (real_url.GetFileName().find(contents.viewprefix) != std::string::npos)
          return contents.name;
      }
    }
  }

  return "";
}

bool CEmbyClient::IsSameClientHostName(const CURL& url)
{
  CURL real_url(url);
  if (real_url.GetProtocol() == "emby")
    real_url = CURL(Base64::Decode(URIUtils::GetFileName(real_url)));

  if (URIUtils::IsStack(real_url.Get()))
    real_url = CURL(XFILE::CStackDirectory::GetFirstStackedFile(real_url.Get()));
  
  return GetHost() == real_url.GetHostName();
}

bool CEmbyClient::ParseViews(enum EmbyViewParsing parser)
{
  bool rtn = false;
  XFILE::CCurlFile emby;
  emby.SetTimeout(10);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  CEmbyUtils::PrepareApiCall(m_userId, m_accessToken, emby);

  CURL curl(m_url);
  // /Users/{UserId}/Views
  curl.SetFileName(curl.GetFileName() + "Users/" + m_userId + "/Views");
  std::string viewsUrl = curl.Get();
  std::string response;
  if (emby.Get(viewsUrl, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    if (parser == EmbyViewParsing::newView || parser == EmbyViewParsing::checkView)
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseViews %d, %s", parser, response.c_str());
#endif
    if (parser == EmbyViewParsing::updateView)
    {
      {
        CSingleLock lock(m_viewMoviesContents_lock);
        m_viewMoviesContents.clear();
      }
      {
        CSingleLock lock(m_viewTVshowContents_lock);
        m_viewTVshowContents.clear();
      }
      m_needUpdate = false;
    }

    auto resultObject = CJSONVariantParser::Parse(response);
    if (!resultObject.isObject() || !resultObject.isMember("Items"))
    {
      CLog::Log(LOGERROR, "CEmbyClient::ParseViews: invalid response for library views from %s", CURL::GetRedacted(viewsUrl).c_str());
      return false;
    }

    static const std::string PropertyViewId = "Id";
    static const std::string PropertyViewName = "Name";
    static const std::string PropertyViewETag = "Etag";
    static const std::string PropertyViewServerID = "ServerId";
    static const std::string PropertyViewCollectionType = "CollectionType";

    std::vector<EmbyViewContent> views;
    std::vector<const std::string> mediaTypes = {
    "movies",
  // musicvideos,
  // homevideos,
    "tvshows",
  // livetv,
  // channels,
    "music"
    };

    const auto& viewsObject = resultObject["Items"];
    for (auto viewIt = viewsObject.begin_array(); viewIt != viewsObject.end_array(); ++viewIt)
    {
      const auto view = *viewIt;
      if (!view.isObject() || !view.isMember(PropertyViewId) ||
          !view.isMember(PropertyViewName) || !view.isMember(PropertyViewCollectionType))
        continue;

      std::string type = view[PropertyViewCollectionType].asString();
      if (type.empty())
        continue;

      if (!std::any_of(mediaTypes.cbegin(), mediaTypes.cend(),
          [type](const MediaType& mediaType)
          {
            return MediaTypes::IsMediaType(type, mediaType);
          }))
        continue;

      EmbyViewContent libraryView = {
        view[PropertyViewId].asString(),
        view[PropertyViewName].asString(),
        view[PropertyViewETag].asString(),
        view[PropertyViewServerID].asString(),
        type,
        "Users/" + m_userId + "/Items?ParentId=" + view[PropertyViewId].asString()
      };
      if (libraryView.id.empty() || libraryView.name.empty())
        continue;

      views.push_back(libraryView);
    }

    for (const auto &content : views)
    {
      if (content.mediaType == "movies")
      {
        CSingleLock lock(m_viewMoviesContents_lock);
        if (parser == EmbyViewParsing::checkView)
        {
          for (const auto &contents : m_viewMoviesContents)
            m_needUpdate = NeedViewUpdate(content, contents, m_serverInfo.ServerName);
        }
        else
        {
          m_viewMoviesContents.push_back(content);
        }
      }
      else if (content.mediaType == "tvshows")
      {
        CSingleLock lock(m_viewTVshowContents_lock);
        if (parser == EmbyViewParsing::checkView)
        {
          for (const auto &contents : m_viewTVshowContents)
            m_needUpdate = NeedViewUpdate(content, contents, m_serverInfo.ServerName);
        }
        else
        {
          m_viewTVshowContents.push_back(content);
        }
      }
      else if (content.mediaType == "artist")
      {
        CSingleLock lock(m_viewArtistContents_lock);
        if (parser == EmbyViewParsing::checkView)
        {
          for (const auto &contents : m_viewArtistContents)
            m_needUpdate = NeedViewUpdate(content, contents, m_serverInfo.ServerName);
        }
        else
        {
          m_viewArtistContents.push_back(content);
        }
      }
      else if (content.mediaType == "photo")
      {
        CSingleLock lock(m_viewPhotosContents_lock);
        if (parser == EmbyViewParsing::checkView)
        {
          for (const auto &contents : m_viewPhotosContents)
            m_needUpdate = NeedViewUpdate(content, contents, m_serverInfo.ServerName);
        }
        else
        {
          m_viewPhotosContents.push_back(content);
        }
      }
      else
      {
        CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found unhandled content type %s",
          m_serverInfo.ServerName.c_str(), content.name.c_str());
      }
    }

    if (!views.empty())
    {
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d movies view",
        m_serverInfo.ServerName.c_str(), (int)m_viewMoviesContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d tvshows view",
        m_serverInfo.ServerName.c_str(), (int)m_viewTVshowContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d artist view",
        m_serverInfo.ServerName.c_str(), (int)m_viewArtistContents.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d photos view",
        m_serverInfo.ServerName.c_str(), (int)m_viewPhotosContents.size());
      rtn = true;
    }
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

bool CEmbyClient::NeedViewUpdate(const EmbyViewContent &content, const EmbyViewContent &contents, const std::string server)
{
  bool rtn = false;
  if (contents.id == content.id)
  {
    if (contents.etag != content.etag)
    {
#if defined(EMBY_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView need update on %s:%s",
        server.c_str(), content.name.c_str());
#endif
      rtn = true;
    }
  }
  return rtn;
}

CFileItemPtr CEmbyClient::FindItemByServiceId(const std::string &serviceId)
{
  for (const auto &item : m_viewItems)
  {
    if (item->GetVideoInfoTag()->m_strServiceId != serviceId)
      return item;
  }
  return nullptr;
}
