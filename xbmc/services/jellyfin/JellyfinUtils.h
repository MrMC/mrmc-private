#pragma once
/*
 *      Copyright (C) 2020 Team MrMC
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
#include "services/ServicesManager.h"

//#define JELLYFIN_DEBUG_VERBOSE
//#define JELLYFIN_DEBUG_TIMING

namespace XFILE
{
  class CCurlFile;
}
class CJellyfinClient;
typedef std::shared_ptr<CJellyfinClient> CJellyfinClientPtr;


static const std::string JellyfinApiKeyHeader = "X-MediaBrowser-Token";
static const std::string JellyfinAuthorizationHeader = "X-Emby-Authorization";

static const std::string JellyfinTypeVideo = "Video";
static const std::string JellyfinTypeAudio = "Audio";
static const std::string JellyfinTypeMovie = "Movie";
static const std::string JellyfinTypeSeries = "Series";
static const std::string JellyfinTypeSeason = "Season";
static const std::string JellyfinTypeSeasons = "Seasons";
static const std::string JellyfinTypeEpisode = "Episode";
static const std::string JellyfinTypeMusicArtist = "MusicArtist";
static const std::string JellyfinTypeMusicAlbum = "MusicAlbum";
static const std::string JellyfinTypeBoxSet = "BoxSet";
static const std::string JellyfinTypeFolder = "Folder";

class CJellyfinUtils
{
public:
  static bool HasClients();
  static void GetClientHosts(std::vector<std::string>& hosts);
  static bool GetIdentity(CURL url, int timeout);
  static void PrepareApiCall(const std::string& userId, const std::string& accessToken, XFILE::CCurlFile &curl);
  static void SetJellyfinItemProperties(CFileItem &item, const char *content);
  static void SetJellyfinItemProperties(CFileItem &item, const char *content, const CJellyfinClientPtr &client);
  static std::string ConstructFileName(const CURL url, const std::string fileNamePath, bool useEmbyInPath = true); // set this to false once Jellyfin turns this off, version 11?);
  static uint64_t TicksToSeconds(uint64_t ticks);
  static uint64_t SecondsToTicks(uint64_t seconds);

  #pragma mark - Jellyfin Server Utils
  static void SetWatched(CFileItem &item);
  static void SetUnWatched(CFileItem &item);
  static void ReportProgress(CFileItem &item, double currentSeconds);
  static void SetPlayState(MediaServicesPlayerState state);
  static bool GetItemSubtiles(CFileItem &item);
  static bool GetMoreItemInfo(CFileItem &item);
  static bool GetMoreResolutions(CFileItem &item);
  static bool GetURL(CFileItem &item);
  static bool SearchJellyfin(CFileItemList &items, std::string strSearchString);
  static bool DeleteJellyfinMedia(CFileItem &item);

  #pragma mark - Jellyfin Recently Added and InProgress
  static bool GetJellyfinRecentlyAddedEpisodes(CFileItemList &items, const std::string url, int limit=25);
  static bool GetJellyfinInProgressShows(CFileItemList &items, const std::string url, int limit=25);
  static bool GetJellyfinRecentlyAddedMovies(CFileItemList &items, const std::string url, int limit=25);
  static bool GetJellyfinInProgressMovies(CFileItemList &items, const std::string url, int limit=25);
  static bool GetAllJellyfinInProgress(CFileItemList &items, bool tvShow);
  static bool GetAllJellyfinRecentlyAddedMoviesAndShows(CFileItemList &items, bool tvShow=false);
  static bool GetAllJellyfinRecentlyAddedAlbums(CFileItemList &items,int limit);
  static bool GetJellyfinRecentlyAddedAlbums(CFileItemList &items, const std::string url, int limit=25);
  static bool GetJellyfinNextUp(CFileItemList &items, const std::string url);

  #pragma mark - Jellyfin Set
  static bool GetJellyfinSet(CFileItemList &items, const std::string url);
  
  #pragma mark - Jellyfin TV
  static bool GetJellyfinSeasons(CFileItemList &items, const std::string url);
  static bool GetJellyfinEpisodes(CFileItemList &items, const std::string url);

  #pragma mark - Jellyfin Music
  static bool GetJellyfinAlbum(CFileItemList &items, std::string url, int limit = 100);
  static bool GetJellyfinArtistAlbum(CFileItemList &items, std::string url);
  static bool GetJellyfinSongs(CFileItemList &items, std::string url);
  static bool GetJellyfinAlbumSongs(CFileItemList &items, std::string url);
  static bool ShowMusicInfo(CFileItem item);
  static bool GetJellyfinAlbumSongs(CFileItem item, CFileItemList &items);
  static bool GetJellyfinMediaTotals(MediaServicesMediaCount &totals);

  static CFileItemPtr ToFileItemPtr(CJellyfinClient *client, const CVariant &object);

  #pragma mark - Jellyfin parsers
  static bool ParseJellyfinVideos(CFileItemList &items, const CURL url, const CVariant &object, std::string type);
  static bool ParseJellyfinSeries(CFileItemList &items, const CURL &url, const CVariant &variant);
  static bool ParseJellyfinSeasons(CFileItemList &items, const CURL &url, const CVariant &series, const CVariant &variant);
  static bool ParseJellyfinAudio(CFileItemList &items, const CURL &url, const CVariant &variant);
  static bool ParseJellyfinAlbum(CFileItemList &items, const CURL &url, const CVariant &variant);
  static bool ParseJellyfinArtists(CFileItemList &items, const CURL &url, const CVariant &variant);
  static bool ParseJellyfinMoviesFilter(CFileItemList &items, const CURL url, const CVariant &object, const std::string &filter);
  static bool ParseJellyfinTVShowsFilter(CFileItemList &items, const CURL url, const CVariant &object, const std::string &filter);
  static CVariant GetJellyfinCVariant(std::string url, std::string filter = "");

private:
  #pragma mark - Jellyfin private
  static CFileItemPtr ToVideoFileItemPtr(CURL url, const CVariant &variant, std::string type);
  static void GetVideoDetails(CFileItem &item, const CVariant &variant);
  static void GetMusicDetails(CFileItem &item, const CVariant &variant);
  static void GetMediaDetals(CFileItem &item, const CVariant &variant, std::string id = "0");
  static void GetResolutionDetails(CFileItem &item, const CVariant &variant);
  static void RemoveSubtitleProperties(CFileItem &item);
  
};
