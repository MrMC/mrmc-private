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

#include "EmbyUtils.h"
#include "EmbyServices.h"
#include "Application.h"
#include "ContextMenuManager.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/StackDirectory.h"
#include "network/Network.h"
#include "utils/Base64.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/JSONVariantParser.h"
#include "utils/URIUtils.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/ZipFile.h"
#include "settings/Settings.h"
#include "utils/JobManager.h"

#include "video/VideoInfoTag.h"
#include "video/windows/GUIWindowVideoBase.h"

#include "music/tags/MusicInfoTag.h"
#include "music/dialogs/GUIDialogSongInfo.h"
#include "music/dialogs/GUIDialogMusicInfo.h"
#include "guilib/GUIWindowManager.h"

static const std::string StandardFields = {
  "DateCreated,Genres,MediaStreams,Overview,Path"
};

static const std::string TVShowsFields = {
  "DateCreated,Genres,MediaStreams,Overview,ShortOverview,Path,RecursiveItemCount"
};

// one tick is 0.1 microseconds
static const uint64_t TicksToSecondsFactor = 10000000;
static uint64_t TicksToSeconds(uint64_t ticks)
{
  return ticks / TicksToSecondsFactor;
}
static uint64_t SecondsToTicks(uint64_t seconds)
{
  return seconds * TicksToSecondsFactor;
}

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
    if (m_itemIDs.size() > 0)
      m_client->UpdateViewItems(m_itemIDs);
    return true;
  }
private:
  CEmbyClientPtr m_client;
  std::vector<std::string> m_itemIDs;
};

static int g_progressSec = 0;
static CFileItem m_curItem;
static MediaServicesPlayerState g_playbackState = MediaServicesPlayerState::stopped;

bool CEmbyUtils::HasClients()
{
  return CEmbyServices::GetInstance().HasClients();
}

bool CEmbyUtils::GetIdentity(CURL url, int timeout)
{
  XFILE::CCurlFile curlfile;
  curlfile.SetTimeout(timeout);
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");

  CURL curl(url);
  curl.SetFileName("emby/system/info/public");
  // do not need user/pass for server info
  curl.SetUserName("");
  curl.SetPassword("");
  curl.SetOptions("");

  std::string path = curl.Get();
  std::string response;
  return curlfile.Get(path, response);
}

void CEmbyUtils::PrepareApiCall(const std::string& userId, const std::string& accessToken, XFILE::CCurlFile &curl)
{
  curl.SetRequestHeader("Accept", "application/json");

  if (!accessToken.empty())
    curl.SetRequestHeader(EmbyApiKeyHeader, accessToken);

  curl.SetRequestHeader(EmbyAuthorizationHeader,
    StringUtils::Format("MediaBrowser Client=\"%s\", Device=\"%s\", DeviceId=\"%s\", Version=\"%s\", UserId=\"%s\"",
      CSysInfo::GetAppName().c_str(), CSysInfo::GetDeviceName().c_str(),
      CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID).c_str(),
      CSysInfo::GetVersionShort().c_str(), userId.c_str()));
}

void CEmbyUtils::SetEmbyItemProperties(CFileItem &item)
{
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(item.GetPath());
  SetEmbyItemProperties(item, client);
}

void CEmbyUtils::SetEmbyItemProperties(CFileItem &item, const CEmbyClientPtr &client)
{
  item.SetProperty("EmbyItem", true);
  item.SetProperty("MediaServicesItem", true);
  if (!client)
    return;
  if (client->IsCloud())
    item.SetProperty("MediaServicesCloudItem", true);
  item.SetProperty("MediaServicesClientID", client->GetUuid());
}

void CEmbyUtils::SetEmbyItemsProperties(CFileItemList &items)
{
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(items.GetPath());
  SetEmbyItemsProperties(items, client);
}

void CEmbyUtils::SetEmbyItemsProperties(CFileItemList &items, const CEmbyClientPtr &client)
{
  items.SetProperty("EmbyItem", true);
  items.SetProperty("MediaServicesItem", true);
  if (!client)
    return;
  if (client->IsCloud())
    items.SetProperty("MediaServicesCloudItem", true);
  items.SetProperty("MediaServicesClientID", client->GetUuid());
}

