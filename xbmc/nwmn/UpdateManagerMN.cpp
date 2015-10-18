/*
 *  Copyright (C) 2014 Team MN
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

#include "UpdateManagerMN.h"
#include "PlayerManagerMN.h"
#include "UtilitiesMN.h"

#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

CUpdateManagerMN::CUpdateManagerMN(const std::string &home)
 : CThread("CUpdateManagerMN")
 , m_strHome(home)
 , m_Override(false)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_update_fired(false)
{
}

CUpdateManagerMN::~CUpdateManagerMN()
{
  m_bStop = false;
  m_http.Cancel();
  StopThread();
}

void CUpdateManagerMN::QueueUpdateForDownload(MNMediaUpdate &update)
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
    MNMediaUpdate queued_update = m_download.front();
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

void CUpdateManagerMN::OverrideDownloadWindow()
{
  m_Override = true;
}

void CUpdateManagerMN::SetDownloadTime(const PlayerSettings &settings)
{
  int hours, minutes;
  CDateTime cur = CDateTime::GetCurrentDateTime();
  
  if (settings.strSettings_update_interval == "daily")
  {
    sscanf(settings.strSettings_update_time.c_str(),"%d:%d", &hours, &minutes);
    m_NextDownloadTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), hours, minutes, 0);
//    sscanf("00:01", "%d:%d", &hours, &minutes);
  }
  else
  {
    // if interval is not "daily", its set to minutes
    int interval = atoi(settings.strSettings_update_time.c_str());
    
    // we add minutes to current time to trigger the next update
    m_NextDownloadTime = cur + CDateTimeSpan(0,0,interval,0);
  }
  
  
  if (m_NextDownloadTime <= CDateTime::GetCurrentDateTime())
  {
    // adjust for when time has past for today
    m_NextDownloadTime = m_NextDownloadTime + CDateTimeSpan(1,0,0,0);
  }
  
  CLog::Log(LOGDEBUG, "**MN** - CUpdateManagerMN::SetDownloadTime() download time is %s ", m_NextDownloadTime.GetAsLocalizedDateTime().c_str());
  m_update_fired = true;
}

void CUpdateManagerMN::Process()
{
  CLog::Log(LOGDEBUG, "**MN** - CUpdateManagerMN::Process Started");

  while (!m_bStop)
  {
    Sleep(500);

    CDateTime cur = CDateTime::GetCurrentDateTime();
    // download can only occur in a datetime window.
    if (m_Override || (m_update_fired && cur >= m_NextDownloadTime))
    {
      CLog::Log(LOGDEBUG, "**MN** - CGUIDialogMN::Refresh()");
      CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
      if (MNPlayerManager)
      {
        m_update_fired = false;
        MNPlayerManager->StopPlaying();
        MNPlayerManager->FullUpdate();
        
      }
    }
  }

  CLog::Log(LOGDEBUG, "**MN** - CUpdateManagerMN::Process Stopped");
}

void CUpdateManagerMN::DoUpdate(MNMediaUpdate &update)
{
  m_update_fired = true;
  
#if defined(TARGET_ANDROID)
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->SendPlayerStatus(kMNStatus_Restarting);

  std::string libpath = "LD_LIBRARY_PATH=/vendor/lib:/system/lib ";
  std::string do_install = "su -c pm install -r " + update.localpath + ";su -c reboot";
  std::string do_update = libpath + do_install;
  CLog::Log(LOGDEBUG, "**MN** - CUpdateManagerMN::DoUpdate: %s", do_update.c_str());
  system(do_update.c_str());
  // not sure if we get here as the 'pm install' will uninstall us before doing the install"
  CApplicationMessenger::Get().Restart();
#endif
}

