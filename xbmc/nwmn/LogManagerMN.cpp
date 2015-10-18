/*
 *  Copyright (C) 2015 Team MN
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

#include "nwmn/LogManagerMN.h"

#include "nwmn/MNMedia.h"
#include "nwmn/UtilitiesMN.h"

#include "URL.h"
#include "FileItem.h"
#include "LangInfo.h"
#include "filesystem/File.h"
#include "filesystem/Directory.h"
#include "filesystem/CurlFile.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

CLogManagerMN::CLogManagerMN(const std::string &home)
 : CThread("CLogManagerMN")
 , m_strHome(home)
{
}

CLogManagerMN::~CLogManagerMN()
{
  m_bStop = true;
  m_wait_event.Set();
  StopThread();
}

void CLogManagerMN::TriggerLogUpload()
{
  m_wait_event.Set();
}

void CLogManagerMN::LogPlayback(PlayerSettings settings, std::string assetID)
{
//  date,assetID
//  2015-02-05 12:01:40-0500,58350
//  2015-02-05 12:05:40-0500,57116
  
  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strFileName = StringUtils::Format("%slog/%s_%s_%s_%s_playback.log",
    m_strHome.c_str(),
    settings.strLocation_id.c_str(),
    settings.strMachine_id.c_str(),
    settings.strMachine_sn.c_str(),
    time.GetAsDBDate().c_str()
  );
  XFILE::CFile file;
  XFILE::auto_buffer buffer;
  
  if (XFILE::CFile::Exists(strFileName))
  {
    file.LoadFile(strFileName, buffer);
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    std::string header = "date,assetID\n";
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  CLangInfo langInfo;
  std::string strData = StringUtils::Format("%s%s,%s\n",
    time.GetAsDBDateTime().c_str(),
    langInfo.GetTimeZone().c_str(),
    assetID.c_str()
  );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

void CLogManagerMN::LogSettings(PlayerSettings settings)
{
  //  date,uptime,disk-used,disk-free,smart-status
  //  2015-03-04 18:08:21+0400,11 days 2 hours 12 minutes,118GB,24GB,Disks OK
  
  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strFileName = StringUtils::Format("%slog/%s_%s_%s_%s_settings.log",
    m_strHome.c_str(),
    settings.strLocation_id.c_str(),
    settings.strMachine_id.c_str(),
    settings.strMachine_sn.c_str(),
    time.GetAsDBDate().c_str()
  );
  
  XFILE::CFile file;
  XFILE::auto_buffer buffer;
  
  if (XFILE::CFile::Exists(strFileName))
  {
    file.LoadFile(strFileName, buffer);
    file.OpenForWrite(strFileName);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    std::string header = "date,uptime,disk-used,disk-free,smart-status\n";
    file.OpenForWrite(strFileName);
    file.Write(header.c_str(), header.size());
  }
  CLangInfo langInfo;
  std::string strData = StringUtils::Format("%s%s,%s,%s,%s,Disks OK\n",
    time.GetAsDBDateTime().c_str(),
    langInfo.GetTimeZone().c_str(),
    GetSystemUpTime().c_str(),
    GetDiskUsed("/").c_str(),
    GetDiskFree("/").c_str()
  );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

void CLogManagerMN::Process()
{
  CLog::Log(LOGDEBUG, "**MN** - CLogManagerMN::Process Started");

  while (!m_bStop)
  {
    m_wait_event.Wait();

    if (!m_bStop)
    {
      CFileItemList items;
      CDateTime time = CDateTime::GetCurrentDateTime();
      std::string datefilter = time.GetAsDBDate();
      std::string srcLogPath = m_strHome + kMNDownloadLogPath;
      XFILE::CDirectory::GetDirectory(srcLogPath, items, ".log", XFILE::DIR_FLAG_NO_FILE_DIRS);
      for (int i = 0; i < items.Size(); ++i)
      {
        std::string localPath = items[i]->GetPath();
        // do not upload todays log file
        if (localPath.find(datefilter) != std::string::npos)
          continue;

        CURL url;
        url.SetProtocol("ftp");
        #if 0
          url.SetUserName("davilla");
          url.SetPassword("neveryoumind");
          url.SetHostName("192.168.2.131");
          url.SetFileName("Public/" + URIUtils::GetFileName(localPath));
        #else
          url.SetUserName("ftp");
          url.SetPassword("f2+va$uP");
          url.SetHostName("nationwidemember.com");
          // do not use and absolute path here, should be relative to ftp site 'home' dir.
          url.SetFileName("tvlogs/" + URIUtils::GetFileName(localPath));
        #endif
        XFILE::CCurlFile *cfile = new XFILE::CCurlFile();
        if (cfile->OpenForWrite(url, true))
        {
          XFILE::CFile localfile;
          XFILE::auto_buffer localfilebuffer;
          localfile.LoadFile(localPath, localfilebuffer);
          ssize_t wlength = cfile->Write(localfilebuffer.get(), localfilebuffer.size());
          if (wlength > 0 && wlength == (ssize_t)localfilebuffer.size())
          {
            XFILE::CFile::Delete(localPath);
            CLog::Log(LOGDEBUG, "**NWMN** - UploadLogs() - %s", localPath.c_str());
          }
          cfile->Close();
        }
        delete cfile;
      }
    }
  }

  CLog::Log(LOGDEBUG, "**MN** - CLogManagerMN::Process Stopped");
}
