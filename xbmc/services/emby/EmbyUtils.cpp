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

#include "video/VideoInfoTag.h"
#include "video/windows/GUIWindowVideoBase.h"

#include "music/tags/MusicInfoTag.h"
#include "music/dialogs/GUIDialogSongInfo.h"
#include "music/dialogs/GUIDialogMusicInfo.h"
#include "guilib/GUIWindowManager.h"

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

static int  g_progressSec = 0;
static CFileItem m_curItem;
static MediaServicesPlayerState g_playbackState = MediaServicesPlayerState::stopped;

bool CEmbyUtils::HasClients()
{
  return CEmbyServices::GetInstance().HasClients();
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
  return false;
}

bool CEmbyUtils::GetEmbyInProgressShows(CFileItemList &items, const std::string url, int limit)
{
  return false;
}

bool CEmbyUtils::GetEmbyRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit)
{
  return false;
}

bool CEmbyUtils::GetEmbyInProgressMovies(CFileItemList &items, const std::string url, int limit)
{
  return false;
}

bool CEmbyUtils::GetAllEmbyInProgress(CFileItemList &items, bool tvShow)
{
  return false;
}

bool CEmbyUtils::GetAllEmbyRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow)
{
  return false;
}

CFileItemPtr ParseVideo(const CEmbyClient *client, const CVariant &object)
{
  return nullptr;
}

CFileItemPtr ParseMusic(const CEmbyClient *client, const CVariant &object)
{
  return nullptr;
}

CFileItemPtr CEmbyUtils::ToFileItemPtr(const CEmbyClient *client, const CVariant &object)
{
  if (object.isNull() || !object.isObject() || !object.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::ToFileItemPtr cvariant is empty");
    return nullptr;
  }

  const auto& items = object["Items"];
  //int totalRecordCount = object["TotalRecordCount"].asInteger();
  for (auto itemsIt = items.begin_array(); itemsIt != items.end_array(); ++itemsIt)
  {
    const auto item = *itemsIt;
    if (!item.isMember("Id"))
      continue;

    std::string mediaType = item["MediaType"].asString();
    if (mediaType == "Video")
      return ParseVideo(client, item);
    else if (mediaType == "Music")
      return ParseMusic(client, item);
  }

  return nullptr;
}


  // Emby Movie/TV
bool CEmbyUtils::GetEmbyMovies(CFileItemList &items, std::string url, std::string filter)
{
  static const std::string PropertyItemPath = "Path";
  static const std::string PropertyItemDateCreated = "DateCreated";
  static const std::string PropertyItemGenres = "Genres";
  static const std::string PropertyItemMediaStreams = "MediaStreams";
  static const std::string PropertyItemOverview = "Overview";
  static const std::string PropertyItemShortOverview = "ShortOverview";
  static const std::string PropertyItemPeople = "People";
  static const std::string PropertyItemSortName = "SortName";
  static const std::string PropertyItemOriginalTitle = "OriginalTitle";
  static const std::string PropertyItemProviderIds = "ProviderIds";
  static const std::string PropertyItemStudios = "Studios";
  static const std::string PropertyItemTaglines = "Taglines";
  static const std::string PropertyItemProductionLocations = "ProductionLocations";
  static const std::string PropertyItemTags = "Tags";
  static const std::string PropertyItemVoteCount = "VoteCount";

  static const std::vector<std::string> Fields = {
    PropertyItemDateCreated,
    PropertyItemGenres,
    PropertyItemMediaStreams,
    PropertyItemOverview,
    PropertyItemShortOverview,
    PropertyItemPath,
//    PropertyItemPeople,
//    PropertyItemProviderIds,
//    PropertyItemSortName,
//    PropertyItemOriginalTitle,
//    PropertyItemStudios,
    PropertyItemTaglines,
//    PropertyItemProductionLocations,
//    PropertyItemTags,
//    PropertyItemVoteCount,
  };

  CURL url2(url);

  const CVariant resultObject = GetEmbyCVariant(url2.Get());

  std::vector<std::string> iDS;
  const auto& objectItems = resultObject["Items"];
  for (auto objectItemIt = objectItems.begin_array(); objectItemIt != objectItems.end_array(); ++objectItemIt)
  {
    const auto item = *objectItemIt;
    iDS.push_back(item["Id"].asString());
  }

  std::string testIDs = StringUtils::Join(iDS, ",");
  url2.SetOption("Ids", testIDs);
  url2.SetOption("Fields", StringUtils::Join(Fields, ","));
  url2.SetOption("ExcludeLocationTypes", "Virtual,Offline");

  const CVariant result = GetEmbyCVariant(url2.Get());

  bool rtn = GetVideoItems(items, url2, result, MediaTypeMovie, false);
  return rtn;
}

