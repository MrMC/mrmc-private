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

enum class EmbySectionParsing
{
  newSection,
  checkSection,
  updateSection,
};

struct EmbyConnection
{
  std::string port;
  std::string address;
  std::string protocol;
  int external;
};

struct EmbySectionsContent
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
  std::string path;
  std::string section;
  std::string art;
  std::string thumb;
};

class CFileItem;
typedef std::shared_ptr<CFileItem> CFileItemPtr;
typedef std::vector<EmbySectionsContent> EmbySectionsContentVector;


class CEmbyClient
{
  friend class CEmbyServices;

public:
  CEmbyClient();
 ~CEmbyClient();

  bool Init(const TiXmlElement* DeviceNode);
  bool Init(const CVariant &data, std::string ip);

  const bool NeedUpdate() const             { return m_needUpdate; }
  const std::string &GetContentType() const { return m_contentType; }
  const std::string &GetServerName() const  { return m_serverName; }
  const std::string &GetUuid() const        { return m_uuid; }
  const std::string &GetOwned() const       { return m_owned; }
  // bool GetPresence() const                  { return m_presence; }
  bool GetPresence() const                  { return true; }
  const std::string &GetProtocol() const    { return m_protocol; }
  const bool &IsLocal() const               { return m_local; }
  const bool IsCloud() const                { return (m_platform == "Cloud"); }

  void  AddSectionItem(CFileItemPtr root)   { m_section_items.push_back(root); };
  std::vector<CFileItemPtr> GetSectionItems()  { return m_section_items; };
  void ClearSectionItems()                  { m_section_items.clear(); };

  const EmbySectionsContentVector GetTvContent() const;
  const EmbySectionsContentVector GetMovieContent() const;
  const EmbySectionsContentVector GetArtistContent() const;
  const EmbySectionsContentVector GetPhotoContent() const;
  const std::string FormatContentTitle(const std::string contentTitle) const;
  std::string FindSectionTitle(const std::string &path);

  std::string GetHost();
  int         GetPort();
  std::string GetUrl();

protected:
  bool        IsSameClientHostName(const CURL& url);
  std::string LookUpUuid(const std::string path) const;
  bool        ParseSections(enum EmbySectionParsing parser);
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
  std::string m_protocol;
  std::string m_platform;
  std::atomic<bool> m_presence;
  std::atomic<bool> m_needUpdate;
  std::vector<CFileItemPtr> m_section_items;
  CCriticalSection  m_criticalMovies;
  CCriticalSection  m_criticalTVShow;
  CCriticalSection  m_criticalArtist;
  CCriticalSection  m_criticalPhoto;
  std::vector<EmbySectionsContent> m_movieSectionsContents;
  std::vector<EmbySectionsContent> m_showSectionsContents;
  std::vector<EmbySectionsContent> m_artistSectionsContents;
  std::vector<EmbySectionsContent> m_photoSectionsContents;
};
