/*
 *      Copyright (C) 2014 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "PlexDirectory.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/PlexFile.h"
#include "services/plex/PlexClient.h"
#include "services/plex/PlexServices.h"
#include "utils/Base64.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"

#include "video/VideoDatabase.h"


using namespace XFILE;

CPlexDirectory::CPlexDirectory()
{ }

CPlexDirectory::~CPlexDirectory()
{ }

bool CPlexDirectory::GetDirectory(const CURL& url, CFileItemList &items)
{
  CVideoDatabase database;
  database.Open();
  bool hasMovies = database.HasContent(VIDEODB_CONTENT_MOVIES);
  bool hasTvShows = database.HasContent(VIDEODB_CONTENT_TVSHOWS);
  database.Close();
  
  std::string strUrl = url.Get();
  if (StringUtils::StartsWithNoCase(url.Get(), "plex://movies/"))
  {
    std::string section = URIUtils::GetFileName(strUrl);
    items.ClearItems();
    
    items.SetPath(strUrl);
    
//    CFileItemPtr pItem(new CFileItem(".."));
//    pItem->SetPath(strUrl);
//    pItem->m_bIsFolder = true;
//    pItem->m_bIsShareOrDrive = false;
//    items.AddFront(pItem, 0);
    std::string basePath = strUrl;
    URIUtils::RemoveSlashAtEnd(basePath);
    basePath = URIUtils::GetFileName(basePath);
    
    if (section.empty())
    {
      if (hasMovies)
      {
        //add local Shows
        CFileItemPtr pItem(new CFileItem("Local Movies"));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        pItem->SetPath("videodb://movies/" + basePath + "/");
        pItem->SetLabel("Local Movies");
        items.Add(pItem);
      }
      //look through all plex servers and pull content data for "movie" type
      std::vector<PlexServer> servers;
      CPlexServices::GetInstance().GetServers(servers);
      for (int i = 0; i < (int)servers.size(); i++)
      {
        std::vector<SectionsContent> contents = servers[i].GetMovieContent();
        if (contents.size() > 1 || (hasMovies && contents.size() == 1))
        {
          for (int c = 0; c < (int)contents.size(); c++)
          {
            std::string title = StringUtils::Format("Plex - %s - %s",servers[i].GetServerName().c_str(),contents[c].title.c_str());
            std::string host = servers[i].GetUrl();
            URIUtils::RemoveSlashAtEnd(host);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(servers[i].GetUrl());
            curl.SetProtocol("http");
            std::string filename = StringUtils::Format("%s/%s", contents[c].section.c_str(), (basePath == "titles"? "all":""));
            curl.SetFileName(filename);
            pItem->SetPath("plex://movies/" + basePath + "/" + Base64::Encode(curl.Get()));
            pItem->SetLabel(title);
            items.Add(pItem);
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(servers[i].GetUrl());
          curl.SetProtocol(servers[i].GetScheme());
          CPlexClient::GetInstance().GetLocalMovies(items,curl.GetFileName() + Base64::Decode(contents[0].section) + "/all");
          items.SetContent("movies");
          items.SetPath("");
        }
        StringUtils::ToCapitalize(basePath);
        items.SetLabel(basePath);
      }
    }
    else
    {
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);
      
      std::string filter = "year";
      if (path == "genres")
        filter = "genre";
      else if (path == "actors")
        filter = "actor";
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
      
      if (path == "titles" || path == "filter")
      {
        CPlexClient::GetInstance().GetLocalMovies(items, Base64::Decode(section));
        items.SetLabel("Titles");
        items.SetContent("movies");
      }
      else
      {
        CPlexClient::GetInstance().GetLocalFilter(items, Base64::Decode(section), "plex://movies/filter/", filter);
        StringUtils::ToCapitalize(path);
        items.SetLabel(path);
        items.SetContent("movies");
      }
    }
    return true;
  }  

  return false;
}

std::string CPlexDirectory::TranslatePath(const CURL &url)
{
  std::string translatedPath;
  if (!CPlexFile::TranslatePath(url, translatedPath))
    return "";

  return translatedPath;
}
