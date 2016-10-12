/*
 *  Copyright (C) 2016 RootCoder, LLC.
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
 *  along with this app; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"

#include "NWTVAPI.h"
#include "NWClient.h"
#include "NWPlayer.h"
#include "NWMediaManager.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "URL.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "utils/FileUtils.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StringUtils.h"
#include "utils/SystemInfo.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

#include "guilib/GUIKeyboardFactory.h"
#include "guilib/GUIWindowManager.h"
#include "settings/MediaSourceSettings.h"
#include "storage/MediaManager.h"

// temp until access moves to core
#include "platform/darwin/DarwinUtils.h"

CCriticalSection CNWClient::m_clientLock;
CNWClient *CNWClient::m_this = NULL;

CNWClient::CNWClient()
 : CThread("CNWClient")
 , m_Startup(true)
 , m_HasNetwork(true)
 , m_FullUpdate(true)
 , m_NextUpdateTime(CDateTime::GetCurrentDateTime())
 , m_NextUpdateInterval(0, 0, 5, 0)
 , m_Player(NULL)
 , m_MediaManager(NULL)
 , m_ClientCallBackFn(NULL)
 , m_ClientCallBackCtx(NULL)
{
  CLog::Log(LOGDEBUG, "**NW** - NW version %f", kNWClient_PlayerFloatVersion);

  // default path to local red directory in home
  std::string home = "special://home/nwmn/";

  // look for a removable disk with an
  // existing nwmn directory
  VECSOURCES sources;
  g_mediaManager.GetRemovableDrives(sources);
  for (size_t indx = 0; indx < sources.size(); indx++)
  {
    std::string test_dir = sources[indx].strPath + "/nwmn/";
    if (XFILE::CFile::Exists(test_dir))
    {
      // found one, use it.
      home = test_dir;
      break;
    }
  }
  CSpecialProtocol::SetCustomPath("nwmn", home);
  CLog::Log(LOGDEBUG, "**NW** - NW special:// path - %s", home.c_str());

    // now we can ref to either location using the same special path.
  m_strHome = "special://nwmn/";
  if (!XFILE::CFile::Exists(m_strHome))
    CUtil::CreateDirectoryEx(m_strHome);

  std::string download_path = m_strHome + kNWClient_DownloadPath;
  if (!XFILE::CFile::Exists(download_path))
    CUtil::CreateDirectoryEx(download_path);

  std::string download_log_path = m_strHome + kNWClient_LogPath;
  if (!XFILE::CFile::Exists(download_log_path))
    CUtil::CreateDirectoryEx(download_log_path);

  std::string video_path = m_strHome + kNWClient_DownloadVideoPath;
  if (!XFILE::CFile::Exists(video_path))
    CUtil::CreateDirectoryEx(video_path);

  std::string videothumbs_path = m_strHome + kNWClient_DownloadVideoThumbNailsPath;
  if (!XFILE::CFile::Exists(videothumbs_path))
    CUtil::CreateDirectoryEx(videothumbs_path);
/*
  std::string music_path = m_strHome + kNWClient_DownloadMusicPath;
  if (!XFILE::CFile::Exists(music_path))
    CUtil::CreateDirectoryEx(music_path);

  std::string musicthumbs_path = m_strHome + kNWClient_DownloadMusicThumbNailsPath;
  if (!XFILE::CFile::Exists(musicthumbs_path))
    CUtil::CreateDirectoryEx(musicthumbs_path);
*/
  std::string webui_path = m_strHome + "webdata/";
  if (!XFILE::CFile::Exists(webui_path))
    CUtil::CreateDirectoryEx(webui_path);

  m_HasNetwork = g_sysinfo.HasInternet();

  LoadLocalPlayer(m_strHome, m_PlayerInfo);
  InitializeInternalsFromPlayer();

  m_MediaManager = new CNWMediaManager();
  m_MediaManager->RegisterAssetUpdateCallBack(this, AssetUpdateCallBack);
  m_Player = new CNWPlayer();

  m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);

  CSingleLock lock(m_clientLock);
  m_this = this;

  ANNOUNCEMENT::CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CNWClient::~CNWClient()
{
  CSingleLock lock(m_clientLock);
  m_this = NULL;
  m_dlgProgress = NULL;
  m_ClientCallBackFn = NULL;
  m_bStop = true;
  StopThread();

  ANNOUNCEMENT::CAnnouncementManager::GetInstance().RemoveAnnouncer(this);
  SendPlayerStatus(kTVAPI_Status_Off);

  SAFE_DELETE(m_Player);
  SAFE_DELETE(m_MediaManager);
}

