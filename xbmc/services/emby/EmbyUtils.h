#pragma once
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

#include <string>
#include "FileItem.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

#define EMBY_DEBUG_VERBOSE

namespace XFILE
{
  class CCurlFile;
}
class CEmbyClient;
typedef std::shared_ptr<CEmbyClient> CEmbyClientPtr;


enum class EmbyUtilsPlayerState
{
  paused = 1,
  playing = 2,
  stopped = 3,
};

typedef struct EmbyMediaCount
{
  int iMovieTotal = 0;
  int iMovieUnwatched = 0;
  int iEpisodeTotal = 0;
  int iEpisodeUnwatched = 0;
  int iShowTotal = 0;
  int iShowUnwatched = 0;
  int iMusicSongs = 0;
  int iMusicAlbums = 0;
  int iMusicArtist = 0;
  
} EmbyMediaCount;

class CEmbyUtils
{
public:
  static bool HasClients();
  static bool GetIdentity(CURL url, int timeout);
  static void GetDefaultHeaders(const std::string& userId, XFILE::CCurlFile &curl);
  static void SetEmbyItemProperties(CFileItem &item);
  static void SetEmbyItemProperties(CFileItem &item, const CEmbyClientPtr &client);
  static void SetEmbyItemsProperties(CFileItemList &items);
  static void SetEmbyItemsProperties(CFileItemList &items, const CEmbyClientPtr &client);

  static void SetWatched(CFileItem &item);
  static void SetUnWatched(CFileItem &item);
  static void ReportProgress(CFileItem &item, double currentSeconds);
  static void SetPlayState(EmbyUtilsPlayerState state);
  static bool GetEmbyRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit=25);
  static bool GetEmbyInProgressShows(CFileItemList &items, const std::string url, int limit=25);
  static bool GetEmbyRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit=25);
  static bool GetEmbyInProgressMovies(CFileItemList &items, const std::string url, int limit=25);
  static bool GetAllEmbyInProgress(CFileItemList &items, bool tvShow);
  static bool GetAllEmbyRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow=false);

  // Emby Movie/TV
  static bool GetEmbyMovies(CFileItemList &items, std::string url, std::string filter = "");
  static bool GetEmbyTvshows(CFileItemList &items, std::string url);
  static bool GetEmbySeasons(CFileItemList &items, const std::string url);
  static bool GetEmbyEpisodes(CFileItemList &items, const std::string url);
  static bool GetEmbyFilter(CFileItemList &items, std::string url, std::string parentPath, std::string filter);
  static bool GetItemSubtiles(CFileItem &item);
  static bool GetMoreItemInfo(CFileItem &item);
  static bool GetMoreResolutions(CFileItem &item);
  static bool GetURL(CFileItem &item);
  static bool SearchEmby(CFileItemList &items, std::string strSearchString);
  
  // Emby Music
  static bool GetEmbyArtistsOrAlbum(CFileItemList &items, std::string url, bool album);
  static bool GetEmbySongs(CFileItemList &items, std::string url);
  static bool ShowMusicInfo(CFileItem item);
  static bool GetEmbyRecentlyAddedAlbums(CFileItemList &items,int limit);
  static bool GetEmbyAlbumSongs(CFileItem item, CFileItemList &items);
  static bool GetEmbyMediaTotals(EmbyMediaCount &totals);

private:
  static void ReportToServer(std::string url, std::string filename);
  static bool GetVideoItems(CFileItemList &items,CURL url, TiXmlElement* rootXmlNode, std::string type, bool formatLabel, int season = -1);
  static void GetVideoDetails(CFileItem &item, const TiXmlElement* videoNode);
  static void GetMusicDetails(CFileItem &item, const TiXmlElement* videoNode);
  static void GetMediaDetals(CFileItem &item, CURL url, const TiXmlElement* videoNode, std::string id = "0");
  static TiXmlDocument GetEmbyXML(std::string url, std::string filter = "");
  static int ParseEmbyMediaXML(TiXmlDocument xml);
  static void RemoveSubtitleProperties(CFileItem &item);
};
