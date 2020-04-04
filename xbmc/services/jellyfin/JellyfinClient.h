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

/*
Direct Play - The client plays the file by accessing the file system
  directly using the Path property. The server is bypassed with this
  mechanism. Whenever possible this is the most desirable form of playback.

Direct Stream - The client streams the file from the server as-is, in
  its original format, without any encoding or remuxing applied.
  Aside from Direct Play, this is the next most desirable playback method.

Transcode - The client streams the file from the server with encoding
  applied in order to convert it to a format that it can understand.
*/


#include <string>
#include <vector>
#include <memory>

#include "URL.h"
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"

class CFileItem;
class CFileItemList;
class CJellyfinViewCache;
class CJellyfinClientSync;
typedef struct JellyfinViewInfo JellyfinViewInfo;
typedef struct JellyfinViewContent JellyfinViewContent;
typedef std::shared_ptr<CFileItem> CFileItemPtr;
typedef std::shared_ptr<CJellyfinViewCache> CJellyfinViewCachePtr;

typedef struct JellyfinServerInfo
{
  std::string UserId;
  std::string AccessToken;

  std::string UserType;
  std::string ServerId;
  std::string AccessKey;
  std::string ServerURL;
  std::string ServerName;
  std::string WanAddress;
  std::string LocalAddress;
} JellyfinServerInfo;
typedef std::vector<JellyfinServerInfo> JellyfinServerInfoVector;


class CJellyfinClient
{
  friend class CJellyfinServices;
  friend class CThreadedFetchViewItems;

public:
  CJellyfinClient();
 ~CJellyfinClient();

  bool Init(const JellyfinServerInfo &serverInfo);

  const bool NeedUpdate() const             { return m_needUpdate; }
  const std::string &GetServerName() const  { return m_serverInfo.ServerName; }
  const std::string &GetUuid() const        { return m_serverInfo.UserId; }
  // bool GetPresence() const                  { return m_presence; }
  bool GetPresence() const                  { return true; }
  const std::string &GetProtocol() const    { return m_protocol; }
  const bool &IsLocal() const               { return m_local; }
  const bool IsCloud() const                { return (m_platform == "Cloud"); }
  const bool IsOwned() const                { return m_owned; }

  void  SetWatched(CFileItem &item);
  void  SetUnWatched(CFileItem &item);

  void  UpdateLibrary(const std::string &content);

  // main view entry points (from CJellyfinDirectory)
  bool  GetMovies(CFileItemList &items, std::string url, bool fromfilter);
  bool  GetMoviesFilter(CFileItemList &items, std::string url, std::string filter);
  bool  GetMoviesFilters(CFileItemList &items, std::string url);
  bool  GetTVShows(CFileItemList &items, std::string url, bool fromfilter);
  bool  GetTVShowsFilter(CFileItemList &items, std::string url, std::string filter);
  bool  GetTVShowFilters(CFileItemList &items, std::string url);
  bool  GetMusicArtists(CFileItemList &items, std::string url);

  void  AddNewViewItems(const std::vector<std::string> &ids);
  void  UpdateViewItems(const std::vector<std::string> &ids);
  void  RemoveViewItems(const std::vector<std::string> &ids);

  const std::vector<JellyfinViewInfo> GetJellyfinSections();
  const std::vector<JellyfinViewInfo> GetViewInfoForMovieContent() const;
  const std::vector<JellyfinViewInfo> GetViewInfoForMusicContent() const;
  const std::vector<JellyfinViewInfo> GetViewInfoForPhotoContent() const;
  const std::vector<JellyfinViewInfo> GetViewInfoForTVShowContent() const;
  const std::string FormatContentTitle(const std::string contentTitle) const;

  const CVariant FetchItemById(const std::string &Id);
  
  std::string GetUrl();
  std::string GetHost();
  int         GetPort();
  std::string GetUserID();

protected:
  bool        IsSameClientHostName(const CURL& url);
  bool        FetchViews();
  bool        FetchViewItems(CJellyfinViewCachePtr &view, const CURL& url, const std::string &type);
  bool        FetchFilterItems(CJellyfinViewCachePtr &view, const CURL &url, const std::string &type, const std::string &filter);
  void        SetPresence(bool presence);
  const CVariant FetchItemByIds(const std::vector<std::string> &Ids);
  const std::string FetchViewIdByItemId(const std::string &Id);

private:
  bool        UpdateItemInCache(const CVariant &variant);
  bool        AppendItemToCache(const std::string &viewId, const CVariant &variant);
  bool        RemoveItemFromCache(const std::string &itemId);

  bool m_local;
  std::string m_url;
  bool m_owned;
  std::string m_protocol;
  std::string m_platform;
  JellyfinServerInfo m_serverInfo;
  std::atomic<bool> m_presence;
  std::atomic<bool> m_needUpdate;
  CJellyfinClientSync  *m_clientSync;

  CJellyfinViewCachePtr m_viewMoviesFilter;
  CJellyfinViewCachePtr m_viewTVShowsFilter;
  CCriticalSection m_viewMoviesFilterLock;
  CCriticalSection m_viewTVShowsFilterLock;

  CCriticalSection m_topViewIDsLock;
  CCriticalSection m_viewMusicLock;
  CCriticalSection m_viewMoviesLock;
  CCriticalSection m_viewPhotosLock;
  CCriticalSection m_viewTVShowsLock;
  std::vector<std::string> m_topViewIDs;
  std::vector<CJellyfinViewCachePtr> m_viewMusic;
  std::vector<CJellyfinViewCachePtr> m_viewMovies;
  std::vector<CJellyfinViewCachePtr> m_viewPhotos;
  std::vector<CJellyfinViewCachePtr> m_viewTVShows;

};