CNWClient* CNWClient::GetClient()
{
  CSingleLock lock(m_clientLock);
  return m_this;
}

void CNWClient::Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (strcmp(sender, "xbmc") != 0)
    return;

  if (flag == ANNOUNCEMENT::Player)
  {
    if (strcmp(message, "OnPlay") == 0)
    {
      CLog::Log(LOGDEBUG, "**MN** - CNWClient::Announce() - Playback started");
      std::string strPath = g_application.CurrentFileItem().GetPath();
      std::string assetID = URIUtils::GetFileName(strPath);
      URIUtils::RemoveExtension(assetID);
      LogFilesPlayed(assetID);
    }
  }
}

void CNWClient::Startup()
{
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Starting Client", "", 4000, false);

  StopThread();
  StopPlaying();
  m_totalAssets = 0;
  m_MediaManager->ClearDownloads();
  m_MediaManager->ClearAssets();

  if (!IsAuthorized())
    while (!DoAuthorize());

  SendPlayerStatus(kTVAPI_Status_Restarting);

  m_Startup = true;
  m_FullUpdate = true;
  ShowStartUpDialog();

  Create();
  if (!m_MediaManager->IsRunning())
    m_MediaManager->Create();
  if (!m_Player->IsRunning())
    m_Player->Create();
}

void CNWClient::FullUpdate()
{
  SendPlayerStatus(kTVAPI_Status_Restarting);
  m_FullUpdate = true;
}

void CNWClient::PlayPause()
{
  m_Player->PlayPause();
}

void CNWClient::PausePlaying()
{
  m_Player->Pause();
}

void CNWClient::StopPlaying()
{
  m_Player->StopPlaying();
}

void CNWClient::PlayNext()
{
  m_Player->PlayNext();
}

void CNWClient::RegisterClientCallBack(const void *ctx, ClientCallBackFn fn)
{
  m_ClientCallBackFn = fn;
  m_ClientCallBackCtx = ctx;
}

void CNWClient::UpdateFromJson(std::string url, std::string machineID, std::string locationID)
{
}

void CNWClient::RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn)
{
  m_Player->RegisterPlayerCallBack(ctx, fn);
}

void CNWClient::Process()
{
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);
  CLog::Log(LOGDEBUG, "**NW** - CNWClient::Process Started");

  SendPlayerStatus(kTVAPI_Status_On);

  while (!m_bStop)
  {
    Sleep(500);

    if (m_Startup)
      ManageStartupDialog();

    CDateTime time = CDateTime::GetCurrentDateTime();
    if (m_FullUpdate || time >= m_NextUpdateTime)
    {
      CLog::Log(LOGDEBUG, "**NW** - time = %s", time.GetAsDBDateTime().c_str());
      CLog::Log(LOGDEBUG, "**NW** - m_NextUpdateTime = %s", m_NextUpdateTime.GetAsDBDateTime().c_str());
      CLog::Log(LOGDEBUG, "**NW** - m_NextUpdateInterval = %d days, %d hours, %d mins",
        m_NextUpdateInterval.GetDays(), m_NextUpdateInterval.GetHours(), m_NextUpdateInterval.GetMinutes());

      m_NextUpdateTime += m_NextUpdateInterval;
      CLog::Log(LOGDEBUG, "**NW** - m_NextUpdateTime = %s", m_NextUpdateTime.GetAsDBDateTime().c_str());

      m_HasNetwork = g_sysinfo.HasInternet();
      m_MediaManager->UpdateNetworkStatus(m_HasNetwork);

      GetPlayerInfo();
      GetActions();
      if (GetProgamInfo())
      {
        m_FullUpdate = false;
        if (m_PlayerInfo.allow_async_player != "no")
          m_Player->Play();
      }

      if (m_ClientCallBackFn)
        (*m_ClientCallBackFn)(m_ClientCallBackCtx, true);

      if (m_Player->IsPlaying())
        SendPlayerStatus(kTVAPI_Status_Playing);
      else
        SendPlayerStatus(kTVAPI_Status_On);
    }
  }

  CLog::Log(LOGDEBUG, "**NW** - CNWClient::Process Stopped");
}

