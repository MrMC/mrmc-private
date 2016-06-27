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
#include "PlexServices.h"
#include "utils/Base64.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"

#include "video/VideoInfoTag.h"

CPlexClient::CPlexClient()
{
}

CPlexClient::~CPlexClient()
{
}

CPlexClient& CPlexClient::GetInstance()
{
  static CPlexClient sPlexClient;
  return sPlexClient;
}

TiXmlDocument CPlexClient::GetPlexXML(std::string url, std::string filter)
{
  CURL url2(url);
  std::string strXML;
  XFILE::CCurlFile http;
  url2.SetProtocol("http");
  
  if (!filter.empty())
    url2.SetFileName(url2.GetFileName() + filter);
  
  http.Get(url2.Get(), strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  return xml;
}

void CPlexClient::GetVideoDetails(CFileItem &item, const TiXmlElement* videoNode)
{
  // looks like plex is sending only one studio?
  std::vector<std::string> studios;
  studios.push_back(XMLUtils::GetAttribute(videoNode, "studio"));
  item.GetVideoInfoTag()->m_studio = studios;
  
  
  // get all genres
  std::vector<std::string> genres;
  const TiXmlElement* genreNode = videoNode->FirstChildElement("Genre");
  if (genreNode)
  {
    while (genreNode)
    {
      std::string genre = XMLUtils::GetAttribute(genreNode, "tag");
      genres.push_back(genre);
      genreNode = genreNode->NextSiblingElement("Genre");
    }
  }
  item.GetVideoInfoTag()->SetGenre(genres);
  
  // get all writers
  std::vector<std::string> writers;
  const TiXmlElement* writerNode = videoNode->FirstChildElement("Writer");
  if (writerNode)
  {
    while (writerNode)
    {
      std::string writer = XMLUtils::GetAttribute(writerNode, "tag");
      writers.push_back(writer);
      writerNode = writerNode->NextSiblingElement("Writer");
    }
  }
  item.GetVideoInfoTag()->SetWritingCredits(writers);
  
  // get all directors
  std::vector<std::string> directors;
  const TiXmlElement* directorNode = videoNode->FirstChildElement("Director");
  if (directorNode)
  {
    while (directorNode)
    {
      std::string director = XMLUtils::GetAttribute(directorNode, "tag");
      directors.push_back(director);
      directorNode = directorNode->NextSiblingElement("Director");
    }
  }
  item.GetVideoInfoTag()->SetDirector(directors);
  
  // get all countries
  std::vector<std::string> countries;
  const TiXmlElement* countryNode = videoNode->FirstChildElement("Country");
  if (countryNode)
  {
    while (countryNode)
    {
      std::string country = XMLUtils::GetAttribute(countryNode, "tag");
      countries.push_back(country);
      countryNode = countryNode->NextSiblingElement("Country");
    }
  }
  item.GetVideoInfoTag()->SetCountry(countries);
  
  // get all roles
  std::vector< SActorInfo > roles;
  const TiXmlElement* roleNode = videoNode->FirstChildElement("Role");
  if (roleNode)
  {
    while (roleNode)
    {
      SActorInfo role;
      role.strName = XMLUtils::GetAttribute(roleNode, "tag");
      roles.push_back(role);
      roleNode = roleNode->NextSiblingElement("Role");
    }
  }
  item.GetVideoInfoTag()->m_cast = roles;
}

void CPlexClient::SetWatched(CFileItem* item)
{
  std::string url = URIUtils::GetParentPath(item->GetPath());
  if (StringUtils::StartsWithNoCase(url, "plex://tvshows/shows/") ||
      StringUtils::StartsWithNoCase(url, "plex://tvshows/seasons/"))
      url = Base64::Decode(URIUtils::GetFileName(item->GetPath()));
  std::string id  = item->GetVideoInfoTag()->m_strPlexId;
  CURL url2(url);
  url2.SetProtocol("http");
  url2.SetFileName(StringUtils::Format(":/scrobble?identifier=com.plexapp.plugins.library&key=%s", id.c_str()));
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url2.Get(), strXML);
}