void CEmbyUtils::SetWatched(CFileItem &item)
{
  // POST to /Users/{UserId}/PlayedItems/{Id}
  std::string url = item.GetPath();
  if (URIUtils::IsStack(url))
    url = XFILE::CStackDirectory::GetFirstStackedFile(url);
  else
    url = URIUtils::GetParentPath(url);
  if (StringUtils::StartsWithNoCase(url, "emby://"))
    url = Base64::Decode(URIUtils::GetFileName(item.GetPath()));

  // need userId which only client knows
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(url);
  if (!client || !client->GetPresence())
    return;

  // use the current date and time if lastPlayed is invalid
  if (!item.GetVideoInfoTag()->m_lastPlayed.IsValid())
    item.GetVideoInfoTag()->m_lastPlayed = CDateTime::GetUTCDateTime();

  // get the URL to updated the item's played state for this user ID
  CURL url2(url);
  url2.SetFileName("emby/Users/" + client->GetUserID() + "/PlayedItems/" + item.GetMediaServiceId());
  url2.SetOptions("");
  // and add the DatePlayed URL parameter
  url2.SetOption("DatePlayed",
    StringUtils::Format("%04i%02i%02i%02i%02i%02i",
      item.GetVideoInfoTag()->m_lastPlayed.GetYear(),
      item.GetVideoInfoTag()->m_lastPlayed.GetMonth(),
      item.GetVideoInfoTag()->m_lastPlayed.GetDay(),
      item.GetVideoInfoTag()->m_lastPlayed.GetHour(),
      item.GetVideoInfoTag()->m_lastPlayed.GetMinute(),
      item.GetVideoInfoTag()->m_lastPlayed.GetSecond()));

  std::string data;
  std::string response;
  // execute the POST request
  XFILE::CCurlFile curl;
  if (curl.Post(url2.Get(), data, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    if (!response.empty())
      CLog::Log(LOGDEBUG, "CEmbyUtils::SetWatched %s", response.c_str());
#endif
  }
}

void CEmbyUtils::SetUnWatched(CFileItem &item)
{
  // DELETE to /Users/{UserId}/PlayedItems/{Id}
  std::string url = item.GetPath();
  if (URIUtils::IsStack(url))
    url = XFILE::CStackDirectory::GetFirstStackedFile(url);
  else
    url = URIUtils::GetParentPath(url);
  if (StringUtils::StartsWithNoCase(url, "emby://"))
    url = Base64::Decode(URIUtils::GetFileName(item.GetPath()));

  // need userId which only client knows
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(url);
  if (!client || !client->GetPresence())
    return;

  // get the URL to updated the item's played state for this user ID
  CURL url2(url);
  url2.SetFileName("emby/Users/" + client->GetUserID() + "/PlayedItems/" + item.GetMediaServiceId());
  url2.SetOptions("");

  std::string data;
  std::string response;
  // execute the DELETE request
  XFILE::CCurlFile curl;
  if (curl.Delete(url2.Get(), data, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    if (!response.empty())
      CLog::Log(LOGDEBUG, "CEmbyUtils::SetUnWatched %s", response.c_str());
#endif
  }
}

void CEmbyUtils::ReportProgress(CFileItem &item, double currentSeconds)
{
  // if we are Emby music, do not report
  if (item.IsAudio())
    return;

  // we get called from Application.cpp every 500ms
  if ((g_playbackState == MediaServicesPlayerState::stopped || g_progressSec <= 0 || g_progressSec > 30))
  {
    std::string status;
    if (g_playbackState == MediaServicesPlayerState::playing )
      status = "playing";
    else if (g_playbackState == MediaServicesPlayerState::paused )
      status = "paused";
    else if (g_playbackState == MediaServicesPlayerState::stopped)
      status = "stopped";

    if (!status.empty())
    {
      std::string url = item.GetPath();
      if (URIUtils::IsStack(url))
        url = XFILE::CStackDirectory::GetFirstStackedFile(url);
      else
      {
        CURL url1(item.GetPath());
        CURL url2(URIUtils::GetParentPath(url));
        CURL url3(url2.GetWithoutFilename());
        url3.SetProtocolOptions(url1.GetProtocolOptions());
        url = url3.Get();
      }
      if (StringUtils::StartsWithNoCase(url, "emby://"))
        url = Base64::Decode(URIUtils::GetFileName(item.GetPath()));

      /*
      # Postdata structure to send to Emby server
      url = "{server}/emby/Sessions/Playing"
      postdata = {
        'QueueableMediaTypes': "Video",
        'CanSeek': True,
        'ItemId': itemId,
        'MediaSourceId': itemId,
        'PlayMethod': playMethod,
        'VolumeLevel': volume,
        'PositionTicks': int(seekTime * 10000000),
        'IsMuted': muted
      }
      */

      CURL url4(item.GetPath());
      if (status == "playing")
      {
        if (g_progressSec < 0)
          // playback started
          url4.SetFileName("emby/Sessions/Playing");
        else
          url4.SetFileName("emby/Sessions/Playing/Progress");
      }
      else if (status == "stopped")
        url4.SetFileName("emby/Sessions/Playing/Stopped");

      std::string id = item.GetMediaServiceId();
      url4.SetOptions("");
      url4.SetOption("QueueableMediaTypes", "Video");
      url4.SetOption("CanSeek", "True");
      url4.SetOption("ItemId", id);
      url4.SetOption("MediaSourceId", id);
      url4.SetOption("PlayMethod", "DirectPlay");
      url4.SetOption("PositionTicks", StringUtils::Format("%llu", SecondsToTicks(currentSeconds)));
      url4.SetOption("IsMuted", "False");
      url4.SetOption("IsPaused", status == "paused" ? "True" : "False");

      std::string data;
      std::string response;
      // execute the POST request
      XFILE::CCurlFile curl;
      if (curl.Post(url4.Get(), data, response))
      {
#if defined(EMBY_DEBUG_VERBOSE)
        if (!response.empty())
          CLog::Log(LOGDEBUG, "CEmbyUtils::ReportProgress %s", response.c_str());
#endif
      }
      g_progressSec = 0;
    }
  }
  g_progressSec++;
}

void CEmbyUtils::SetPlayState(MediaServicesPlayerState state)
{
  g_progressSec = -1;
  g_playbackState = state;
}

bool CEmbyUtils::GetEmbyRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);

  url2.SetFileName(url2.GetFileName() + "/Latest");
  
  url2.SetOption("IncludeItemTypes", "Episode");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  CVariant variant = GetEmbyCVariant(url2.Get());

  std::map<std::string, CVariant> variantMap;
  variantMap["Items"] = variant;
  variant = CVariant(variantMap);

  bool rtn = GetVideoItems(items, url2, variant, MediaTypeEpisode);
  return rtn;
}

bool CEmbyUtils::GetEmbyInProgressShows(CFileItemList &items, const std::string url, int limit)
{
  // SortBy=DatePlayed&SortOrder=Descending&Filters=IsResumable&Limit=5
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", "Episode");
  url2.SetOption("SortBy", "DatePlayed");
  url2.SetOption("SortOrder", "Descending");
  url2.SetOption("Filters", "IsResumable");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("Recursive", "true");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  CVariant result = GetEmbyCVariant(url2.Get());

  bool rtn = GetVideoItems(items, url2, result, MediaTypeEpisode);
  return rtn;
}

