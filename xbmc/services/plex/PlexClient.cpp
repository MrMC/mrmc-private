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
  m_strUrl = "http://192.168.1.200:32400";
}

CPlexClient::~CPlexClient()
{
  
}

CPlexClient& CPlexClient::GetInstance()
{
  static CPlexClient sPlexClient;
  return sPlexClient;
}

void CPlexClient::HandleMedia(CFileItemList &items, bool &bResult , std::string strDirectory)
{
  /*
  "videodb://tvshows/genres"
  "videodb://tvshows/titles"
  "videodb://tvshows/years"
  "videodb://tvshows/actors"
  "videodb://tvshows/studios"
  "videodb://tvshows/tags"
  
   <Directory secondary="1" key="collection" title="By Collection" />
   <Directory secondary="1" key="firstCharacter" title="By First Letter" />
   <Directory secondary="1" key="genre" title="By Genre" />
   <Directory secondary="1" key="year" title="By Year" />
   <Directory secondary="1" key="contentRating" title="By Content Rating" />
   
   "videodb://movies/titles"
   "videodb://movies/years"
   "videodb://movies/genres"
   "videodb://movies/actors"
   "videodb://movies/directors"
   "videodb://movies/sets"
   "videodb://movies/countries"
   "videodb://movies/studios"
   
   "videodb://movies/tags"
   
   <Directory secondary="1" key="collection" title="By Collection" />
   <Directory secondary="1" key="genre" title="By Genre" />
   <Directory secondary="1" key="year" title="By Year" />
   <Directory secondary="1" key="decade" title="By Decade" />
   <Directory secondary="1" key="director" title="By Director" />
   <Directory secondary="1" key="actor" title="By Starring Actor" />
   <Directory secondary="1" key="country" title="By Country" />
   <Directory secondary="1" key="contentRating" title="By Content Rating" />
   <Directory secondary="1" key="rating" title="By Rating" />
   <Directory secondary="1" key="resolution" title="By Resolution" />
   <Directory secondary="1" key="firstCharacter" title="By First Letter" />
   
   */
// start MOVIES
  if (StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/titles/"))
  {
    GetLocalMovies(items);
    items.SetContent("movies");
    bResult = true;
  }
  else if (StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/years/")     ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/genres/")    ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/actors/")    ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/directors/") ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/sets/")      ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/countries/") ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://movies/studios/")
           )
  {
    if (items.GetContent() == "movies")
    {
      std::string strLabel = items.GetLabel();
      if (strLabel.empty())
      {
        strLabel = URIUtils::GetFileName(items.GetPath());
      }
      std::string strFilter = "?" + m_filter + "=" + m_vFilter[strLabel];
      GetLocalMovies(items, strFilter);
      items.SetContent("movies");
    }
    else
    {
      std::string filter = "year";
      if (items.GetContent() == "genres")
        filter = "genre";
      else if (items.GetContent() == "actors")
        filter = "actor";
      else if (items.GetContent() == "actors")
        filter = "actor";
      else if (items.GetContent() == "directors")
        filter = "director";
      else if (items.GetContent() == "sets")
        filter = "collection";
      else if (items.GetContent() == "countries")
        filter = "country";
      else if (items.GetContent() == "studios")
        filter = "studio";

      GetLocalFilter(items, filter ,strDirectory, true );
      items.SetContent("movies");
    }
    bResult = true;
  }
// start TVSHOWS
  else if (StringUtils::StartsWithNoCase(strDirectory, "videodb://tvshows/titles/"))
  {
    if (items.GetContent() == "tvshows")
    {
      // list all plex tvShows
      GetLocalTvshows(items);
      items.SetContent("tvshows");
      bResult = true;
    }
  }
  else if (StringUtils::StartsWithNoCase(strDirectory, "videodb://tvshows/years/")     ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://tvshows/genres/")    ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://tvshows/actors/")    ||
           StringUtils::StartsWithNoCase(strDirectory, "videodb://tvshows/studios/")
           )
  {
    if (items.GetContent() == "tvshows")
    {
      std::string strLabel = items.GetLabel();
      if (strLabel.empty())
      {
        strLabel = URIUtils::GetFileName(items.GetPath());
      }
      std::string strFilter = "?" + m_filter + "=" + m_vFilter[strLabel];
      GetLocalTvshows(items, strFilter);
      items.SetContent("tvshows");
    }
    else if (items.GetContent() != "episodes" && items.GetContent() != "seasons")
    {
      std::string filter = "year";
      if (items.GetContent() == "genres")
        filter = "genre";
      else if (items.GetContent() == "actors")
        filter = "actor";
      else if (items.GetContent() == "studios")
        filter = "studio";
      
      GetLocalFilter(items, filter, strDirectory , false);
      items.SetContent("tvshows");
    }
    bResult = true;
  }
  else if (StringUtils::StartsWithNoCase(strDirectory, "plex://tvshow/"))
  {
    // list shows here
    GetLocalSeasons(items,strDirectory);
    items.SetContent("seasons");
    bResult = true;
  }
  else if (StringUtils::StartsWithNoCase(strDirectory, "plex://seasons/"))
  {
    // list seasons here
    GetLocalEpisodes(items,strDirectory);
    items.SetContent("episodes");
    bResult = true;
  }
}

