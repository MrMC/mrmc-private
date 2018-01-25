/*
 *      Copyright (C) 2018 Team MrMC
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

#include "TVOSDirectory.h"
#include "TVOSFile.h"
#include "FileItem.h"
#include "SpecialProtocol.h"
#include "URL.h"
#include "platform/darwin/DarwinNSUserDefaults.h"
#include "utils/URIUtils.h"
#include "utils/log.h"


using namespace XFILE;

CTVOSDirectory::CTVOSDirectory()
{
}

CTVOSDirectory::~CTVOSDirectory()
{
}

bool CTVOSDirectory::GetDirectory(const CURL& url, CFileItemList &items)
{
  std::string rootpath = CSpecialProtocol::TranslatePath(url);
  size_t found = rootpath.find("Caches/home/userdata");
  if (found == std::string::npos)
    return false;

  // we never save directories but files with full paths
  std::vector<std::string> contents;
  CDarwinNSUserDefaults::GetDirectoryContents(rootpath, contents);
  for (const auto &path : contents)
  {
    std::string itemLabel = URIUtils::GetFileName(path);
    CFileItemPtr pItem(new CFileItem(itemLabel));
    pItem->m_bIsFolder = false;
    pItem->SetPath(URIUtils::AddFileToFolder("file://", path));
    if (!(m_flags & DIR_FLAG_NO_FILE_INFO))
    {
      struct __stat64 buffer;
      CTVOSFile tvOSFile;
      CURL url2(pItem->GetPath());
      if (tvOSFile.Stat(url2, &buffer) == 0)
      {
        // fake the file datetime
        FILETIME fileTime, localTime;
        TimeTToFileTime(buffer.st_mtime, &fileTime);
        FileTimeToLocalFileTime(&fileTime, &localTime);
        pItem->m_dateTime = localTime;
        // all this to get the file size
        pItem->m_dwSize = buffer.st_size;
      }
    }
    items.Add(pItem);
  }

  return !contents.empty();
}

bool CTVOSDirectory::Exists(const CURL& url)
{
  return true;
}

DIR_CACHE_TYPE CTVOSDirectory::GetCacheType(const CURL& url) const
{
  return DIR_CACHE_NEVER;
}