bool CEmbyUtils::GetEmbyRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);

  url2.SetFileName(url2.GetFileName() + "/Latest");

  url2.SetOption("IncludeItemTypes", "Movie");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  CVariant variant = GetEmbyCVariant(url2.Get());

  std::map<std::string, CVariant> variantMap;
  variantMap["Items"] = variant;
  variant = CVariant(variantMap);

  bool rtn = GetVideoItems(items, url2, variant, MediaTypeMovie);
  return rtn;
}

bool CEmbyUtils::GetEmbyInProgressMovies(CFileItemList &items, const std::string url, int limit)
{
  // SortBy=DatePlayed&SortOrder=Descending&Filters=IsResumable&Limit=5
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", "Movie");
  url2.SetOption("SortBy", "DatePlayed");
  url2.SetOption("SortOrder", "Descending");
  url2.SetOption("Filters", "IsResumable");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  CVariant result = GetEmbyCVariant(url2.Get());

  bool rtn = GetVideoItems(items, url2, result, MediaTypeMovie);
  return rtn;
}

bool CEmbyUtils::GetAllEmbyInProgress(CFileItemList &items, bool tvShow)
{
  bool rtn = false;

  if (CEmbyServices::GetInstance().HasClients())
  {
    CFileItemList embyItems;
    bool limitToLocal = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYLIMITHOMETOLOCAL);
    //look through all emby clients and pull in progress for each library section
    std::vector<CEmbyClientPtr> clients;
    CEmbyServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {
      if (limitToLocal && !client->IsOwned())
        continue;

      EmbyViewContentVector contents;
      if (tvShow)
        contents = client->GetTvShowContent();
      else
        contents = client->GetMoviesContent();
      for (const auto &content : contents)
      {
        std::string userId = client->GetUserID();
        CURL curl(client->GetUrl());
        curl.SetProtocol(client->GetProtocol());
        curl.SetOption("ParentId", content.id);
        curl.SetFileName("Users/" + userId + "/Items");

        if (tvShow)
          rtn = GetEmbyInProgressShows(embyItems, curl.Get(), 10);
        else
          rtn = GetEmbyInProgressMovies(embyItems, curl.Get(), 10);

        items.Append(embyItems);
        embyItems.ClearItems();
      }
    }
  }
  return rtn;
}

bool CEmbyUtils::GetAllEmbyRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow)
{
  bool rtn = false;

  if (CEmbyServices::GetInstance().HasClients())
  {
    CFileItemList embyItems;
    bool limitToLocal = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYLIMITHOMETOLOCAL);
    //look through all emby clients and pull recently added for each library section
    std::vector<CEmbyClientPtr> clients;
    CEmbyServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {
      if (limitToLocal && !client->IsOwned())
        continue;

      EmbyViewContentVector contents;
      if (tvShow)
        contents = client->GetTvShowContent();
      else
        contents = client->GetMoviesContent();
      for (const auto &content : contents)
      {
        std::string userId = client->GetUserID();
        CURL curl(client->GetUrl());
        curl.SetProtocol(client->GetProtocol());
        curl.SetOption("ParentId", content.id);
        curl.SetFileName("Users/" + userId + "/Items");

        if (tvShow)
          rtn = GetEmbyRecentlyAddedEpisodes(embyItems, curl.Get(), 10);
        else
          rtn = GetEmbyRecentlyAddedMovies(embyItems, curl.Get(), 10);

        items.Append(embyItems);
        embyItems.ClearItems();
      }
    }
  }
  return rtn;
}

CFileItemPtr ParseMusic(const CEmbyClient *client, const CVariant &variant)
{
  return nullptr;
}

CFileItemPtr CEmbyUtils::ToFileItemPtr(CEmbyClient *client, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::ToFileItemPtr cvariant is empty");
    return nullptr;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantitemsIt = variantItems.begin_array(); variantitemsIt != variantItems.end_array(); ++variantitemsIt)
  {
    const auto variantItem = *variantitemsIt;
    if (!variantItem.isMember("Id"))
      continue;

    CFileItemList items;
    std::string type = variantItem["Type"].asString();
    std::string mediaType = variantItem["MediaType"].asString();
    CURL url2(client->GetUrl());
    url2.SetProtocol(client->GetProtocol());
    url2.SetPort(client->GetPort());
    url2.SetFileName("emby/Users/" + client->GetUserID() + "/Items");

    if (type == "Movie")
    {
      GetVideoItems(items, url2, variant, MediaTypeMovie);
    }
    else if (type == "Series")
    {
      ParseEmbySeries(items, url2, variant);
    }
    else if (type == "Season")
    {
      CURL url3(url2);
      std::string seriesID = variantItem["ParentId"].asString();
      url3.SetOptions("");
      url3.SetOption("Ids", seriesID);
      url3.SetOption("Fields", "Overview,Genres");
      url3.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
      const CVariant seriesObject = CEmbyUtils::GetEmbyCVariant(url3.Get());
      ParseEmbySeasons(items, url2, seriesObject, variant);
    }
    else if (type == "Episode")
    {
      GetVideoItems(items, url2, variant, MediaTypeEpisode);
    }
    //else if (type == "Music")
    //{
    //  return ParseMusic(client, variantItem);
    //}

    return items[0];
  }

  return nullptr;
}

  // Emby Movie/TV
bool CEmbyUtils::GetEmbyMovies(CFileItemList &items, std::string url, std::string filter)
{
  bool rtn = false;
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", "Movie");
  //url2.SetOption("Fields", "Etag,DateCreated");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  const CVariant variant = GetEmbyCVariant(url2.Get());

  rtn = GetVideoItems(items, url2, variant, MediaTypeMovie);

  /*
  if (GetVideoItems(items, url2, variant, MediaTypeMovie))
  {
    CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(url2.Get());
    if (client || client->GetPresence())
    {
      std::vector<std::string> itemIds;
      const auto& objectItems = variant["Items"];
      int counter = 0;
      for (unsigned int k = 0; k < objectItems.size(); ++k)
      {
        const auto objectItem = objectItems[k];
        itemIds.push_back(objectItem["Id"].asString());
        counter += 1;
        if ( counter > 100 || k == objectItems.size() - 1)
        {
          // split jobs in 100 item chunks
          CJobManager::GetInstance().AddJob(new CEmbyUtilsJob(client,itemIds), NULL);
          itemIds.clear();
          counter = 0;
        }
        CLog::Log(LOGERROR, "CEmbyUtils::GetEmbyMovies counter = %i, obsize = %i ", counter, objectItems.size());
      }
    }
  }
  */
  return rtn;
}

