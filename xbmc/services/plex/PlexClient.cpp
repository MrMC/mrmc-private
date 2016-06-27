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
//  m_strUrl = "http://192.168.2.202:32400";
}

CPlexClient::~CPlexClient()
{
}

CPlexClient& CPlexClient::GetInstance()
{
  static CPlexClient sPlexClient;
  return sPlexClient;
}

//void CPlexClient::HandleMedia(CFileItemList &items, bool &bResult , std::string strDirectory)
//{
//  if (StringUtils::StartsWithNoCase(strDirectory, "videodb://recentlyaddedepisodes/"))
//  {
//    GetLocalRecentlyAddedEpisodes(items);
//    items.SetContent("episodes");
//    bResult = true;
//  }
//  else if (StringUtils::StartsWithNoCase(strDirectory, "videodb://recentlyaddedmovies/"))
//  {
//    GetLocalRecentlyAddedMovies(items);
//    items.SetContent("movies");
//    bResult = true;
//  }
//}

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
    /*
     attributes:
     
     ratingKey="65"
     key="/library/metadata/65"
     studio="Plan B Entertainment"
     type="movie"
     title="12 Years a Slave"
     contentRating="R"
     summary="In the pre-Civil War United States, Solomon Northup, a free black man from upstate New York, is abducted and sold into slavery. Facing cruelty as well as unexpected kindnesses Solomon struggles not only to stay alive, but to retain his dignity. In the twelfth year of his unforgettable odyssey, Solomon’s chance meeting with a Canadian abolitionist will forever alter his life."
     rating="7.8"
     viewOffset="158000"
     lastViewedAt="1465154711"
     year="2013"
     tagline="The extraordinary true story of Solomon Northup"
     thumb="/library/metadata/65/thumb/1465152014"
     art="/library/metadata/65/art/1465152014"
     duration="8050153"
     originallyAvailableAt="2013-10-30"
     addedAt="1392893688"
     updatedAt="1465152014"
     chapterSource=""
     
     */
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
      url.SetFileName(XMLUtils::GetAttribute(videoNode, "parentThumb"));
      plexItem->SetArt("tvshow.poster", url.Get());
      plexItem->SetArt("thumb", url.Get());
      plexItem->SetArt("tvshow.thumb", url.Get());
      plexItem->SetIconImage(url.Get());
    }
    else
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      plexItem->SetLabel(XMLUtils::GetAttribute(videoNode, "title"));
      // is this the only way to set token and URL??
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
      /*
       attributes:
       
       videoResolution="720"
       id="65"
       duration="8050153"
       bitrate="964"
       width="1280"
       height="536"
       aspectRatio="2.35"
       audioChannels="2"
       audioCodec="aac"
       videoCodec="h264"
       container="mp4"
       videoFrameRate="24p"
       optimizedForStreaming="1"
       audioProfile="lc"
       has64bitOffsets="0"
       videoProfile="high"
       */
      
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
        
        /*
         
         attributes:
         
         id="66"
         key="/library/parts/66/file.mp4"
         duration="8050153"
         file="/share/Movies/12 Years a Slave (2013)/12.Years.a.Slave.2013.720p.BluRay.x264.YIFY.mp4"
         size="970087940"
         audioProfile="lc"
         container="mp4"
         has64bitOffsets="0"
         optimizedForStreaming="1"
         videoProfile="high"
         
         */
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
  // this shit is needed to display movies/episodes properly ... dont ask
  // good thing it didnt take 2 days to figure it out
  items.SetProperty("library.filter", "true");
}

