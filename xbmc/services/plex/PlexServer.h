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

class PlexServer
{
  friend class CPlexDiscovery;

public:
  PlexServer(std::string data, std::string ip);

  const std::string &GetContentType() const { return m_sContentType; }
  const std::string &GetServerName() const { return m_sServerName; }
  int64_t GetUpdated() const { return m_nUpdated; }
  const std::string &GetUuid() const { return m_sUuid; }
  const std::string &GetVersion() const { return m_sVersion; }
  const std::string &GetAuthToken() const { return m_authToken; }
  void  SetAuthToken(std::string token) { m_authToken = token; }

  const bool &IsLocal() const { return m_bLocal; }

  std::string GetHost();
  int         GetPort();
  std::string GetUri();

protected:
  void ParseData(std::string data, std::string ip);

private:
  std::string m_sDiscovery;

  int         m_nOwned;
  bool        m_bLocal;
  int         m_nMaster;
  std::string m_sRole;
  std::string m_sContentType;
  std::string m_sUuid;
  std::string m_sServerName;
  std::string m_uri;
  std::string m_authToken;
  int64_t     m_nUpdated;
  std::string m_sVersion;
};