bool CEmbyUtils::GetEmbyTvshows(CFileItemList &items, std::string url)
{
  CURL url2(url);
  url2.SetOption("IncludeItemTypes", "Series");
  url2.SetOption("Fields", TVShowsFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");

 /*
   params = {
   
   'ParentId': parentid,
   'ArtistIds': artist_id,
   'IncludeItemTypes': itemtype,
   'LocationTypes': "FileSystem,Remote,Offline",
   'CollapseBoxSetItems': False,
   'IsVirtualUnaired': False,
   'IsMissing': False,
   'Recursive': True,
   'Limit': 1
   }
  */

  const CVariant variant = GetEmbyCVariant(url2.Get());

  CURL url3(url);
  bool rtn = ParseEmbySeries(items, url3, variant);
  return rtn;
}

bool CEmbyUtils::GetEmbySeasons(CFileItemList &items, const std::string url)
{
  // "Shows/\(query.seriesId)/Seasons"
  bool rtn = false;

  CURL url2(url);
  url2.SetOption("IncludeItemTypes", "Seasons");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline,Virtual");
  url2.SetOption("Fields", "Etag,DateCreated,RecursiveItemCount");
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");

  const CVariant variant = GetEmbyCVariant(url2.Get());
  std::string seriesName;
  if (!variant.isNull() || variant.isObject() || variant.isMember("Items"))
  {
    CURL url3(url);
    std::string seriesID = url3.GetOption("ParentId");
    url3.SetOptions("");
    url3.SetOption("Ids", seriesID);
    url3.SetOption("Fields", "Overview,Genres");
    url3.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
    const CVariant seriesObject = CEmbyUtils::GetEmbyCVariant(url3.Get());

    rtn = ParseEmbySeasons(items, url2, seriesObject, variant);
  }
  return rtn;
}

bool CEmbyUtils::GetEmbyEpisodes(CFileItemList &items, const std::string url)
{
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", "Episode");
  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", StandardFields);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  const CVariant variant = GetEmbyCVariant(url2.Get());

  bool rtn = GetVideoItems(items, url2, variant, MediaTypeEpisode);
  return rtn;
}

bool CEmbyUtils::GetEmbyTVFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter)
{
  /*
   std::string filter = "year";
   if (path == "genres")
   filter = "genre";
   else if (path == "actors")
   filter = "actor";
   else if (path == "directors")
   filter = "director";
   else if (path == "sets")
   filter = "collection";
   else if (path == "countries")
   filter = "country";
   else if (path == "studios")
   filter = "studio";

   http://192.168.1.200:8096/emby/Genres?SortBy=SortName&SortOrder=Ascending&IncludeItemTypes=Movie&Recursive=true&EnableTotalRecordCount=false&ParentId=f137a2dd21bbc1b99aa5c0f6bf02a805&userId=cf28f6d51dd54c63a27fed6600c5b6cb
   */

  std::string userID = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYUSERID);

  CURL url2(url);

  url2.SetFileName("emby/"+ filter);
  url2.SetOption("IncludeItemTypes", "Series");

  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", "Etag,DateCreated");
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  const CVariant variant = GetEmbyCVariant(url2.Get());

  bool rtn = false;
  
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::GetEmbyMovieFilter invalid response from %s", url2.GetRedacted().c_str());
    return false;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    rtn = true;
    const auto item = *variantItemIt;
    CFileItemPtr newItem(new CFileItem());
    std::string title = item["Name"].asString();
    std::string key = item["Id"].asString();
    newItem->m_bIsFolder = true;
    newItem->m_bIsShareOrDrive = false;

    if (filter == "Genres")
      url2.SetOption("GenreIds", key);
    else if (filter == "Years")
      url2.SetOption("Years", title);

    url2.SetFileName("Users/" + userID +"/Items");
    newItem->SetPath(parentPath + Base64::Encode(url2.Get()));
    newItem->SetLabel(title);
    newItem->SetProperty("SkipLocalArt", true);
    items.Add(newItem);
  }
  return rtn;
}

bool CEmbyUtils::GetEmbyMovieFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter)
{
  /*
  std::string filter = "year";
  if (path == "genres")
    filter = "genre";
  else if (path == "actors")
    filter = "actor";
  else if (path == "directors")
    filter = "director";
  else if (path == "sets")
    filter = "collection";
  else if (path == "countries")
    filter = "country";
  else if (path == "studios")
    filter = "studio";
   
   http://192.168.1.200:8096/emby/Genres?SortBy=SortName&SortOrder=Ascending&IncludeItemTypes=Movie&Recursive=true&EnableTotalRecordCount=false&ParentId=f137a2dd21bbc1b99aa5c0f6bf02a805&userId=cf28f6d51dd54c63a27fed6600c5b6cb
  */

  std::string userID = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYUSERID);

  CURL url2(url);
  if (filter != "Collections")
  {
    url2.SetFileName("emby/"+ filter);
    url2.SetOption("IncludeItemTypes", "Movie");
  }
  else
  {
    url2.SetOption("IncludeItemTypes", "BoxSet");
    url2.SetOption("Recursive", "true");
    url2.SetOption("ParentId", "");
  }

  //url2.SetOption("LocationTypes", "FileSystem,Remote,Offline");
  url2.SetOption("Fields", "Etag,DateCreated");
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  const CVariant variant = GetEmbyCVariant(url2.Get());

  bool rtn = false;

  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::GetEmbyMovieFilter invalid response from %s", url2.GetRedacted().c_str());
    return false;
  }

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    rtn = true;
    const auto item = *variantItemIt;
    CFileItemPtr newItem(new CFileItem());
    std::string title = item["Name"].asString();
    std::string key = item["Id"].asString();
    newItem->m_bIsFolder = true;
    newItem->m_bIsShareOrDrive = false;

    if (filter == "Genres")
      url2.SetOption("GenreIds", key);
    else if (filter == "Years")
      url2.SetOption("Years", title);
    else if (filter == "Collections")
      url2.SetOption("ParentId", key);

    url2.SetFileName("Users/" + userID +"/Items");
    newItem->SetPath(parentPath + Base64::Encode(url2.Get()));
    newItem->SetLabel(title);
    newItem->SetProperty("SkipLocalArt", true);
    items.Add(newItem);
  }
  return rtn;
}