bool CEmbyUtils::GetEmbyTvshows(CFileItemList &items, std::string url)
{
  static const std::string PropertyItemPath = "Path";
  static const std::string PropertyItemDateCreated = "DateCreated";
  static const std::string PropertyItemGenres = "Genres";
  static const std::string PropertyItemMediaStreams = "MediaStreams";
  static const std::string PropertyItemOverview = "Overview";
  static const std::string PropertyItemShortOverview = "ShortOverview";
  static const std::string PropertyItemPeople = "People";
  static const std::string PropertyItemSortName = "SortName";
  static const std::string PropertyItemOriginalTitle = "OriginalTitle";
  static const std::string PropertyItemProviderIds = "ProviderIds";
  static const std::string PropertyItemStudios = "Studios";
  static const std::string PropertyItemTaglines = "Taglines";
  static const std::string PropertyItemProductionLocations = "ProductionLocations";
  static const std::string PropertyItemTags = "Tags";
  static const std::string PropertyItemVoteCount = "VoteCount";

  static const std::vector<std::string> Fields = {
    PropertyItemDateCreated,
    PropertyItemGenres,
    PropertyItemMediaStreams,
    PropertyItemOverview,
    PropertyItemShortOverview,
    PropertyItemPath,
//    PropertyItemPeople,
//    PropertyItemProviderIds,
//    PropertyItemSortName,
//    PropertyItemOriginalTitle,
//    PropertyItemStudios,
    PropertyItemTaglines,
//    PropertyItemProductionLocations,
//    PropertyItemTags,
//    PropertyItemVoteCount,
  };

  bool rtn = false;

  CURL url2(url);
  std::vector<std::string> iDS;
  const CVariant resultObject = GetEmbyCVariant(url2.Get());
  if (!resultObject.isNull() && resultObject.isObject())
  {
    const auto& objectItems = resultObject["Items"];
    for (auto objectItemIt = objectItems.begin_array(); objectItemIt != objectItems.end_array(); ++objectItemIt)
    {
      const auto item = *objectItemIt;
      iDS.push_back(item["Id"].asString());
    }
  }

  std::string testIDs = StringUtils::Join(iDS, ",");
  url2.SetOption("Ids", testIDs);
  url2.SetOption("Fields", StringUtils::Join(Fields, ","));
  url2.SetOption("ExcludeLocationTypes", "Virtual,Offline");
  const CVariant object = GetEmbyCVariant(url2.Get());

  if (!object.isNull() || object.isObject() || object.isMember("Items"))
  {
    const auto& objectItems = object["Items"];
    for (auto objectItemIt = objectItems.begin_array(); objectItemIt != objectItems.end_array(); ++objectItemIt)
    {
      const auto item = *objectItemIt;
      rtn = true;

      std::string fanart;
      std::string value;
      // clear url options
      CURL url2(url);
      url2.SetOptions("");

      CFileItemPtr newItem(new CFileItem());
      // set m_bIsFolder to true to indicate we are tvshow list
      newItem->m_bIsFolder = true;

      std::string title = item["Name"].asString();
      newItem->SetLabel(title);

      CDateTime premiereDate;
      premiereDate.SetFromW3CDateTime(item["PremiereDate"].asString());
      newItem->m_dateTime = premiereDate;

      url2.SetFileName("Videos/" + item["Id"].asString() +"/stream?static=true");
      newItem->SetPath("emby://tvshows/shows/" + Base64::Encode(url2.Get()));
      newItem->SetMediaServiceId(item["Id"].asString());
      newItem->SetMediaServiceFile(item["Path"].asString());

      url2.SetFileName("Items/" + item["Id"].asString() + "/Images/Primary");
      newItem->SetArt("thumb", url2.Get());
      newItem->SetIconImage(url2.Get());
      url2.SetFileName("Items/" + item["Id"].asString() + "/Images/Banner");
      newItem->SetArt("banner", url2.Get());
      url2.SetFileName("Items/" + item["Id"].asString() + "/Images/Backdrop");
      newItem->SetArt("fanart", url2.Get());

      newItem->GetVideoInfoTag()->m_playCount = static_cast<int>(item["UserData"]["PlayCount"].asInteger());
      newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, newItem->GetVideoInfoTag()->m_playCount > 0);

      newItem->GetVideoInfoTag()->m_strTitle = title;
      newItem->GetVideoInfoTag()->m_strStatus = item["Status"].asString();

      newItem->GetVideoInfoTag()->m_type = MediaTypeMovie;
      newItem->GetVideoInfoTag()->m_strFileNameAndPath = newItem->GetPath();
      newItem->GetVideoInfoTag()->SetSortTitle(item["SortName"].asString());
      newItem->GetVideoInfoTag()->SetOriginalTitle(item["OriginalTitle"].asString());
      //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
      newItem->GetVideoInfoTag()->SetPlot(item["Overview"].asString());
      newItem->GetVideoInfoTag()->SetPlotOutline(item["ShortOverview"].asString());
      newItem->GetVideoInfoTag()->m_firstAired = premiereDate;
      newItem->GetVideoInfoTag()->SetPremiered(premiereDate);
      newItem->GetVideoInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
      newItem->GetVideoInfoTag()->SetYear(static_cast<int>(item["ProductionYear"].asInteger()));
      newItem->GetVideoInfoTag()->SetRating(item["CommunityRating"].asFloat(), static_cast<int>(item["VoteCount"].asInteger()), "", true);
      newItem->GetVideoInfoTag()->m_strMPAARating = item["OfficialRating"].asString();
/*
      newItem->GetVideoInfoTag()->m_iSeason = iSeasons;
      newItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(directoryNode, "leafCount").c_str());
      newItem->GetVideoInfoTag()->m_playCount = (int)watchedEpisodes >= plexItem->GetVideoInfoTag()->m_iEpisode;

      newItem->SetProperty("totalseasons", iSeasons);
      newItem->SetProperty("totalepisodes", newItem->GetVideoInfoTag()->m_iEpisode);
      newItem->SetProperty("numepisodes",   newItem->GetVideoInfoTag()->m_iEpisode);
      newItem->SetProperty("watchedepisodes", watchedEpisodes);
      newItem->SetProperty("unwatchedepisodes", newItem->GetVideoInfoTag()->m_iEpisode - watchedEpisodes);

      GetVideoDetails(*newItem, item);
*/
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

bool CEmbyUtils::GetEmbySeasons(CFileItemList &items, const std::string url)
{
  return false;
}

bool CEmbyUtils::GetEmbyEpisodes(CFileItemList &items, const std::string url)
{
  return false;
}

bool CEmbyUtils::GetEmbyFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter)
{
  return false;
}

bool CEmbyUtils::GetItemSubtiles(CFileItem &item)
{
  return false;
}

bool CEmbyUtils::GetMoreItemInfo(CFileItem &item)
{
  return false;
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

bool CEmbyUtils::GetVideoItems(CFileItemList &items, CURL url, const CVariant &object, std::string type, bool formatLabel, int season)
{
  bool rtn = false;
  if (object.isNull() || !object.isObject() || !object.isMember("Items"))
  {
    CLog::Log(LOGERROR, "CEmbyUtils::GetVideoItems invalid response from %s", url.GetRedacted().c_str());
    return false;
  }

  const auto& objectItems = object["Items"];
  for (auto objectItemIt = objectItems.begin_array(); objectItemIt != objectItems.end_array(); ++objectItemIt)
  {
    const auto item = *objectItemIt;
    rtn = true;
    CFileItemPtr newItem(new CFileItem());

    std::string fanart;
    std::string value;
    // clear url options
    CURL url2(url);
    url2.SetOptions("");
/*    // if we have season means we are listing episodes, we need to get the fanart from rootXmlNode.
    // movies has it in videoNode
    if (season > -1)
    {
      value = XMLUtils::GetAttribute(videoNode, "thumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url.SetFileName(value);
      newItem->SetArt("thumb", url.Get());
      newItem->SetArt("tvshow.thumb", url.Get());
      newItem->SetIconImage(url.Get());
      fanart = XMLUtils::GetAttribute(rootXmlNode, "art");
      videoInfo->m_strShowTitle = XMLUtils::GetAttribute(rootXmlNode, "grandparentTitle");
      videoInfo->m_iSeason = season;
      videoInfo->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());
    }
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
 
    else
*/  {
      url2.SetFileName("Items/" + item["Id"].asString() + "/Images/Primary");
      newItem->SetArt("thumb", url2.Get());
      newItem->SetIconImage(url2.Get());
    }

    std::string title = item["Name"].asString();
    newItem->SetLabel(title);
    newItem->m_dateTime.SetFromW3CDateTime(item["PremiereDate"].asString());

    url2.SetFileName("Items/" + item["Id"].asString() + "/Images/Backdrop");
    newItem->SetArt("fanart", url2.Get());

    newItem->GetVideoInfoTag()->m_strTitle = title;
    newItem->GetVideoInfoTag()->SetSortTitle(item["SortName"].asString());
    newItem->GetVideoInfoTag()->SetOriginalTitle(item["OriginalTitle"].asString());

    url2.SetFileName("Videos/" + item["Id"].asString() +"/stream?static=true");
    newItem->SetPath(url2.Get());
    newItem->SetMediaServiceId(item["Id"].asString());
    newItem->SetMediaServiceFile(item["Path"].asString());
    newItem->GetVideoInfoTag()->m_strFileNameAndPath = url2.Get();

    //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
    newItem->GetVideoInfoTag()->m_type = type;
    newItem->GetVideoInfoTag()->SetPlot(item["Overview"].asString());
    newItem->GetVideoInfoTag()->SetPlotOutline(item["ShortOverview"].asString());

    CDateTime premiereDate;
    premiereDate.SetFromW3CDateTime(item["PremiereDate"].asString());
    newItem->GetVideoInfoTag()->m_firstAired = premiereDate;
    newItem->GetVideoInfoTag()->SetPremiered(premiereDate);
    newItem->GetVideoInfoTag()->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());

    newItem->GetVideoInfoTag()->SetYear(static_cast<int>(item["ProductionYear"].asInteger()));
    newItem->GetVideoInfoTag()->SetRating(item["CommunityRating"].asFloat(), static_cast<int>(item["VoteCount"].asInteger()), "", true);
    newItem->GetVideoInfoTag()->m_strMPAARating = item["OfficialRating"].asString();

    GetVideoDetails(*newItem, item);

    newItem->GetVideoInfoTag()->m_duration = static_cast<int>(TicksToSeconds(item["RunTimeTicks"].asInteger()));
    newItem->GetVideoInfoTag()->m_resumePoint.totalTimeInSeconds = newItem->GetVideoInfoTag()->m_duration;
    newItem->GetVideoInfoTag()->m_playCount = static_cast<int>(item["UserData"]["PlayCount"].asInteger());
    newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, newItem->GetVideoInfoTag()->m_playCount > 0);
    newItem->GetVideoInfoTag()->m_lastPlayed.SetFromW3CDateTime(item["UserData"]["LastPlayedDate"].asString());
    newItem->GetVideoInfoTag()->m_resumePoint.timeInSeconds = static_cast<int>(TicksToSeconds(item["UserData"]["PlaybackPositionTicks"].asUnsignedInteger()));

    GetMediaDetals(*newItem, item);

    if (formatLabel)
    {
      CLabelFormatter formatter("%H. %T", "");
      formatter.FormatLabel(newItem.get());
      newItem->SetLabelPreformated(true);
    }
    SetEmbyItemProperties(*newItem);
    items.Add(newItem);
  }
  // this is needed to display movies/episodes properly ... dont ask
  // good thing it didnt take 2 days to figure it out
  items.SetProperty("library.filter", "true");
  SetEmbyItemProperties(items);

  return rtn;
}