void CNWClient::ShowStartUpDialog()
{
  m_dlgProgress->SetHeading("Download and Verify media files");
  m_dlgProgress->SetLine(1, StringUtils::Format("Client Startup: Fetching Player and Program Info"));
  m_dlgProgress->Open();
  m_dlgProgress->ShowProgressBar(true);
}

void CNWClient::CloseStartUpDialog()
{
   m_dlgProgress->Close();
}

bool CNWClient::ManageStartupDialog()
{
  if (m_PlayerInfo.allow_async_player == "no")
  {
    if (m_dlgProgress->IsCanceled())
    {
      m_Startup = false;
      m_FullUpdate = false;
      CloseStartUpDialog();
      
      StopPlaying();
      m_MediaManager->ClearDownloads();
      m_MediaManager->ClearAssets();
      
      if (m_ClientCallBackFn)
        (*m_ClientCallBackFn)(m_ClientCallBackCtx, false);
      SendPlayerStatus(kTVAPI_Status_On);
    }
    else if (m_dlgProgress->IsDialogRunning())
    {
      if (m_MediaManager->GetLocalAssetCount() > 0 && m_MediaManager->GetDownloadCount() == 0)
      {
        m_Startup = false;
        CloseStartUpDialog();
        m_Player->Play();
        if (m_ClientCallBackFn)
          (*m_ClientCallBackFn)(m_ClientCallBackCtx, true);
      }
    }
  }
  else
  {
    // when starting up, two choices
    // 1) dialog was canceled -> return to main window and idle
    // 2) dialog is up -> waiting for download, verify and start of playback
    if (m_dlgProgress->IsCanceled())
    {
      m_Startup = false;
      m_FullUpdate = false;
      CloseStartUpDialog();

      StopPlaying();
      m_MediaManager->ClearDownloads();
      m_MediaManager->ClearAssets();

      if (m_ClientCallBackFn)
        (*m_ClientCallBackFn)(m_ClientCallBackCtx, false);
      SendPlayerStatus(kTVAPI_Status_On);
    }
    else if (g_application.m_pPlayer->IsPlaying() && m_dlgProgress->IsDialogRunning())
    {
      m_Startup = false;
      CloseStartUpDialog();
      if (m_ClientCallBackFn)
        (*m_ClientCallBackFn)(m_ClientCallBackCtx, true);
    }

  }

  return m_Startup;
}

void CNWClient::GetPlayerInfo()
{
  if (m_HasNetwork)
  {
    TVAPI_Machine machine;
    machine.apiKey = m_PlayerInfo.apiKey;
    machine.apiSecret = m_PlayerInfo.apiSecret;
    TVAPI_GetMachine(machine);

    m_PlayerInfo.id  = machine.id;
    m_PlayerInfo.name = machine.machine_name;
    m_PlayerInfo.vendor = machine.vendor;
    m_PlayerInfo.member = machine.member;
    m_PlayerInfo.timezone = machine.timezone;
    m_PlayerInfo.description = machine.description;
    m_PlayerInfo.serial_number = machine.serial_number;
    m_PlayerInfo.warranty_number = machine.warranty_number;
    m_PlayerInfo.macaddress = GetWiredMACAddress();
    m_PlayerInfo.macaddress_wireless = GetWirelessMACAddress();
    m_PlayerInfo.hardware_version = "";
    m_PlayerInfo.software_version = kNWClient_PlayerFloatVersion * 10;

    m_PlayerInfo.playlist_id = machine.playlist_id;
    m_PlayerInfo.video_format = machine.video_format;
    m_PlayerInfo.update_time = machine.update_time;
    m_PlayerInfo.update_interval = machine.update_interval;

    m_PlayerInfo.allow_new_content = machine.allow_new_content;
    m_PlayerInfo.allow_software_update = machine.allow_software_update;
    m_PlayerInfo.allow_async_player = "no";
    m_PlayerInfo.status = machine.status;
    m_PlayerInfo.apiKey = machine.apiKey;
    m_PlayerInfo.apiSecret = machine.apiSecret;
    m_PlayerInfo.tvapiURLBase = TVAPI_GetURLBASE();
    InitializeInternalsFromPlayer();

    SaveLocalPlayer(m_strHome, m_PlayerInfo);
  }
  else
  {
    if (HasLocalPlayer(m_strHome))
    {
      LoadLocalPlayer(m_strHome, m_PlayerInfo);
      InitializeInternalsFromPlayer();
    }
  }
}