bool CEmbyUtils::GetItemSubtiles(CFileItem &item)
{
  return false;
}

bool CEmbyUtils::GetMoreItemInfo(CFileItem &item)
{
  std::string url = URIUtils::GetParentPath(item.GetPath());
  if (StringUtils::StartsWithNoCase(url, "emby://"))
    url = Base64::Decode(URIUtils::GetFileName(item.GetPath()));

  CURL url2(url);
  CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(url2.Get());
  if (!client || !client->GetPresence())
    return false;

  std::string itemId;
  if (item.HasProperty("EmbySeriesID") && !item.GetProperty("EmbySeriesID").asString().empty())
    itemId = item.GetProperty("EmbySeriesID").asString();
  else
    itemId = item.GetMediaServiceId();

  url2.SetFileName("emby/Users/" + client->GetUserID() + "/Items");
  url2.SetOptions("");
  url2.SetOption("Fields", "Genres,People");
  url2.SetOption("IDs", itemId);
  url2.SetProtocolOptions(url2.GetProtocolOptions() + "&format=json");
  const CVariant variant = GetEmbyCVariant(url2.Get());

  GetVideoDetails(item, variant["Items"][0]);
  return true;
}

bool CEmbyUtils::GetMoreResolutions(CFileItem &item)
{
  return true;
}

bool CEmbyUtils::GetURL(CFileItem &item)
{
  return true;
}

bool CEmbyUtils::SearchEmby(CFileItemList &items, std::string strSearchString)
{
  return false;
}


  // Emby Music
bool CEmbyUtils::GetEmbyArtistsOrAlbum(CFileItemList &items, std::string url, bool album)
{
  return false;
}

bool CEmbyUtils::GetEmbySongs(CFileItemList &items, std::string url)
{
  return false;
}

bool CEmbyUtils::ShowMusicInfo(CFileItem item)
{
  return false;
}

bool CEmbyUtils::GetEmbyRecentlyAddedAlbums(CFileItemList &items,int limit)
{
  return false;
}

bool CEmbyUtils::GetEmbyAlbumSongs(CFileItem item, CFileItemList &items)
{
  return false;
}

bool CEmbyUtils::GetEmbyMediaTotals(MediaServicesMediaCount &totals)
{
  return false;
}


void CEmbyUtils::ReportToServer(std::string url, std::string filename)
{
}

bool CEmbyUtils::ParseEmbySeries(CFileItemList &items, const CURL &url, const CVariant &variant)
{
  bool rtn = false;
  if (!variant.isNull() || variant.isObject() || variant.isMember("Items"))
  {
    const auto& variantItems = variant["Items"];
    for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
    {
      const auto item = *variantItemIt;
      rtn = true;

      std::string value;
      std::string fanart;
      std::string itemId = item["Id"].asString();
      std::string seriesId = item["SeriesId"].asString();
      // clear url options
      CURL url2(url);
      url2.SetOption("ParentId", itemId);
 //     url2.SetOptions("");

      CFileItemPtr newItem(new CFileItem());
      // set m_bIsFolder to true to indicate we are tvshow list
      newItem->m_bIsFolder = true;

      std::string title = item["Name"].asString();
      newItem->SetLabel(title);

      CDateTime premiereDate;
      premiereDate.SetFromW3CDateTime(item["PremiereDate"].asString());
      newItem->m_dateTime = premiereDate;

     // url2.SetFileName("Users/" + itemId + "/Items");
      newItem->SetPath("emby://tvshows/shows/" + Base64::Encode(url2.Get()));
      newItem->SetMediaServiceId(itemId);
      newItem->SetMediaServiceFile(item["Path"].asString());

      url2.SetFileName("Items/" + itemId + "/Images/Primary");
      newItem->SetArt("thumb", url2.Get());
      newItem->SetIconImage(url2.Get());
      url2.SetFileName("Items/" + itemId + "/Images/Banner");
      newItem->SetArt("banner", url2.Get());
      url2.SetFileName("Items/" + itemId + "/Images/Backdrop");
      newItem->SetArt("fanart", url2.Get());

      newItem->GetVideoInfoTag()->m_playCount = static_cast<int>(item["UserData"]["PlayCount"].asInteger());
      newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, item["UserData"]["Played"].asBoolean());

      newItem->GetVideoInfoTag()->m_strTitle = title;
      newItem->GetVideoInfoTag()->m_strStatus = item["Status"].asString();

      newItem->GetVideoInfoTag()->m_type = MediaTypeTvShow;
      newItem->GetVideoInfoTag()->m_strFileNameAndPath = newItem->GetPath();
      newItem->GetVideoInfoTag()->SetSortTitle(item["SortName"].asString());
      newItem->GetVideoInfoTag()->SetOriginalTitle(item["OriginalTitle"].asString());
      newItem->SetProperty("EmbySeriesID", seriesId);
      //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
      newItem->GetVideoInfoTag()->SetPlot(item["Overview"].asString());
      newItem->GetVideoInfoTag()->SetPlotOutline(item["ShortOverview"].asString());
      newItem->GetVideoInfoTag()->m_firstAired = premiereDate;
      newItem->GetVideoInfoTag()->SetPremiered(premiereDate);
      newItem->GetVideoInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
      newItem->GetVideoInfoTag()->SetYear(static_cast<int>(item["ProductionYear"].asInteger()));
      newItem->GetVideoInfoTag()->SetRating(item["CommunityRating"].asFloat(), static_cast<int>(item["VoteCount"].asInteger()), "", true);
      newItem->GetVideoInfoTag()->m_strMPAARating = item["OfficialRating"].asString();

      int totalEpisodes = item["RecursiveItemCount"].asInteger() - item["ChildCount"].asInteger();
      int unWatchedEpisodes = static_cast<int>(item["UserData"]["UnplayedItemCount"].asInteger());
      int watchedEpisodes = totalEpisodes - unWatchedEpisodes;
      int iSeasons        = static_cast<int>(item["ChildCount"].asInteger());

      newItem->GetVideoInfoTag()->m_iSeason = iSeasons;
      newItem->GetVideoInfoTag()->m_iEpisode = totalEpisodes;
      newItem->GetVideoInfoTag()->m_playCount = (int)watchedEpisodes >= newItem->GetVideoInfoTag()->m_iEpisode;

      newItem->SetProperty("totalseasons", iSeasons);
      newItem->SetProperty("totalepisodes", newItem->GetVideoInfoTag()->m_iEpisode);
      newItem->SetProperty("numepisodes",   newItem->GetVideoInfoTag()->m_iEpisode);
      newItem->SetProperty("watchedepisodes", watchedEpisodes);
      newItem->SetProperty("unwatchedepisodes", unWatchedEpisodes);

      GetVideoDetails(*newItem, item);
      SetEmbyItemProperties(*newItem);
      items.Add(newItem);
    }
    // this is needed to display movies/episodes properly ... dont ask
    // good thing it didnt take 2 days to figure it out
    items.SetProperty("library.filter", "true");
    SetEmbyItemProperties(items);
  }
  return rtn;
}

