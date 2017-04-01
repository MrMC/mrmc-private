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
#include "EmbyViewCache.h"
#include "EmbyServices.h"
#include "EmbyUtils.h"

#include "Application.h"
#include "URL.h"
#include "GUIUserMessages.h"
#include "TextureCache.h"

#include "filesystem/CurlFile.h"
#include "filesystem/StackDirectory.h"
#include "guilib/GUIWindowManager.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Base64.h"
#include "utils/JobManager.h"
#include "utils/JSONVariantParser.h"
#include "utils/Variant.h"
#include "video/VideoInfoTag.h"
#include "music/tags/MusicInfoTag.h"

#include <string>

static const std::string MoviesFields = {
  "DateCreated,Genres,MediaStreams,Overview,Path"
};

static const std::string TVShowsFields = {
  "DateCreated,Genres,MediaStreams,Overview,ShortOverview,Path,RecursiveItemCount"
};

class CEmbyUtilsJob: public CJob
{
public:
  CEmbyUtilsJob(CEmbyClientPtr client, std::vector<std::string> itemIDs)
  :m_client(client),
  m_itemIDs(itemIDs)
  {
  }
  virtual ~CEmbyUtilsJob()
  {
  }
  virtual bool DoWork()
  {
    if (m_client && m_itemIDs.size() > 0)
      m_client->UpdateViewItems(m_itemIDs);
    return true;
  }
private:
  CEmbyClientPtr m_client;
  std::vector<std::string> m_itemIDs;
};

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

bool CEmbyClient::Init(const EmbyServerInfo &serverInfo)
{
  m_local = true;
  m_serverInfo = serverInfo;
  m_owned = serverInfo.UserType == "Linked";

  // protocol (http/https) and port will be in ServerUrl
  CURL curl(m_serverInfo.ServerURL);
  curl.SetProtocolOptions("&X-MediaBrowser-Token=" + serverInfo.AccessToken);
  m_url = curl.Get();
  m_protocol = curl.GetProtocol();

  if (m_clientSync)
    SAFE_DELETE(m_clientSync);

  m_clientSync = new CEmbyClientSync(m_serverInfo.ServerName, m_serverInfo.ServerURL,
    CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID).c_str(), serverInfo.AccessToken);
  m_clientSync->Start();

  return true;
}

