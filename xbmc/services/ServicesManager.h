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

#include <map>

#include "utils/JobManager.h"
#include "threads/SharedSection.h"
#include "filesystem/IDirectory.h"

class CURL;
class CFileItem;
class CFileItemList;

class IMediaServicesHandler: public XFILE::IDirectory
{
public:
  virtual ~IMediaServicesHandler() { }

  virtual void SetWatched(CFileItem &item) { }
  virtual void SetUnWatched(CFileItem &item) { }
  virtual void SetResumePoint(CFileItem &item) { }
  virtual void UpdateFileProgressState(CFileItem &item, double currentTime) { }
  virtual void GetAllRecentlyAddedMovies(CFileItemList &recentlyAdded, int itemLimit) { }
  virtual void GetAllRecentlyAddedShows(CFileItemList &recentlyAdded, int itemLimit){ }

  virtual bool GetDirectory(const CURL& url, CFileItemList &items);
  virtual XFILE::DIR_CACHE_TYPE GetCacheType(const CURL& url);
};

class CServicesManager: public CJobQueue
{
public:
  static CServicesManager &GetInstance();

  bool HasServices();
  void SetWatched(CFileItem &item);
  void SetUnWatched(CFileItem &item);
  void SetResumePoint(CFileItem &item);
  void UpdateFileProgressState(CFileItem &item, double currentTime);
  void GetAllRecentlyAddedMovies(CFileItemList &recentlyAdded, int itemLimit);
  void GetAllRecentlyAddedShows(CFileItemList &recentlyAdded, int itemLimit);

  bool GetDirectory(const CURL& url, CFileItemList &items);
  XFILE::DIR_CACHE_TYPE GetCacheType(const CURL& url);

  void RegisterMediaServicesHandler(IMediaServicesHandler *mediaServicesHandler);
  void UnregisterSettingsHandler(IMediaServicesHandler *mediaServicesHandler);

private:
  // private construction, and no assignements; use the provided singleton methods
  CServicesManager();
  CServicesManager(const CServicesManager&);
  virtual ~CServicesManager();


  typedef std::vector<IMediaServicesHandler*> MediaServicesHandlers;
  MediaServicesHandlers m_mediaServicesHandlers;
  CSharedSection m_mediaServicesCritical;
};
