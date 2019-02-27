#pragma once
/*
 *      Copyright (C) 2018 Christian Browet
 *      http://kodi.tv
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

#include <pthread.h>

#include <androidjni/Service.h>
#include <androidjni/Context.h>

#include "threads/Event.h"
#include "threads/SharedSection.h"

class CXBMCService
    : public CJNIService
{
  friend class XBMCApp;

public:
  CXBMCService(jobject thiz);

  static CXBMCService* get() { return m_xbmcserviceinstance; }
  static jboolean _launchApplication(JNIEnv*, jobject thiz);
  int android_printf(const char* format...);

  std::string getDeviceName() const;

  void Deinitialize();

  /*!
   * \brief If external storage is available, it returns the path for the external storage (for the specified type)
   * \param path will contain the path of the external storage (for the specified type)
   * \param type optional type. Possible values are "", "files", "music", "videos", "pictures", "photos, "downloads"
   * \return true if external storage is available and a valid path has been stored in the path parameter
   */
  static bool GetExternalStorage(std::string &path, const std::string &type = "");
  static bool GetStorageUsage(const std::string &path, std::string &usage);

protected:
  void run();
  void SetupEnv();

  CEvent m_appReady;

private:
  static CCriticalSection m_SvcMutex;
  static bool m_SvcThreadCreated;
  static pthread_t m_SvcThread;
  static CXBMCService* m_xbmcserviceinstance;

  void StartApplication();
  void StopApplication();
};