void CEmbyClient::SetWatched(CFileItem &item)
{
  std::string itemId = item.GetMediaServiceId();
  std::string content = item.GetProperty("MediaServicesContent").asString();
  CDateTime lastPlayed;
  if (item.IsVideo())
    lastPlayed = item.GetVideoInfoTag()->m_lastPlayed;
  else if (item.IsAudio())
    lastPlayed = item.GetMusicInfoTag()->m_lastPlayed;
  else
    lastPlayed = CDateTime::GetUTCDateTime();

  if (content == "movies")
  {
    for (auto &view : m_viewMovies)
    {
      bool hit = view->SetWatched(itemId,
        item.GetVideoInfoTag()->m_playCount, item.GetVideoInfoTag()->m_resumePoint.timeInSeconds);
      if (hit)
        break;
    }
  }
  else if (content == "tvshows")
  {
    for (auto &view : m_viewTVShows)
    {
      bool hit = view->SetWatched(itemId,
        item.GetVideoInfoTag()->m_playCount, item.GetVideoInfoTag()->m_resumePoint.timeInSeconds);
      if (hit)
        break;
    }
  }

  // POST to /Users/{UserId}/PlayedItems/{Id}
  CURL curl(m_url);
  curl.SetFileName("emby/Users/" + GetUserID() + "/PlayedItems/" + itemId);
  curl.SetOptions("");
  // and add the DatePlayed URL parameter
  curl.SetOption("DatePlayed",
    StringUtils::Format("%04i%02i%02i%02i%02i%02i",
      lastPlayed.GetYear(),
      lastPlayed.GetMonth(),
      lastPlayed.GetDay(),
      lastPlayed.GetHour(),
      lastPlayed.GetMinute(),
      lastPlayed.GetSecond()));

  std::string data;
  std::string response;
  // execute the POST request
  XFILE::CCurlFile curlfile;
  if (curlfile.Post(curl.Get(), data, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    if (!response.empty())
      CLog::Log(LOGDEBUG, "CEmbyClient::SetWatched %s", response.c_str());
#endif
  }
}

void CEmbyClient::SetUnWatched(CFileItem &item)
{
  std::string itemId = item.GetMediaServiceId();
  std::string content = item.GetProperty("MediaServicesContent").asString();
  if (content == "movies")
  {
    for (auto &view : m_viewMovies)
    {
      bool hit = view->SetUnWatched(itemId);
      if (hit)
        break;
    }
  }
  else if (content == "tvshows")
  {
    for (auto &view : m_viewTVShows)
    {
      bool hit = view->SetUnWatched(itemId);
      if (hit)
        break;
    }
  }
  else if (content == "song")
  {
  }

  // DELETE to /Users/{UserId}/PlayedItems/{Id}
  CURL curl(m_url);
  curl.SetFileName("emby/Users/" + GetUserID() + "/PlayedItems/" + itemId);
  curl.SetOptions("");

  std::string data;
  std::string response;
  // execute the DELETE request
  XFILE::CCurlFile curlfile;
  if (curlfile.Delete(curl.Get(), data, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    if (!response.empty())
      CLog::Log(LOGDEBUG, "CEmbyUtils::SetUnWatched %s", response.c_str());
#endif
  }
}

bool CEmbyClient::GetMovies(CFileItemList &items, std::string url)
{
  //TODO: fix this for multiple view contents
  bool rtn = false;
  CURL curl(url);
  for (auto &view : m_viewMovies)
  {
    if (view->GetItems().isNull())
      FetchViewItems(view, EmbyTypeMovie);

    rtn = CEmbyUtils::ParseEmbyVideos(items, curl, view->GetItems(), MediaTypeMovie);
    if (rtn)
      break;
  }
  return rtn;
}

bool CEmbyClient::GetTVShows(CFileItemList &items, std::string url)
{
  //TODO: fix this for multiple view contents
  bool rtn = false;
  CURL curl(url);
  for (auto &view : m_viewTVShows)
  {
    if (view->GetItems().isNull())
      FetchViewItems(view, EmbyTypeSeries);

    rtn = CEmbyUtils::ParseEmbySeries(items, curl, view->GetItems());
    if (rtn)
      break;
  }
  return rtn;
}

bool CEmbyClient::GetMusicArtists(CFileItemList &items, std::string url)
{
  //TODO: fix this for multiple view contents
  bool rtn = false;
  CURL curl(url);
  for (auto &view : m_viewMusic)
  {
    if (view->GetItems().isNull())
      FetchViewItems(view, EmbyTypeMusicArtist);

    rtn = CEmbyUtils::ParseEmbyArtists(items, curl, view->GetItems());
    if (rtn)
      break;
  }
  return rtn;
}

void CEmbyClient::AddNewViewItems(const std::vector<std::string> &ids)
{
  CLog::Log(LOGDEBUG, "CEmbyClient::AddNewViewItem");
  const CVariant variant = FetchItemByIds(ids);
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyClient::AddNewViewItems invalid response");
    return;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    AppendItemToCache(*variantItemIt);

    std::map<std::string, CVariant> variantMap;
    variantMap["Items"].push_back(*variantItemIt);

    CFileItemPtr item = CEmbyUtils::ToFileItemPtr(this, variantMap);
    if (item != nullptr)
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_ADD_ITEM, 1, item);
      g_windowManager.SendThreadMessage(msg);
    }
  }
}

