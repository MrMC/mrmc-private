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
#include "utils/JSONVariantWriter.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"
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

static int  g_progressSec = 0;
static CFileItem m_curItem;
static EmbyUtilsPlayerState g_playbackState = EmbyUtilsPlayerState::stopped;

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
}

void CEmbyUtils::SetUnWatched(CFileItem &item)
{
}

void CEmbyUtils::ReportProgress(CFileItem &item, double currentSeconds)
{
}

void CEmbyUtils::SetPlayState(EmbyUtilsPlayerState state)
{
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
    PropertyItemPeople,
    PropertyItemProviderIds,
    PropertyItemSortName,
    PropertyItemOriginalTitle,
    PropertyItemStudios,
    PropertyItemTaglines,
    PropertyItemProductionLocations,
    PropertyItemTags,
    PropertyItemVoteCount
  };

  CURL url2(url);
  url2.SetOption("Fields", StringUtils::Join(Fields, ","));

  const CVariant resultObject = GetEmbyCVariant(url);
  bool rtn = GetVideoItems(items, url2, resultObject, MediaTypeMovie, false);
  return rtn;
}

bool CEmbyUtils::GetEmbyTvshows(CFileItemList &items, std::string url)
{
  return false;
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
  return false;
}

bool CEmbyUtils::GetURL(CFileItem &item)
{
  return false;
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

bool CEmbyUtils::GetVideoItems(CFileItemList &items,CURL url, const CVariant &object, std::string type, bool formatLabel, int season)
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
    const auto objectitem = *objectItemIt;
    rtn = true;
    CFileItemPtr newItem(new CFileItem());
/*
    std::string fanart;
    std::string value;
    // if we have season means we are listing episodes, we need to get the fanart from rootXmlNode.
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
      newItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(rootXmlNode, "grandparentTitle");
      newItem->GetVideoInfoTag()->m_iSeason = season;
      newItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());
    }
    else if (((TiXmlElement*) videoNode)->Attribute("grandparentTitle")) // only recently added episodes have this
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      newItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(videoNode, "grandparentTitle");
      newItem->GetVideoInfoTag()->m_iSeason = atoi(XMLUtils::GetAttribute(videoNode, "parentIndex").c_str());
      newItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());

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
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      plexItem->SetLabel(XMLUtils::GetAttribute(videoNode, "title"));

      value = XMLUtils::GetAttribute(videoNode, "thumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url.SetFileName(value);
      newItem->SetArt("thumb", url.Get());
      newItem->SetIconImage(url.Get());
    }
*/
    std::string title = objectitem["Name"].asString();
    newItem->SetLabel(title);
    newItem->GetVideoInfoTag()->m_strTitle = title;
    //newItem->GetVideoInfoTag()->m_strServiceId = XMLUtils::GetAttribute(videoNode, "ratingKey");
    //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
    newItem->GetVideoInfoTag()->m_type = type;
    //newItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(videoNode, "tagline"));
    //newItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(videoNode, "summary"));

    CDateTime firstAired;
    firstAired.SetFromDBDate(objectitem["PremiereDate"].asString());
    newItem->GetVideoInfoTag()->m_firstAired = firstAired;

    newItem->GetVideoInfoTag()->m_dateAdded.SetFromW3CDateTime(objectitem["DateCreated"].asString());
/*
    if (!fanart.empty() && (fanart[0] == '/'))
      StringUtils::TrimLeft(fanart, "/");
    url.SetFileName(fanart);
    newItem->SetArt("fanart", url.Get());
*/
    newItem->GetVideoInfoTag()->SetYear(static_cast<int>(objectitem["ProductionYear"].asInteger()));
    newItem->GetVideoInfoTag()->SetRating(objectitem["CommunityRating"].asFloat());
    newItem->GetVideoInfoTag()->m_strMPAARating = objectitem["OfficialRating"].asString();
/*
    // lastViewedAt means that it was watched, if so we set m_playCount to 1 and set overlay
    if (((TiXmlElement*) videoNode)->Attribute("lastViewedAt"))
    {
      newItem->GetVideoInfoTag()->m_playCount = 1;
    }
*/
    newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, newItem->HasVideoInfoTag() && newItem->GetVideoInfoTag()->m_playCount > 0);

    GetVideoDetails(*newItem, objectitem);
/*
    CBookmark m_bookmark;
    m_bookmark.timeInSeconds = atoi(XMLUtils::GetAttribute(videoNode, "viewOffset").c_str())/1000;
    m_bookmark.totalTimeInSeconds = atoi(XMLUtils::GetAttribute(videoNode, "duration").c_str())/1000;
    newItem->GetVideoInfoTag()->m_resumePoint = m_bookmark;
    newItem->m_lStartOffset = atoi(XMLUtils::GetAttribute(videoNode, "viewOffset").c_str())/1000;
*/
    GetMediaDetals(*newItem, url, objectitem);

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

void CEmbyUtils::GetVideoDetails(CFileItem &item, const CVariant &object)
{
}

void CEmbyUtils::GetMusicDetails(CFileItem &item, const CVariant &object)
{
}

void CEmbyUtils::GetMediaDetals(CFileItem &item, CURL url, const CVariant &object, std::string id)
{
}

CVariant CEmbyUtils::GetEmbyCVariant(std::string url, std::string filter)
{
  XFILE::CCurlFile emby;
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  //CEmbyUtils::PrepareApiCall(m_userId, m_accessToken, emby);

  CURL curl(url);
  std::string response;
  if (emby.Get(curl.Get(), response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CEmbyUtils::GetEmbyCVariant %s", response.c_str());
#endif
    auto resultObject = CJSONVariantParser::Parse((const unsigned char*)response.c_str(), response.size());
    if (resultObject.isObject())
      return resultObject;
  }
  return CVariant(CVariant::VariantTypeNull);
}

//int ParseEmbyMediaXML(TiXmlDocument xml);
void CEmbyUtils::RemoveSubtitleProperties(CFileItem &item)
{
}

