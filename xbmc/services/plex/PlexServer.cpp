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

#include "PlexServer.h"

#include "URL.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

#include <string>
#include <sstream>


PlexServer::PlexServer(std::string data, std::string ip)
{
  ParseData(data, ip);
}

void PlexServer::ParseData(std::string data, std::string ip)
{
  int port = 0;
  std::istringstream f(data);
  std::string s;
  while (std::getline(f, s))
  {
    int pos = s.find(':');
    if (pos > 0)
    {
      std::string substr = s.substr(0, pos);
      std::string name = StringUtils::Trim(substr);
      substr = s.substr(pos + 1);
      std::string val = StringUtils::Trim(substr);
      if (name == "Content-Type")
        m_contentType = val;
      else if (name == "Resource-Identifier")
        m_uuid = val;
      else if (name == "Name")
        m_serverName = val;
      else if (name == "Port")
        port = atoi(val.c_str());
      else if (name == "Updated-At")
        m_updated = atol(val.c_str());
      else if (name == "Version")
        m_version = val;
    }
  }

  m_local = true;

  CURL url;
  url.SetHostName(ip);
  url.SetPort(port);
  url.SetProtocol("http");

  m_url = url.Get();
}

std::string PlexServer::GetUrl()
{
  return m_url;
}

std::string PlexServer::GetHost()
{
  CURL url(m_url);
  return url.GetHostName();
}

int PlexServer::GetPort()
{
  CURL url(m_url);
  return url.GetPort();
}
/*
std::shared_ptr<Poco::Net::HTTPClientSession> PlexServer::GetClientSession()
{
  Poco::URI uri(m_uri);
  std::shared_ptr<Poco::Net::HTTPClientSession> pHttpSession = nullptr;
  if (uri.getScheme().find("https") != std::string::npos)
    pHttpSession = std::make_shared<Poco::Net::HTTPSClientSession>(uri.getHost(), uri.getPort());
  else
    pHttpSession = std::make_shared<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());

  //pHttpSession->setTimeout(Poco::Timespan(5, 0)); // set 5 seconds Timeout
  return pHttpSession;
}

std::shared_ptr<Poco::Net::HTTPClientSession> PlexServer::MakeRequest(
 bool &ok, std::string path, const std::map<std::string, std::string> &queryParameters)
{
    Poco::URI uri(path);
    // Create a request with an optional query
    if (queryParameters.size())
    {
        for (auto const &pair : queryParameters)
        {
            // addQueryParameter does the encode already
            uri.addQueryParameter(pair.first, pair.second);
        }
    }
    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery(),
                                   Poco::Net::HTTPMessage::HTTP_1_1);

    request.add("User-Agent",
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_2) AppleWebKit/537.17 (KHTML, like Gecko) Chrome/24.0.1312.52 Safari/537.17");
    request.add("X-Plex-Client-Identifier", Config::GetInstance().GetUUID());
    request.add("X-Plex-Device", "PC");
    request.add("X-Plex-Device-Name", Config::GetInstance().GetHostname());
    request.add("X-Plex-Language", Config::GetInstance().GetLanguage());
    request.add("X-Plex-Model", "Linux");
    request.add("X-Plex-Platform", "MrMC");
    request.add("X-Plex-Product",  "MrMC/Plex");
    request.add("X-Plex-Provides", "player");
    request.add("X-Plex-Version",  VERSION);

    if (Config::GetInstance().UsePlexAccount && !GetAuthToken().empty())
    {
        // Add PlexToken to Header
        request.add("X-Plex-Token", GetAuthToken());
    }
    auto cSession = GetClientSession();
    ok = true;
    try {
        cSession->sendRequest(request);
    } catch (Poco::TimeoutException &exc) {
        esyslog("[plex] Timeout: %s", path.c_str());
        ok = false;
    } catch (Poco::Exception &exc) {
        esyslog("[plex] Oops Exception: %s", exc.displayText().c_str());
        ok = false;
    }
    return cSession;
}
*/