bool CNWClient::GetProgamInfo()
{
  bool rtn = false;

  if (m_HasNetwork)
  {
    TVAPI_Playlist playlist;
    playlist.apiKey = m_PlayerInfo.apiKey;
    playlist.apiSecret = m_PlayerInfo.apiSecret;
    TVAPI_GetPlaylist(playlist, m_PlayerInfo.playlist_id);

    if (m_FullUpdate || m_ProgramInfo.updated_date != playlist.updated_date)
    {
      TVAPI_PlaylistItems playlistItems;
      playlistItems.apiKey = m_PlayerInfo.apiKey;
      playlistItems.apiSecret = m_PlayerInfo.apiSecret;
      TVAPI_GetPlaylistItems(playlistItems, m_PlayerInfo.playlist_id);

      m_ProgramInfo.video_format = m_PlayerInfo.video_format;
      CreatePlaylist(m_strHome, m_ProgramInfo, playlist, playlistItems);
      SaveLocalPlaylist(m_strHome, m_ProgramInfo);

      // send msg to GUIWindowMN to change to horz/vert
      // see m_ProgramInfo.layout
      // make sure control id's match those in CGUIWindowMN.cpp
      if (m_ProgramInfo.layout == "horizontal")
      {
        // if we are vertical, switch to horizontal
        if (CSettings::GetInstance().GetBool(CSettings::MN_VERTICAL))
        {
          CGUIMessage msg(GUI_MSG_CLICKED, 90144, WINDOW_MEMBERNET);
          g_windowManager.SendMessage(msg);
        }
      }
      else if (m_ProgramInfo.layout == "vertical")
      {
        // if we are horizontal, switch to vertical
        if (!CSettings::GetInstance().GetBool(CSettings::MN_VERTICAL))
        {
          CGUIMessage msg(GUI_MSG_CLICKED, 90134, WINDOW_MEMBERNET);
          g_windowManager.SendMessage(msg);
        }
      }

      // queue all assets belonging to this playlist
      m_Player->QueueProgramInfo(m_ProgramInfo);
      int total_assets = 0;
      for (auto group : m_ProgramInfo.groups)
      {
        total_assets += group.assets.size();
        m_MediaManager->QueueAssetsForDownload(group.assets);
      }
      m_totalAssets = total_assets;

      rtn = true;
    }
  }
  else
  {
    if (HasLocalPlaylist(m_strHome))
    {
      std::string updated_date = m_ProgramInfo.updated_date;
      if (LoadLocalPlaylist(m_strHome, m_ProgramInfo))
      {
        if (m_FullUpdate || m_ProgramInfo.updated_date != updated_date)
        {
          // send msg to GUIWindowMN to change to horz/vert
          // see m_ProgramInfo.layout
          // make sure control id's match those in CGUIWindowMN.cpp
          if (m_ProgramInfo.layout == "horizontal")
          {
            // if we are vertical, switch to horizontal
            if (CSettings::GetInstance().GetBool(CSettings::MN_VERTICAL))
            {
              CGUIMessage msg(GUI_MSG_CLICKED, 90144, WINDOW_MEMBERNET);
              g_windowManager.SendMessage(msg);
            }
          }
          else if (m_ProgramInfo.layout == "vertical")
          {
            // if we are horizontal, switch to vertical
            if (!CSettings::GetInstance().GetBool(CSettings::MN_VERTICAL))
            {
              CGUIMessage msg(GUI_MSG_CLICKED, 90134, WINDOW_MEMBERNET);
              g_windowManager.SendMessage(msg);
            }
          }

          // queue all assets belonging to this mediagroup
          m_Player->QueueProgramInfo(m_ProgramInfo);
          int total_assets = 0;
          for (auto group : m_ProgramInfo.groups)
          {
            total_assets += group.assets.size();
            m_MediaManager->QueueAssetsForDownload(group.assets);
          }
          m_totalAssets = total_assets;
        }
      }
      // if we are off-line, always return true so m_FullUpdate will get set right
      rtn = true;
    }
  }

  return rtn;
}