bool CEmbyUtils::ParseEmbySeasons(CFileItemList &items, const CURL &url, const CVariant &series, const CVariant &variant)
{
  bool rtn = false;

  std::string seriesName;
  const auto& seriesItem = series["Items"][0];
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    const auto item = *variantItemIt;

    std::string value;
    std::string fanart;
    std::string itemId = item["Id"].asString();
    std::string seriesId = item["SeriesId"].asString();
    // clear url options
    CURL url2(url);
    url2.SetOptions("");
    url2.SetOption("ParentId", itemId);

    CFileItemPtr newItem(new CFileItem());
    // set m_bIsFolder to true to indicate we are tvshow list
    newItem->m_bIsFolder = true;

    //CURL url1(url);
    //url1.SetFileName("/Users/" + itemId + "/Items");
    newItem->SetLabel(item["Name"].asString());
    newItem->SetPath("emby://tvshows/seasons/" + Base64::Encode(url2.Get()));
    newItem->SetMediaServiceId(itemId);
    newItem->SetMediaServiceFile(item["Path"].asString());

    url2.SetFileName("Items/" + seriesId + "/Images/Primary");
    newItem->SetArt("thumb", url2.Get());
    newItem->SetIconImage(url2.Get());
    url2.SetFileName("Items/" + seriesId + "/Images/Banner");
    newItem->SetArt("banner", url2.Get());
    url2.SetFileName("Items/" + seriesId + "/Images/Backdrop");
    newItem->SetArt("fanart", url2.Get());

    newItem->GetVideoInfoTag()->m_type = MediaTypeSeason;
    newItem->GetVideoInfoTag()->m_strTitle = item["Name"].asString();
    // we get these from rootXmlNode, where all show info is
    seriesName = item["SeriesName"].asString();
    newItem->GetVideoInfoTag()->m_strShowTitle = item["SeriesName"].asString();
    newItem->GetVideoInfoTag()->SetPlotOutline(seriesItem["Overview"].asString());
    newItem->GetVideoInfoTag()->SetPlot(seriesItem["Overview"].asString());
    newItem->GetVideoInfoTag()->SetYear(seriesItem["ProductionYear"].asInteger());
    std::vector<std::string> genres;
    const auto& streams = seriesItem["Genres"];
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      genres.push_back(stream.asString());
    }
    newItem->GetVideoInfoTag()->SetGenre(genres);
    newItem->SetProperty("EmbySeriesID", seriesId);

    int totalEpisodes = item["RecursiveItemCount"].asInteger();
    int unWatchedEpisodes = item["UserData"]["UnplayedItemCount"].asInteger();
    int watchedEpisodes = totalEpisodes - unWatchedEpisodes;
    int iSeason = item["IndexNumber"].asInteger();
    newItem->GetVideoInfoTag()->m_iSeason = iSeason;
    newItem->GetVideoInfoTag()->m_iEpisode = totalEpisodes;
    newItem->GetVideoInfoTag()->m_playCount = item["UserData"]["PlayCount"].asInteger();

    newItem->SetProperty("totalepisodes", totalEpisodes);
    newItem->SetProperty("numepisodes", totalEpisodes);
    newItem->SetProperty("watchedepisodes", watchedEpisodes);
    newItem->SetProperty("unwatchedepisodes", unWatchedEpisodes);

    newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, item["UserData"]["Played"].asBoolean());
    SetEmbyItemProperties(*newItem);
    items.Add(newItem);
  }
  items.SetLabel(seriesName);
  items.SetProperty("showplot", seriesItem["Overview"].asString());
  SetEmbyItemProperties(items);
  items.SetProperty("library.filter", "true");

  return rtn;
}

