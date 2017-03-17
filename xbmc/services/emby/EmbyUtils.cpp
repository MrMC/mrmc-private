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
    const auto item = *objectItemIt;
    rtn = true;
    CFileItemPtr newItem(new CFileItem());

    CVideoInfoTag* videoInfo = newItem->GetVideoInfoTag();

    std::string fanart;
    std::string value;
    // clear url options
    url.SetOptions("");
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
      url.SetFileName("Items/" + item["Id"].asString() + "/Images/Primary");
      newItem->SetArt("thumb", url.Get());
      newItem->SetIconImage(url.Get());
    }

    url.SetFileName("Items/" + item["Id"].asString() + "/Images/Backdrop");
    newItem->SetArt("fanart", url.Get());
    
    std::string title = item["Name"].asString();
    newItem->SetLabel(title);
    newItem->m_dateTime.SetFromW3CDateTime(item["PremiereDate"].asString());

    videoInfo->m_strTitle = title;
    videoInfo->SetSortTitle(item["SortName"].asString());
    videoInfo->SetOriginalTitle(item["OriginalTitle"].asString());
    videoInfo->SetPath(item["Path"].asString());
    //videoInfo->m_strServiceId = XMLUtils::GetAttribute(videoNode, "ratingKey");
    //newItem->SetProperty("EmbyShowKey", XMLUtils::GetAttribute(rootXmlNode, "grandparentRatingKey"));
    videoInfo->m_type = type;
    videoInfo->SetPlot(item["Overview"].asString());
    videoInfo->SetPlotOutline(item["ShortOverview"].asString());

    CDateTime premiereDate;
    premiereDate.SetFromW3CDateTime(item["PremiereDate"].asString());
    videoInfo->m_firstAired = premiereDate;
    videoInfo->SetPremiered(premiereDate);
    videoInfo->m_dateAdded.SetFromW3CDateTime(item["DateCreated"].asString());
/*
    if (!fanart.empty() && (fanart[0] == '/'))
      StringUtils::TrimLeft(fanart, "/");
    url.SetFileName(fanart);
    newItem->SetArt("fanart", url.Get());
*/
    videoInfo->SetYear(static_cast<int>(item["ProductionYear"].asInteger()));
    videoInfo->SetRating(item["CommunityRating"].asFloat(), static_cast<int>(item["VoteCount"].asInteger()), "", true);
    videoInfo->m_strMPAARating = item["OfficialRating"].asString();

    GetVideoDetails(newItem, item);

    videoInfo->m_duration = static_cast<int>(TicksToSeconds(item["RunTimeTicks"].asUnsignedInteger()));
    videoInfo->m_playCount = static_cast<int>(item["UserData"]["PlayCount"].asInteger());
    newItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, videoInfo->m_playCount > 0);
    videoInfo->m_lastPlayed.SetFromW3CDateTime(item["UserData"]["LastPlayedDate"].asString());
    CBookmark resumePoint = videoInfo->m_resumePoint;
    resumePoint.timeInSeconds = static_cast<int>(TicksToSeconds(item["UserData"]["PlaybackPositionTicks"].asUnsignedInteger()));
    if (videoInfo->m_duration > 0 && resumePoint.timeInSeconds > 0)
    {
      resumePoint.totalTimeInSeconds = videoInfo->m_duration;
      resumePoint.type = CBookmark::RESUME;
    }
    videoInfo->m_resumePoint = resumePoint;
    //newItem->m_lStartOffset = atoi(XMLUtils::GetAttribute(videoNode, "viewOffset").c_str())/1000;

    GetMediaDetals(newItem, url, item);

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

void CEmbyUtils::GetVideoDetails(CFileItemPtr pitem, const CVariant &item)
{
}

void CEmbyUtils::GetMusicDetails(CFileItemPtr pitem, const CVariant &item)
{
}

void CEmbyUtils::GetMediaDetals(CFileItemPtr pitem, CURL url, const CVariant &item, std::string id)
{
  if (item.isMember("MediaStreams") && item["MediaStreams"].isArray())
  {
    const auto& streams = item["MediaStreams"];
    for (auto streamIt = streams.begin_array(); streamIt != streams.end_array(); ++streamIt)
    {
      const auto stream = *streamIt;
      const auto streamType = stream["Type"].asString();
      CStreamDetail* streamDetail = nullptr;
      if (streamType == "Video")
      {
        CStreamDetailVideo* videoStream = new CStreamDetailVideo();
        videoStream->m_strCodec = stream["Codec"].asString();
        videoStream->m_strLanguage = stream["Language"].asString();
        videoStream->m_iWidth = static_cast<int>(stream["Width"].asInteger());
        videoStream->m_iHeight = static_cast<int>(stream["Height"].asInteger());
        videoStream->m_iDuration = pitem->GetVideoInfoTag()->m_duration;

        streamDetail = videoStream;
      }
      else if (streamType == "Audio")
      {
        CStreamDetailAudio* audioStream = new CStreamDetailAudio();
        audioStream->m_strCodec = stream["Codec"].asString();
        audioStream->m_strLanguage = stream["Language"].asString();
        audioStream->m_iChannels = static_cast<int>(stream["Channels"].asInteger());

        streamDetail = audioStream;
      }
      else if (streamType == "Subtitle")
      {
        CStreamDetailSubtitle* subtitleStream = new CStreamDetailSubtitle();
        subtitleStream->m_strLanguage = stream["Language"].asString();

        streamDetail = subtitleStream;
      }

      if (streamDetail != nullptr)
        pitem->GetVideoInfoTag()->m_streamDetails.AddStream(streamDetail);
    }
  }
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