void CPlexClient::SetWatched(std::string id)
{
  // http://localhost:32400/:/scrobble?identifier=com.plexapp.plugins.library&amp;key=
  
  std::string scrobbleUrl = StringUtils::Format("%s/:/scrobble?identifier=com.plexapp.plugins.library&key=%s", m_strUrl.c_str(), id.c_str());
  XFILE::CCurlFile http;
  std::string response;
  http.Get(scrobbleUrl, response);
}

void CPlexClient::SetUnWatched(std::string id)
{
  //http://localhost:32400/:/unscrobble?identifier=com.plexapp.plugins.library&amp;key=
    std::string unscrobbleUrl = StringUtils::Format("%s/:/unscrobble?identifier=com.plexapp.plugins.library&key=%s", m_strUrl.c_str(), id.c_str());
  XFILE::CCurlFile http;
  std::string response;
  http.Get(unscrobbleUrl, response);
}

void CPlexClient::SetOffset(CFileItem item, int offsetSeconds)
{
  // looks like this needs proper server communication using headers, maybe plexTalk.cpp that has all the server communication shit?
  // item.GetVideoInfoTag()->m_strPlexId has ratingKey
  // offsetSeconds is time, time in milliseconds
  // https://www.reddit.com/r/PleX/comments/476a1x/making_an_android_plex_app_what_can_icant_i_do/?
  //192.168.1.200:32400/:/progress?key=418&identifier=com.plexapp.plugins.library&time=7765&state=stopped
  //  http://192.168.1.200:32400/:/timeline?ratingKey=65&key=/library/metadata/65&state=stopped&playQueueItemID=3&time=3010&duration=8050153
}

void CPlexClient::GetVideoItems(CFileItemList &items, TiXmlElement* rootXmlNode, std::string type, int season /* = -1 */)
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
    // if we have season means we are listing episodes, we need to get the fanart from rootXmlNode.
    // movies has it in videoNode
    if (season > -1)
    {
      fanart = XMLUtils::GetAttribute(rootXmlNode, "art");
      plexItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(videoNode, "grandparentTitle");
    }
    else
    {
      fanart = XMLUtils::GetAttribute(videoNode, "art");
      plexItem->SetLabel(XMLUtils::GetAttribute(videoNode, "title"));
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
    
    plexItem->SetArt("fanart", m_strUrl + fanart);
    plexItem->SetArt("thumb", m_strUrl + XMLUtils::GetAttribute(videoNode, "thumb"));
    plexItem->GetVideoInfoTag()->m_iYear = atoi(XMLUtils::GetAttribute(videoNode, "year").c_str());
    plexItem->GetVideoInfoTag()->m_fRating = atof(XMLUtils::GetAttribute(videoNode, "rating").c_str());
    plexItem->GetVideoInfoTag()->m_strMPAARating = XMLUtils::GetAttribute(videoNode, "contentRating");
    plexItem->GetVideoInfoTag()->m_iSeason = season;
    plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(videoNode, "index").c_str());
    
    // lastViewedAt means that it was watched, if so we set m_playCount to 1 and set overlay
    if (((TiXmlElement*) videoNode)->Attribute("lastViewedAt"))
    {
      plexItem->GetVideoInfoTag()->m_playCount = 1;
    }
    plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->HasVideoInfoTag() && plexItem->GetVideoInfoTag()->m_playCount > 0);
    
    // looks like plex is sending only one studio?
    std::vector<std::string> studios;
    studios.push_back(XMLUtils::GetAttribute(videoNode, "studio"));
    plexItem->GetVideoInfoTag()->m_studio = studios;
    
    
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
    plexItem->GetVideoInfoTag()->SetGenre(genres);
    
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
    plexItem->GetVideoInfoTag()->SetWritingCredits(writers);
    
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
    plexItem->GetVideoInfoTag()->SetDirector(directors);
    
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
    plexItem->GetVideoInfoTag()->SetCountry(countries);
    
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
    plexItem->GetVideoInfoTag()->m_cast = roles;
    
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
        std::string path = m_strUrl + ((TiXmlElement*) partNode)->Attribute("key");
        plexItem->SetPath(path);
      }
    }
    
    videoNode = videoNode->NextSiblingElement("Video");
    items.Add(plexItem);
  }
}

