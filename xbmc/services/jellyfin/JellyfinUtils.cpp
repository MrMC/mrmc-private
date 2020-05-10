
/*
 *      Copyright (C) 2020 Team MrMC
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

#include "JellyfinUtils.h"
#include "JellyfinServices.h"
#include "JellyfinViewCache.h"
#include "Application.h"
#include "ContextMenuManager.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/DirectoryCache.h"
#include "filesystem/StackDirectory.h"
#include "network/Network.h"
#include "utils/Base64URL.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/JSONVariantParser.h"
#include "utils/URIUtils.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/ZipFile.h"
#include "settings/Settings.h"
#include "settings/MediaSettings.h"
#include "utils/JobManager.h"

#include "video/VideoInfoTag.h"
#include "video/windows/GUIWindowVideoBase.h"

#include "music/tags/MusicInfoTag.h"
#include "music/dialogs/GUIDialogSongInfo.h"
#include "music/dialogs/GUIDialogMusicInfo.h"
#include "guilib/GUIWindowManager.h"

// new API fields
/*
CriticRating,OfficialRating,CommunityRating,PremiereDate,ProductionYear,DisplayOrder,Video3DFormat,AirDays,AirTime,StartDate,EndDate,Status
*/

static const std::string StandardFields = {
  "DateCreated,PremiereDate,ProductionYear,Genres,MediaStreams,Overview,MediaSources,Path,ImageTags,BackdropImageTags"
};

static const std::string MoviesFields = {
  "DateCreated,PremiereDate,ProductionYear,Genres,MediaStreams,MediaSources,Overview,ShortOverview,Path,ImageTags,BackdropImageTags,RecursiveItemCount,ProviderIds"
};

static const std::string TVShowsFields = {
  "DateCreated,PremiereDate,ProductionYear,Genres,MediaStreams,MediaSources,Overview,ShortOverview,Path,ImageTags,BackdropImageTags,RecursiveItemCount"
};

static const std::string MoviesSetFields = {
  "DateCreated,PremiereDate,ProductionYear,Genres,MediaStreams,MediaSources,Overview,ShortOverview,Path,ImageTags,BackdropImageTags,RecursiveItemCount,ProviderIds,ItemCounts,ParentId"
};

static int g_progressSec = 0;
static CFileItem m_curItem;
// one tick is 0.1 microseconds
static const uint64_t TicksToSecondsFactor = 10000000;

static MediaServicesPlayerState g_playbackState = MediaServicesPlayerState::stopped;

bool CJellyfinUtils::HasClients()
{
  return CJellyfinServices::GetInstance().HasClients();
}

void CJellyfinUtils::GetClientHosts(std::vector<std::string>& hosts)
{
  std::vector<CJellyfinClientPtr> clients;
  CJellyfinServices::GetInstance().GetClients(clients);
  for (const auto &client : clients)
    hosts.push_back(client->GetHost());
}

bool CJellyfinUtils::GetIdentity(CURL url, int timeout)
{
  XFILE::CCurlFile curlfile;
  curlfile.SetTimeout(timeout);
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  curlfile.SetSilent(true);

  CURL curl(url);
  curl.SetFileName(ConstructFileName(curl, "system/info/public"));
  // do not need user/pass for server info
  curl.SetUserName("");
  curl.SetPassword("");
  curl.SetOptions("");

  std::string path = curl.Get();
  std::string response;
  return curlfile.Get(path, response);
}

void CJellyfinUtils::PrepareApiCall(const std::string& userId, const std::string& accessToken, XFILE::CCurlFile &curl)
{
  curl.SetRequestHeader("Accept", "application/json");

  if (!accessToken.empty())
    curl.SetRequestHeader(JellyfinApiKeyHeader, accessToken);

  curl.SetRequestHeader(JellyfinAuthorizationHeader,
    StringUtils::Format("MediaBrowser Client=\"%s\", Device=\"%s\", DeviceId=\"%s\", Version=\"%s\", UserId=\"%s\"",
      CSysInfo::GetAppName().c_str(), CSysInfo::GetDeviceName().c_str(),
      CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID).c_str(),
      CSysInfo::GetVersionShort().c_str(), userId.c_str()));
}

void CJellyfinUtils::SetJellyfinItemProperties(CFileItem &item, const char *content)
{
  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(item.GetPath());
  SetJellyfinItemProperties(item, content, client);
}

void CJellyfinUtils::SetJellyfinItemProperties(CFileItem &item, const char *content, const CJellyfinClientPtr &client)
{
  item.SetProperty("JellyfinItem", true);
  item.SetProperty("MediaServicesItem", true);
  if (!client)
    return;
  if (client->IsCloud())
    item.SetProperty("MediaServicesCloudItem", true);
  item.SetProperty("MediaServicesContent", content);
  item.SetProperty("MediaServicesClientID", client->GetUuid());
  item.SetProperty("SkipLocalArt",true);
}

std::string CJellyfinUtils::ConstructFileName(const CURL url, const std::string fileNamePath, bool useEmbyInPath)
{
  std::string fileName;
  std::string urlFilename = url.GetShareName();
  if (!urlFilename.empty())
    fileName = urlFilename;

  if (useEmbyInPath && !(urlFilename == "emby" && StringUtils::StartsWith(url.GetFileName(), "emby")))
    fileName = fileName + (fileName.empty() ? "emby":"/emby");


  fileName = fileName + (fileName.empty() ? "":"/") + fileNamePath;

  return fileName;
}

uint64_t CJellyfinUtils::TicksToSeconds(uint64_t ticks)
{
  return ticks / TicksToSecondsFactor;
}
uint64_t CJellyfinUtils::SecondsToTicks(uint64_t seconds)
{
  return seconds * TicksToSecondsFactor;
}

#pragma mark - Jellyfin Server Utils
void CJellyfinUtils::SetWatched(CFileItem &item)
{
  // use the current date and time if lastPlayed is invalid
  if (!item.GetVideoInfoTag()->m_lastPlayed.IsValid())
    item.GetVideoInfoTag()->m_lastPlayed = CDateTime::GetUTCDateTime();

  std::string url = item.GetPath();
  if (URIUtils::IsStack(url))
    url = XFILE::CStackDirectory::GetFirstStackedFile(url);
  else
    url = URIUtils::GetParentPath(url);
  if (StringUtils::StartsWithNoCase(url, "jellyfin://"))
    url = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));

  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url);
  if (!client || !client->GetPresence())
    return;

  client->SetWatched(item);
}

void CJellyfinUtils::SetUnWatched(CFileItem &item)
{
  std::string url = item.GetPath();
  if (URIUtils::IsStack(url))
    url = XFILE::CStackDirectory::GetFirstStackedFile(url);
  else
    url = URIUtils::GetParentPath(url);
  if (StringUtils::StartsWithNoCase(url, "jellyfin://"))
    url = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));

  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url);
  if (!client || !client->GetPresence())
    return;

  client->SetUnWatched(item);
}

