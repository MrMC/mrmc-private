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

#include "XBMCService.h"

#include <stdlib.h>
#include <iostream>
#include <sstream>

#include <android/log.h>

#include <androidjni/jutils.hpp>
#include <androidjni/File.h>
#include <androidjni/System.h>
#include <androidjni/Build.h>
#include <androidjni/Environment.h>
#include <androidjni/StatFs.h>
#include <androidjni/jutils-details.hpp>

#include "Application.h"
#include "CompileInfo.h"
#include "FileItem.h"
#include "utils/StringUtils.h"
#include "platform/MCRuntimeLibContext.h"
#include "platform/MCRuntimeLib.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/log.h"

#define GIGABYTES       1073741824

CCriticalSection CXBMCService::m_SvcMutex;
bool CXBMCService::m_SvcThreadCreated = false;
pthread_t CXBMCService::m_SvcThread;
CXBMCService* CXBMCService::m_xbmcserviceinstance = nullptr;

template<class T, void(T::*fn)()>
void* thread_run(void* obj)
{
  (static_cast<T*>(obj)->*fn)();
  return NULL;
}

using namespace jni;

CXBMCService::CXBMCService(jobject thiz)
  : CJNIBase()
  , CJNIService(thiz)

{
  m_xbmcserviceinstance = this;
}

int CXBMCService::android_printf(const char *format, ...)
{
  // For use before CLog is setup by XBMC_Run()
  va_list args;
  va_start(args, format);
  int result = __android_log_vprint(ANDROID_LOG_DEBUG, CCompileInfo::GetAppName(), format, args);
  va_end(args);
  return result;
}

void CXBMCService::SetupEnv()
{
  setenv("XBMC_ANDROID_SYSTEM_LIBS", CJNISystem::getProperty("java.library.path").c_str(), 0);
  setenv("XBMC_ANDROID_LIBS", getApplicationInfo().nativeLibraryDir.c_str(), 0);
  setenv("XBMC_ANDROID_APK", getPackageResourcePath().c_str(), 0);
  setenv("XBMC_ANDROID_CACHE", getCacheDir().getAbsolutePath().c_str(), 0);

  std::string appName = CCompileInfo::GetAppName();
  StringUtils::ToLower(appName);
  std::string className = CCompileInfo::GetPackage();

  std::string xbmcHome = CJNISystem::getProperty("xbmc.home", "");
  if (xbmcHome.empty())
  {
    std::string cacheDir = getCacheDir().getAbsolutePath();
    setenv("MRMC_BIN_HOME", (cacheDir + "/apk/assets").c_str(), 0);
    setenv("MRMC_HOME", (cacheDir + "/apk/assets").c_str(), 0);
  }
  else
  {
    setenv("MRMC_BIN_HOME", (xbmcHome + "/assets").c_str(), 0);
    setenv("MRMC_HOME", (xbmcHome + "/assets").c_str(), 0);
  }

  std::string externalDir = CJNISystem::getProperty("xbmc.data", "");
  if (externalDir.empty())
  {
    CJNIFile androidPath = getExternalFilesDir("");
    if (!androidPath)
      androidPath = getDir(className.c_str(), 1);

    if (androidPath)
      externalDir = androidPath.getAbsolutePath();
  }

  if (!externalDir.empty())
    setenv("HOME", externalDir.c_str(), 0);
  else
    setenv("HOME", getenv("MRMC_TEMP"), 0);

  std::string apkPath = getenv("XBMC_ANDROID_APK");
  apkPath += "/assets/python2.7";
  setenv("PYTHONHOME", apkPath.c_str(), 1);
  setenv("PYTHONPATH", "", 1);
  setenv("PYTHONOPTIMIZE","", 1);
  setenv("PYTHONNOUSERSITE", "1", 1);
}

void CXBMCService::run()
{
  std::string threadname = "MCRuntimeLib";
  pthread_setname_np(pthread_self(), threadname.c_str());

  int status = 0;

  SetupEnv();
  MCRuntimeLib::Context context;

  android_printf(" => running MCRuntimeLib...");
  try
  {
    //nice(10);
    status = MCRuntimeLib_Run(false);
    android_printf(" => App_Run finished with %d", status);
  }
  catch(...)
  {
    android_printf("ERROR: Exception caught on main loop. Exiting");
  }
}

void CXBMCService::Deinitialize()
{
  stopSelf();
}