void CPlexClient::GetLocalMovies(CFileItemList &items, std::string url, std::string filter)
{
 /*
  <Video ratingKey="65" key="/library/metadata/65" studio="Plan B Entertainment" type="movie" title="12 Years a Slave" contentRating="R" summary="In the pre-Civil War United States, Solomon Northup, a free black man from upstate New York, is abducted and sold into slavery. Facing cruelty as well as unexpected kindnesses Solomon struggles not only to stay alive, but to retain his dignity. In the twelfth year of his unforgettable odyssey, Solomon’s chance meeting with a Canadian abolitionist will forever alter his life." rating="7.8" viewOffset="158000" lastViewedAt="1465154711" year="2013" tagline="The extraordinary true story of Solomon Northup" thumb="/library/metadata/65/thumb/1465152014" art="/library/metadata/65/art/1465152014" duration="8050153" originallyAvailableAt="2013-10-30" addedAt="1392893688" updatedAt="1465152014" chapterSource="">
  <Media videoResolution="720" id="65" duration="8050153" bitrate="964" width="1280" height="536" aspectRatio="2.35" audioChannels="2" audioCodec="aac" videoCodec="h264" container="mp4" videoFrameRate="24p" optimizedForStreaming="1" audioProfile="lc" has64bitOffsets="0" videoProfile="high">
  <Part id="66" key="/library/parts/66/file.mp4" duration="8050153" file="/share/Movies/12 Years a Slave (2013)/12.Years.a.Slave.2013.720p.BluRay.x264.YIFY.mp4" size="970087940" audioProfile="lc" container="mp4" has64bitOffsets="0" optimizedForStreaming="1" videoProfile="high" />
  </Media>
  <Genre tag="Drama" />
  <Genre tag="History" />
  <Writer tag="John Ridley" />
  <Director tag="Steve McQueen" />
  <Country tag="USA" />
  <Country tag="United Kingdom" />
  <Role tag="Chiwetel Ejiofor" />
  <Role tag="Michael Fassbender" />
  <Role tag="Benedict Cumberbatch" />
  </Video>
  */
  
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
  /*
   <Directory
     ratingKey="2255"
     key="/library/metadata/2255/children" 
     studio="The CW" 
     type="show" 
     title="The 100" 
     titleSort="100" 
     contentRating="TV-14" 
     summary="Based on the books by Kass Morgan, this show takes place 100 years in the future, when the Earth has been abandoned due to radioactivity. The last surviving humans live on an ark orbiting the planet — but the ark won&apos;t last forever. So the repressive regime picks 100 expendable juvenile delinquents to send down to Earth to see if the planet is still habitable."
     index="1" 
     rating="8.0" 
     year="2014" 
     thumb="/library/metadata/2255/thumb/1465158605" 
     art="/library/metadata/2255/art/1465158605" 
     banner="/library/metadata/2255/banner/1465158605" 
     theme="/library/metadata/2255/theme/1465158605" 
     duration="2700000" 
     originallyAvailableAt="2014-03-19" 
     leafCount="44"
     viewedLeafCount="0"
     childCount="3" 
     addedAt="1410897813" 
     updatedAt="1465158605">
     
   <Genre tag="Drama" />
   <Genre tag="Science-Fiction" />
   <Genre tag="Suspense" />
   <Role tag="Bob Morley" />
   <Role tag="Marie Avgeropoulos" />
   <Role tag="Eliza Taylor" />
   </Directory>
   
   */
  
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
  /*
   <MediaContainer size="4" allowSync="1" art="/library/metadata/2255/art/1465158605" banner="/library/metadata/2255/banner/1465158605" identifier="com.plexapp.plugins.library" key="2255" librarySectionID="2" librarySectionTitle="TV Shows" librarySectionUUID="dd1b7de4-a445-4c1a-b3c9-5d3f7232d857" mediaTagPrefix="/system/bundle/media/flags/" mediaTagVersion="1454491350" nocache="1" parentIndex="1" parentTitle="The 100" parentYear="2014" summary="Based on the books by Kass Morgan, this show takes place 100 years in the future, when the Earth has been abandoned due to radioactivity. The last surviving humans live on an ark orbiting the planet — but the ark won&apos;t last forever. So the repressive regime picks 100 expendable juvenile delinquents to send down to Earth to see if the planet is still habitable." theme="/library/metadata/2255/theme/1465158605" thumb="/library/metadata/2255/thumb/1465158605" title1="TV Shows" title2="The 100" viewGroup="season" viewMode="65593">
   <Directory leafCount="44" thumb="/library/metadata/2255/thumb/1465158605" viewedLeafCount="0" key="/library/metadata/2255/allLeaves" title="All episodes" />
   
   <Directory 
     ratingKey="2289"
     key="/library/metadata/2289/children"
     parentRatingKey="2255" 
     type="season" 
     title="Season 1" 
     parentKey="/library/metadata/2255" 
     summary="" 
     index="1"
     thumb="/library/metadata/2289/thumb/1465158577" 
     leafCount="13" 
     viewedLeafCount="0" 
     addedAt="1410897813" 
     updatedAt="1465158577">
   </Directory>
   
   <Directory ratingKey="2272" key="/library/metadata/2272/children" parentRatingKey="2255" type="season" title="Season 2" parentKey="/library/metadata/2255" summary="" index="2" thumb="/library/metadata/2272/thumb/1465158583" leafCount="16" viewedLeafCount="0" addedAt="1414103447" updatedAt="1465158583"></Directory>
   <Directory ratingKey="2256" key="/library/metadata/2256/children" parentRatingKey="2255" type="season" title="Season 3" parentKey="/library/metadata/2255" summary="" index="3" thumb="/library/metadata/2256/thumb/1465158605" leafCount="15" viewedLeafCount="0" addedAt="1453437187" updatedAt="1465158605"></Directory>
   </MediaContainer>
   
   */
  
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
        // set m_bIsFolder to true to indicate we wre tvshow list
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

void CPlexClient::GetLocalRecentlyAddedEpisodes(CFileItemList &items)
{
  // /library/sections/2/recentlyAdded?X-Plex-Container-Start=0&X-Plex-Container-Size=10
  
  std::string seasonsXmlPath; // = StringUtils::Format("%s/library/sections/2/recentlyAdded?X-Plex-Container-Start=0&X-Plex-Container-Size=10", m_strUrl.c_str());
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(seasonsXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
//    GetVideoItems(items,rootXmlNode, MediaTypePlexEpisode);
//    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
    items.Sort(SortByDateAdded, SortOrderDescending);
  }
}

void CPlexClient::GetLocalRecentlyAddedMovies(CFileItemList &items)
{
  // /library/sections/2/recentlyAdded?X-Plex-Container-Start=0&X-Plex-Container-Size=10
  
  std::string seasonsXmlPath;// = StringUtils::Format("%s/library/sections/1/recentlyAdded?X-Plex-Container-Start=0&X-Plex-Container-Size=10", m_strUrl.c_str());
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(seasonsXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
//    GetVideoItems(items,rootXmlNode, MediaTypePlexMovie);
    //    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
    items.Sort(SortByDateAdded, SortOrderDescending);
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