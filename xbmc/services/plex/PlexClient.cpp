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
#include "utils/XBMCTinyXML.h"
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

void CPlexClient::SetWatched(std::string id)
{
  // http://localhost:32400/:/scrobble?identifier=com.plexapp.plugins.library&amp;key=
  
  std::string url = "http://192.168.1.200:32400";
  std::string scrobbleUrl = StringUtils::Format("%s/:/scrobble?identifier=com.plexapp.plugins.library&key=%s", url.c_str(), id.c_str());
  XFILE::CCurlFile http;
  std::string response;
  http.Get(scrobbleUrl, response);
}

void CPlexClient::SetUnWatched(std::string id)
{
  //http://localhost:32400/:/unscrobble?identifier=com.plexapp.plugins.library&amp;key=
  std::string url = "http://192.168.1.200:32400";
    std::string unscrobbleUrl = StringUtils::Format("%s/:/unscrobble?identifier=com.plexapp.plugins.library&key=%s", url.c_str(), id.c_str());
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

void CPlexClient::GetLocalMovies(CFileItemList &items)
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
  
  std::string url = "http://192.168.1.200:32400";
  std::string movieXmlPath = url + "/library/sections/1/all";
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(movieXmlPath, strXML);

  TiXmlDocument xml;
  xml.Parse(strXML.c_str());

  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
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
      plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(videoNode, "ratingKey");
      plexItem->GetVideoInfoTag()->m_type = MediaTypePlexMovie;
      plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(videoNode, "title");
      plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(videoNode, "tagline"));
      plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(videoNode, "summary"));
      plexItem->SetArt("fanart", url + XMLUtils::GetAttribute(videoNode, "art"));
      plexItem->SetArt("thumb", url + XMLUtils::GetAttribute(videoNode, "thumb"));
      plexItem->GetVideoInfoTag()->m_iYear = atoi(XMLUtils::GetAttribute(videoNode, "year").c_str());
      plexItem->GetVideoInfoTag()->m_fRating = atof(XMLUtils::GetAttribute(videoNode, "rating").c_str());
      plexItem->GetVideoInfoTag()->m_strMPAARating = XMLUtils::GetAttribute(videoNode, "contentRating");
      
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
        const char* videoResolution = ((TiXmlElement*) mediaNode)->Attribute("videoResolution");
      
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
          std::string path = url + ((TiXmlElement*) partNode)->Attribute("key");
          plexItem->SetPath(path);
//          plexItem->GetVideoInfoTag()->SetFile(path);
        }
      }

      videoNode = videoNode->NextSiblingElement("Video");
      items.Add(plexItem);
    }
  }
}

void CPlexClient::GetLocalTvshows(CFileItemList &items)
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
  
  std::string url = "http://192.168.1.200:32400";
  std::string tvshowXmlPath = url + "/library/sections/2/all";
  
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
      // set m_bIsFolder to true to indicate we wre tvshow list
      plexItem->m_bIsFolder = true;
      plexItem->SetPath("plex://tvshow");
      plexItem->GetVideoInfoTag()->m_strPlexId = XMLUtils::GetAttribute(directoryNode, "ratingKey");
      plexItem->GetVideoInfoTag()->m_type = MediaTypePlexEpisode;
      plexItem->GetVideoInfoTag()->m_strTitle = XMLUtils::GetAttribute(directoryNode, "title");
      plexItem->GetVideoInfoTag()->SetPlotOutline(XMLUtils::GetAttribute(directoryNode, "tagline"));
      plexItem->GetVideoInfoTag()->SetPlot(XMLUtils::GetAttribute(directoryNode, "summary"));
      plexItem->SetArt("fanart", url + XMLUtils::GetAttribute(directoryNode, "art"));
      plexItem->SetArt("thumb", url + XMLUtils::GetAttribute(directoryNode, "thumb"));
      plexItem->GetVideoInfoTag()->m_iYear = atoi(XMLUtils::GetAttribute(directoryNode, "year").c_str());
      plexItem->GetVideoInfoTag()->m_fRating = atof(XMLUtils::GetAttribute(directoryNode, "rating").c_str());
      plexItem->GetVideoInfoTag()->m_strMPAARating = XMLUtils::GetAttribute(directoryNode, "contentRating");
      
      time_t addedTime = atoi(XMLUtils::GetAttribute(directoryNode, "addedAt").c_str());
      CDateTime aTime(addedTime);
      plexItem->GetVideoInfoTag()->m_dateAdded = aTime;
      plexItem->GetVideoInfoTag()->m_iSeason = atoi(XMLUtils::GetAttribute(directoryNode, "childCount").c_str());
      plexItem->GetVideoInfoTag()->m_iEpisode = atoi(XMLUtils::GetAttribute(directoryNode, "leafCount").c_str());
      plexItem->GetVideoInfoTag()->m_playCount = atoi(XMLUtils::GetAttribute(directoryNode, "viewedLeafCount").c_str());
      
//      plexItem->m_dateTime = XMLUtils::GetAttribute(directoryNode, "contentRating");
      plexItem->SetProperty("totalseasons", XMLUtils::GetAttribute(directoryNode, "childCount"));
      plexItem->SetProperty("totalepisodes", plexItem->GetVideoInfoTag()->m_iEpisode);
      plexItem->SetProperty("numepisodes", plexItem->GetVideoInfoTag()->m_iEpisode); // will be changed later to reflect watchmode setting
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