void CPlexClient::GetLocalMovies(CFileItemList &items, std::string filter)
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
  
  std::string movieXmlPath = m_strUrl + "/library/sections/1/all" + filter;
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(movieXmlPath, strXML);

  TiXmlDocument xml;
  xml.Parse(strXML.c_str());

  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    GetVideoItems(items,rootXmlNode, MediaTypePlexMovie);
    // this shit is needed to display movies properly ... dont ask, same as episodes.
    // good thing it didnt take 2 days to figure it out
    items.SetProperty("library.filter", "true");
  }
}

void CPlexClient::GetLocalTvshows(CFileItemList &items, std::string filter)
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
  
  std::string tvshowXmlPath = m_strUrl + "/library/sections/2/all"+ filter;
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(tvshowXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    const TiXmlElement* directoryNode = rootXmlNode->FirstChildElement("Directory");
    while (directoryNode)
    {
      CFileItemPtr plexItem(new CFileItem());
      // set m_bIsFolder to true to indicate we are tvshow list
      plexItem->m_bIsFolder = true;
      plexItem->SetPath("plex://tvshow/" + XMLUtils::GetAttribute(directoryNode, "ratingKey"));
      plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(directoryNode, "ratingKey");
      plexItem->GetVideoInfoTag()->m_type = MediaTypePlexTvShow;
      plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(directoryNode, "title");
      plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(directoryNode, "tagline"));
      plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(directoryNode, "summary"));
      plexItem->SetArt("fanart", m_strUrl + XMLUtils::GetAttribute(directoryNode, "art"));
      plexItem->SetArt("thumb", m_strUrl + XMLUtils::GetAttribute(directoryNode, "thumb"));
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
      
      plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->HasVideoInfoTag() && plexItem->GetVideoInfoTag()->m_playCount > 0);
      
      // looks like plex is sending only one studio?
      std::vector<std::string> studios;
      studios.push_back(XMLUtils::GetAttribute(directoryNode, "studio"));
      plexItem->GetVideoInfoTag()->m_studio = studios;
      
      
      // get all genres
      std::vector<std::string> genres;
      const TiXmlElement* genreNode = directoryNode->FirstChildElement("Genre");
      if (genreNode)
      {
        while (genreNode)
        {
          std::string genre = XMLUtils::GetAttribute(genreNode, "tag");
          genres.push_back(genre);
          genreNode = genreNode->NextSiblingElement("Genre");
        }
      }
      plexItem->GetVideoInfoTag()->SetGenre(genres);
      
      // get all writers
      std::vector<std::string> writers;
      const TiXmlElement* writerNode = directoryNode->FirstChildElement("Writer");
      if (writerNode)
      {
        while (writerNode)
        {
          std::string writer = XMLUtils::GetAttribute(writerNode, "tag");
          writers.push_back(writer);
          writerNode = writerNode->NextSiblingElement("Writer");
        }
      }
      plexItem->GetVideoInfoTag()->SetWritingCredits(writers);
      
      // get all directors
      std::vector<std::string> directors;
      const TiXmlElement* directorNode = directoryNode->FirstChildElement("Director");
      if (directorNode)
      {
        while (directorNode)
        {
          std::string director = XMLUtils::GetAttribute(directorNode, "tag");
          directors.push_back(director);
          directorNode = directorNode->NextSiblingElement("Director");
        }
      }
      plexItem->GetVideoInfoTag()->SetDirector(directors);
      
      // get all countries
      std::vector<std::string> countries;
      const TiXmlElement* countryNode = directoryNode->FirstChildElement("Country");
      if (countryNode)
      {
        while (countryNode)
        {
          std::string country = XMLUtils::GetAttribute(countryNode, "tag");
          countries.push_back(country);
          countryNode = countryNode->NextSiblingElement("Country");
        }
      }
      plexItem->GetVideoInfoTag()->SetCountry(countries);
      
      // get all roles
      std::vector< SActorInfo > roles;
      const TiXmlElement* roleNode = directoryNode->FirstChildElement("Role");
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
      plexItem->GetVideoInfoTag()->m_cast = roles;
      
      items.Add(plexItem);
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
  }
}

