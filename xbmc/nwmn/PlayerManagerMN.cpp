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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"

#include "nwmn/PlayerManagerMN.h"
#include "nwmn/MNMedia.h"
#include "nwmn/UtilitiesMN.h"
#include "nwmn/UpdateManagerMN.h"
#include "nwmn/LogManagerMN.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "interfaces/Builtins.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "settings/MediaSettings.h"
#include "settings/MediaSourceSettings.h"
#include "storage/MediaManager.h"
#include "utils/FileUtils.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"
#include "video/VideoInfoTag.h"

#include <string>

using namespace ANNOUNCEMENT;

class CVideoDownloader : public CThread
{
public:
  CVideoDownloader(const MNMediaAsset &asset)
  : CThread("CVideoDownloader")
  , m_asset(asset)
  {
    Create();
  }
  
  virtual ~CVideoDownloader()
  {
    // there is no direct check for open so indirect it.
    if (m_http.GetLength())
      m_http.Cancel();
    StopThread();
  }
  
  void Cancel()
  {
    m_http.Cancel();
  }

  int GetPercentDone()
  {
    int percent = 0;
    int64_t req_size = m_http.GetLength();
    if (req_size > 0)
      percent = 100 * (float)m_http.GetPosition() / req_size;
    
    return percent;
  }

protected:
  virtual void Process()
  {
    unsigned int size = strtol(m_asset.video_fileSize.c_str(), NULL, 10);
    m_http.Download(m_asset.video_url, m_asset.video_localpath, &size);
  }

  XFILE::CCurlFile  m_http;
  MNMediaAsset      m_asset;
};


CCriticalSection CPlayerManagerMN::m_player_lock;
CPlayerManagerMN *CPlayerManagerMN::m_PlayerManager = NULL;

CPlayerManagerMN::CPlayerManagerMN()
 : CThread("CPlayerManagerMN")
 , m_Startup(true)
 , m_HasNetwork(true)
 , m_CreatePlaylist(false)
 , m_CheckAssets(false)
 , m_NextUpdateTime(CDateTime::GetCurrentDateTime())
 , m_NextUpdateInterval(0, 0, 5, 0)
 , m_NextReportInterval(0, 6, 0, 0)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_LogManager(NULL)
 , m_UpdateManager(NULL)
 , m_dlgProgress(NULL)
 , m_PlayerCallBackFn(NULL)
 , m_PlayerCallBackCtx(NULL)
{
  CLog::Log(LOGDEBUG, "**MN** - MN version %f", kMNPlayerFloatVersion);
  
  // default path to local MN directory in home
  std::string home = "special://home/MN/";
  
  // look for a removable disk with an
  // existing MN directory
  VECSOURCES sources;
  g_mediaManager.GetRemovableDrives(sources);
  for (size_t indx = 0; indx < sources.size(); indx++)
  {
    std::string test_dir = sources[indx].strPath + "/MN/";
    if (XFILE::CFile::Exists(test_dir))
    {
      // found one, use it.
      home = test_dir;
      break;
    }
  }
  CSpecialProtocol::SetMNPath(home);
  CLog::Log(LOGDEBUG, "**MN** - MN special:// path - %s", home.c_str());
  
  // now we can ref to either location using the same special path.
  m_strHome = "special://MN/";
  if (!XFILE::CFile::Exists(m_strHome))
    CUtil::CreateDirectoryEx(m_strHome);
  
  std::string log_path = m_strHome + kMNDownloadLogPath;
  if (!XFILE::CFile::Exists(log_path))
  {
    std::string old_dir = "special://profile/addon_data/script.nationwide_membernet/log";
    if (XFILE::CFile::Exists(old_dir))
    {
      ::MoveFile(CSpecialProtocol::TranslatePath(old_dir).c_str(),
                 CSpecialProtocol::TranslatePath(log_path).c_str());
    }
    else
    {
      CUtil::CreateDirectoryEx(log_path);
    }
  }

  std::string download_path = m_strHome + kMNDownloadPath;
  if (!XFILE::CFile::Exists(download_path))
  {
    std::string old_dir = "special://profile/addon_data/script.nationwide_membernet/downloads";
    if (XFILE::CFile::Exists(old_dir))
    {
      ::MoveFile(CSpecialProtocol::TranslatePath(old_dir).c_str(),
                 CSpecialProtocol::TranslatePath(download_path).c_str());
    }
    else
    {
      CUtil::CreateDirectoryEx(download_path);
    }
  }
  
  std::string update_path = m_strHome + kMNDownloadUpdatePath;
  if (!XFILE::CFile::Exists(update_path))
    CUtil::CreateDirectoryEx(update_path);
  
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().AddAnnouncer(this);
  
  m_UpdateManager = new CUpdateManagerMN(m_strHome);
  m_UpdateManager->Create();
  
  m_LogManager = new CLogManagerMN(m_strHome);
  m_LogManager->Create();

  m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
  
  CSingleLock lock(m_player_lock);
  m_PlayerManager = this;
}

