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

enum PlexSectionParsing
{
  newSection,
  checkSection,
  updateSection,
};

struct PlexSectionsContent
{
  int port;
  std::string type;
  std::string title;
  std::string agent;
  std::string scanner;
  std::string language;
  std::string uuid;
  std::string updatedAt;
  std::string address;
  //std::string serverName;
  //std::string serverVersion;
  std::string path;
  std::string section;
  std::string art;
  std::string thumb;
};

class CFileItem;
typedef std::shared_ptr<CFileItem> CFileItemPtr;
typedef std::vector<PlexSectionsContent> PlexSectionsContentVector;


class CPlexClient
{
  friend class CPlexServices;

public:
  CPlexClient(std::string data, std::string ip);
  CPlexClient(const TiXmlElement* DeviceNode);
 ~CPlexClient();

  const bool NeedUpdate() const             { return m_needUpdate; }
  const std::string &GetContentType() const { return m_contentType; }
  const std::string &GetServerName() const  { return m_serverName; }
  const std::string &GetUuid() const        { return m_uuid; }
  const std::string &GetOwned() const       { return m_owned; }
  bool GetPresence() const                  { return m_presence; }
  const std::string &GetScheme() const      { return m_scheme; }
  const bool &IsLocal() const { return m_local; }

  void  SetRootItem(CFileItemPtr root)      { m_root_item = root; };
  CFileItemPtr GetRootItem()                { return m_root_item;};

  const PlexSectionsContentVector GetTvContent() const;
  const PlexSectionsContentVector GetMovieContent() const;
  std::string GetHost();
  int         GetPort();
  std::string GetUrl();

protected:
  bool        IsMe(const CURL& url);
  std::string LookUpUuid(const std::string path) const;
  bool        ParseSections(PlexSectionParsing parser);
  void        SetPresence(bool presence);

private:
  bool        m_local;
  std::string m_contentType;
  std::string m_uuid;
  std::string m_owned;
  std::string m_serverName;
  std::string m_url;
  std::string m_accessToken;
  std::string m_httpsRequired;
  std::string m_scheme;
  std::atomic<bool> m_presence;
  std::atomic<bool> m_needUpdate;
  CFileItemPtr m_root_item;
  CCriticalSection  m_criticalMovies;
  CCriticalSection  m_criticalTVShow;
  std::vector<PlexSectionsContent> m_movieSectionsContents;
  std::vector<PlexSectionsContent> m_showSectionsContents;
};
