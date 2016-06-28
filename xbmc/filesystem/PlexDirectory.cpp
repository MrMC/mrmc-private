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
#include "services/plex/PlexUtils.h"
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
  
  items.ClearItems();
  std::string strUrl = url.Get();
  std::string section = URIUtils::GetFileName(strUrl);
  items.SetPath(strUrl);
  std::string basePath = strUrl;
  URIUtils::RemoveSlashAtEnd(basePath);
  basePath = URIUtils::GetFileName(basePath);
  
  if (StringUtils::StartsWithNoCase(strUrl, "plex://movies/"))
  {
    if (section.empty())
    {
      if (hasMovies)
      {
        //add local Movies
        CFileItemPtr pItem(new CFileItem("MrMC - Movies"));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedmovies")
          pItem->SetPath("videodb://recentlyaddedmovies/");
        else
          pItem->SetPath("videodb://movies/" + basePath + "/");
        pItem->SetLabel("MrMC - Movies");
        items.Add(pItem);
      }
      //look through all plex clients and pull content data for "movie" type
      std::vector<CPlexClient> clients;
      CPlexServices::GetInstance().GetClients(clients);
      for (int i = 0; i < (int)clients.size(); i++)
      {
        std::vector<SectionsContent> contents = clients[i].GetMovieContent();
        if (contents.size() > 1 || ((hasMovies || clients.size() > 1) && contents.size() == 1))
        {
          for (int c = 0; c < (int)contents.size(); c++)
          {
            std::string title = StringUtils::Format("Plex - %s - %s",clients[i].GetServerName().c_str(),contents[c].title.c_str());
            std::string host = clients[i].GetUrl();
            URIUtils::RemoveSlashAtEnd(host);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(clients[i].GetUrl());
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
          CURL curl(clients[i].GetUrl());
          curl.SetProtocol(clients[i].GetScheme());
          CPlexUtils::GetLocalMovies(items,curl.GetFileName() + Base64::Decode(contents[0].section) + "/all");
          items.SetContent("movies");
          items.SetPath("");
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedmovies")
          label = "Recently added movies";
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
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
        CPlexUtils::GetLocalMovies(items, Base64::Decode(section));
        items.SetLabel("Titles");
        items.SetContent("movies");
      }
      if (path == "recentlyaddedmovies")
      {
        CPlexUtils::GetLocalRecentlyAddedMovies(items, Base64::Decode(section));
        items.SetLabel("Recently added movies");
        items.SetContent("movies");
      }
      else
      {
        CPlexUtils::GetLocalFilter(items, Base64::Decode(section), "plex://movies/filter/", filter);
        StringUtils::ToCapitalize(path);
        items.SetLabel(path);
        items.SetContent("movies");
      }
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "plex://tvshows/"))
  {
    if (section.empty())
    {
      if (hasTvShows)
      {
        //add local Shows
        CFileItemPtr pItem(new CFileItem("MrMC - TvShows"));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedepisodes")
          pItem->SetPath("videodb://recentlyaddedepisodes/");
        else
          pItem->SetPath("videodb://tvshows/" + basePath + "/");
        pItem->SetLabel("MrMC - TvShows");
        items.Add(pItem);
      }
      //look through all plex servers and pull content data for "show" type
      std::vector<CPlexClient> clients;
      CPlexServices::GetInstance().GetClients(clients);
      for (int i = 0; i < (int)clients.size(); i++)
      {
        std::vector<SectionsContent> contents = clients[i].GetTvContent();
        if (contents.size() > 1 || ((hasTvShows || clients.size() > 1) && contents.size() == 1))
        {
          for (int c = 0; c < (int)contents.size(); c++)
          {
            std::string title = StringUtils::Format("Plex - %s - %s",clients[i].GetServerName().c_str(),contents[c].title.c_str());
            std::string host = clients[i].GetUrl();
            URIUtils::RemoveSlashAtEnd(host);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(clients[i].GetUrl());
            curl.SetProtocol("http");
            std::string filename = StringUtils::Format("%s/%s", contents[c].section.c_str(), (basePath == "titles"? "all":""));
            curl.SetFileName(filename);
            pItem->SetPath("plex://tvshows/" + basePath + "/" + Base64::Encode(curl.Get()));
            pItem->SetLabel(title);
            items.Add(pItem);
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(clients[i].GetUrl());
          curl.SetProtocol(clients[i].GetScheme());
          CPlexUtils::GetLocalTvshows(items,curl.GetFileName() + Base64::Decode(contents[0].section) + "/all");
          items.SetContent("tvshows");
          items.SetPath("");
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedepisodes")
          label = "Recently added episodes";
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
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
      else if (path == "studios")
        filter = "studio";
      
      if (path == "titles" || path == "filter")
      {
        CPlexUtils::GetLocalTvshows(items,Base64::Decode(section));
        items.SetLabel("Titles");
        items.SetContent("tvshows");
      }
      else if (path == "shows")
      {
        CPlexUtils::GetLocalSeasons(items,Base64::Decode(section));
        items.SetContent("seasons");
      }
      else if (path == "seasons")
      {
        CPlexUtils::GetLocalEpisodes(items,Base64::Decode(section));
        items.SetContent("episodes");
      }
      if (path == "recentlyaddedepisodes")
      {
        CPlexUtils::GetLocalRecentlyAddedEpisodes(items, Base64::Decode(section));
        items.SetLabel("Recently added episodes");
        items.SetContent("episodes");
      }
      else
      {
        CPlexUtils::GetLocalFilter(items, Base64::Decode(section), "plex://tvshows/filter/", filter);
        StringUtils::ToCapitalize(path);
        items.SetLabel(path);
        items.SetContent("tvshows");
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