CPlayerManagerMN::~CPlayerManagerMN()
{
  CSingleLock lock(m_player_lock);

  SAFE_DELETE(m_LogManager);
  SAFE_DELETE(m_UpdateManager);
  m_PlayerCallBackFn = NULL;
  SAFE_DELETE(m_PlayerManager);
  m_dlgProgress = NULL;
  m_http.Cancel();
  StopThread();
  
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().RemoveAnnouncer(this);
}

void CPlayerManagerMN::Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (strcmp(sender, "xbmc") != 0)
    return;
  
  if (flag == Player)
  {
    if (strcmp(message, "OnPlay") == 0)
    {
      CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::Announce() - Playback started");
      std::string strPath = g_application.CurrentFileItem().GetPath();
      std::string assetID = URIUtils::GetFileName(strPath);
      URIUtils::RemoveExtension(assetID);
      m_LogManager->LogPlayback(m_settings, assetID.c_str());
    }
  }
}

CPlayerManagerMN* CPlayerManagerMN::GetPlayerManager()
{
  CSingleLock lock(m_player_lock);
  return m_PlayerManager;
}

void CPlayerManagerMN::Startup()
{
  StartDialog();
  ParseMediaXML(m_settings, m_categories, m_OnDemand);
  ParseSettingsXML(m_settings);
  m_LogManager->LogSettings(m_settings);
  m_LogManager->TriggerLogUpload();
  m_UpdateManager->SetDownloadTime(m_settings);
  CheckAssets();
  CreatePlaylist();
  Create();
}

void CPlayerManagerMN::SetSettings(PlayerSettings settings)
{
  m_settings = settings;
  CSettings::GetInstance().SetString("MN.location_id" ,settings.strLocation_id);
  CSettings::GetInstance().SetString("MN.machine_id"  ,settings.strMachine_id);
  CSettings::GetInstance().SetString("MN.url_feed"    ,settings.strUrl_feed);
  StopPlaying();
  StopThread();
  Startup();
}


void CPlayerManagerMN::FullUpdate()
{
  StopThread();
  m_settings = GetSettings();
  Startup();
}

void CPlayerManagerMN::PlayPause()
{
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::PlayPause()");
}

void CPlayerManagerMN::PausePlaying()
{
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::PausePlaying()");
}

void CPlayerManagerMN::StopPlaying()
{
  CSingleLock lock(m_player_lock);
  g_application.StopPlaying();
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::StopPlaying()");
}

void CPlayerManagerMN::PlayNext()
{
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::PlayNext()");
}

void CPlayerManagerMN::PlayPrevious()
{
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::PlayPrevious()");
}