void CPlexClient::SetUnWatched(CFileItem* item)
{
  std::string url = URIUtils::GetParentPath(item->GetPath());
  if (StringUtils::StartsWithNoCase(url, "plex://tvshows/shows/") ||
      StringUtils::StartsWithNoCase(url, "plex://tvshows/seasons/"))
    url = Base64::Decode(URIUtils::GetFileName(item->GetPath()));
  std::string id  = item->GetVideoInfoTag()->m_strPlexId;
  CURL url2(url);
  url2.SetProtocol("http");
  url2.SetFileName(StringUtils::Format(":/unscrobble?identifier=com.plexapp.plugins.library&key=%s", id.c_str()));
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url2.Get(), strXML);
}

void CPlexClient::SetOffset(CFileItem item, int offsetSeconds)
{
  std::string url = URIUtils::GetParentPath(item.GetPath());
  std::string id  = item.GetVideoInfoTag()->m_strPlexId;
  CURL url2(url);
  url2.SetProtocol("http");
  url2.SetFileName(StringUtils::Format(":/progress?key=%s&identifier=com.plexapp.plugins.library&time=%i&state=stopped", id.c_str(), offsetSeconds*1000));
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url2.Get(), strXML);
}

void CPlexClient::GetVideoItems(CFileItemList &items, CURL url, TiXmlElement* rootXmlNode, std::string type, int season /* = -1 */)
{
  const TiXmlElement* videoNode = rootXmlNode->FirstChildElement("Video");
  while (videoNode)
  {
    CFileItemPtr plexItem(new CFileItem());
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
      plexItem->SetArt("thumb", url.Get());
      plexItem->SetIconImage(url.Get());
      fanart = XMLUtils::GetAttribute(rootXmlNode, "art");
      plexItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(rootXmlNode, "grandparentTitle");
      plexItem->GetVideoInfoTag()->m_iSeason = season;
      plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());
    }
    else if (((TiXmlElement*) videoNode)->Attribute("grandparentTitle")) // only recently added episodes have this
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      plexItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(videoNode, "grandparentTitle");
      plexItem->GetVideoInfoTag()->m_iSeason = atoi(XMLUtils::GetAttribute(videoNode, "parentIndex").c_str());
      plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());
      
      value = XMLUtils::GetAttribute(videoNode, "thumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url.SetFileName(value);
      plexItem->SetArt("thumb", url.Get());

      value = XMLUtils::GetAttribute(videoNode, "parentThumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url.SetFileName(value);
      plexItem->SetArt("tvshow.poster", url.Get());
      plexItem->SetArt("tvshow.thumb", url.Get());
      plexItem->SetIconImage(url.Get());
    }
    else
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      plexItem->SetLabel(XMLUtils::GetAttribute(videoNode, "title"));
      
      value = XMLUtils::GetAttribute(videoNode, "thumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url.SetFileName(value);
      plexItem->SetArt("thumb", url.Get());
      plexItem->SetIconImage(url.Get());
    }
    
    std::string title = XMLUtils::GetAttribute(videoNode, "title");
    plexItem->SetLabel(title);
    plexItem->GetVideoInfoTag()->m_strTitle = title;
    plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(videoNode, "ratingKey");
    plexItem->GetVideoInfoTag()->m_type = type;
    plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(videoNode, "tagline"));
    plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(videoNode, "summary"));
    
    CDateTime firstAired;
    firstAired.SetFromDBDate(XMLUtils::GetAttribute(videoNode, "originallyAvailableAt"));
    plexItem->GetVideoInfoTag()->m_firstAired = firstAired;
    
    time_t addedTime = atoi(XMLUtils::GetAttribute(videoNode, "addedAt").c_str());
    CDateTime aTime(addedTime);
    plexItem->GetVideoInfoTag()->m_dateAdded = aTime;
    
    if (!fanart.empty() && (fanart[0] == '/'))
      StringUtils::TrimLeft(fanart, "/");
    url.SetFileName(fanart);
    plexItem->SetArt("fanart", url.Get());
    
    plexItem->GetVideoInfoTag()->m_iYear = atoi(XMLUtils::GetAttribute(videoNode, "year").c_str());
    plexItem->GetVideoInfoTag()->m_fRating = atof(XMLUtils::GetAttribute(videoNode, "rating").c_str());
    plexItem->GetVideoInfoTag()->m_strMPAARating = XMLUtils::GetAttribute(videoNode, "contentRating");
    
    // lastViewedAt means that it was watched, if so we set m_playCount to 1 and set overlay
    if (((TiXmlElement*) videoNode)->Attribute("lastViewedAt"))
    {
      plexItem->GetVideoInfoTag()->m_playCount = 1;
    }
    plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->HasVideoInfoTag() && plexItem->GetVideoInfoTag()->m_playCount > 0);
    
    
    GetVideoDetails(*plexItem, videoNode);
    
    const TiXmlElement* mediaNode = videoNode->FirstChildElement("Media");
    if (mediaNode)
    {
      CStreamDetails details;
      CStreamDetailVideo *p = new CStreamDetailVideo();
      p->m_strCodec = XMLUtils::GetAttribute(mediaNode, "videoCodec");
      p->m_fAspect = atof(XMLUtils::GetAttribute(mediaNode, "aspectRatio").c_str());
      p->m_iWidth = atoi(XMLUtils::GetAttribute(mediaNode, "width").c_str());
      p->m_iHeight = atoi(XMLUtils::GetAttribute(mediaNode, "height").c_str());
      p->m_iDuration = atoi(XMLUtils::GetAttribute(mediaNode, "videoCodec").c_str());
      details.AddStream(p);
      
      CStreamDetailAudio *a = new CStreamDetailAudio();
      a->m_strCodec = XMLUtils::GetAttribute(mediaNode, "audioCodec");
      a->m_iChannels = atoi(XMLUtils::GetAttribute(mediaNode, "audioChannels").c_str());
      a->m_strLanguage = XMLUtils::GetAttribute(mediaNode, "audioChannels");
      details.AddStream(a);
      
      plexItem->GetVideoInfoTag()->m_streamDetails = details;
      
      /// plex has duration in milliseconds
      plexItem->GetVideoInfoTag()->m_duration = atoi(XMLUtils::GetAttribute(mediaNode, "duration").c_str())/1000;
      
      CBookmark m_bookmark;
      m_bookmark.timeInSeconds = atoi(XMLUtils::GetAttribute(videoNode, "viewOffset").c_str())/1000;
      m_bookmark.totalTimeInSeconds = atoi(XMLUtils::GetAttribute(mediaNode, "duration").c_str())/1000;
      plexItem->GetVideoInfoTag()->m_resumePoint = m_bookmark;
      plexItem->m_lStartOffset = atoi(XMLUtils::GetAttribute(videoNode, "viewOffset").c_str())/1000;
      
      const TiXmlElement* partNode = mediaNode->FirstChildElement("Part");
      if (partNode)
      {
        std::string key = ((TiXmlElement*) partNode)->Attribute("key");
        if (!key.empty() && (key[0] == '/'))
          StringUtils::TrimLeft(key, "/");
        url.SetFileName(key);
        plexItem->SetPath(url.Get());
        plexItem->GetVideoInfoTag()->m_strFileNameAndPath = url.Get();
        plexItem->GetVideoInfoTag()->m_strPlexFile = XMLUtils::GetAttribute(partNode, "file");
      }
    }
    
    videoNode = videoNode->NextSiblingElement("Video");
    items.Add(plexItem);
  }
  // this is needed to display movies/episodes properly ... dont ask
  // good thing it didnt take 2 days to figure it out
  items.SetProperty("library.filter", "true");
}