bool CXBMCService::GetExternalStorage(std::string &path, const std::string &type /* = "" */)
{
  std::string sType;
  std::string mountedState;
  bool mounted = false;

  if(type == "files" || type.empty())
  {
    CJNIFile external = CJNIEnvironment::getExternalStorageDirectory();
    if (external)
      path = external.getAbsolutePath();
  }
  else
  {
    if (type == "music")
      sType = "Music"; // Environment.DIRECTORY_MUSIC
    else if (type == "videos")
      sType = "Movies"; // Environment.DIRECTORY_MOVIES
    else if (type == "pictures")
      sType = "Pictures"; // Environment.DIRECTORY_PICTURES
    else if (type == "photos")
      sType = "DCIM"; // Environment.DIRECTORY_DCIM
    else if (type == "downloads")
      sType = "Download"; // Environment.DIRECTORY_DOWNLOADS
    if (!sType.empty())
    {
      CJNIFile external = CJNIEnvironment::getExternalStoragePublicDirectory(sType);
      if (external)
        path = external.getAbsolutePath();
    }
  }
  mountedState = CJNIEnvironment::getExternalStorageState();
  mounted = (mountedState == "mounted" || mountedState == "mounted_ro");
  return mounted && !path.empty();
}

bool CXBMCService::GetStorageUsage(const std::string &path, std::string &usage)
{
#define PATH_MAXLEN 50

  if (path.empty())
  {
    std::ostringstream fmt;
    fmt.width(PATH_MAXLEN);  fmt << std::left  << "Filesystem";
    fmt.width(12);  fmt << std::right << "Size";
    fmt.width(12);  fmt << "Used";
    fmt.width(12);  fmt << "Avail";
    fmt.width(12);  fmt << "Use %";

    usage = fmt.str();
    return false;
  }

  CJNIStatFs fileStat(path);
  if (!fileStat)
  {
    CLog::Log(LOGERROR, "CXBMCApp::GetStorageUsage cannot stat %s", path.c_str());
    return false;
  }

  int blockSize = fileStat.getBlockSize();
  int blockCount = fileStat.getBlockCount();
  int freeBlocks = fileStat.getFreeBlocks();

  if (blockSize <= 0 || blockCount <= 0 || freeBlocks < 0)
    return false;

  float totalSize = (float)blockSize * blockCount / GIGABYTES;
  float freeSize = (float)blockSize * freeBlocks / GIGABYTES;
  float usedSize = totalSize - freeSize;
  float usedPercentage = usedSize / totalSize * 100;

  std::ostringstream fmt;
  fmt << std::fixed;
  fmt.precision(1);
  fmt.width(PATH_MAXLEN);  fmt << std::left  << (path.size() < PATH_MAXLEN-1 ? path : StringUtils::Left(path, PATH_MAXLEN-4) + "...");
  fmt.width(12);  fmt << std::right << totalSize << "G"; // size in GB
  fmt.width(12);  fmt << usedSize << "G"; // used in GB
  fmt.width(12);  fmt << freeSize << "G"; // free
  fmt.precision(0);
  fmt.width(12);  fmt << usedPercentage << "%"; // percentage used

  usage = fmt.str();
  return true;
}

void CXBMCService::StartApplication()
{
  CSingleLock lock(m_SvcMutex);

  if( !m_SvcThreadCreated)
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&m_SvcThread, &attr, thread_run<CXBMCService, &CXBMCService::run>, this);
    pthread_attr_destroy(&attr);

    m_SvcThreadCreated = true;
  }
  // Wait for the service to settle
  int nb = 0;
  while(!g_application.IsInitialized() && nb < 30)
  {
    usleep(1 * 1000000);
    nb++;
  }
}

void CXBMCService::StopApplication()
{
  pthread_join(m_SvcThread, NULL);
}

jboolean CXBMCService::_launchApplication(JNIEnv*, jobject thiz)
{
  jobject o = (jobject)xbmc_jnienv()->NewGlobalRef(thiz);
  m_xbmcserviceinstance = new CXBMCService(o);
  m_xbmcserviceinstance->StartApplication();
  return g_application.IsInitialized();
}

std::string CXBMCService::getDeviceName() const
{
  if (!m_object)
    return "";
  return jcast<std::string>(get_field<jhstring>(m_object, "mDeviceName"));
}