void CJellyfinUtils::ReportProgress(CFileItem &item, double currentSeconds)
{
  // if we are Jellyfin music, do not report
  if (item.IsAudio())
    return;

  // we get called from Application.cpp every 500ms
  if ((g_playbackState == MediaServicesPlayerState::stopped || g_progressSec <= 0 || g_progressSec > 30))
  {
    g_progressSec = 0;
    
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
        CURL curl1(item.GetPath());
        CURL curl2(URIUtils::GetParentPath(url));
        CURL curl3(curl2.GetWithoutFilename());
        curl3.SetProtocolOptions(curl1.GetProtocolOptions());
        url = curl3.Get();
      }
      if (StringUtils::StartsWithNoCase(url, "jellyfin://"))
        url = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));

      /*
      # Postdata structure to send to Jellyfin server
      url = "{server}/jellyfin/Sessions/Playing"
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

      CURL curl(item.GetPath());
      if (status == "playing" || status == "paused")
      {
        if (g_progressSec < 0)
          // playback started
          curl.SetFileName(ConstructFileName(curl, "Sessions/Playing"));
        else
          curl.SetFileName(ConstructFileName(curl, "Sessions/Playing/Progress"));
      }
      else if (status == "stopped")
        curl.SetFileName(ConstructFileName(curl, "Sessions/Playing/Stopped"));

      std::string id = item.GetMediaServiceId();
      curl.SetOptions("");
      curl.SetOption("QueueableMediaTypes", "Video");
      curl.SetOption("CanSeek", "True");
      curl.SetOption("ItemId", id);
      curl.SetOption("MediaSourceId", id);
      curl.SetOption("PlayMethod", "DirectPlay");
      curl.SetOption("PositionTicks", StringUtils::Format("%llu", SecondsToTicks(currentSeconds)));
      curl.SetOption("IsMuted", "False");
      curl.SetOption("IsPaused", status == "paused" ? "True" : "False");

      std::string data;
      std::string response;
      // execute the POST request
      XFILE::CCurlFile curlfile;
      if (curlfile.Post(curl.Get(), data, response))
      {
#if defined(JELLYFIN_DEBUG_VERBOSE)
        if (!response.empty())
          CLog::Log(LOGDEBUG, "CJellyfinUtils::ReportProgress %s", response.c_str());
#endif
      }
      // mrmc assumes that less than 3 min, we want to start at beginning
      // but jellyfin see stop with zero PositionTicks as a completed play
      // and we will get the item marked watched when we get an jellyfin update msg.
      // so we need to followup and fixup jellyfin server side.
      if (g_playbackState == MediaServicesPlayerState::stopped && currentSeconds <= 0.0)
        CJellyfinUtils::SetUnWatched(item);
    }
  }
  g_progressSec++;
}

void CJellyfinUtils::SetPlayState(MediaServicesPlayerState state)
{
  g_progressSec = -1;
  g_playbackState = state;
}

bool CJellyfinUtils::GetItemSubtiles(CFileItem &item)
{
  return false;
}

bool CJellyfinUtils::GetMoreItemInfo(CFileItem &item)
{
  std::string url = URIUtils::GetParentPath(item.GetPath());
  if (StringUtils::StartsWithNoCase(url, "jellyfin://"))
    url = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));
  
  CURL url2(url);
  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url2.Get());
  if (!client || !client->GetPresence())
    return false;
  
  std::string itemId;
  if (item.HasProperty("JellyfinSeriesID") && !item.GetProperty("JellyfinSeriesID").asString().empty())
    itemId = item.GetProperty("JellyfinSeriesID").asString();
  else
    itemId = item.GetMediaServiceId();
  
  url2.SetFileName(ConstructFileName(url2, "Users/", false) + client->GetUserID() + "/Items");
  url2.SetOptions("");
  url2.SetOption("Fields", "Genres,People");
  url2.SetOption("IDs", itemId);
  const CVariant variant = GetJellyfinCVariant(url2.Get());
  
  GetVideoDetails(item, variant["Items"][0]);
  
  if (item.HasProperty("JellyfinMovieTrailer") && !item.GetProperty("JellyfinMovieTrailer").asString().empty())
  {
    url2.SetFileName(ConstructFileName(url2, item.GetProperty("JellyfinMovieTrailer").asString()));
    url2.SetOptions("");
    const CVariant variant = GetJellyfinCVariant(url2.Get());
    std::string testUrl = "Videos/" + variant[0]["Id"].asString() +"/stream?static=true";
    url2.SetFileName(ConstructFileName(url2, testUrl));
    item.GetVideoInfoTag()->m_strTrailer = url2.Get();
  }

  
  return true;
}

bool CJellyfinUtils::GetMoreResolutions(CFileItem &item)
{
  std::string id = item.GetMediaServiceId();
  std::string url = item.GetVideoInfoTag()->m_strFileNameAndPath;

  if (URIUtils::IsStack(url))
    url = XFILE::CStackDirectory::GetFirstStackedFile(url);
  else
    url = URIUtils::GetParentPath(url);

  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url);
  CURL curl(client->GetUrl());
  curl.SetProtocol(client->GetProtocol());
  curl.SetFileName(ConstructFileName(curl, "Users/", false) + client->GetUserID() + "/Items/" + id);


  CContextButtons choices;
  std::vector<CFileItem> resolutionList;

  RemoveSubtitleProperties(item);
  CVariant variant = GetJellyfinCVariant(curl.Get());
  if (!variant.isNull() && variant.isObject() && variant.isMember("MediaSources"))
  {
//    CFileItemPtr item = ToVideoFileItemPtr(url, objectItem, videoType);
    const CVariant video(variant["MediaSources"]);
    if (!video.isNull())
    {
//      const CVariant variantMedia = makeVariantArrayIfSingleItem(video["Media"]);
      for (auto variantIt = video.begin_array(); variantIt != video.end_array(); ++variantIt)
      {
        if (*variantIt != CVariant::VariantTypeNull)
        {
          CFileItem mediaItem(item);
          GetResolutionDetails(mediaItem, *variantIt);
//          GetMediaDetals(mediaItem, curl, *variantIt);
          resolutionList.push_back(mediaItem);
          choices.Add(resolutionList.size(), mediaItem.GetProperty("JellyfinResolutionChoice").c_str());
        }
      }
    }
    if (resolutionList.size() < 2)
      return true;

    int button = CGUIDialogContextMenu::ShowAndGetChoice(choices);
    if (button > -1)
    {
      m_curItem = resolutionList[button - 1];
      item.UpdateInfo(m_curItem, false);
      item.SetPath(m_curItem.GetPath());
      return true;
    }
  }

  return false;
}

bool CJellyfinUtils::GetURL(CFileItem &item)
{
  return true;
}

bool CJellyfinUtils::SearchJellyfin(CFileItemList &items, std::string strSearchString)
{
  bool rtn = false;
  
  if (CJellyfinServices::GetInstance().HasClients())
  {
    CFileItemList jellyfinItems;
    std::string personID;
    //look through all jellyfin clients and search
    std::vector<CJellyfinClientPtr> clients;
    CJellyfinServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {
      CURL curl(client->GetUrl());
      curl.SetProtocol(client->GetProtocol());
      curl.SetOption("userId", client->GetUserID());
      curl.SetOption("searchTerm", strSearchString);
      curl.SetFileName(ConstructFileName(curl, "Hints"));
      CVariant variant = GetJellyfinCVariant(curl.Get());
      
      personID = variant["SearchHints"][0]["ItemId"].asString();

      if (personID.empty())
        return false;
      
      // get all tvshows with selected actor
      variant.clear();
      curl.SetOptions("");
      curl.SetFileName(ConstructFileName(curl, "Users/") + client->GetUserID() + "/Items");
      curl.SetOption("IncludeItemTypes", "Series");
      curl.SetOption("Fields", TVShowsFields);
      curl.SetOption("Recursive","true");
      curl.SetOption("PersonIds", personID);
      variant = GetJellyfinCVariant(curl.Get());
      
      ParseJellyfinSeries(jellyfinItems, curl, variant);
      CGUIWindowVideoBase::AppendAndClearSearchItems(jellyfinItems, "[" + g_localizeStrings.Get(20343) + "] ", items);
      
      // get all movies with selected actor
      variant.clear();
      curl.SetOption("IncludeItemTypes", "Movie");
      variant = GetJellyfinCVariant(curl.Get());
      ParseJellyfinVideos(jellyfinItems, curl, variant, MediaTypeMovie);
      CGUIWindowVideoBase::AppendAndClearSearchItems(jellyfinItems, "[" + g_localizeStrings.Get(20338) + "] ", items);
    }
    rtn = items.Size() > 0;
  }
  return rtn;
}

bool CJellyfinUtils::DeleteJellyfinMedia(CFileItem &item)
{
  CURL curl(item.GetURL());
  curl.SetFileName(ConstructFileName(curl, "Items/") + item.GetMediaServiceId());
  curl.SetOptions("");
  std::string response;
  std::string data;
  XFILE::CCurlFile jellyfin;
  jellyfin.Delete(curl.Get(), data, response);
  return true;
}

#pragma mark - Jellyfin Recently Added and InProgress
bool CJellyfinUtils::GetJellyfinRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);

  url2.SetFileName(url2.GetFileName() + "/Latest");
  url2.SetOption("IncludeItemTypes", JellyfinTypeEpisode);
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  url2.SetOption("Fields", StandardFields);
  CVariant variant = GetJellyfinCVariant(url2.Get());

  std::map<std::string, CVariant> variantMap;
  variantMap["Items"] = variant;
  variant = CVariant(variantMap);

  bool rtn = ParseJellyfinVideos(items, url2, variant, MediaTypeEpisode);
  return rtn;
}

bool CJellyfinUtils::GetJellyfinInProgressShows(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", JellyfinTypeEpisode);
  url2.SetOption("SortBy", "DatePlayed");
  url2.SetOption("SortOrder", "Descending");
  url2.SetOption("Filters", "IsResumable");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("Recursive", "true");
  url2.SetOption("Fields", StandardFields);
  CVariant result = GetJellyfinCVariant(url2.Get());

  bool rtn = ParseJellyfinVideos(items, url2, result, MediaTypeEpisode);
  return rtn;
}

bool CJellyfinUtils::GetJellyfinRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);
  url2.SetFileName(url2.GetFileName() + "/Latest");
  url2.SetOption("IncludeItemTypes", JellyfinTypeMovie);
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  url2.SetOption("Fields", MoviesFields);
  CVariant variant = GetJellyfinCVariant(url2.Get());

  std::map<std::string, CVariant> variantMap;
  variantMap["Items"] = variant;
  variant = CVariant(variantMap);

  bool rtn = ParseJellyfinVideos(items, url2, variant, MediaTypeMovie);
  return rtn;
}

bool CJellyfinUtils::GetJellyfinInProgressMovies(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);

  url2.SetOption("IncludeItemTypes", JellyfinTypeMovie);
  url2.SetOption("SortBy", "DatePlayed");
  url2.SetOption("SortOrder", "Descending");
  url2.SetOption("Filters", "IsResumable");
  url2.SetOption("Limit", StringUtils::Format("%i",limit));
  url2.SetOption("GroupItems", "False");
  url2.SetOption("Fields", MoviesFields);
  url2.SetOption("Recursive", "true");
  CVariant result = GetJellyfinCVariant(url2.Get());

  bool rtn = ParseJellyfinVideos(items, url2, result, MediaTypeMovie);
  return rtn;
}

bool CJellyfinUtils::GetAllJellyfinInProgress(CFileItemList &items, bool tvShow)
{
  bool rtn = false;

  if (CJellyfinServices::GetInstance().HasClients())
  {
    CFileItemList jellyfinItems;
//    int limitTo = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_JELLYFINLIMITHOMETO);
//    if (limitTo < 2)
//      return false;
    //look through all jellyfin clients and pull in progress for each library section
    std::vector<CJellyfinClientPtr> clients;
    CJellyfinServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {
//      if (limitTo == 2 && !client->IsOwned())
//        continue;

      std::vector<JellyfinViewInfo> viewinfos;
      if (tvShow)
        viewinfos = client->GetViewInfoForTVShowContent();
      else
        viewinfos = client->GetViewInfoForMovieContent();
      for (const auto &viewinfo : viewinfos)
      {
        std::string userId = client->GetUserID();
        CURL curl(client->GetUrl());
        curl.SetProtocol(client->GetProtocol());
        curl.SetOption("ParentId", viewinfo.id);
        curl.SetFileName(ConstructFileName(curl, "Users/") + userId + "/Items");

        if (tvShow)
          rtn = GetJellyfinInProgressShows(jellyfinItems, curl.Get(), 10);
        else
          rtn = GetJellyfinInProgressMovies(jellyfinItems, curl.Get(), 10);

        items.Append(jellyfinItems);
        jellyfinItems.ClearItems();
      }
    }
  }
  return rtn;
}

bool CJellyfinUtils::GetAllJellyfinRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow)
{
  bool rtn = false;

  if (CJellyfinServices::GetInstance().HasClients())
  {
    CFileItemList jellyfinItems;
    //look through all jellyfin clients and pull recently added for each library section
    std::vector<CJellyfinClientPtr> clients;
    CJellyfinServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {
      std::vector<JellyfinViewInfo> contents;
      if (tvShow)
        contents = client->GetViewInfoForTVShowContent();
      else
        contents = client->GetViewInfoForMovieContent();
      for (const auto &content : contents)
      {
        std::string userId = client->GetUserID();
        CURL curl(client->GetUrl());
        curl.SetProtocol(client->GetProtocol());
        curl.SetOption("ParentId", content.id);
        curl.SetFileName(ConstructFileName(curl, "Users/") + userId + "/Items");

        if (tvShow)
          rtn = GetJellyfinRecentlyAddedEpisodes(jellyfinItems, curl.Get(), 10);
        else
          rtn = GetJellyfinRecentlyAddedMovies(jellyfinItems, curl.Get(), 10);

        items.Append(jellyfinItems);
        jellyfinItems.ClearItems();
      }
    }
  }
  return rtn;
}

bool CJellyfinUtils::GetAllJellyfinRecentlyAddedAlbums(CFileItemList &items,int limit)
{
  
  bool rtn = false;
  
  if (CJellyfinServices::GetInstance().HasClients())
  {
    CFileItemList jellyfinItems;
    //look through all jellyfin clients and pull recently added for each library section
    std::vector<CJellyfinClientPtr> clients;
    CJellyfinServices::GetInstance().GetClients(clients);
    for (const auto &client : clients)
    {      
      std::vector<JellyfinViewInfo> viewinfos;
      viewinfos = client->GetViewInfoForMusicContent();
      for (const auto &viewinfo : viewinfos)
      {
        std::string userId = client->GetUserID();
        CURL curl(client->GetUrl());
        curl.SetProtocol(client->GetProtocol());
        curl.SetOption("ParentId", viewinfo.id);
        curl.SetFileName(ConstructFileName(curl, "Users/") + userId + "/Items/Latest");
        
        rtn = GetJellyfinAlbum(jellyfinItems, curl.Get(), 10);
        
        items.Append(jellyfinItems);
        jellyfinItems.ClearItems();
      }
    }
  }
  return rtn;
}

bool CJellyfinUtils::GetJellyfinRecentlyAddedAlbums(CFileItemList &items, const std::string url, int limit)
{
  bool rtn = false;
  if (CJellyfinServices::GetInstance().HasClients())
  {
    CFileItemList jellyfinItems;

    CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url);
    std::vector<JellyfinViewInfo> viewinfos;
    viewinfos = client->GetViewInfoForMusicContent();
    for (const auto &viewinfo : viewinfos)
    {
      std::string userId = client->GetUserID();
      CURL curl(client->GetUrl());
      curl.SetProtocol(client->GetProtocol());
      curl.SetOption("ParentId", viewinfo.id);
      curl.SetFileName(ConstructFileName(curl, "Users/") + userId + "/Items/Latest");
      
      rtn = GetJellyfinAlbum(jellyfinItems, curl.Get(), limit);
      
      items.Append(jellyfinItems);
//      items.Sort(SortByDateAdded, SortOrderDescending);
//      items.SetProperty("library.filter", "true");
      items.GetMusicInfoTag()->m_type = MediaTypeAlbum;
      jellyfinItems.ClearItems();
    }
  }
  return rtn;
}

bool CJellyfinUtils::GetJellyfinNextUp(CFileItemList &items, const std::string url)
{
  bool rtn = false;
  if (CJellyfinServices::GetInstance().HasClients())
  {
    CURL url2(url);
    url2.SetOption("Fields", TVShowsFields);
    CVariant variant = GetJellyfinCVariant(url2.Get());

    rtn = ParseJellyfinVideos(items, url2, variant, "");
  }
  return rtn;
}


#pragma mark - Jellyfin Set
bool CJellyfinUtils::GetJellyfinSet(CFileItemList &items, const std::string url)
{
  CURL url1(url);
  std::string setName = url1.GetOption("SetName");
  url1.SetOption("Fields", MoviesSetFields);
  CVariant variant = GetJellyfinCVariant(url1.Get());
  
  bool rtn = ParseJellyfinVideos(items, url1, variant, MediaTypeMovie);
  
  items.SetLabel(setName);
  return rtn;
}

#pragma mark - Jellyfin TV
bool CJellyfinUtils::GetJellyfinSeasons(CFileItemList &items, const std::string url)
{
  bool rtn = false;
  
  CURL url2(url);
  url2.SetOption("IncludeItemTypes", JellyfinTypeSeasons);
  url2.SetOption("Fields", "Etag,DateCreated,PremiereDate,ProductionYear,ImageTags,RecursiveItemCount");
  std::string parentId = url2.GetOption("ParentId");
  url2.SetOptions("");
  url2.SetOption("ParentId", parentId);
  
  const CVariant variant = GetJellyfinCVariant(url2.Get());
  
  if (!variant.isNull() || variant.isObject() || variant.isMember("Items"))
  {
    CURL url3(url);
    std::string seriesID = url3.GetOption("ParentId");
    url3.SetOptions("");
    url3.SetOption("Ids", seriesID);
    url3.SetOption("Fields", "Overview,Genres,DateCreated,PremiereDate,ProductionYear");
    const CVariant seriesObject = CJellyfinUtils::GetJellyfinCVariant(url3.Get());
    
    rtn = ParseJellyfinSeasons(items, url2, seriesObject, variant);
  }
  return rtn;
}

bool CJellyfinUtils::GetJellyfinEpisodes(CFileItemList &items, const std::string url)
{
  CURL url2(url);
  
  url2.SetOption("IncludeItemTypes", JellyfinTypeEpisode);
  url2.SetOption("Fields", StandardFields);
  const CVariant variant = GetJellyfinCVariant(url2.Get());
  
  bool rtn = ParseJellyfinVideos(items, url2, variant, MediaTypeEpisode);
  return rtn;
}

#pragma mark - Jellyfin Music
CFileItemPtr ParseMusic(const CJellyfinClient *client, const CVariant &variant)
{
  return nullptr;
}

bool CJellyfinUtils::GetJellyfinAlbum(CFileItemList &items, std::string url, int limit)
{
  CURL curl(url);
  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(curl.Get());
  if (!client || !client->GetPresence())
    return false;
  
  curl.SetOption("IncludeItemTypes", JellyfinTypeAudio);
  curl.SetOption("Limit", StringUtils::Format("%i",limit));
  curl.SetOption("Fields", "BasicSyncInfo");
  CVariant variant = GetJellyfinCVariant(curl.Get());
  
  if(!variant.isMember("Items"))
  {
    std::map<std::string, CVariant> variantMap;
    variantMap["Items"] = variant;
    variant = CVariant(variantMap);
  }

  curl.SetFileName(ConstructFileName(curl, "Users/") + client->GetUserID() + "/Items");
  bool rtn = ParseJellyfinAlbum(items, curl, variant);
  return rtn;

}

bool CJellyfinUtils::GetJellyfinArtistAlbum(CFileItemList &items, std::string url)
{
  CURL curl(url);
  CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(curl.Get());
  if (!client || !client->GetPresence())
    return false;
  
  curl.SetOptions("");
  curl.SetOption("Recursive", "true");
  curl.SetOption("Fields", "Etag,Genres,DateCreated,PremiereDate,ProductionYear,");
  curl.SetOption("IncludeItemTypes", JellyfinTypeMusicAlbum);
  curl.SetOption("ArtistIds", curl.GetProtocolOption("ArtistIds"));
  curl.SetFileName(ConstructFileName(curl, "Users/") + client->GetUserID() + "/Items");
  const CVariant variant = GetJellyfinCVariant(curl.Get());

  bool rtn = ParseJellyfinAlbum(items, curl, variant);
  return rtn;
}

bool CJellyfinUtils::GetJellyfinSongs(CFileItemList &items, std::string url)
{
  return false;
}

bool CJellyfinUtils::GetJellyfinAlbumSongs(CFileItemList &items, std::string url)
{
  CURL curl(url);
  curl.SetOption("Fields", "Etag,DateCreated,PremiereDate,ProductionYear,MediaStreams,ItemCounts,Genres");
  const CVariant variant = GetJellyfinCVariant(curl.Get());

  bool rtn = ParseJellyfinAudio(items, curl, variant);
  return rtn;
}

bool CJellyfinUtils::ShowMusicInfo(CFileItem item)
{
  std::string type = item.GetMusicInfoTag()->m_type;
  if (type == MediaTypeSong)
  {
    CGUIDialogSongInfo *dialog = (CGUIDialogSongInfo *)g_windowManager.GetWindow(WINDOW_DIALOG_SONG_INFO);
    if (dialog)
    {
      dialog->SetSong(&item);
      dialog->Open();
    }
  }
  else if (type == MediaTypeAlbum)
  {
    CGUIDialogMusicInfo *pDlgAlbumInfo = (CGUIDialogMusicInfo*)g_windowManager.GetWindow(WINDOW_DIALOG_MUSIC_INFO);
    if (pDlgAlbumInfo)
    {
      pDlgAlbumInfo->SetAlbum(item);
      pDlgAlbumInfo->Open();
    }
  }
  else if (type == MediaTypeArtist)
  {
    CGUIDialogMusicInfo *pDlgArtistInfo = (CGUIDialogMusicInfo*)g_windowManager.GetWindow(WINDOW_DIALOG_MUSIC_INFO);
    if (pDlgArtistInfo)
    {
      pDlgArtistInfo->SetArtist(item);
      pDlgArtistInfo->Open();
    }
  }
  return true;
}

bool CJellyfinUtils::GetJellyfinAlbumSongs(CFileItem item, CFileItemList &items)
{
  std::string url = URIUtils::GetParentPath(item.GetPath());
  if (StringUtils::StartsWithNoCase(url, "jellyfin://"))
    url = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));
  
  return GetJellyfinAlbumSongs(items, url);
}

bool CJellyfinUtils::GetJellyfinMediaTotals(MediaServicesMediaCount &totals)
{
  return false;
}

#pragma mark - Jellyfin parsers
CFileItemPtr CJellyfinUtils::ToFileItemPtr(CJellyfinClient *client, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ToFileItemPtr cvariant is empty");
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
    url2.SetFileName(ConstructFileName(url2, "Users/") + client->GetUserID() + "/Items");

    if (type == JellyfinTypeMovie)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeMovie: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinVideos(items, url2, variant, MediaTypeMovie);
    }
    else if (type == JellyfinTypeSeries)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeSeries: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinSeries(items, url2, variant);
    }
    else if (type == JellyfinTypeSeason)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeSeason: %s",
        variantItem["Name"].asString().c_str());
#endif
      CURL url3(url2);
      std::string seriesID = variantItem["ParentId"].asString();
      url3.SetOptions("");
      url3.SetOption("Ids", seriesID);
      url3.SetOption("Fields", "Overview,Genres,DateCreated,PremiereDate,ProductionYear");
      const CVariant seriesObject = CJellyfinUtils::GetJellyfinCVariant(url3.Get());
      ParseJellyfinSeasons(items, url2, seriesObject, variant);
    }
    else if (type == JellyfinTypeEpisode)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeEpisode: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinVideos(items, url2, variant, MediaTypeEpisode);
    }
    else if (type == JellyfinTypeAudio)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeAudio: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinAudio(items, url2, variant);
    }
    else if (type == JellyfinTypeMusicAlbum)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeMusicAlbum: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinAlbum(items, url2, variant);
    }
    else if (type == JellyfinTypeMusicArtist)
    {
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr JellyfinTypeMusicArtist: %s",
        variantItem["Name"].asString().c_str());
#endif
      ParseJellyfinArtists(items, url2, variant);
    }
    else if (type == JellyfinTypeFolder)
    {
      // ignore these, useless info
    }
    else
    {
      CLog::Log(LOGDEBUG, "CJellyfinUtils::ToFileItemPtr unknown type: %s with name %s",
        type.c_str(), variantItem["Name"].asString().c_str());
    }

    return items[0];
  }

  return nullptr;
}

bool CJellyfinUtils::ParseJellyfinVideos(CFileItemList &items, CURL url, const CVariant &variant, std::string type)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinVideos invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

#if defined(JELLYFIN_DEBUG_TIMING)
  unsigned int currentTime = XbmcThreads::SystemClockMillis();
#endif
  bool rtn = false;
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto objectItem = *variantItemIt;
    rtn = true;

    // ignore raw blueray rips, these are designed to be
    // direct played (ie via mounted filesystem)
    // and we do not do that yet.
    if (objectItem["VideoType"].asString() == "BluRay")
      continue;
    if (objectItem["VideoType"].asString() == "Dvd")
      continue;
    if (objectItem["IsFolder"].asBoolean())
      continue;

    std::string videoType = type;
    if (videoType.empty())
    {
      videoType = objectItem["Type"].asString();
      StringUtils::ToLower(videoType);
    }
    CFileItemPtr item = ToVideoFileItemPtr(url, objectItem, videoType);
    items.Add(item);
  }
  // this is needed to display movies/episodes properly ... dont ask
  // good thing it didnt take 2 days to figure it out
  items.SetLabel(variantItems[0]["SeasonName"].asString());
//  items.SetProperty("library.filter", "true");
  if (type == MediaTypeTvShow)
    SetJellyfinItemProperties(items, "episodes");
  else
    SetJellyfinItemProperties(items, "movies");

#if defined(JELLYFIN_DEBUG_TIMING)
  int delta = XbmcThreads::SystemClockMillis() - currentTime;
  if (delta > 1)
  {
    CLog::Log(LOGDEBUG, "CJellyfinUtils::GetVideoItems %d(msec) for %d items",
      XbmcThreads::SystemClockMillis() - currentTime, variantItems.size());
  }
#endif
  return rtn;
}

bool CJellyfinUtils::ParseJellyfinSeries(CFileItemList &items, const CURL &url, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinSeries invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  bool rtn = false;
  std::string imagePath;

  if (!variant.isNull() || variant.isObject() || variant.isMember("Items"))
  {
    const auto& variantItems = variant["Items"];
    for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
    {
      if (*variantItemIt == CVariant::VariantTypeNull)
        continue;

      const auto item = *variantItemIt;
      rtn = true;

      // local vars for common fields
      std::string itemId = item["Id"].asString();
      std::string seriesId = item["SeriesId"].asString();
      // clear url options
      CURL curl(url);
      curl.SetOption("ParentId", itemId);

      CFileItemPtr newItem(new CFileItem());
      // set m_bIsFolder to true to indicate we are series list
      newItem->m_bIsFolder = true;

      std::string title = item["Name"].asString();
      newItem->SetLabel(title);

      CDateTime premiereDate;
      premiereDate.SetFromW3CDateTime(item["PremiereDate"].asString());
      newItem->m_dateTime = premiereDate;

      newItem->SetPath("jellyfin://tvshows/shows/" + Base64URL::Encode(curl.Get()));
      newItem->SetMediaServiceId(itemId);
      newItem->SetMediaServiceFile(item["Path"].asString());

      curl.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Primary");
      imagePath = curl.Get();
      newItem->SetArt("thumb", imagePath);
      newItem->SetIconImage(imagePath);

      curl.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Banner");
      imagePath = curl.Get();
      newItem->SetArt("banner", imagePath);

      curl.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Backdrop");
      imagePath = curl.Get();
      newItem->SetArt("fanart", imagePath);

      newItem->GetVideoInfoTag()->m_playCount = static_cast<int>(item["UserData"]["PlayCount"].asInteger());
      newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, newItem->GetVideoInfoTag()->m_playCount > 0);

      newItem->GetVideoInfoTag()->m_strTitle = title;
      newItem->GetVideoInfoTag()->m_strStatus = item["Status"].asString();

      newItem->GetVideoInfoTag()->m_type = MediaTypeTvShow;
      newItem->GetVideoInfoTag()->m_strFileNameAndPath = newItem->GetPath();
      newItem->GetVideoInfoTag()->SetSortTitle(title);
      newItem->GetVideoInfoTag()->SetOriginalTitle(title);
      //newItem->GetVideoInfoTag()->SetSortTitle(item["SortName"].asString());
      //newItem->GetVideoInfoTag()->SetOriginalTitle(item["OriginalTitle"].asString());
      newItem->SetProperty("JellyfinSeriesID", seriesId);
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
      SetJellyfinItemProperties(*newItem, "tvshows");
      items.Add(newItem);
    }
    // this is needed to display movies/episodes properly ... dont ask
    // good thing it didnt take 2 days to figure it out
//    items.SetProperty("library.filter", "true");
    items.SetCacheToDisc(CFileItemList::CACHE_NEVER);
    SetJellyfinItemProperties(items, "tvshows");
  }
  return rtn;
}

bool CJellyfinUtils::ParseJellyfinSeasons(CFileItemList &items, const CURL &url, const CVariant &series, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject() || series.isNull() || !series.isObject())
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinSeasons invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  bool rtn = false;
  std::string imagePath;
  std::string seriesName;
  std::string seriesId;
  const auto& seriesItem = series["Items"][0];

  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();
    seriesId = item["SeriesId"].asString();
    // clear url options
    CURL curl(url);
    CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(curl.Get());
    curl.SetOptions("");
    curl.SetFileName(ConstructFileName(curl, "Shows/") + seriesId + "/Episodes");
    curl.SetOption("seasonId", itemId);
    curl.SetOption("userId",client->GetUserID());
    CFileItemPtr newItem(new CFileItem());
    // set m_bIsFolder to true to indicate we are seasons list
    newItem->m_bIsFolder = true;

    newItem->SetLabel(item["Name"].asString());
    newItem->SetPath("jellyfin://tvshows/seasons/" + Base64URL::Encode(curl.Get()));
    newItem->SetMediaServiceId(itemId);
    newItem->SetMediaServiceFile(item["Path"].asString());

    if (item.isMember("ImageTags") && item["ImageTags"].isMember("Primary"))
      curl.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Primary");
    else
      curl.SetFileName(ConstructFileName(curl, "Items/") + seriesId + "/Images/Primary");
    imagePath = curl.Get();
    newItem->SetArt("thumb", imagePath);
    newItem->SetIconImage(imagePath);

    curl.SetFileName(ConstructFileName(curl, "Items/") + seriesId + "/Images/Banner");
    imagePath = curl.Get();
    newItem->SetArt("banner", imagePath);
    curl.SetFileName(ConstructFileName(curl, "Items/") + seriesId + "/Images/Backdrop");
    newItem->SetArt("fanart", imagePath);

    newItem->GetVideoInfoTag()->m_type = MediaTypeSeason;
    newItem->GetVideoInfoTag()->m_strTitle = item["Name"].asString();
    // we get these from rootXmlNode, where all show info is
    seriesName = item["SeriesName"].asString();
    newItem->GetVideoInfoTag()->m_strShowTitle = seriesName;
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
    newItem->SetProperty("JellyfinSeriesID", seriesId);

    int totalEpisodes = item["RecursiveItemCount"].asInteger();
    int unWatchedEpisodes = item["UserData"]["UnplayedItemCount"].asInteger();
    int watchedEpisodes = totalEpisodes - unWatchedEpisodes;
    int iSeason = item["IndexNumber"].asInteger();
    newItem->GetVideoInfoTag()->m_iSeason = iSeason;
    newItem->GetVideoInfoTag()->m_iEpisode = totalEpisodes;
    newItem->GetVideoInfoTag()->m_playCount = (totalEpisodes == watchedEpisodes) ? 1 : 0;

    newItem->SetProperty("totalepisodes", totalEpisodes);
    newItem->SetProperty("numepisodes", totalEpisodes);
    newItem->SetProperty("watchedepisodes", watchedEpisodes);
    newItem->SetProperty("unwatchedepisodes", unWatchedEpisodes);

    newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, (newItem->GetVideoInfoTag()->m_playCount > 0) && (newItem->GetVideoInfoTag()->m_iEpisode > 0));

    SetJellyfinItemProperties(*newItem, "seasons");
    items.Add(newItem);
  }
  
  SetJellyfinItemProperties(items, "seasons");
  items.SetProperty("showplot", seriesItem["Overview"].asString());
  
  if (!items.IsEmpty())
  {
    int iFlatten = CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOLIBRARY_FLATTENTVSHOWS);
    int itemsSize = items.GetObjectCount();
    int firstIndex = items.Size() - itemsSize;
    // check if the last item is the "All seasons" item which should be ignored for flattening
    if (!items[items.Size() - 1]->HasVideoInfoTag() || items[items.Size() - 1]->GetVideoInfoTag()->m_iSeason < 0)
      itemsSize -= 1;
    
    bool bFlatten = (itemsSize == 1 && iFlatten == 1) || iFlatten == 2 ||                              // flatten if one one season or if always flatten is enabled
    (itemsSize == 2 && iFlatten == 1 &&                                                // flatten if one season + specials
     (items[firstIndex]->GetVideoInfoTag()->m_iSeason == 0 || items[firstIndex + 1]->GetVideoInfoTag()->m_iSeason == 0));
    
    if (iFlatten > 0 && !bFlatten && (WatchedMode)CMediaSettings::GetInstance().GetWatchedMode("tvshows") == WatchedModeUnwatched)
    {
      int count = 0;
      for(int i = 0; i < items.Size(); i++)
      {
        const CFileItemPtr item = items.Get(i);
        if (item->GetProperty("unwatchedepisodes").asInteger() != 0 && item->GetVideoInfoTag()->m_iSeason > 0)
          count++;
      }
      bFlatten = (count < 2); // flatten if there is only 1 unwatched season (not counting specials)
    }
    
    if (bFlatten)
    { // flatten if one season or flatten always
      CFileItemList tempItems;
      
      // clear url options
      CURL curl(url);
      curl.SetOptions("");
      curl.SetOption("ParentId", seriesId);
      curl.SetOption("Recursive", "true");
      
      XFILE::CDirectory::GetDirectory("jellyfin://tvshows/seasons/" + Base64URL::Encode(curl.Get()),tempItems);
      
      items.Clear();
      items.Assign(tempItems);
    }
  }
  items.SetLabel(seriesName);
//  items.SetProperty("library.filter", "true");
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);

  return rtn;
}

bool CJellyfinUtils::ParseJellyfinAudio(CFileItemList &items, const CURL &url, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject())
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinAudio invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  // clear base url options
  CURL curl(url);
  curl.SetOptions("");
  std::string imagePath;

  bool rtn = false;
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();
    std::string albumId = item["AlbumId"].asString();

    CFileItemPtr jellyfinItem(new CFileItem());
    jellyfinItem->SetLabel(item["Name"].asString());
    curl.SetFileName(ConstructFileName(curl, "Audio/") + itemId +"/stream?static=true");
    jellyfinItem->SetPath(curl.Get());
    jellyfinItem->SetMediaServiceId(itemId);
    jellyfinItem->SetProperty("JellyfinSongKey", itemId);
    jellyfinItem->GetMusicInfoTag()->m_type = MediaTypeSong;
    jellyfinItem->GetMusicInfoTag()->SetTitle(item["Name"].asString());
    jellyfinItem->GetMusicInfoTag()->SetAlbum(item["Album"].asString());
    jellyfinItem->GetMusicInfoTag()->SetYear(item["ProductionYear"].asInteger());
    jellyfinItem->GetMusicInfoTag()->SetTrackNumber(item["IndexNumber"].asInteger());
    jellyfinItem->GetMusicInfoTag()->SetDuration(TicksToSeconds(variant["RunTimeTicks"].asInteger()));

    curl.SetFileName(ConstructFileName(curl, "Items/") + albumId + "/Images/Primary");
    imagePath = curl.Get();
    jellyfinItem->SetArt("thumb", imagePath);
    jellyfinItem->SetProperty("thumb", imagePath);

    curl.SetFileName(ConstructFileName(curl, "Items/") + albumId + "/Images/Backdrop");
    imagePath = curl.Get();
    jellyfinItem->SetArt("fanart", imagePath);
    jellyfinItem->SetProperty("fanart", imagePath);

    GetMusicDetails(*jellyfinItem, item);

    jellyfinItem->GetMusicInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
    jellyfinItem->GetMusicInfoTag()->m_lastPlayed.SetFromW3CDateTime(item["LastPlayedDate"].asString());
    jellyfinItem->GetMusicInfoTag()->SetLoaded(true);
    SetJellyfinItemProperties(*jellyfinItem, MediaTypeSong);
    items.Add(jellyfinItem);
  }
//  items.SetProperty("library.filter", "true");
  items.GetMusicInfoTag()->m_type = MediaTypeSong;
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);
  SetJellyfinItemProperties(items, MediaTypeSong);

  return rtn;
}

bool CJellyfinUtils::ParseJellyfinAlbum(CFileItemList &items, const CURL &url, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinAlbum invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  // clear base url options
  CURL curl(url);
  curl.SetOptions("");
  std::string imagePath;

  bool rtn = false;
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();

    CFileItemPtr jellyfinItem(new CFileItem());
    // set m_bIsFolder to true to indicate we are artist list

    jellyfinItem->m_bIsFolder = true;
    jellyfinItem->SetLabel(item["Name"].asString());
    curl.SetOption("ParentId", itemId);
    jellyfinItem->SetPath("jellyfin://music/albumsongs/" + Base64URL::Encode(curl.Get()));
    jellyfinItem->SetMediaServiceId(itemId);

    jellyfinItem->GetMusicInfoTag()->m_type = MediaTypeAlbum;
    jellyfinItem->GetMusicInfoTag()->SetTitle(item["Name"].asString());

    jellyfinItem->GetMusicInfoTag()->SetArtistDesc(item["ArtistItems"]["Name"].asString());
    jellyfinItem->SetProperty("artist", item["ArtistItems"]["Name"].asString());
    jellyfinItem->SetProperty("JellyfinAlbumKey", item["Id"].asString());

    jellyfinItem->GetMusicInfoTag()->SetAlbum(item["Name"].asString());
    jellyfinItem->GetMusicInfoTag()->SetYear(item["ProductionYear"].asInteger());
    
    CURL curl2(url);
    curl2.SetOptions("");
    curl2.RemoveProtocolOption("ArtistIds");
    curl2.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Primary");
    imagePath = curl2.Get();
    jellyfinItem->SetArt("thumb", imagePath);
    jellyfinItem->SetProperty("thumb", imagePath);

    curl2.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Backdrop");
    imagePath = curl2.Get();
    jellyfinItem->SetArt("fanart", imagePath);
    jellyfinItem->SetProperty("fanart", imagePath);

    jellyfinItem->GetMusicInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
    jellyfinItem->GetMusicInfoTag()->m_lastPlayed.SetFromW3CDateTime(item["LastPlayedDate"].asString());

    GetMusicDetails(*jellyfinItem, item);
    SetJellyfinItemProperties(*jellyfinItem, MediaTypeAlbum);
    items.Add(jellyfinItem);
  }
//  items.SetProperty("library.filter", "true");
  items.GetMusicInfoTag()->m_type = MediaTypeAlbum;
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);
  SetJellyfinItemProperties(items, MediaTypeAlbum);

  return rtn;
}

bool CJellyfinUtils::ParseJellyfinArtists(CFileItemList &items, const CURL &url, const CVariant &variant)
{
  if (variant.isNull() || !variant.isObject())
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinArtists invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  // clear base url options
  CURL curl(url);
  curl.SetOptions("");
  std::string imagePath;

  bool rtn = false;
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();

    CFileItemPtr jellyfinItem(new CFileItem());
    // set m_bIsFolder to true to indicate we are artist list

    jellyfinItem->m_bIsFolder = true;
    jellyfinItem->SetLabel(item["Name"].asString());
    curl.SetProtocolOption("ArtistIds", itemId);
    curl.SetFileName(ConstructFileName(curl, "Items"));
    jellyfinItem->SetPath("jellyfin://music/artistalbums/" + Base64URL::Encode(curl.Get()));
    jellyfinItem->SetMediaServiceId(itemId);

    jellyfinItem->GetMusicInfoTag()->m_type = MediaTypeArtist;
    jellyfinItem->GetMusicInfoTag()->SetTitle(item["Name"].asString());

    jellyfinItem->GetMusicInfoTag()->SetYear(item["ProductionYear"].asInteger());

    CURL curl2(url);
    curl2.SetOptions("");
    curl2.RemoveProtocolOption("ArtistIds");
    curl2.SetFileName(ConstructFileName(curl, "Items/") + item["Id"].asString() + "/Images/Primary");
    imagePath = curl2.Get();
    jellyfinItem->SetArt("thumb", imagePath);
    jellyfinItem->SetProperty("thumb", imagePath);

    curl2.SetFileName(ConstructFileName(curl, "Items/") + itemId + "/Images/Backdrop");
    imagePath = curl2.Get();
    jellyfinItem->SetArt("fanart", imagePath);
    jellyfinItem->SetProperty("fanart", imagePath);

    jellyfinItem->GetMusicInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
    jellyfinItem->GetMusicInfoTag()->m_lastPlayed.SetFromW3CDateTime(item["LastPlayedDate"].asString());

    GetMusicDetails(*jellyfinItem, item);

    SetJellyfinItemProperties(*jellyfinItem, MediaTypeArtist);
    items.Add(jellyfinItem);
  }
//  items.SetProperty("library.filter", "true");
  items.GetMusicInfoTag()->m_type = MediaTypeArtist;
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);
  SetJellyfinItemProperties(items, MediaTypeArtist);

  return rtn;
}

bool CJellyfinUtils::ParseJellyfinMoviesFilter(CFileItemList &items, CURL url, const CVariant &variant, const std::string &filter)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinMoviesFilter invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  bool rtn = false;
  CURL curl(url);
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();
    std::string itemName = item["Name"].asString();

    CFileItemPtr newItem(new CFileItem());
    newItem->m_bIsFolder = true;
    newItem->m_bIsShareOrDrive = false;

    CURL curl1(url);
    curl1.SetOption("Fields", "DateCreated,PremiereDate,ProductionYear,Genres,MediaStreams,Overview,Path,ProviderIds");
    if (filter == "Genres")
      curl1.SetOption("Genres", itemName);
    else if (filter == "Years")
      curl1.SetOption("Years", itemName);
    else if (filter == "Collections")
      curl1.SetOption("ParentId", itemId);

    newItem->SetPath("jellyfin://movies/filter/" + Base64URL::Encode(curl1.Get()));
    newItem->SetLabel(itemName);
    newItem->SetProperty("SkipLocalArt", true);
    items.Add(newItem);
  }
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);

  return rtn;
}

bool CJellyfinUtils::ParseJellyfinTVShowsFilter(CFileItemList &items, const CURL url, const CVariant &variant, const std::string &filter)
{
  if (variant.isNull() || !variant.isObject() || !variant.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CJellyfinUtils::ParseJellyfinTVShowsFilter invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  bool rtn = false;
  CURL curl1(url);
  const auto& variantItems = variant["Items"];
  for (auto variantItemIt = variantItems.begin_array(); variantItemIt != variantItems.end_array(); ++variantItemIt)
  {
    if (*variantItemIt == CVariant::VariantTypeNull)
      continue;

    const auto item = *variantItemIt;
    rtn = true;

    // local vars for common fields
    std::string itemId = item["Id"].asString();
    std::string itemName = item["Name"].asString();

    CFileItemPtr newItem(new CFileItem());
    newItem->m_bIsFolder = true;
    newItem->m_bIsShareOrDrive = false;

    if (filter == "Genres")
      curl1.SetOption("Genres", itemName);
    else if (filter == "Years")
      curl1.SetOption("Years", itemName);
    else if (filter == "Collections")
      curl1.SetOption("ParentId", itemId);

    newItem->SetPath("jellyfin://tvshows/filter/" + Base64URL::Encode(curl1.Get()));
    newItem->SetLabel(itemName);
    newItem->SetProperty("SkipLocalArt", true);
    items.Add(newItem);
  }
  items.SetCacheToDisc(CFileItemList::CACHE_NEVER);

  return rtn;
}

CVariant CJellyfinUtils::GetJellyfinCVariant(std::string url, std::string filter)
{
#if defined(JELLYFIN_DEBUG_TIMING)
  unsigned int currentTime = XbmcThreads::SystemClockMillis();
#endif
  
  XFILE::CCurlFile jellyfin;
  jellyfin.SetRequestHeader("Cache-Control", "no-cache");
  jellyfin.SetRequestHeader("Content-Type", "application/json");
  jellyfin.SetRequestHeader("Accept-Encoding", "gzip");
  
  CURL curl(url);
  // this is key to get back gzip encoded content
  curl.SetProtocolOption("seekable", "0");
  // we always want json back
  curl.SetProtocolOptions(curl.GetProtocolOptions() + "&format=json");
  std::string response;
  if (jellyfin.Get(curl.Get(), response))
  {
#if defined(JELLYFIN_DEBUG_TIMING)
    CLog::Log(LOGDEBUG, "CJellyfinUtils::GetJellyfinCVariant %d(msec) for %lu bytes",
              XbmcThreads::SystemClockMillis() - currentTime, response.size());
#endif
    if (jellyfin.GetContentEncoding() == "gzip")
    {
      std::string buffer;
      if (XFILE::CZipFile::DecompressGzip(response, buffer))
        response = std::move(buffer);
      else
        return CVariant(CVariant::VariantTypeNull);
    }
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinUtils::GetJellyfinCVariant %s", curl.Get().c_str());
    CLog::Log(LOGDEBUG, "CJellyfinUtils::GetJellyfinCVariant %s", response.c_str());
#endif
#if defined(JELLYFIN_DEBUG_TIMING)
    currentTime = XbmcThreads::SystemClockMillis();
#endif
    CVariant resultObject;
    if (CJSONVariantParser::Parse(response, resultObject))
    {
#if defined(JELLYFIN_DEBUG_TIMING)
      CLog::Log(LOGDEBUG, "CJellyfinUtils::GetJellyfinCVariant parsed in %d(msec)",
                XbmcThreads::SystemClockMillis() - currentTime);
#endif
      // recently added does not return proper object, we make one up later
      if (resultObject.isObject() || resultObject.isArray())
        return resultObject;
    }
  }
  return CVariant(CVariant::VariantTypeNull);
}

#pragma mark - Jellyfin private
CFileItemPtr CJellyfinUtils::ToVideoFileItemPtr(CURL url, const CVariant &variant, std::string type)
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
  // if we have "ParentIndexNumber" means we are listing episodes
  if (variant.isMember("ParentIndexNumber"))
  {
    url2.SetFileName(CJellyfinUtils::ConstructFileName(url, "Items/", false) + itemId + "/Images/Primary");
    item->SetArt("thumb", url2.Get());
    item->SetIconImage(url2.Get());
    CVariant fanarts = variant["BackdropImageTags"];
    url2.SetFileName(CJellyfinUtils::ConstructFileName(url, "Items/", false) + itemId + "/Images/Backdrop");
    fanart = url2.Get();
    if (fanarts.size() > 0)
      item->SetArt("fanart", fanart);

    item->GetVideoInfoTag()->m_strShowTitle = variant["SeriesName"].asString();
    item->GetVideoInfoTag()->m_iSeason = variant["ParentIndexNumber"].asInteger();
    item->GetVideoInfoTag()->m_iEpisode = variant["IndexNumber"].asInteger();
    item->SetLabel(variant["SeasonName"].asString());
    item->SetProperty("JellyfinSeriesID", seriesId);
    std::string seasonEpisode = StringUtils::Format("S%02i:E%02i", item->GetVideoInfoTag()->m_iSeason, item->GetVideoInfoTag()->m_iEpisode);
    item->SetProperty("SeasonEpisode", seasonEpisode);
    if (variant.isMember("ParentThumbItemId"))
    {
      url2.SetFileName("Items/" + variant["ParentThumbItemId"].asString() + "/Images/Primary");
      item->SetArt("tvshow.thumb", url2.Get());
      item->SetArt("tvshow.poster", url2.Get());
    }
    else
    {
      url2.SetFileName("Items/" + variant["SeriesId"].asString() + "/Images/Primary");
      item->SetArt("tvshow.thumb", url2.Get());
      item->SetArt("tvshow.poster", url2.Get());
    }
    if (variant.isMember("SeasonId"))
    {
      url2.SetFileName("Items/" + variant["SeasonId"].asString() + "/Images/Primary");
      item->SetArt("season.poster", url2.Get());
    }

    item->SetArtFallback("tvshow.poster", "season.poster");
    item->SetArtFallback("tvshow.thumb", "season.poster");
  }
  else
  {
    CVariant fanarts = variant["BackdropImageTags"];
    url2.SetFileName(CJellyfinUtils::ConstructFileName(url, "Items/", false) + itemId + "/Images/Primary");
    item->SetArt("thumb", url2.Get());
    item->SetIconImage(url2.Get());
    url2.SetFileName(CJellyfinUtils::ConstructFileName(url, "Items/", false) + itemId + "/Images/Backdrop"); //"Items/"
    fanart = url2.Get();
    if (fanarts.size() > 0)
      item->SetArt("fanart", fanart);
    
    CVariant paramsProvID = variant["ProviderIds"];
    if (paramsProvID.isObject())
    {
      for (CVariant::iterator_map it = paramsProvID.begin_map(); it != paramsProvID.end_map(); ++it)
      {
        std::string strFirst = it->first;
        StringUtils::ToLower(strFirst);
        item->GetVideoInfoTag()->SetUniqueID(it->second.asString(),strFirst);
      }
    }
  }

  std::string title = variant["Name"].asString();
  item->SetLabel(title);
  item->m_dateTime.SetFromW3CDateTime(variant["PremiereDate"].asString());

  if (variant["IsFolder"].asBoolean())
  {
    // clear url options
    CURL curl(url);
    curl.SetOptions("");
    curl.SetOption("ParentId", itemId);
    curl.SetOption("SetName", title);
    item->m_bIsFolder = true;
    std::string testURL = curl.Get();
    std::string testURL2;
    curl.SetOption("Fields", MoviesFields);
    item->SetPath("jellyfin://movies/set/" + Base64URL::Encode(curl.Get()));
  }
  else
  {
    url2.SetFileName(CJellyfinUtils::ConstructFileName(url, "Videos/", false) + itemId +"/stream?static=true"); //"Videos/"
    item->SetPath(url2.Get());
  }

  item->GetVideoInfoTag()->m_strTitle = title;
  item->GetVideoInfoTag()->SetSortTitle(title);
  item->GetVideoInfoTag()->SetOriginalTitle(title);
  //item->GetVideoInfoTag()->SetSortTitle(variant["SortName"].asString());
  //item->GetVideoInfoTag()->SetOriginalTitle(variant["OriginalTitle"].asString());

  item->SetMediaServiceId(itemId);
  item->SetMediaServiceFile(variant["Path"].asString());
  item->GetVideoInfoTag()->m_strFileNameAndPath = url2.Get();

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
  item->GetVideoInfoTag()->m_resumePoint.timeInSeconds = static_cast<int>(TicksToSeconds(variant["UserData"]["PlaybackPositionTicks"].asUnsignedInteger()));
  
  // ["UserData"]["PlayCount"] means that it was watched, if so we set m_playCount to 1 and set overlay.
  // If we have ["UserData"]["PlaybackPositionTicks"]/m_resumePoint.timeInSeconds that means we are partially watched and should not set m_playCount to 1
  
  if (variant["UserData"]["PlayCount"].asInteger() && item->GetVideoInfoTag()->m_resumePoint.timeInSeconds <= 0)
  {
    item->GetVideoInfoTag()->m_playCount = static_cast<int>(variant["UserData"]["PlayCount"].asInteger());
  }
  
  item->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, item->GetVideoInfoTag()->m_playCount > 0);


  GetMediaDetals(*item, variant, itemId);

  if (type == MediaTypeMovie )
    SetJellyfinItemProperties(*item, "movies");
  else
    SetJellyfinItemProperties(*item, "episodes");
  return item;
}

void CJellyfinUtils::GetVideoDetails(CFileItem &item, const CVariant &variant)
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
        std::string urlStr = URIUtils::GetParentPath(item.GetPath());
        if (StringUtils::StartsWithNoCase(urlStr, "jellyfin://"))
          urlStr = Base64URL::Decode(URIUtils::GetFileName(item.GetPath()));
        CURL url(urlStr);
        url.SetFileName(CJellyfinUtils::ConstructFileName(url, "Items/", false) + peep["Id"].asString() + "/Images/Primary");
        role.strMonogram = StringUtils::Monogram(role.strName);
        role.thumb = url.Get();
        roles.push_back(role);
      }
    }

    item.GetVideoInfoTag()->m_cast = roles;
    item.GetVideoInfoTag()->SetDirector(directors);
  }
}

void CJellyfinUtils::GetMusicDetails(CFileItem &item, const CVariant &variant)
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
    item.GetMusicInfoTag()->SetGenre(genres);
  }
  if (variant.isMember("ArtistItems"))
  {
    // get all artists
    std::vector<std::string> artists;
    const auto& artistsItems = variant["ArtistItems"];
    for (auto artistIt = artistsItems.begin_array(); artistIt != artistsItems.end_array(); ++artistIt)
    {
      const auto artist = *artistIt;
      artists.push_back(artist["Name"].asString());
    }
    item.GetMusicInfoTag()->SetArtist(StringUtils::Join(artists, ","));
    item.GetMusicInfoTag()->SetAlbumArtist(artists);
    item.SetProperty("artist", StringUtils::Join(artists, ","));
    item.GetMusicInfoTag()->SetArtistDesc(StringUtils::Join(artists, ","));
  }
}

void CJellyfinUtils::GetMediaDetals(CFileItem &item, const CVariant &variant, std::string id)
{
  if (variant.isMember("MediaStreams") && variant["MediaStreams"].isArray())
  {
    CStreamDetails streamDetail;
    const auto& sources = variant["MediaSources"][0];
    std::string mediaID = sources["Id"].asString();
    const auto& streams = variant["MediaStreams"];
    int iSubPart = 1;
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      const auto streamType = stream["Type"].asString();
      if (streamType == "Video")
      {
        CStreamDetailVideo* videoStream = new CStreamDetailVideo();
        videoStream->m_strCodec = stream["Codec"].asString();
        videoStream->m_fAspect = (float)stream["Width"].asInteger()/stream["Height"].asInteger();
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
        
        if (stream["IsExternal"].asBoolean() && stream["IsTextSubtitleStream"].asBoolean())
        {
          CURL url(item.GetPath());
//          url.SetFileName("Videos/" + id + "/" + mediaID + "/Subtitles/" + stream["Index"].asString() + "/Stream.srt");
          url.SetFileName(CJellyfinUtils::ConstructFileName(url, "Videos/", false) + id + "/" + mediaID + "/Subtitles/" + stream["Index"].asString() + "/Stream.srt");
          std::string propertyKey = StringUtils::Format("subtitle:%i", iSubPart);
          std::string propertyLangKey = StringUtils::Format("subtitle:%i_language", iSubPart);
          item.SetProperty(propertyKey, url.Get());
          item.SetProperty(propertyLangKey, stream["Language"].asString());
          iSubPart ++;
        }
      }
    }
    item.GetVideoInfoTag()->m_streamDetails = streamDetail;
  }
  if (variant.isMember("LocalTrailerCount") && variant["LocalTrailerCount"].asInteger() > 0)
  {
    CURL url(item.GetPath());
    CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(url.Get());
    std::string userId = client->GetUserID();
    item.SetProperty("JellyfinMovieTrailer",CJellyfinUtils::ConstructFileName(url, "Users/", false) + userId + "/Items/" + id +"/LocalTrailers");
  }
}

void CJellyfinUtils::GetResolutionDetails(CFileItem &item, const CVariant &variant)
{
  if (variant.isMember("MediaStreams") && variant["MediaStreams"].isArray())
  {
    CStreamDetails streamDetail;
    item.SetProperty("JellyfinResolutionChoice", variant["Name"].asString());
    const auto& streams = variant["MediaStreams"];
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      const auto streamType = stream["Type"].asString();
      if (streamType == "Video")
      {
        CStreamDetailVideo* videoStream = new CStreamDetailVideo();
        videoStream->m_strCodec = stream["Codec"].asString();
        videoStream->m_fAspect = (float)stream["Width"].asInteger()/stream["Height"].asInteger();
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
    }
    CURL url(item.GetPath());
    url.SetOption("mediaSourceId", variant["Id"].asString());
    item.SetPath(url.Get());
    item.GetVideoInfoTag()->m_streamDetails = streamDetail;
    // mediaSourceId=f02ab5ce70f0c9c288c471e50f61b154
  }
}

void CJellyfinUtils::RemoveSubtitleProperties(CFileItem &item)
{
}
