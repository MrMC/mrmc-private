/*
 *  Copyright (C) 2014 Team RED
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

#include "UpdateManagerRed.h"
#include "PlayerManagerRed.h"
#include "UtilitiesRed.h"

#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "utils/StringUtils.h"

CUpdateManagerRed::CUpdateManagerRed(const std::string &home)
 : CThread("CUpdateManagerRed")
 , m_strHome(home)
 , m_Override(false)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_NextDownloadDuration(0, 6, 0, 0)
 , m_update_fired(false)
{
}

CUpdateManagerRed::~CUpdateManagerRed()
{
  m_bStop = false;
  m_http.Cancel();
  StopThread();
}

void CUpdateManagerRed::QueueUpdateForDownload(RedMediaUpdate &update)
{
  if (m_update_fired)
    return;

  // if key does not match, abort this update
  if (GetMD5(update.md5) != update.key)
  return;
  
  CSingleLock lock(m_download_lock);
  if (!m_download.empty())
  {
    // compare to existing queued update
    RedMediaUpdate queued_update = m_download.front();
    if (update.md5 == queued_update.md5)
      return;
    
    // check if new version is higher than queued version
    // is yes, pop the queued and queue the new update.
    if (update.version > queued_update.version)
      m_download.pop();
  }
  lock.Leave();

  // check if we already have this update on disk
  if (XFILE::CFile::Exists(update.localpath))
  {
    // verify it by md5 check
    if (StringUtils::EqualsNoCase(update.md5, CUtil::GetFileMD5(update.localpath)))
    {
      DoUpdate(update);
      // should never get here.
      return;
    }
  }

  // update is validated, queue for download
  lock.Enter();
  m_download.push(update);
}

void CUpdateManagerRed::OverrideDownloadWindow()
{
  m_Override = true;
}

void CUpdateManagerRed::SetDownloadTime(const CDateTime &time, const CDateTimeSpan &duration)
{
  m_NextDownloadTime = time;
  m_NextDownloadDuration = duration;
}

void CUpdateManagerRed::Process()
{
  CLog::Log(LOGDEBUG, "**RED** - CUpdateManagerRed::Process Started");

  while (!m_bStop)
  {
    Sleep(500);

    CDateTime cur = CDateTime::GetCurrentDateTime();
    // download can only occur in a datetime window.
    if (m_Override || cur >= m_NextDownloadTime)
    {
      CDateTime end = m_NextDownloadTime + m_NextDownloadDuration;
      if (!m_Override && cur >= end)
      {
        //CLog::Log(LOGDEBUG, "CUpdateManagerRed::Process cur %s, end %s",
        //  cur.GetAsSaveString().c_str(), end.GetAsSaveString().c_str());
       
        // complicated but required. we get download time as a
        // hh:mm:ss field, duration is in hours. So we have to
        // be able to span over the beginning or end of a 24 hour day.
        // for example. bgn at 6pm, end at 6am. bgn would be 18:00:00,
        // duration would be 12.
        CDateTime cur = CDateTime::GetCurrentDateTime();
        CDateTime bgn = m_NextDownloadTime;
        m_NextDownloadTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), bgn.GetHour(), bgn.GetMinute(), bgn.GetSecond());
        continue;
      }

      CSingleLock download_lock(m_download_lock);
      if (!m_download.empty() && !m_update_fired)
      {
        // fetch the update to download
        RedMediaUpdate update = m_download.front();
        download_lock.Leave();

        // download it
        unsigned int size = strtol(update.size.c_str(), NULL, 10);
        if (size && m_http.Download(update.url, update.localpath, &size))
        {
          // verify download by md5 check
          if (StringUtils::EqualsNoCase(update.md5, CUtil::GetFileMD5(update.localpath)))
          {
            // we have a verified update, keep it and install
            download_lock.Enter();
            DoUpdate(update);
            // should never get here.
            m_download.pop();
          }
          else
          {
            CLog::Log(LOGERROR, "**RED** - CUpdateManagerRed::Process "
                      "md5 mismatch for %s", update.localpath.c_str());
            // must be bad file, delete and leave it in the queue
            XFILE::CFile::Delete(update.localpath);
         }
        }
      }
    }
  }

  CLog::Log(LOGDEBUG, "**RED** - CUpdateManagerRed::Process Stopped");
}

void CUpdateManagerRed::DoUpdate(RedMediaUpdate &update)
{
  m_update_fired = true;
  
#if defined(TARGET_ANDROID)
  CPlayerManagerRed* RedPlayerManager = CPlayerManagerRed::GetPlayerManager();
  if (RedPlayerManager)
    RedPlayerManager->SendPlayerStatus(kRedStatus_Restarting);

  std::string libpath = "LD_LIBRARY_PATH=/vendor/lib:/system/lib ";
  std::string do_install= "su -c pm install -r " + update.localpath;
  std::string do_launch = "su -c am start -n org.xbmc.xbmc/.Splash";
  std::string do_update = libpath + do_install + ";" + do_launch;
  CLog::Log(LOGDEBUG, "**RED** - CUpdateManagerRed::DoUpdate: %s", do_update.c_str());
  system(do_update.c_str());
  // not sure if we get here as the 'pm install' will uninstall us before doing the install"
  CApplicationMessenger::Get().Restart();
#endif
}