void CPlexClient::GetLocalMovies(CFileItemList &items, std::string url, std::string filter)
{
  CURL url2(url);
  TiXmlDocument xml = GetPlexXML(url);

  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    GetVideoItems(items, url2, rootXmlNode, MediaTypePlexMovie);
  }
}

void CPlexClient::GetLocalTvshows(CFileItemList &items, std::string url)
{
  std::string value;
  TiXmlDocument xml = GetPlexXML(url);
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    const TiXmlElement* directoryNode = rootXmlNode->FirstChildElement("Directory");
    while (directoryNode)
    {
      CFileItemPtr plexItem(new CFileItem());
      // set m_bIsFolder to true to indicate we are tvshow list
      plexItem->m_bIsFolder = true;
      plexItem->SetLabel(XMLUtils::GetAttribute(directoryNode, "title"));
      CURL url1(url);
      url1.SetProtocol("http");
      url1.SetFileName("library/metadata/" + XMLUtils::GetAttribute(directoryNode, "ratingKey") + "/children");
      plexItem->SetPath("plex://tvshows/shows/" + Base64::Encode(url1.Get()));
      plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(directoryNode, "ratingKey");
      plexItem->GetVideoInfoTag()->m_type = MediaTypePlexTvShow;
      plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(directoryNode, "title");
      plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(directoryNode, "tagline"));
      plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(directoryNode, "summary"));
      value = XMLUtils::GetAttribute(directoryNode, "thumb");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url1.SetFileName(value);
      plexItem->SetArt("thumb", url1.Get());
      
      value = XMLUtils::GetAttribute(directoryNode, "art");
      if (!value.empty() && (value[0] == '/'))
        StringUtils::TrimLeft(value, "/");
      url1.SetFileName(value);
      plexItem->SetArt("fanart", url1.Get());
      
      plexItem->GetVideoInfoTag()->m_iYear = atoi(XMLUtils::GetAttribute(directoryNode, "year").c_str());
      plexItem->GetVideoInfoTag()->m_fRating = atof(XMLUtils::GetAttribute(directoryNode, "rating").c_str());
      plexItem->GetVideoInfoTag()->m_strMPAARating = XMLUtils::GetAttribute(directoryNode, "contentRating");
      
      time_t addedTime = atoi(XMLUtils::GetAttribute(directoryNode, "addedAt").c_str());
      CDateTime aTime(addedTime);
      plexItem->GetVideoInfoTag()->m_dateAdded = aTime;
      plexItem->GetVideoInfoTag()->m_iSeason = atoi(XMLUtils::GetAttribute(directoryNode, "childCount").c_str());
      plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(directoryNode, "leafCount").c_str());
      plexItem->GetVideoInfoTag()->m_playCount = atoi(XMLUtils::GetAttribute(directoryNode, "viewedLeafCount").c_str());
      
      plexItem->SetProperty("totalseasons", XMLUtils::GetAttribute(directoryNode, "childCount"));
      plexItem->SetProperty("totalepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
      plexItem->SetProperty("numepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
      plexItem->SetProperty("watchedepisodes", plexItem->GetVideoInfoTag()->m_playCount);
      plexItem->SetProperty("unwatchedepisodes", plexItem->GetVideoInfoTag()->m_iEpisode - plexItem->GetVideoInfoTag()->m_playCount);
      
      plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->GetVideoInfoTag()->m_playCount >= plexItem->GetVideoInfoTag()->m_iEpisode);
      
      GetVideoDetails(*plexItem, directoryNode);
      
      items.Add(plexItem);
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
  }
  items.SetProperty("library.filter", "true");
}