void CNWClient::GetActions()
{
  if (!m_HasNetwork)
    return;

  TVAPI_Actions actions;
  actions.apiKey = m_PlayerInfo.apiKey;
  actions.apiSecret = m_PlayerInfo.apiSecret;
  if (TVAPI_GetActionQueue(actions))
  {
    for (auto action: actions.actions)
    {
      if (action.action == kTVAPI_ActionHealth)
      {
        TVAPI_HealthReport health;
        health.apiKey = m_PlayerInfo.apiKey;
        health.apiSecret = m_PlayerInfo.apiSecret;
        health.date = CDateTime::GetCurrentDateTime().GetAsDBDateTime();
        health.uptime = GetSystemUpTime();
        health.disk_used = GetDiskUsed("/");
        health.disk_free = GetDiskFree("/");
        health.smart_status = "Disk Ok";
        TVAPI_ReportHealth(health);
        ClearAction(actions, action.id);
      }
      else if (action.action == kTVAPI_ActionDownloaded)
      {
        SendFilesDownloaded();
        ClearAction(actions, action.id);
      }
      else if (action.action == kTVAPI_ActionFilePlayed)
      {
        SendFilesPlayed();
        ClearAction(actions, action.id);
      }
      else if (action.action == kTVAPI_ActionPlay)
      {
        m_Player->Play();
        ClearAction(actions, action.id);
      }
      else if (action.action == kTVAPI_ActionStop)
      {
        m_Player->StopPlaying();
        ClearAction(actions, action.id);
      }
      else if (action.action == kTVAPI_ActionRestart)
      {
        // flip the order so server action gets cleared
        //ClearAction(actions, action.id);
        //CApplicationMessenger::Get().Restart();
      }
      else if (action.action == kTVAPI_ActionSwitchOff)
      {
      }
    }
    actions.actions.clear();
  }
}

void CNWClient::ClearAction(TVAPI_Actions &actions, std::string id)
{
  if (!m_HasNetwork)
    return;

  // clear a server side requested action
  for (auto action: actions.actions)
  {
    if (action.id == id)
    {
      TVAPI_ActionStatus actionStatus;
      actionStatus.apiKey = m_PlayerInfo.apiKey;
      actionStatus.apiSecret = m_PlayerInfo.apiSecret;
      actionStatus.id = action.id;
      actionStatus.status = kTVAPI_ActionStatusCompleted;
      TVAPI_UpdateActionStatus(actionStatus);
      break;
    }
  }
}

void CNWClient::SendFilesPlayed()
{
  if (!m_HasNetwork)
    return;

  std::string filename = m_strHome + "log/" + m_PlayerInfo.id + "_playback.log";
  XFILE::CFile file;
  XFILE::auto_buffer buffer;

  CSingleLock lock(m_reportLock);
  if (XFILE::CFile::Exists(filename))
  {
    file.LoadFile(filename, buffer);
    std::string charbuffer(buffer.get());

    TVAPI_Files playedFiles;
    playedFiles.apiKey = m_PlayerInfo.apiKey;
    playedFiles.apiSecret = m_PlayerInfo.apiSecret;

    std::vector<std::string> lines = StringUtils::Split(charbuffer, '\n');
    for (auto line: lines)
    {
      std::vector<std::string> items = StringUtils::Split(line, ',');
      if (items.size() == 2)
      {
        TVAPI_File playedFile;
        playedFile.date = items[0];
        playedFile.id = items[1];
        playedFiles.files.push_back(playedFile);
      }
    }

    if (!playedFiles.files.empty())
    {
      if (TVAPI_ReportFilesPlayed(playedFiles, m_PlayerInfo.serial_number))
        XFILE::CFile::Delete(filename);
    }
  }
}