void CPlexClient::GetLocalSeasons(CFileItemList &items, const std::string directory)
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
  
  items.ClearItems();
  items.SetPath(directory);
  CFileItemPtr pItem(new CFileItem(".."));
  pItem->SetPath(directory);
  pItem->m_bIsFolder = true;
  pItem->m_bIsShareOrDrive = false;
  items.AddFront(pItem, 0);
  
  std::string strID = URIUtils::GetFileName(directory);
  std::string seasonsXmlPath = m_strUrl + "/library/metadata/" + strID + "/children";
//  http://192.168.1.200:32400/library/metadata/2255/children
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(seasonsXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
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
        plexItem->SetPath("plex://seasons/" + XMLUtils::GetAttribute(directoryNode, "ratingKey"));
        plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(directoryNode, "ratingKey");
        plexItem->GetVideoInfoTag()->m_type = MediaTypePlexSeason;
        plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(directoryNode, "title");
        // we get these from rootXmlNode, where all show info is
        plexItem->GetVideoInfoTag()->m_strShowTitle = XMLUtils::GetAttribute(rootXmlNode, "parentTitle");
        plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(rootXmlNode, "tagline"));
        plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(rootXmlNode, "summary"));
        plexItem->SetArt("fanart", m_strUrl + XMLUtils::GetAttribute(rootXmlNode, "art"));
        /// -------
        plexItem->SetArt("thumb", m_strUrl + XMLUtils::GetAttribute(directoryNode, "thumb"));
        plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(directoryNode, "leafCount").c_str());
        plexItem->GetVideoInfoTag()->m_playCount = atoi(XMLUtils::GetAttribute(directoryNode, "viewedLeafCount").c_str());
        
        plexItem->SetProperty("totalseasons", XMLUtils::GetAttribute(directoryNode, "childCount"));
        plexItem->SetProperty("totalepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
        plexItem->SetProperty("numepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
        plexItem->SetProperty("watchedepisodes", plexItem->GetVideoInfoTag()->m_playCount);
        plexItem->SetProperty("unwatchedepisodes", plexItem->GetVideoInfoTag()->m_iEpisode - plexItem->GetVideoInfoTag()->m_playCount);
        
        plexItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, plexItem->HasVideoInfoTag() && plexItem->GetVideoInfoTag()->m_playCount > 0);
        
        items.Add(plexItem);
      }
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
  }
}

void CPlexClient::GetLocalEpisodes(CFileItemList &items, const std::string directory)
{
  items.ClearItems();
  items.SetPath(directory);
  CFileItemPtr pItem(new CFileItem(".."));
  pItem->SetPath(directory);
  pItem->m_bIsFolder = true;
  pItem->m_bIsShareOrDrive = false;
  items.AddFront(pItem, 0);
  
  std::string strID = URIUtils::GetFileName(directory);
  std::string seasonsXmlPath = m_strUrl + "/library/metadata/" + strID + "/children";
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(seasonsXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    int season = atoi(XMLUtils::GetAttribute(rootXmlNode, "parentIndex").c_str());
    GetVideoItems(items,rootXmlNode, MediaTypePlexEpisode, season);
    items.SetLabel(XMLUtils::GetAttribute(rootXmlNode, "title2"));
    // this shit is needed to display episodes properly ... dont ask
    items.SetProperty("library.filter", "true");
  }
}

void CPlexClient::GetLocalFilter(CFileItemList &items, std::string filter, std::string parentPath , bool movie)
{
  
  m_vFilter.clear();
  m_filter = filter;
  
  std::string filterXmlPath = m_strUrl + "/library/sections/" + (movie ? "1":"2") + "/" + filter;
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(filterXmlPath, strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    const TiXmlElement* directoryNode = rootXmlNode->FirstChildElement("Directory");
    while (directoryNode)
    {
      std::string title = XMLUtils::GetAttribute(directoryNode, "title");
      std::string key = XMLUtils::GetAttribute(directoryNode, "key");
      bool add = true;
      for (int i = 0; i < items.Size(); i++)
      {
        if (items[i]->GetLabel() == title)
        {
          add = false;
        }
      }
      
      if (add)
      {
        CFileItemPtr pItem(new CFileItem(title));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        pItem->SetPath(parentPath + "/" + title);
        pItem->SetLabel(title);
        items.Add(pItem);
      }
      
      m_vFilter[title] = key;
      directoryNode = directoryNode->NextSiblingElement("Directory");
    }
  }
}