void CPlexClient::GetLocalSeasons(CFileItemList &items, const std::string url)
{
  TiXmlDocument xml = GetPlexXML(url);
  std::string value;
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    const TiXmlElement* directoryNode = rootXmlNode->FirstChildElement("Directory");
    while (directoryNode)
    {
      // only get the seasons listing, the one with "ratingKey"
      if (((TiXmlElement*) directoryNode)->Attribute("ratingKey"))
      {
        CFileItemPtr plexItem(new CFileItem());
        plexItem->m_bIsFolder = true;
        plexItem->SetLabel(XMLUtils::GetAttribute(directoryNode, "title"));
        CURL url1(url);
        url1.SetProtocol("http");
        url1.SetFileName("library/metadata/" + XMLUtils::GetAttribute(directoryNode, "ratingKey") + "/children");
        plexItem->SetPath("plex://tvshows/seasons/" + Base64::Encode(url1.Get()));
        plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(directoryNode, "ratingKey");
        plexItem->GetVideoInfoTag()->m_type = MediaTypePlexSeason;
        plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(directoryNode, "title");
        // we get these from rootXmlNode, where all show info is
        plexItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(rootXmlNode, "parentTitle");
        plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(rootXmlNode, "tagline"));
        plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(rootXmlNode, "summary"));
        value = XMLUtils::GetAttribute(rootXmlNode, "art");
        if (!value.empty() && (value[0] == '/'))
          StringUtils::TrimLeft(value, "/");
        url1.SetFileName(value);
        plexItem->SetArt("fanart", url1.Get());
        /// -------
        value = XMLUtils::GetAttribute(directoryNode, "thumb");
        if (!value.empty() && (value[0] == '/'))
          StringUtils::TrimLeft(value, "/");
        url1.SetFileName(value);
        plexItem->SetArt("thumb", url1.Get());
        plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(directoryNode, "leafCount").c_str());
        plexItem->GetVideoInfoTag()->m_playCount = atoi(XMLUtils::GetAttribute(directoryNode, "viewedLeafCount").c_str());
        
        plexItem->SetProperty("totalseasons", XMLUtils::GetAttribute(directoryNode, "childCount"));
        plexItem->SetProperty("totalepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
        plexItem->SetProperty("numepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
        plexItem->SetProperty("watchedepisodes", plexItem->GetVideoInfoTag()->m_playCount);
        plexItem->SetProperty("unwatchedepisodes", plexItem->GetVideoInfoTag()->m_iEpisode - plexItem->GetVideoInfoTag()->m_playCount);
        
        plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->GetVideoInfoTag()->m_playCount >= plexItem->GetVideoInfoTag()->m_iEpisode);
        
        items.Add(plexItem);
      }
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
  }
  items.SetProperty("library.filter", "true");
}