void CEmbyClient::UpdateViewItems(const std::vector<std::string> &ids)
{
  CLog::Log(LOGDEBUG, "CEmbyClient::UpdateViewItems");
  const CVariant variant = FetchItemByIds(ids);
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyClient::UpdateViewItems invalid response");
    return;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    UpdateItemInCache(*variantItemIt);

    std::map<std::string, CVariant> variantMap;
    variantMap["Items"].push_back(*variantItemIt);

    CFileItemPtr item = CEmbyUtils::ToFileItemPtr(this, variantMap);
    if (item != nullptr)
    {
      // hack, if season state has changed, maybe show state has changed as well. seems like emby bug
      if (item->GetVideoInfoTag()->m_type == MediaTypeSeason &&  item->HasProperty("EmbySeriesID"))
      {
        std::vector<std::string> seriesIds;
        seriesIds.push_back(item->GetProperty("EmbySeriesID").asString());
        UpdateViewItems(seriesIds);
      }

      bool needArtRefresh = false;
      std::string thumb = item->GetArt("thumb");
      if (!thumb.empty() && CTextureCache::GetInstance().HasCachedImage(thumb))
      {
        needArtRefresh = true;
        CTextureCache::GetInstance().ClearCachedImage(thumb);
      }

      std::string banner = item->GetArt("banner");
      if (!banner.empty() && CTextureCache::GetInstance().HasCachedImage(banner))
      {
        needArtRefresh = true;
        CTextureCache::GetInstance().ClearCachedImage(banner);
      }

      std::string fanart = item->GetArt("fanart");
      if (!fanart.empty() && CTextureCache::GetInstance().HasCachedImage(fanart))
      {
        needArtRefresh = true;
        CTextureCache::GetInstance().ClearCachedImage(fanart);
      }
      // -------------
      if (needArtRefresh)
      {
        CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REFRESH_THUMBS);
        g_windowManager.SendThreadMessage(msg);
      }
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 1, item);
      g_windowManager.SendThreadMessage(msg);
    }
  }
}

void CEmbyClient::RemoveViewItems(const std::vector<std::string> &ids)
{
  CLog::Log(LOGDEBUG, "CEmbyClient::RemoveViewItems");
  const CVariant variant = FetchItemByIds(ids);
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyClient::RemoveViewItems invalid response");
    return;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    RemoveItemFromCache(*variantItemIt);

    std::map<std::string, CVariant> variantMap;
    variantMap["Items"].push_back(*variantItemIt);

    CFileItemPtr item = CEmbyUtils::ToFileItemPtr(this, variantMap);
    if (item != nullptr)
    {
      // -------------
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REMOVE_ITEM, 1, item);
      g_windowManager.SendThreadMessage(msg);
    }
  }
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

std::string CEmbyClient::GetUserID()
{
  return m_serverInfo.UserId;
}

const std::vector<EmbyViewInfo> CEmbyClient::GetViewInfoForMovieContent() const
{
  std::vector<EmbyViewInfo> infos;
  for (auto &view : m_viewMovies)
    infos.push_back(view->GetInfo());
  return infos;
}

const std::vector<EmbyViewInfo> CEmbyClient::GetViewInfoForMusicContent() const
{
  std::vector<EmbyViewInfo> infos;
  for (auto &view : m_viewMusic)
    infos.push_back(view->GetInfo());
  return infos;
}

const std::vector<EmbyViewInfo> CEmbyClient::GetViewInfoForPhotoContent() const
{
  std::vector<EmbyViewInfo> infos;
  for (auto &view : m_viewPhotos)
    infos.push_back(view->GetInfo());
  return infos;
}

const std::vector<EmbyViewInfo> CEmbyClient::GetViewInfoForTVShowContent() const
{
  std::vector<EmbyViewInfo> infos;
  for (auto &view : m_viewTVShows)
    infos.push_back(view->GetInfo());
  return infos;
}