bool CEmbyUtils::GetVideoItems(CFileItemList &items, CURL url, const CVariant &variant, std::string type)
{
  bool rtn = false;
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::GetVideoItems invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

#if defined(EMBY_DEBUG_TIMING)
  unsigned int currentTime = XbmcThreads::SystemClockMillis();
#endif
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    const auto objectItem = *variantItemIt;
    CFileItemPtr item = ToVideoFileItemPtr(url, objectItem, type);
    items.Add(item);
    rtn = true;
  }
  // this is needed to display movies/episodes properly ... dont ask
  // good thing it didnt take 2 days to figure it out
  items.SetLabel(variantItems[0]["SeasonName"].asString());
  items.SetProperty("library.filter", "true");
  SetEmbyItemProperties(items);

#if defined(EMBY_DEBUG_TIMING)
  int delta = XbmcThreads::SystemClockMillis() - currentTime;
  if (delta > 1)
  {
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetVideoItems %d(msec) for %d items",
      XbmcThreads::SystemClockMillis() - currentTime, variantItems.size());
  }
#endif
  return rtn;
}

CFileItemPtr CEmbyUtils::ToVideoFileItemPtr(CURL url, const CVariant &variant, std::string type)
{
  // clear base url options
  CURL url2(url);
  url2.SetOptions("");

  CFileItemPtr item(new CFileItem());

  // cache common accessors.
  std::string itemId = variant["Id"].asString();
  std::string seriesId = variant["SeriesId"].asString();

  std::string value;
  std::string fanart;
  // if we have season means we are listing episodes, we need to get the fanart from rootXmlNode.
  // movies has it in videoNode
  if (variant.isMember("ParentIndexNumber"))
  {
    url2.SetFileName("Items/" + itemId + "/Images/Primary");
    item->SetArt("thumb", url2.Get());
    item->SetIconImage(url2.Get());
    url2.SetFileName("Items/" + itemId + "/Images/Backdrop");
    fanart = url2.Get();
    item->GetVideoInfoTag()->m_strShowTitle = variant["SeriesName"].asString();
    item->GetVideoInfoTag()->m_iSeason = variant["ParentIndexNumber"].asInteger();
    item->GetVideoInfoTag()->m_iEpisode = variant["IndexNumber"].asInteger();
    item->SetLabel(variant["SeasonName"].asString());
    item->SetProperty("EmbySeriesID", seriesId);
  }
/*
  else if (((TiXmlElement*) videoNode)->Attribute("grandparentTitle")) // only recently added episodes have this
  {
    fanart = XMLUtils::GetAttribute(videoNode, "art");
    videoInfo->m_strShowTitle = XMLUtils::GetAttribute(videoNode, "grandparentTitle");
    videoInfo->m_iSeason = atoi(XMLUtils::GetAttribute(videoNode, "parentIndex").c_str());
    videoInfo->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());

    value = XMLUtils::GetAttribute(videoNode, "thumb");
    if (!value.empty() && (value[0] == '/'))
      StringUtils::TrimLeft(value, "/");
    url.SetFileName(value);
    newItem->SetArt("thumb", url.Get());

    value = XMLUtils::GetAttribute(videoNode, "parentThumb");
    if (value.empty())
      value = XMLUtils::GetAttribute(videoNode, "grandparentThumb");
    if (!value.empty() && (value[0] == '/'))
      StringUtils::TrimLeft(value, "/");
    url.SetFileName(value);
    newItem->SetArt("tvshow.poster", url.Get());
    newItem->SetArt("tvshow.thumb", url.Get());
    newItem->SetIconImage(url.Get());
    std::string seasonEpisode = StringUtils::Format("S%02iE%02i", plexItem->GetVideoInfoTag()->m_iSeason, plexItem->GetVideoInfoTag()->m_iEpisode);
    newItem->SetProperty("SeasonEpisode", seasonEpisode);
  }
*/
  else
  {
    url2.SetFileName("Items/" + itemId + "/Images/Primary");
    item->SetArt("thumb", url2.Get());
    item->SetIconImage(url2.Get());
    url2.SetFileName("Items/" + itemId + "/Images/Backdrop");
    fanart = url2.Get();
  }

  std::string title = variant["Name"].asString();
  item->SetLabel(title);
  item->m_dateTime.SetFromW3CDateTime(variant["PremiereDate"].asString());

  item->SetArt("fanart", fanart);

  item->GetVideoInfoTag()->m_strTitle = title;
  item->GetVideoInfoTag()->SetSortTitle(variant["SortName"].asString());
  item->GetVideoInfoTag()->SetOriginalTitle(variant["OriginalTitle"].asString());

  url2.SetFileName("Videos/" + itemId +"/stream?static=true");
  item->SetPath(url2.Get());
  item->SetMediaServiceId(itemId);
  item->SetMediaServiceFile(variant["Path"].asString());
  item->GetVideoInfoTag()->m_strFileNameAndPath = url2.Get();

  //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
  item->GetVideoInfoTag()->m_type = type;
  item->GetVideoInfoTag()->SetPlot(variant["Overview"].asString());
  item->GetVideoInfoTag()->SetPlotOutline(variant["ShortOverview"].asString());

  CDateTime premiereDate;
  premiereDate.SetFromW3CDateTime(variant["PremiereDate"].asString());
  item->GetVideoInfoTag()->m_firstAired = premiereDate;
  item->GetVideoInfoTag()->SetPremiered(premiereDate);
  item->GetVideoInfoTag()->m_dateAdded.SetFromW3CDateTime(variant["DateCreated"].asString());

  item->GetVideoInfoTag()->SetYear(static_cast<int>(variant["ProductionYear"].asInteger()));
  item->GetVideoInfoTag()->SetRating(variant["CommunityRating"].asFloat(), static_cast<int>(variant["VoteCount"].asInteger()), "", true);
  item->GetVideoInfoTag()->m_strMPAARating = variant["OfficialRating"].asString();

  GetVideoDetails(*item, variant);

  item->GetVideoInfoTag()->m_duration = static_cast<int>(TicksToSeconds(variant["RunTimeTicks"].asInteger()));
  item->GetVideoInfoTag()->m_resumePoint.totalTimeInSeconds = item->GetVideoInfoTag()->m_duration;
  item->GetVideoInfoTag()->m_playCount = static_cast<int>(variant["UserData"]["PlayCount"].asInteger());
  item->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, variant["UserData"]["Played"].asBoolean());
  item->GetVideoInfoTag()->m_lastPlayed.SetFromW3CDateTime(variant["UserData"]["LastPlayedDate"].asString());
  item->GetVideoInfoTag()->m_resumePoint.timeInSeconds = static_cast<int>(TicksToSeconds(variant["UserData"]["PlaybackPositionTicks"].asUnsignedInteger()));

  GetMediaDetals(*item, variant);

  SetEmbyItemProperties(*item);
  return item;
}