void CEmbyUtils::GetVideoDetails(CFileItem &fileitem, const CVariant &cvariant)
{
  // get all genres
  std::vector<std::string> genres;
  const auto& streams = cvariant["Genres"];
  for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
  {
    const auto stream = *streamIt;
    genres.push_back(stream.asString());
  }
  fileitem.GetVideoInfoTag()->SetGenre(genres);
}

void CEmbyUtils::GetMusicDetails(CFileItem &fileitem, const CVariant &cvariant)
{
}

void CEmbyUtils::GetMediaDetals(CFileItem &fileitem, const CVariant &cvariant, std::string id)
{
  if (cvariant.isMember("MediaStreams") && cvariant["MediaStreams"].isArray())
  {
    CStreamDetails streamDetail;
    const auto& streams = cvariant["MediaStreams"];
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
        videoStream->m_iDuration = fileitem.GetVideoInfoTag()->m_duration;

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
    fileitem.GetVideoInfoTag()->m_streamDetails = streamDetail;
  }
}

CVariant CEmbyUtils::GetEmbyCVariant(std::string url, std::string filter)
{
  XFILE::CCurlFile emby;
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  emby.SetRequestHeader("Accept-Encoding", "gzip");

  CURL curl(url);
  // this is key to get back gzip encoded content
  curl.SetProtocolOption("seekable", "0");
  std::string response;
  if (emby.Get(curl.Get(), response))
  {
    if (emby.GetContentEncoding() == "gzip")
    {
      std::string buffer;
      if (XFILE::CZipFile::DecompressGzip(response, buffer))
        response = std::move(buffer);
      else
        return CVariant(CVariant::VariantTypeNull);
    }
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant %s", response.c_str());
#endif
    auto resultObject = CJSONVariantParser::Parse(response);
    if (resultObject.isObject())
      return resultObject;
  }
  return CVariant(CVariant::VariantTypeNull);
}

void CEmbyUtils::RemoveSubtitleProperties(CFileItem &item)
{
}