const std::string CEmbyClient::FormatContentTitle(const std::string contentTitle) const
{
  std::string owned = IsOwned() ? "O":"S";
  std::string title = StringUtils::Format("Emby(%s) - %s - %s %s",
              owned.c_str(), GetServerName().c_str(), contentTitle.c_str(), GetPresence()? "":"(off-line)");
  return title;
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

bool CEmbyClient::FetchViews()
{
  CLog::Log(LOGDEBUG, "CEmbyClient::FetchViews");
  bool rtn = false;
  XFILE::CCurlFile emby;
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  CEmbyUtils::PrepareApiCall(m_serverInfo.UserId, m_serverInfo.AccessToken, emby);

  CURL curl(m_url);
  // /Users/{UserId}/Views
  curl.SetFileName(curl.GetFileName() + "Users/" + m_serverInfo.UserId + "/Views");
  std::string path = curl.Get();
  std::string response;
  if (emby.Get(path, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CEmbyClient::FetchViews %s", response.c_str());
#endif

    CVariant resultObject;
    if (!CJSONVariantParser::Parse(response, resultObject) ||
        !resultObject.isObject() || !resultObject.isMember("Items"))
    {
      CLog::Log(LOGERROR, "CEmbyClient::FetchViews: invalid response for library views from %s", CURL::GetRedacted(path).c_str());
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

      EmbyViewContent libraryView;
      libraryView.id = view[PropertyViewId].asString();
      libraryView.name = view[PropertyViewName].asString();
      libraryView.etag = view[PropertyViewETag].asString();
      libraryView.prefix = "Users/" + m_serverInfo.UserId + "/Items?ParentId=" + view[PropertyViewId].asString();
      libraryView.serverId = view[PropertyViewServerID].asString();
      libraryView.iconId = view["ImageTags"]["Primary"].asString();
      libraryView.mediaType = type;
      if (libraryView.id.empty() || libraryView.name.empty())
        continue;

      views.push_back(libraryView);
    }

    for (const auto &content : views)
    {
      if (content.mediaType == "movies")
      {
        CEmbyViewCache *viewCache = new CEmbyViewCache();
        viewCache->Init(content);
        m_viewMovies.push_back(viewCache);
      }
      else if (content.mediaType == "tvshows")
      {
        CEmbyViewCache *viewCache = new CEmbyViewCache();
        viewCache->Init(content);
        m_viewTVShows.push_back(viewCache);
      }
      else if (content.mediaType == "music")
      {
        CEmbyViewCache *viewCache = new CEmbyViewCache();
        viewCache->Init(content);
        m_viewMusic.push_back(viewCache);
      }
      else if (content.mediaType == "photo")
      {
        CEmbyViewCache *viewCache = new CEmbyViewCache();
        viewCache->Init(content);
        m_viewPhotos.push_back(viewCache);
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
        m_serverInfo.ServerName.c_str(), (int)m_viewMovies.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d tvshows view",
        m_serverInfo.ServerName.c_str(), (int)m_viewTVShows.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d Music view",
        m_serverInfo.ServerName.c_str(), (int)m_viewMusic.size());
      CLog::Log(LOGDEBUG, "CEmbyClient::ParseView %s found %d photos view",
        m_serverInfo.ServerName.c_str(), (int)m_viewPhotos.size());
      rtn = true;
    }
  }

  return rtn;
}

bool CEmbyClient::FetchViewItems(CEmbyViewCache *view, const std::string &type)
{
  CLog::Log(LOGDEBUG, "CEmbyClient::FetchViewItems");
  bool rtn = false;

  CURL curl(m_url);
#if 1
  if (type == EmbyTypeMovie)
  {
    curl.SetFileName("Users/" + GetUserID() + "/Items");
    curl.SetOption("IncludeItemTypes", type);
    curl.SetOption("Fields", MoviesFields);
    // must be last, wtf?
    curl.SetOption("ParentId", view->GetId());
  }
  else if (type == EmbyTypeSeries)
  {
    // also konow as TVShows for non-eu'ers
    curl.SetFileName("Users/" + GetUserID() + "/Items");
    curl.SetOption("IncludeItemTypes", type);
    curl.SetOption("Fields", TVShowsFields);
    // must be last, wtf?
    curl.SetOption("ParentId", view->GetId());
  }
  else if (type == EmbyTypeMusicArtist)
  {
    //TODO: why is this different than for movies/series ?
    // maybe should be using "MusicArtist" ?
    curl.SetFileName("/emby/Artists");
    curl.SetOption("Fields", "Etag,Genres");
    curl.SetProtocolOption("userId", GetUserID());
  }
  else
  {
    CLog::Log(LOGDEBUG, "CEmbyClient::FetchViewItems unknown type: %s", type.c_str());
    return false;
  }
  //CEmbyUtils::GetEmbyCVariant 3801(msec) for 123524 bytes
#else
  curl.SetFileName(view->prefix);
  //CEmbyUtils::GetEmbyCVariant 3376(msec) for 105713 bytes
#endif
  std::string path = curl.Get();
  CVariant variant = CEmbyUtils::GetEmbyCVariant(path);
  if (variant.isNull())
  {
    CLog::Log(LOGERROR, "CEmbyClient::FetchViewItems: invalid response for views items from %s", CURL::GetRedacted(path).c_str());
    return false;
  }

  view->SetItems(variant);

  return rtn;
}

void CEmbyClient::SetPresence(bool presence)
{
  if (m_presence != presence)
    m_presence = presence;
}

const CVariant CEmbyClient::FetchItemById(const std::string &Id)
{
  std::vector<std::string> Ids;
  Ids.push_back(Id);
  return FetchItemByIds(Ids);
}

const CVariant CEmbyClient::FetchItemByIds(const std::vector<std::string> &Ids)
{
  if (Ids.size() < 1)
    return CVariant(CVariant::VariantTypeNull);

  static const std::string Fields = {
    "DateCreated,Genres,MediaStreams,Overview,ShortOverview,Path,ImageTags,Taglines,RecursiveItemCount"
  };

  CURL curl(m_url);
  curl.SetFileName("emby/Users/" + GetUserID() + "/Items/");
  curl.SetOptions("");
  curl.SetOption("Ids", StringUtils::Join(Ids, ","));
  curl.SetOption("Fields", Fields);
  const CVariant variant = CEmbyUtils::GetEmbyCVariant(curl.Get());
  return variant;
}

bool CEmbyClient::AppendItemToCache(const CVariant &variant)
{
  std::string type = variant["Type"].asString();
  for (auto &view : m_viewMovies)
  {
    if (type == EmbyTypeMovie)
      return view->AppendItem(variant);
  }
  for (auto &view : m_viewTVShows)
  {
    if (type == EmbyTypeSeries)
      return view->AppendItem(variant);
  }
  for (auto &view : m_viewMusic)
  {
    if (type == EmbyTypeMusicArtist)
      return view->AppendItem(variant);
  }
  return false;
}

bool CEmbyClient::UpdateItemInCache(const CVariant &variant)
{
  std::string type = variant["Type"].asString();
  for (auto &view : m_viewMovies)
  {
    if (type == EmbyTypeMovie)
      return view->UpdateItem(variant);
  }
  for (auto &view : m_viewTVShows)
  {
    if (type == EmbyTypeSeries)
      return view->UpdateItem(variant);
  }
  for (auto &view : m_viewMusic)
  {
    if (type == EmbyTypeMusicArtist)
      return view->UpdateItem(variant);
  }
  return false;
}

bool CEmbyClient::RemoveItemFromCache(const CVariant &variant)
{
  std::string type = variant["Type"].asString();
  std::string itemId = variant["Id"].asString();
  for (auto &view : m_viewMovies)
  {
    if (type == EmbyTypeMovie)
      return view->RemoveItem(itemId);
  }
  for (auto &view : m_viewTVShows)
  {
    if (type == EmbyTypeSeries)
      return view->RemoveItem(variant);
  }
  for (auto &view : m_viewMusic)
  {
    if (type == EmbyTypeMusicArtist)
      return view->RemoveItem(variant);
  }
  return false;
}
