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

#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"


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

void CPlexClient::GetLocalMovies(std::string url)
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
  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url, strXML);

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
      type="movie" title="12 Years a Slave"
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
      const char* ratingKey = ((TiXmlElement*) videoNode)->Attribute("ratingKey");

      
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
          
          const char* file = ((TiXmlElement*) partNode)->Attribute("file");
        }
      }

      videoNode = videoNode->NextSiblingElement("Video");
    }
  }
}