void CEmbyUtils::GetVideoDetails(CFileItem &item, const CVariant &variant)
{
  if (variant.isMember("Genres"))
  {
    // get all genres
    std::vector<std::string> genres;
    const auto& streams = variant["Genres"];
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      genres.push_back(stream.asString());
    }
    item.GetVideoInfoTag()->SetGenre(genres);
  }

  if (variant.isMember("People"))
  {
    std::vector< SActorInfo > roles;
    std::vector<std::string> directors;
    const auto& peeps = variant["People"];
    for (auto peepsIt = peeps.begin_array(); peepsIt != peeps.end_array(); ++peepsIt)
    {
      const auto peep = *peepsIt;
      if (peep["Type"].asString() == "Director")
        directors.push_back(peep["Name"].asString());
      else if (peep["Type"].asString() == "Actor")
      {
        SActorInfo role;
        role.strName = peep["Name"].asString();
        role.strRole = peep["Role"].asString();
        // Items/acae838242b43ad786c2cae52ff412d2/Images/Primary
        std::string urlStr = URIUtils::GetParentPath(item.GetPath());
        if (StringUtils::StartsWithNoCase(urlStr, "emby://"))
          urlStr = Base64::Decode(URIUtils::GetFileName(item.GetPath()));
        CURL url(urlStr);
        url.SetFileName("Items/" + peep["Id"].asString() + "/Images/Primary");
        role.thumb = url.Get();
        roles.push_back(role);
      }
    }

    item.GetVideoInfoTag()->m_cast = roles;
    item.GetVideoInfoTag()->SetDirector(directors);
  }
}

void CEmbyUtils::GetMusicDetails(CFileItem &item, const CVariant &variant)
{
}

void CEmbyUtils::GetMediaDetals(CFileItem &item, const CVariant &variant, std::string id)
{
  if (variant.isMember("MediaStreams") && variant["MediaStreams"].isArray())
  {
    CStreamDetails streamDetail;
    const auto& streams = variant["MediaStreams"];
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      const auto streamType = stream["Type"].asString();
      if (streamType == "Video")
      {
        CStreamDetailVideo* videoStream = new CStreamDetailVideo();
        videoStream->m_strCodec = stream["Codec"].asString();
        videoStream->m_strLanguage = stream["Language"].asString();
        videoStream->m_iWidth = static_cast<int>(stream["Width"].asInteger());
        videoStream->m_iHeight = static_cast<int>(stream["Height"].asInteger());
        videoStream->m_iDuration = item.GetVideoInfoTag()->m_duration;

        streamDetail.AddStream(videoStream);
      }
      else if (streamType == "Audio")
      {
        CStreamDetailAudio* audioStream = new CStreamDetailAudio();
        audioStream->m_strCodec = stream["Codec"].asString();
        audioStream->m_strLanguage = stream["Language"].asString();
        audioStream->m_iChannels = static_cast<int>(stream["Channels"].asInteger());

        streamDetail.AddStream(audioStream);
      }
      else if (streamType == "Subtitle")
      {
        CStreamDetailSubtitle* subtitleStream = new CStreamDetailSubtitle();
        subtitleStream->m_strLanguage = stream["Language"].asString();

        streamDetail.AddStream(subtitleStream);
      }
    }
    item.GetVideoInfoTag()->m_streamDetails = streamDetail;
  }
}

CVariant CEmbyUtils::GetEmbyCVariant(std::string url, std::string filter)
{
#if defined(EMBY_DEBUG_TIMING)
  unsigned int currentTime = XbmcThreads::SystemClockMillis();
#endif

  XFILE::CCurlFile emby;
  //emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  emby.SetRequestHeader("Accept-Encoding", "gzip");

  CURL curl(url);
  // this is key to get back gzip encoded content
  curl.SetProtocolOption("seekable", "0");
  std::string response;
  if (emby.Get(curl.Get(), response))
  {
#if defined(EMBY_DEBUG_TIMING)
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant %d(msec) for %lu bytes",
      XbmcThreads::SystemClockMillis() - currentTime, response.size());
#endif
    if (emby.GetContentEncoding() == "gzip")
    {
      std::string buffer;
      if (XFILE::CZipFile::DecompressGzip(response, buffer))
        response = std::move(buffer);
      else
        return CVariant(CVariant::VariantTypeNull);
    }
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant %s", curl.Get().c_str());
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant %s", response.c_str());
#endif
#if defined(EMBY_DEBUG_TIMING)
    currentTime = XbmcThreads::SystemClockMillis();
#endif
    CVariant resultObject;
    if (CJSONVariantParser::Parse(response, resultObject))
    {
#if defined(EMBY_DEBUG_TIMING)
      CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant parsed in %d(msec)",
        XbmcThreads::SystemClockMillis() - currentTime);
#endif
      // recently added does not return proper object, we make one up later
      if (resultObject.isObject() || resultObject.isArray())
        return resultObject;
    }
  }
  return CVariant(CVariant::VariantTypeNull);
}

void CEmbyUtils::RemoveSubtitleProperties(CFileItem &item)
{
}
