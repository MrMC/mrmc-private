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
#include "utils/JobManager.h"
#include "video/VideoInfoTag.h"

class CServicesManagerJob: public CJob
{
public:
  CServicesManagerJob(CFileItem &item, double currentTime, std::string strFunction)
  :m_item(*new CFileItem(item)),
  m_function(strFunction),
  m_currentTime(currentTime)
  {
  }
  virtual ~CServicesManagerJob()
  {
    
  }
  virtual bool DoWork()
  {
    if (m_function == "SetWatched")
      CPlexUtils::SetWatched(m_item);
    else if (m_function == "SetUnWatched")
      CPlexUtils::SetWatched(m_item);
    else if (m_function == "SetResume")
      CPlexUtils::SetOffset(m_item, m_item.GetVideoInfoTag()->m_resumePoint.timeInSeconds);
    else if (m_function == "SetProgress")
      CPlexUtils::ReportProgress(m_item, m_currentTime);
    return true;
  }
  virtual bool operator==(const CJob *job) const
  {
    return true;
  }
private:
  CFileItem      &m_item;
  std::string    m_function;
  double         m_currentTime;
};


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
    AddJob(new CServicesManagerJob(item, 0, "SetWatched"));
  }
}

void CServicesManager::SetUnWatched(CFileItem &item)
{
  if (item.HasProperty("PlexItem"))
  {
    AddJob(new CServicesManagerJob(item, 0, "SetUnWatched"));
  }
}

void CServicesManager::SetResumePoint(CFileItem &item)
{
  if (item.HasProperty("PlexItem"))
  {
    AddJob(new CServicesManagerJob(item, 0, "SetResume"));
  }
}

void CServicesManager::UpdateFileProgressState(CFileItem &item, double currentTime)
{
  if (item.HasProperty("PlexItem"))
  {
    AddJob(new CServicesManagerJob(item, currentTime, "SetProgress"));
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

void CServicesManager::RegisterMediaServicesHandler(IMediaServicesHandler *mediaServicesHandler)
{
  if (mediaServicesHandler == nullptr)
    return;

  CExclusiveLock lock(m_mediaServicesCritical);
  if (find(m_mediaServicesHandlers.begin(), m_mediaServicesHandlers.end(), mediaServicesHandler) == m_mediaServicesHandlers.end())
    m_mediaServicesHandlers.push_back(mediaServicesHandler);
}

void CServicesManager::UnregisterSettingsHandler(IMediaServicesHandler *mediaServicesHandler)
{
  if (mediaServicesHandler == NULL)
    return;

  CExclusiveLock lock(m_mediaServicesCritical);
  MediaServicesHandlers::iterator it = find(m_mediaServicesHandlers.begin(), m_mediaServicesHandlers.end(), mediaServicesHandler);
  if (it != m_mediaServicesHandlers.end())
    m_mediaServicesHandlers.erase(it);
}