PlayerSettings CPlayerManagerMN::GetSettings()
{
  PlayerSettings settings;
  settings.strLocation_id                = CSettings::GetInstance().GetString("MN.location_id");
  settings.strMachine_id                 = CSettings::GetInstance().GetString("MN.machine_id");
  settings.strMachine_description        = CSettings::GetInstance().GetString("MN.machine_description");
  settings.strMachine_ethernet_id        = CSettings::GetInstance().GetString("MN.machine_ethernet_id");
  settings.strMachine_hw_version         = CSettings::GetInstance().GetString("MN.machine_hw_version");
  settings.strMachine_name               = CSettings::GetInstance().GetString("MN.machine_name");
  settings.strMachine_purchase_date      = CSettings::GetInstance().GetString("MN.machine_purchase_date");
  settings.strMachine_sn                 = CSettings::GetInstance().GetString("MN.machine_sn");
  settings.strMachine_vendor             = CSettings::GetInstance().GetString("MN.machine_vendor");
  settings.strMachine_warrenty_nr        = CSettings::GetInstance().GetString("MN.machine_warrenty_nr");
  settings.strMachine_wireless_id        = CSettings::GetInstance().GetString("MN.machine_wireless_id");
  settings.strSettings_cf_bundle_version = CSettings::GetInstance().GetString("MN.settings_cf_bundle_version");
  settings.strSettings_update_interval   = CSettings::GetInstance().GetString("MN.settings_update_interval");
  settings.strSettings_update_time       = CSettings::GetInstance().GetString("MN.settings_update_time");
  settings.strUrl_feed                   = CSettings::GetInstance().GetString("MN.url_feed");
  
  if (settings.strUrl_feed.empty()) // Deafult settings
  {
    settings.strUrl_feed = "http://www.nationwidemember.com";
    settings.strLocation_id = "10140003";
    settings.strMachine_id  = "1147";
    
    CSettings::GetInstance().SetString("MN.location_id" ,settings.strLocation_id);
    CSettings::GetInstance().SetString("MN.machine_id"  ,settings.strMachine_id);
    CSettings::GetInstance().SetString("MN.url_feed"    ,settings.strUrl_feed);
  }
  
  if (settings.strMachine_sn.empty())
    settings.strMachine_sn = "UNKNOWN";
  return settings;
}

std::vector<MNCategory> CPlayerManagerMN::GetCategories()
{
  return m_categories;
}

MNCategory CPlayerManagerMN::GetOndemand()
{
  return m_OnDemand;
}

void CPlayerManagerMN::RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn)
{
  m_PlayerCallBackFn = fn;
  m_PlayerCallBackCtx = ctx;
}

void CPlayerManagerMN::Process()
{
  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::Process Started");
  while (!m_bStop)
  {
    Sleep(500);
    
    if (m_CheckAssets)
    {
      m_CheckAssets = false;
      CheckAndDownloadAssets();
    }
    
    if (m_CreatePlaylist)
    {
      // create playlist and start playing
      int itemCount = 0;
      bool run = false;
      m_CreatePlaylist = false;
      // do not manually delete PlayListItems,
      // ownership transfers when handed off to the playlistPlayer.
      CFileItemList *PlayListItems = new CFileItemList;

      for (size_t cat = 0; cat < m_categories.size(); cat++)
      {
        for (size_t i = 0; i < m_categories[cat].items.size(); i++)
        {
          run = true;
          ++itemCount;
        }
      }
      
      // copy order of categories so that we can pop items from it later
      std::vector <std::string> Categories_order = m_settings.intCategories_order;
      // copy categories, we want it intact here
      std::vector<MNCategory> copyCategories = m_categories;
      
      while (itemCount > 0)
      {
        // if we have popped all categories from the order, start from the begining
        if (Categories_order.size() < 1)
          Categories_order = m_settings.intCategories_order;
        
        // current category
        std::string cat = Categories_order.at(0);
        Categories_order.erase(Categories_order.begin());
     
        for (size_t c = 0; c < copyCategories.size(); c++)
        {
          // get category from categories using current order
          if (copyCategories[c].id == cat && copyCategories[c].items.size() > 0)
          {
            CFileItemPtr item(new CFileItem(copyCategories[c].items[0].title));
            item->SetLabel2(m_categories[c].items[0].title);
            item->SetPath(CSpecialProtocol::TranslatePath(copyCategories[c].items[0].video_localpath));
            item->GetVideoInfoTag()->m_strTitle = copyCategories[c].items[0].title;
            item->GetVideoInfoTag()->m_streamDetails.Reset();
            PlayListItems->Add(item);
            --itemCount;
            // pop the added asset
            copyCategories[c].items.erase(copyCategories[c].items.begin());
          }
        }
      }
      
      if (run)
      {
        CSingleLock lock(m_player_lock);
        
        CMediaSettings::GetInstance().SetVideoStartWindowed(false);
        PlayListItems->SetProperty("repeat", PLAYLIST::REPEAT_ALL);
        g_playlistPlayer.Add(PLAYLIST_VIDEO, *PlayListItems);
        g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
        // do not call g_playlistPlayer.Play directly, we are not on main thread.
        KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_PLAYLISTPLAYER_PLAY, 0);

        CloseDialog();
      }
    }
  }

  CLog::Log(LOGDEBUG, "**MN** - CPlayerManagerMN::Process Stopped");
}

