/*
 *      Copyright (C) 2014 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "PlexDirectory.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/PlexFile.h"
#include "services/plex/PlexClient.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"

using namespace XFILE;

CPlexDirectory::CPlexDirectory()
{ }

CPlexDirectory::~CPlexDirectory()
{ }

bool CPlexDirectory::GetDirectory(const CURL& url, CFileItemList &items)
{
  // change the protocol back to http
  CURL url2(url);
  // should look up server and grap the scheme from it.
  url2.SetProtocol("http");

  std::string response;
  XFILE::CCurlFile plex;
  if (plex.Get(url2.Get(), response))
  {
    CPlexClient::GetInstance().GetMovies(items, response, MediaTypePlexMovie);
    return true;
  }

  return false;
}

std::string CPlexDirectory::TranslatePath(const CURL &url)
{
  std::string translatedPath;
  if (!CPlexFile::TranslatePath(url, translatedPath))
    return "";

  return translatedPath;
}
