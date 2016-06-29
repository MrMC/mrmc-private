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
  };

class CPlexClient
{
  friend class CPlexServices;

public:
  CPlexClient(std::string data, std::string ip);
  CPlexClient(const TiXmlElement* DeviceNode);

  const std::string &GetContentType() const { return m_contentType; }
  const std::string &GetServerName() const { return m_serverName; }
  const std::string &GetUuid() const { return m_uuid; }
  const std::string &GetAuthToken() const { return m_authToken; }
  const std::string &GetScheme() const { return m_scheme; }
  void  SetAuthToken(std::string token) { m_authToken = token; }
  const std::vector<SectionsContent> &GetTvContent() const { return m_showSectionsContents; }
  const std::vector<SectionsContent> &GetMovieContent() const { return m_movieSectionsContents; }
  

  const bool &IsLocal() const { return m_local; }

  std::string GetHost();
  int         GetPort();
  std::string GetUrl();

protected:
  void ParseData(std::string data, std::string ip);
  void ParseSections();

private:
  bool        m_local;
  std::string m_contentType;
  std::string m_uuid;
  std::string m_serverName;
  std::string m_url;
  std::string m_authToken;
  std::string m_scheme;
  std::vector<SectionsContent> m_movieSectionsContents;
  std::vector<SectionsContent> m_showSectionsContents;
};