void CNWClient::LogFilesPlayed(std::string assetID)
{
  //  date,assetID
  //  2015-02-05 12:01:40-0500,58350
  //  2015-02-05 12:05:40-0500,57116

  std::string filename = m_strHome + "log/" + m_PlayerInfo.id + "_playback.log";
  XFILE::CFile file;
  XFILE::auto_buffer buffer;

  CSingleLock lock(m_reportLock);
  if (XFILE::CFile::Exists(filename))
  {
    file.LoadFile(filename, buffer);
    file.OpenForWrite(filename);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    file.OpenForWrite(filename);
    file.Write(buffer.get(), buffer.size());
  }
  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strData = StringUtils::Format("%s,%s\n",
    time.GetAsDBDateTime().c_str(),
    assetID.c_str()
  );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

void CNWClient::SendFilesDownloaded()
{
  if (!m_HasNetwork)
    return;

  std::string filename = m_strHome + "log/" + m_PlayerInfo.id + "_download.log";
  XFILE::CFile file;
  XFILE::auto_buffer buffer;

  CSingleLock lock(m_reportLock);
  if (XFILE::CFile::Exists(filename))
  {
    file.LoadFile(filename, buffer);
    std::string charbuffer(buffer.get());

    TVAPI_Files downloadedFiles;
    downloadedFiles.apiKey = m_PlayerInfo.apiKey;
    downloadedFiles.apiSecret = m_PlayerInfo.apiSecret;

    std::vector<std::string> lines = StringUtils::Split(charbuffer, '\n');
    for (auto line: lines)
    {
      std::vector<std::string> items = StringUtils::Split(line, ',');
      if (items.size() == 2)
      {
        TVAPI_File downloadedFile;
        downloadedFile.date = items[0];
        downloadedFile.id = items[1];
        downloadedFiles.files.push_back(downloadedFile);
      }
    }

    if (!downloadedFiles.files.empty())
    {
      if (TVAPI_ReportFilesDownloaded(downloadedFiles))
        XFILE::CFile::Delete(filename);
    }
  }
}

void CNWClient::LogFilesDownLoaded(std::string assetID)
{
  if (!m_HasNetwork)
    return;

  //  date,assetID
  //  2015-02-05 12:01:40-0500,58350
  //  2015-02-05 12:05:40-0500,57116
  std::string filename = m_strHome + "log/" + m_PlayerInfo.id + "_download.log";
  XFILE::CFile file;
  XFILE::auto_buffer buffer;

  CSingleLock lock(m_reportLock);
  if (XFILE::CFile::Exists(filename))
  {
    file.LoadFile(filename, buffer);
    file.OpenForWrite(filename);
    file.Write(buffer.get(), buffer.size());
  }
  else
  {
    file.OpenForWrite(filename);
    file.Write(buffer.get(), buffer.size());
  }

  CDateTime time = CDateTime::GetCurrentDateTime();
  std::string strData = StringUtils::Format("%s,%s\n",
    time.GetAsDBDateTime().c_str(),
    assetID.c_str()
  );
  file.Write(strData.c_str(), strData.size());
  file.Close();
}

bool CNWClient::SendPlayerStatus(const std::string status)
{
  m_PlayerInfo.status = status;

  if (!m_HasNetwork)
    return true;

  TVAPI_MachineUpdate machineUpdate;
  machineUpdate.apiKey = m_PlayerInfo.apiKey;
  machineUpdate.apiSecret = m_PlayerInfo.apiSecret;
  machineUpdate.playlist_id = m_PlayerInfo.id;
  machineUpdate.name = m_PlayerInfo.name;
  machineUpdate.description = m_PlayerInfo.description;
  machineUpdate.serial_number = m_PlayerInfo.serial_number;
  machineUpdate.warranty_number = m_PlayerInfo.warranty_number;
  machineUpdate.macaddress = m_PlayerInfo.macaddress;
  machineUpdate.macaddress_wireless = m_PlayerInfo.macaddress_wireless;
  machineUpdate.vendor = m_PlayerInfo.vendor;
  machineUpdate.hardware_version = m_PlayerInfo.hardware_version;
  machineUpdate.timezone = m_PlayerInfo.timezone;
  machineUpdate.status = m_PlayerInfo.status;
  machineUpdate.allow_new_content = m_PlayerInfo.allow_new_content;
  machineUpdate.allow_software_update = m_PlayerInfo.allow_software_update;
  machineUpdate.update_interval = m_PlayerInfo.update_interval;
  machineUpdate.update_time = m_PlayerInfo.update_time;
  return TVAPI_UpdateMachineInfo(machineUpdate);
}

void CNWClient::InitializeInternalsFromPlayer()
{
  if (!m_PlayerInfo.update_time.empty() && !m_PlayerInfo.update_interval.empty())
  {
    // "update_interval":"86400" or "daily", "update_time":"24:00"
    CDateTime time = CDateTime::GetCurrentDateTime();
    if (m_PlayerInfo.update_interval == "daily")
    {
      int hours, minutes;
      sscanf(m_PlayerInfo.update_time.c_str(),"%d:%d", &hours, &minutes);
      m_NextUpdateTime.SetDateTime(time.GetYear(), time.GetMonth(), time.GetDay(), hours, minutes, 0);

      // next update is 24 hours from m_NextUpdateTime
      m_NextUpdateInterval.SetDateTimeSpan(0, 24, 0, 0);
    }
    else
    {
      // if update_interval is not "daily", it is set to minutes
      int update_interval = atoi(m_PlayerInfo.update_interval.c_str());
      // we add minutes to current time to trigger the next update
      m_NextUpdateTime = time + CDateTimeSpan(0, 0, update_interval, 0);
      m_NextUpdateInterval.SetDateTimeSpan(0, 0, update_interval, 0);
    }

    // make sure we schedual next update past the current time
    if (time >= m_NextUpdateTime)
      m_NextUpdateTime += m_NextUpdateInterval;

    CLog::Log(LOGDEBUG, "**NW** - m_NextUpdateTime = %s", m_NextUpdateTime.GetAsDBDateTime().c_str());
    CLog::Log(LOGDEBUG, "**NW** - m_NextUpdateInterval = %d days, %d hours, %d mins",
      m_NextUpdateInterval.GetDays(), m_NextUpdateInterval.GetHours(), m_NextUpdateInterval.GetMinutes());

  }

  if (!m_PlayerInfo.tvapiURLBase.empty())
    TVAPI_SetURLBASE(m_PlayerInfo.tvapiURLBase);
}

bool CNWClient::CreatePlaylist(std::string home, NWPlaylist &playList,
  const TVAPI_Playlist &playlist, const TVAPI_PlaylistItems &playlistItems)
{
  // convert server structures to player structure
  playList.id = std::stoi(playlist.id);
  playList.name = playlist.name;
  playList.type = playlist.type;
  // format is "2013-08-22"
  playList.layout = playlist.layout;
  playList.updated_date = playlist.updated_date;
  playList.groups.clear();
  playList.play_order.clear();

  for (auto catagory : playlist.categories)
  {
    NWGroup group;
    group.id = std::stoi(catagory.id);
    group.name = catagory.name;
    group.next_asset_index = 0;
    // always remember original group play order
    // this determines group play order. ie.
    // pick 1st group, play asset, pick next group, play asset, do all groups
    // cycle back, pick 1st group, play next asset...
    playList.play_order.push_back(group.id);

    // check if we already have handled this group
    auto it = std::find_if(playList.groups.begin(), playList.groups.end(),
      [group](const NWGroup &existingGroup) { return existingGroup.id == group.id; });
    if (it == playList.groups.end())
    {
      // not present, pull out assets assigned to this group
      for (auto item : playlistItems.items)
      {
        if (item.tv_category_id == catagory.id)
        {
          NWAsset asset;
          asset.id = std::stoi(item.id);
          asset.name = item.name;
          asset.group_id = std::stoi(item.tv_category_id);
          asset.valid = false;

          for (auto file : item.files)
          {
            if (file.type == playList.video_format)
            {
              // trap out bad urls
              if (file.path.find("proxy.membernettv.com") != std::string::npos)
                continue;
              asset.id = std::stoi(item.id);
              asset.type = file.type;
              asset.video_url = file.path;
              asset.video_md5 = file.etag;
              asset.video_size = std::stoi(file.size);
              // format is "2013-02-27 01:00:00"
              asset.available_to.SetFromDBDateTime(item.availability_to);
              asset.available_from.SetFromDBDateTime(item.availability_from);
              asset.video_basename = URIUtils::GetFileName(asset.video_url);
              std::string video_extension = URIUtils::GetExtension(asset.video_url);
              std::string localpath = kNWClient_DownloadVideoPath + std::to_string(asset.id) + video_extension;
              asset.video_localpath = URIUtils::AddFileToFolder(home, localpath);
              break;
            }
          }
          // if we got an complete asset, save it.
          if (!asset.video_url.empty())
          {
            // trap out bad urls
            if (item.thumb.path.find("proxy.membernettv.com") == std::string::npos)
            {
              // bring over thumb references
              asset.thumb_url = item.thumb.path;
              asset.thumb_md5 = item.thumb.etag;
              asset.thumb_size = std::stoi(item.thumb.size);
              asset.thumb_basename = URIUtils::GetFileName(asset.thumb_url);
              std::string thumb_extension = URIUtils::GetExtension(asset.thumb_url);
              std::string localpath = kNWClient_DownloadVideoThumbNailsPath + std::to_string(asset.id) + thumb_extension;
              asset.thumb_localpath = URIUtils::AddFileToFolder(home, localpath);
            }
            group.assets.push_back(asset);
          }
        }
      }
      // add the new group regardless of it there are assets present
      // player will skip over this group if there are no assets.
      playList.groups.push_back(group);
    }
  }

  return false;
}

void CNWClient::AssetUpdateCallBack(const void *ctx, NWAsset &asset, bool wasDownloaded)
{
  CNWClient *client = (CNWClient*)ctx;
  client->m_Player->MarkValidated(asset);
  if (wasDownloaded)
    client->LogFilesDownLoaded(std::to_string(asset.id));

  if (client->m_Player->IsPlaying() || client->m_PlayerInfo.allow_async_player == "no")
  {
    if (client->m_Startup && client->m_dlgProgress->IsDialogRunning())
    {
      int assetcount = client->m_MediaManager->GetLocalAssetCount();
      client->m_dlgProgress->SetLine(1, StringUtils::Format("Checking: %s", asset.name.c_str()));
      client->m_dlgProgress->SetLine(2, StringUtils::Format("Asset %i/%i", assetcount, client->m_totalAssets));
      client->m_dlgProgress->SetPercentage(int(float(assetcount) / float(client->m_totalAssets) * 100));
    }
  }
}

bool CNWClient::DoAuthorize()
{
/*
  m_activate.code = "HPPALSRK/A";
  if (!TVAPI_DoActivate(m_activate))
  {
    m_activate.apiKey = "gMFQKKYS/Ib3Kyo/2oMA";
    m_activate.apiSecret = "HtqhPrk3JyvX5bDSay75OY1RHTvGAhxwg51Kh7KJ";
  }
*/
  if (!m_HasNetwork)
    return !m_PlayerInfo.apiKey.empty() && !m_PlayerInfo.apiSecret.empty();

  std::string code = "";
  const std::string header = "Enter Activation Code";

  if (CGUIKeyboardFactory::ShowAndGetInput(code, CVariant{header}, false))
  {
    size_t test_url_pos = code.find("test://");
    if (test_url_pos != std::string::npos)
      test_url_pos = code.find("TEST://");

    if (test_url_pos != std::string::npos)
    {
      // if we find code starts with test site url, switch to test site
      TVAPI_SetURLBASE(kTVAPI_URLBASE_TESTSITE);
      code.erase(test_url_pos + 1);
    }

    TVAPI_Activate activate;
    activate.code = code;
    activate.application_id = CDarwinUtils::GetHardwareUUID();

    if (code.find("NWMNDEMO4K") != std::string::npos)
    {
      m_PlayerInfo.apiKey = "wAE/V6Gq3X3h0ZOjcK/A";
      m_PlayerInfo.apiSecret = "dMLudiHXRKc18ZJXFk4o7pyhaSww41/kvnjbmc4L";
      TVAPI_SetURLBASE(kTVAPI_URLBASE);
      SaveLocalPlayer(m_strHome, m_PlayerInfo);
      return true;
    }
    else if (code.find("NWMNDEMO") != std::string::npos)
    {
      // special case for code of 'NWMNDEMO'
      m_PlayerInfo.apiKey = "LbpCC91TBDsoHExRxvtV";
      m_PlayerInfo.apiSecret = "RN16RS1PUZVt8xgW+URBjU0o/ZXcdLWUDA45v2qQ";
      TVAPI_SetURLBASE(kTVAPI_URLBASE_TESTSITE);
      SaveLocalPlayer(m_strHome, m_PlayerInfo);
      return true;
    }
    else
    {
      if (TVAPI_DoActivate(activate))
      {
        m_PlayerInfo.apiKey = activate.apiKey;
        m_PlayerInfo.apiSecret = activate.apiSecret;
        TVAPI_Status  status;
        status.apiKey = m_PlayerInfo.apiKey;
        status.apiSecret = m_PlayerInfo.apiSecret;
        SaveLocalPlayer(m_strHome, m_PlayerInfo);
        return TVAPI_GetStatus(status);
      }
    }
  }

  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Activation Failed/Cancelled", "", 4000, false);
  return false;
}

bool CNWClient::IsAuthorized()
{
  if (!m_HasNetwork)
    return !m_PlayerInfo.apiKey.empty() && !m_PlayerInfo.apiSecret.empty();

  if (!m_PlayerInfo.apiKey.empty() && !m_PlayerInfo.apiSecret.empty())
  {
    TVAPI_Status  status;
    status.apiKey = m_PlayerInfo.apiKey;
    status.apiSecret = m_PlayerInfo.apiSecret;
    return TVAPI_GetStatus(status);
  }
  else
    return false;
}
