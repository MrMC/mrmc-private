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
#include "utils/XMLUtils.h"

struct SectionsContent
{
  /*
   <Directory type="movie" title="Movies" agent="com.plexapp.agents.imdb" scanner="Plex Movie Scanner" language="en" uuid="ba486c42-7de0-4472-b4b7-58ff40ffb54a" updatedAt="1465148433" host="94.203.11.95" address="94.203.11.95" port="21499" serverName="Ametovic-Qnap" serverVersion="0.9.16.6.1993-5089475" machineIdentifier="d44a733a35eabd1c67339602de0f8e5f4f9e1063" path="/library/sections/1" key="http://94.203.11.95:21499/library/sections/1" art="http://94.203.11.95:21499/:/resources/movie-fanart.jpg" unique="0" local="0" owned="1" accessToken="zYyEu9uEqdCF94bWzKRb"/>
   */
  int         port;
  std::string type;
  std::string title;
  std::string agent;
  std::string scanner;
  std::string language;
  std::string uuid;
  std::string updatedAt;
  std::string address;
  std::string serverName;
  std::string serverVersion;
  std::string machineIdentifier;
  std::string path;
  std::string section;
  std::string art;
  };

class PlexServer
{
  friend class CPlexServices;

public:
  PlexServer(std::string data, std::string ip);
  PlexServer(const TiXmlElement* ServerNode);

  const std::string &GetContentType() const { return m_contentType; }
  const std::string &GetServerName() const { return m_serverName; }
  int64_t GetUpdated() const { return m_updated; }
  const std::string &GetUuid() const { return m_uuid; }
  const std::string &GetVersion() const { return m_version; }
  const std::string &GetAuthToken() const { return m_authToken; }
  void  SetAuthToken(std::string token) { m_authToken = token; }
  const std::vector<SectionsContent> &GetTvContent() const { return m_showSectionsContents; }
  const std::vector<SectionsContent> &GetMovieContent() const { return m_movieSectionsContents; }
  

  const bool &IsLocal() const { return m_local; }

  std::string GetHost();
  int         GetPort();
  std::string GetUrl();

protected:
  void ParseData(std::string data, std::string ip);
  void GetIdentity();
  void ParseSections();

private:
  std::string m_sDiscovery;

  bool        m_local;
  std::string m_contentType;
  std::string m_uuid;
  std::string m_serverName;
  std::string m_url;
  std::string m_authToken;
  int64_t     m_updated;
  std::string m_version;
  std::vector<SectionsContent> m_movieSectionsContents;
  std::vector<SectionsContent> m_showSectionsContents;
};