void CPlexClient::GetLocalEpisodes(CFileItemList &items, const std::string url)
{
  CURL url2(url);
  TiXmlDocument xml = GetPlexXML(url);
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    int season = atoi(XMLUtils::GetAttribute(rootXmlNode, "parentIndex").c_str());
    GetVideoItems(items,url2,rootXmlNode, MediaTypePlexEpisode, season);
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
  }
}

void CPlexClient::GetLocalRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit)
{
  CURL url2(url);
  std::string strXML;
  XFILE::CCurlFile http;
  url2.SetProtocol("http");
  
  url2.SetFileName(url2.GetFileName() + "recentlyAdded");
  url2.SetProtocolOptions(url2.GetProtocolOptions() + StringUtils::Format("&X-Plex-Container-Start=0&X-Plex-Container-Size=%i", limit));
  
  http.Get(url2.Get(), strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    GetVideoItems(items,url2,rootXmlNode, MediaTypePlexEpisode);
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
    items.Sort(SortByDateAdded, SortOrderDescending);
  }
}

void CPlexClient::GetLocalRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit)
{  
  CURL url2(url);
  std::string strXML;
  XFILE::CCurlFile http;
  url2.SetProtocol("http");
  
  url2.SetFileName(url2.GetFileName() + "recentlyAdded");
  url2.SetProtocolOptions(url2.GetProtocolOptions() + StringUtils::Format("&X-Plex-Container-Start=0&X-Plex-Container-Size=%i", limit));
  
  http.Get(url2.Get(), strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    GetVideoItems(items,url2,rootXmlNode, MediaTypePlexMovie);
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
    items.Sort(SortByDateAdded, SortOrderDescending);
  }
}

void CPlexClient::GetAllRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow)
{
  //look through all plex servers and pull recently added for each library section
  std::vector<PlexServer> servers;
  std::vector<SectionsContent> contents;
  CPlexServices::GetInstance().GetServers(servers);
  for (int i = 0; i < (int)servers.size(); i++)
  {
    if (tvShow)
      contents = servers[i].GetTvContent();
    else
      contents = servers[i].GetMovieContent();
    for (int c = 0; c < (int)contents.size(); c++)
    {
      CURL curl(servers[i].GetUrl());
      curl.SetProtocol("http");
      curl.SetFileName(curl.GetFileName() + contents[c].section + "/");
      
      if (tvShow)
        GetLocalRecentlyAddedEpisodes(items,curl.Get(), 10);
      else
      {
        GetLocalRecentlyAddedMovies(items,curl.Get(), 10);
      }
      
    }
  }
}

void CPlexClient::GetLocalFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter)
{
  TiXmlDocument xml = GetPlexXML(url,filter);
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    const TiXmlElement* directoryNode = rootXmlNode->FirstChildElement("Directory");
    while (directoryNode)
    {
      std::string title = XMLUtils::GetAttribute(directoryNode, "title");
      std::string key = XMLUtils::GetAttribute(directoryNode, "key");
      CFileItemPtr pItem(new CFileItem(title));
      pItem->m_bIsFolder = true;
      pItem->m_bIsShareOrDrive = false;
      CURL url3(url);
      url3.SetProtocol("http");
      url3.SetFileName(url3.GetFileName() + "all?" + filter + "=" + key);
      pItem->SetPath(parentPath + Base64::Encode(url3.Get()));
      pItem->SetLabel(title);
      pItem->SetProperty("SkipLocalArt", true);
      items.Add(pItem);
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
  }
}