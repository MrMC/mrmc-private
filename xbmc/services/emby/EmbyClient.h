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

#include "URL.h"
#include "utils/XMLUtils.h"
#include "threads/CriticalSection.h"

enum class EmbyViewParsing
{
  newView,
  checkView,
  updateView,
};

typedef struct EmbyServerInfo
{
  std::string Id;
  std::string Version;
  std::string ServerName;
  std::string WanAddress;
  std::string LocalAddress;
  std::string OperatingSystem;
} EmbyServerInfo;

struct EmbyConnection
{
  std::string port;
  std::string address;
  std::string protocol;
  int external;
};

struct EmbyViewContent
{
  std::string id;
  std::string name;
  std::string etag;
  std::string serverid;
  std::string mediaType;
  std::string viewprefix;
};

class CFileItem;
class CEmbyClientSync;
typedef std::shared_ptr<CFileItem> CFileItemPtr;
typedef std::vector<EmbyViewContent> EmbyViewContentVector;

class CEmbyClient
{
  friend class CEmbyServices;

public:
  CEmbyClient();
 ~CEmbyClient();

  bool Init(const std::string &userId, const std::string &accessToken, const EmbyServerInfo &serverInfo);

  const bool NeedUpdate() const             { return m_needUpdate; }
  const std::string &GetServerName() const  { return m_serverInfo.ServerName; }
  const std::string &GetUuid() const        { return m_userId; }
  const std::string &GetOwned() const       { return m_owned; }
  // bool GetPresence() const                  { return m_presence; }
  bool GetPresence() const                  { return true; }
  const std::string &GetProtocol() const    { return m_protocol; }
  const bool &IsLocal() const               { return m_local; }
  const bool IsCloud() const                { return (m_platform == "Cloud"); }

  void  AddViewItem(CFileItemPtr root)
  {
    auto item = std::find(m_viewItems.begin(), m_viewItems.end(), root);
    if (item == m_viewItems.end())
      m_viewItems.push_back(root);
  };
  std::vector<CFileItemPtr> GetViewItems()  { return m_viewItems; };
  void ClearViewItems()                     { m_viewItems.clear(); };

  const EmbyViewContentVector GetTvContent() const;
  const EmbyViewContentVector GetMovieContent() const;
  const EmbyViewContentVector GetArtistContent() const;
  const EmbyViewContentVector GetPhotoContent() const;
  const std::string FormatContentTitle(const std::string contentTitle) const;
  std::string FindViewName(const std::string &path);
  CFileItemPtr FindItemByServiceId(const std::string &Id);

  std::string GetHost();
  int         GetPort();
  std::string GetUrl();

protected:
  bool        IsSameClientHostName(const CURL& url);
  bool        ParseViews(enum EmbyViewParsing parser);
  void        SetPresence(bool presence);
  bool        NeedViewUpdate(const EmbyViewContent &content, const EmbyViewContent &contents, const std::string server);

private:
  bool        m_local;
  std::string m_owned;
  std::string m_userId;
  std::string m_accessToken;
  std::string m_url;
  std::string m_protocol;
  std::string m_platform;
  EmbyServerInfo m_serverInfo;
  std::atomic<bool> m_presence;
  std::atomic<bool> m_needUpdate;
  CEmbyClientSync *m_clientSync;

  CCriticalSection  m_viewItemsLock;
  std::vector<CFileItemPtr> m_viewItems;

  CCriticalSection  m_viewMoviesContentsLock;
  CCriticalSection  m_viewTVshowContentsLock;
  CCriticalSection  m_viewArtistContentsLock;
  CCriticalSection  m_viewPhotosContentsLock;
  std::vector<EmbyViewContent> m_viewMoviesContents;
  std::vector<EmbyViewContent> m_viewTVshowContents;
  std::vector<EmbyViewContent> m_viewArtistContents;
  std::vector<EmbyViewContent> m_viewPhotosContents;
};
