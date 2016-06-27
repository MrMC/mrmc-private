#pragma once
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

#include <string>
#include "FileItem.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

class CPlexClient
{
public:
  CPlexClient();
  ~CPlexClient();
  static CPlexClient &GetInstance();
  void SetWatched(CFileItem* item);
  void SetUnWatched(CFileItem* item);
  void SetOffset(CFileItem item, int offsetSeconds);
  void GetLocalRecentlyAddedEpisodes(CFileItemList &items);
  void GetLocalRecentlyAddedMovies(CFileItemList &items);
  

  void GetLocalMovies(CFileItemList &items, std::string url, std::string filter = "");
  void GetLocalTvshows(CFileItemList &items, std::string url);
  void GetLocalSeasons(CFileItemList &items, const std::string url);
  void GetLocalEpisodes(CFileItemList &items, const std::string url);
  void GetLocalFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter);

private:
  // private construction, and no assignements; use the provided singleton methods
  void GetVideoItems(CFileItemList &items,CURL url, TiXmlElement* rootXmlNode, std::string type, int season = -1);
  void GetVideoDetails(CFileItem &item, const TiXmlElement* videoNode);
  TiXmlDocument GetPlexXML(std::string url, std::string filter = "");
  
};
