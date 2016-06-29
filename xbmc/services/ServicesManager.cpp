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

#include "services/ServicesManager.h"

#include "services/plex/PlexUtils.h"
#include "video/VideoInfoTag.h"

CServicesManager::CServicesManager()
{
}

CServicesManager::~CServicesManager()
{
}

CServicesManager& CServicesManager::GetInstance()
{
  static CServicesManager sServicesManager;
  return sServicesManager;
}


bool CServicesManager::HasServices()
{
  return CPlexUtils::HasClients();
}

void CServicesManager::SetWatched(CFileItem &item)
{
  if (item.HasProperty("PlexItem"))
  {
    CPlexUtils::SetWatched(item);
  }
}

void CServicesManager::SetUnWatched(CFileItem &item)
{
  if (item.HasProperty("PlexItem"))
  {
    CPlexUtils::SetUnWatched(item);
  }
}

void CServicesManager::SetResumePoint(CFileItem &item)
{
  if (item.HasProperty("PlexItem"))
  {
    CPlexUtils::SetOffset(item, item.GetVideoInfoTag()->m_resumePoint.timeInSeconds);
  }
}

void CServicesManager::UpdateFileProgressState(CFileItem &item, double currentTime)
{
  if (item.HasProperty("PlexItem"))
  {
    CPlexUtils::ReportProgress(item, currentTime);
  }
}

void CServicesManager::GetAllRecentlyAddedMovies(CFileItemList &recentlyAdded, int itemLimit)
{
  if (CPlexUtils::GetAllRecentlyAddedMoviesAndShows(recentlyAdded, false))
  {
    CFileItemList temp;
    recentlyAdded.Sort(SortByDateAdded, SortOrderDescending);
    for (int i = 0; i < recentlyAdded.Size() && i < itemLimit; i++)
      temp.Add(recentlyAdded.Get(i));

    recentlyAdded.ClearItems();
    recentlyAdded.Append(temp);
  }
}

void CServicesManager::GetAllRecentlyAddedShows(CFileItemList &recentlyAdded, int itemLimit)
{
  if (CPlexUtils::GetAllRecentlyAddedMoviesAndShows(recentlyAdded, true))
  {
    CFileItemList temp;
    recentlyAdded.Sort(SortByDateAdded, SortOrderDescending);
    for (int i = 0; i < recentlyAdded.Size() && i < itemLimit; i++)
      temp.Add(recentlyAdded.Get(i));

    recentlyAdded.ClearItems();
    recentlyAdded.Append(temp);
  }
}