bool CPlayerManagerMN::CheckAndDownloadAssets()
{
  int itemCount = 0;
  for (size_t cat = 0; cat < m_categories.size(); cat++)
    itemCount += m_categories[cat].items.size();
  
  m_http_count = 0;
  for (size_t cat = 0; cat < m_categories.size(); cat++)
  {
    std::string localpath = m_strHome + kMNDownloadPath + m_categories[cat].id + "/";
    if (m_categories[cat].items.size() && !XFILE::CFile::Exists(localpath))
      CUtil::CreateDirectoryEx(localpath);

    for (size_t i = 0; i < m_categories[cat].items.size(); i++)
    {
      MNMediaAsset &asset = m_categories[cat].items[i];
      std::string video_extension = URIUtils::GetExtension(asset.video_url);
      std::string thumb_extension = URIUtils::GetExtension(asset.thumb_url);
      asset.video_localpath = localpath + asset.id + video_extension;
      asset.thumb_localpath = localpath + asset.id + thumb_extension;
      asset.valid = false;

      while (!m_dlgProgress->IsCanceled())
      {
        m_dlgProgress->SetLine(1, StringUtils::Format("Checking: %s", asset.title.c_str()));
        m_dlgProgress->SetLine(2, StringUtils::Format("Asset %i/%i", m_http_count+1, itemCount));
        m_dlgProgress->SetPercentage(int(float(m_http_count)/float(itemCount)*100));

        if (XFILE::CFile::Exists(asset.video_localpath))
        {
          if (StringUtils::EqualsNoCase(asset.video_md5, CUtil::GetFileMD5(asset.video_localpath)))
          {
            m_http_count++;
            asset.valid = true;
            break;
          }
        }
        else
        {
          if (XFILE::CFile::Delete(asset.video_localpath))
          {
            // remove any thumbnail for this asset
            if (!asset.thumb_localpath.empty())
              XFILE::CFile::Delete(asset.thumb_localpath);
          }
        }

        m_http_title = asset.title;
        m_http_count++;
        unsigned int size = strtol(asset.video_fileSize.c_str(), NULL, 10);
        if (size)
        {
          // kick off the asset download
          CVideoDownloader videoDownLoader(asset);
          m_dlgProgress->SetLine(1, StringUtils::Format("Downloading: %s", asset.title.c_str()));
          while (!m_dlgProgress->IsCanceled() && videoDownLoader.IsRunning())
          {
            m_dlgProgress->SetPercentage(videoDownLoader.GetPercentDone());
            Sleep(100);
          }
          // check if we got canceled and stop the download
          if (m_dlgProgress->IsCanceled())
          {
            videoDownLoader.Cancel();
            break;
          }
          
          // verify download by md5 check
          if (StringUtils::EqualsNoCase(asset.video_md5, CUtil::GetFileMD5(asset.video_localpath)))
          {
            asset.valid = true;
            // quick grab of thumbnail with no error checking.
            unsigned int size = strtol(asset.thumb_fileSize.c_str(), NULL, 10);
            if (size && !asset.thumb_localpath.empty() && !XFILE::CFile::Exists(asset.thumb_localpath))
              m_http.Download(asset.thumb_url, asset.thumb_localpath, &size);
          }
        }
        m_http_title = "";
        break;
      }
    }
  }

  return !m_dlgProgress->IsCanceled();
}


void CPlayerManagerMN::CreatePlaylist()
{
  m_CreatePlaylist = true;
}

void CPlayerManagerMN::CheckAssets()
{
  m_CheckAssets = true;
}

void CPlayerManagerMN::StartDialog()
{
  m_dlgProgress->SetHeading("MemberNet");
  m_dlgProgress->SetLine(0, "Downloading media files and creating playlist");
  m_dlgProgress->SetLine(1, "Downloading Media and Settings XML files");
  m_dlgProgress->Open();
  m_dlgProgress->ShowProgressBar(true);
}

void CPlayerManagerMN::CloseDialog()
{
   m_dlgProgress->Close();
}