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

namespace XFILE
{
  class CCurlFile;
}

class CPlexUtils
{
public:
  static void GetDefaultHeaders(XFILE::CCurlFile &curl);

  static void SetWatched(CFileItem &item);
  static void SetUnWatched(CFileItem &item);
  static void SetOffset(CFileItem &item, int offsetSeconds);
  static bool GetLocalRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit=25);
  static bool GetLocalRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit=25);
  static bool GetAllRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow=false);

  static bool GetLocalMovies(CFileItemList &items, std::string url, std::string filter = "");
  static bool GetLocalTvshows(CFileItemList &items, std::string url);
  static bool GetLocalSeasons(CFileItemList &items, const std::string url);
  static bool GetLocalEpisodes(CFileItemList &items, const std::string url);
  static bool GetLocalFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter);
  static void ReportProgress(CFileItemPtr item);

private:
  static bool GetVideoItems(CFileItemList &items,CURL url, TiXmlElement* rootXmlNode, std::string type, int season = -1);
  static void GetVideoDetails(CFileItem &item, const TiXmlElement* videoNode);
  static TiXmlDocument GetPlexXML(std::string url, std::string filter = "");
};
