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
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "utils/FileUtils.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StringUtils.h"
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
 , m_FullUpdate(false)
 , m_NextUpdateTime(CDateTime::GetCurrentDateTime())
 , m_NextUpdateInterval(0, 0, 5, 0)
 , m_NextReportInterval(0, 6, 0, 0)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_NextDownloadDuration(0, 6, 0, 0)
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

  LoadLocalPlayer(m_strHome, m_PlayerInfo);
  m_activate.apiKey = m_PlayerInfo.apiKey;
  m_activate.apiSecret = m_PlayerInfo.apiSecret;
  m_activate.application_id = CDarwinUtils::GetHardwareUUID();

  m_MediaManager = new CNWMediaManager();
  m_MediaManager->RegisterAssetUpdateCallBack(this, AssetUpdateCallBack);
  m_Player = new CNWPlayer();

  CSingleLock lock(m_clientLock);
  m_this = this;

  ANNOUNCEMENT::CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CNWClient::~CNWClient()
{
  CSingleLock lock(m_clientLock);
  m_this = NULL;
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
      CSingleLock lock(m_reportLock);
      LogPlayback(m_strHome, m_PlayerInfo.id, assetID);
    }
  }
}

void CNWClient::Startup()
{
  if (!IsAuthorized())
    while (!DoAuthorize());

  m_status.apiKey = m_activate.apiKey;
  m_status.apiSecret = m_activate.apiSecret;
  TVAPI_GetStatus(m_status);

/*
  TVAPI_Actions actions;
  actions.apiKey = m_activate.apiKey;
  actions.apiSecret = m_activate.apiSecret;
  TVAPI_GetActionQueue(actions);
*/

  SendPlayerStatus(kTVAPI_Status_Restarting);

  Create();
  m_MediaManager->Create();
  m_Player->Create();
}

void CNWClient::SetSettings(NWPlayerSettings settings)
{
  CSettings::GetInstance().SetString(CSettings::MN_LOCATION_ID,settings.strLocation_id);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_ID,settings.strMachine_id);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_DESCRIPTION,settings.strMachine_description);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_ETHERNET_ID,settings.strMachine_ethernet_id);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_HW_VERSION,settings.strMachine_hw_version);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_NAME,settings.strMachine_name);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_PURCHASE_DATE,settings.strMachine_purchase_date);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_SN,settings.strMachine_sn);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_VENDOR,settings.strMachine_vendor);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_WARRANTY,settings.strMachine_warrenty_nr);
  CSettings::GetInstance().SetString(CSettings::MN_MACHINE_WIRELESS,settings.strMachine_wireless_id);
  CSettings::GetInstance().SetString(CSettings::MN_SETTINGS_CF_BUNDLE,settings.strSettings_cf_bundle_version);
  CSettings::GetInstance().SetString(CSettings::MN_SETTINGS_UPDATE_INTERVAL,settings.strSettings_update_interval);
  CSettings::GetInstance().SetString(CSettings::MN_SETTINGS_UPDATE_TIME,settings.strSettings_update_time);
  CSettings::GetInstance().SetString(CSettings::MN_URL,settings.strUrl_feed);
  CSettings::GetInstance().Save();
}

NWPlayerSettings CNWClient::GetSettings()
{
  NWPlayerSettings settings;
  settings.strLocation_id                = CSettings::GetInstance().GetString(CSettings::MN_LOCATION_ID);
  settings.strMachine_id                 = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_ID);
  settings.strMachine_description        = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_DESCRIPTION);
  settings.strMachine_ethernet_id        = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_ETHERNET_ID);
  settings.strMachine_hw_version         = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_HW_VERSION);
  settings.strMachine_name               = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_NAME);
  settings.strMachine_purchase_date      = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_PURCHASE_DATE);
  settings.strMachine_sn                 = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_SN);
  settings.strMachine_vendor             = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_VENDOR);
  settings.strMachine_warrenty_nr        = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_WARRANTY);
  settings.strMachine_wireless_id        = CSettings::GetInstance().GetString(CSettings::MN_MACHINE_WIRELESS);
  settings.strSettings_cf_bundle_version = CSettings::GetInstance().GetString(CSettings::MN_SETTINGS_CF_BUNDLE);
  settings.strSettings_update_interval   = CSettings::GetInstance().GetString(CSettings::MN_SETTINGS_UPDATE_INTERVAL);
  settings.strSettings_update_time       = CSettings::GetInstance().GetString(CSettings::MN_SETTINGS_UPDATE_TIME);
  settings.strUrl_feed                   = CSettings::GetInstance().GetString(CSettings::MN_URL);
  
  if (settings.strMachine_sn.empty())
    settings.strMachine_sn = "UNKNOWN";

  if (settings.strMachine_ethernet_id.empty())
  {
    CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
    if (iface)
    {
      settings.strMachine_ethernet_id = iface->GetMacAddress();
      CSettings::GetInstance().SetString(CSettings::MN_MACHINE_ETHERNET_ID,settings.strMachine_ethernet_id);
    }
  }

  return settings;
}

void CNWClient::FullUpdate()
{
  SendPlayerStatus(kTVAPI_Status_Restarting);
  m_FullUpdate = true;
}

void CNWClient::GetStats(CDateTime &NextUpdateTime, CDateTime &NextDownloadTime, CDateTimeSpan &NextDownloadDuration)
{
  NextUpdateTime = m_NextUpdateTime;
  NextDownloadTime = m_NextDownloadTime;
  NextDownloadDuration = m_NextDownloadDuration;
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

bool CNWClient::SendPlayerStatus(const std::string status)
{
  return false;
}

void CNWClient::RegisterClientCallBack(const void *ctx, ClientCallBackFn fn)
{
  m_ClientCallBackFn = fn;
  m_ClientCallBackCtx = ctx;
}

void CNWClient::RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn)
{
  m_Player->RegisterPlayerCallBack(ctx, fn);
}

void CNWClient::Process()
{
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);
  CLog::Log(LOGDEBUG, "**NW** - CNWClient::Process Started");

  while (!m_bStop)
  {
    Sleep(100);

    CDateTime time = CDateTime::GetCurrentDateTime();
    if (m_FullUpdate || time >= m_NextUpdateTime)
    {
      m_NextUpdateTime = time + m_NextUpdateInterval;

      m_HasNetwork = GetPlayerStatus();
      m_MediaManager->UpdateNetworkStatus(m_HasNetwork);

      if (m_FullUpdate)
        SendNetworkInfo();

      GetPlayerInfo();
      GetActions();
      if (GetProgamInfo())
      {
        m_FullUpdate = false;
        m_Player->Play();
      }

      if (m_ClientCallBackFn)
        (*m_ClientCallBackFn)(m_ClientCallBackCtx, true);

      if (m_Player->IsPlaying())
        SendPlayerStatus(kTVAPI_Status_On);
      else
        SendPlayerStatus(kTVAPI_Status_Off);
    }
  }

  CLog::Log(LOGDEBUG, "**NW** - CNWClient::Process Stopped");
}

void CNWClient::GetPlayerInfo()
{
  if (m_HasNetwork)
  {
    TVAPI_Machine machine;
    machine.apiKey = m_activate.apiKey;
    machine.apiSecret = m_activate.apiSecret;
    TVAPI_GetMachine(machine);

  std::string allow_new_content;
  std::string allow_software_update;
  std::string status;
  std::string apiKey;
  std::string apiSecret;
  std::string tvapiURLBase;

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
    /*
    // "update_interval":"86400","update_time":"24:00"
    if (settings.strSettings_update_interval == "daily")
    {
      sscanf(settings.strSettings_update_time.c_str(),"%d:%d", &hours, &minutes);
      m_NextDownloadTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), hours, minutes, 0);
    }
    else
    {
      // if interval is not "daily", its set to minutes
      int interval = atoi(settings.strSettings_update_time.c_str());

      // we add minutes to current time to trigger the next update
      m_NextDownloadTime = cur + CDateTimeSpan(0,0,interval,0);
    }
    */
    m_PlayerInfo.allow_new_content = machine.allow_new_content;
    m_PlayerInfo.allow_software_update = machine.allow_software_update;
    m_PlayerInfo.status = machine.status;
    m_PlayerInfo.apiKey = machine.apiKey;
    m_PlayerInfo.apiSecret = machine.apiSecret;

    m_PlayerInfo.tvapiURLBase = TVAPI_GetURLBASE();
;
    /*
    std::string apiKey;
    std::string apiSecret;
    // reply
    std::string id;
    std::string member;
    std::string machine_name;
    std::string description;
    std::string playlist_id;
    std::string status;
    std::string vendor;
    std::string hardware;
    std::string timezone;
    std::string serial_number;
    std::string warranty_number;
    std::string video_format;
    std::string allow_new_content;
    std::string allow_software_update;
    std::string update_time;
    std::string update_interval;
    NWMachineLocation location;
    NWMachineNetwork  network;
    NWMachineSettings settings;
    NWMachineMenu     menu;
    NWMachineMembernet_software  membernet_software;
    NWMachineApple_software apple_software;
    */
    SaveLocalPlayer(m_strHome, m_PlayerInfo);
  }
  else
  {
    if (HasLocalPlayer(m_strHome))
      LoadLocalPlayer(m_strHome, m_PlayerInfo);
  }
}

bool CNWClient::GetPlayerStatus()
{
  m_status.apiKey = m_activate.apiKey;
  m_status.apiSecret = m_activate.apiSecret;
  return TVAPI_GetStatus(m_status);
}

bool CNWClient::GetProgamInfo()
{
  bool rtn = false;

  if (m_HasNetwork)
  {
    TVAPI_Playlist playlist;
    playlist.apiKey = m_activate.apiKey;
    playlist.apiSecret = m_activate.apiSecret;
    TVAPI_GetPlaylist(playlist, m_PlayerInfo.playlist_id);

    if (m_ProgramInfo.updated_date != playlist.updated_date)
    {
      TVAPI_PlaylistItems playlistItems;
      playlistItems.apiKey = m_activate.apiKey;
      playlistItems.apiSecret = m_activate.apiSecret;
      TVAPI_GetPlaylistItems(playlistItems, m_PlayerInfo.playlist_id);

      m_ProgramInfo.video_format = m_PlayerInfo.video_format;
      CreatePlaylist(m_strHome, m_ProgramInfo, playlist, playlistItems);
      SaveLocalPlaylist(m_strHome, m_ProgramInfo);

      // queue all assets belonging to this mediagroup
      m_Player->QueueProgramInfo(m_ProgramInfo);
      for (auto group : m_ProgramInfo.groups)
        m_MediaManager->QueueAssetsForDownload(group.assets);

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
        if (m_ProgramInfo.updated_date != updated_date)
        {
          // queue all assets belonging to this mediagroup
          m_Player->QueueProgramInfo(m_ProgramInfo);
          for (auto group : m_ProgramInfo.groups)
            m_MediaManager->QueueAssetsForDownload(group.assets);
          rtn = true;
        }
      }
    }
  }

  return rtn;
}

void CNWClient::SendFilesDownloaded()
{
/*
  if (m_HasNetwork)
  {
    // compare program list to files on disk, send what we have on disk

    CDBManagerRed database;
    database.Open();
    std::vector<NWAsset> assets; // this holds all downloaded assest at the time of a query
    database.GetAllDownloadedAssets(assets);
    database.Close();

    // below if we need all files that are on local disc
    CFileItemList items;
    XFILE::CDirectory::GetDirectory(m_strHome + kRedDownloadMusicPath, items, "", XFILE::DIR_FLAG_NO_FILE_DIRS);

    for (int i=0; i < items.Size(); i++)
    {
      std::string itemName = URIUtils::GetFileName(items[i]->GetPath());
      CLog::Log(LOGDEBUG, "**NW** - SendFilesDownloaded() - %s", itemName.c_str());
    }
  }
*/
}

void CNWClient::SendPlayerHealth()
{
/*
  if (m_HasNetwork)
  {
    CLog::Log(LOGDEBUG, "**NW** - CNWClient::SendPlayerHealth");
    // date=2014-10-03 06:25:30-0800
    // uptime=0 days 5 hours 22 minutes
    //
    // function=playerHealth&id=8&date=2014-10-03+06%3A25%3A30-0800&uptime=0+days+5+hours+22+minutes&disk_used=63GB&disk_free=395GB&smart_status=Disks+OK&format=xml&security=1538c84eef34e81f6cbe807f10095fa7&apiKey=cFpN1RnsW9YulGb2Vhvy
    std::string function = "function=playerHealth&id=" + m_PlayerInfo.strPlayerID;

    std::string date = CDateTime::GetCurrentDateTime().GetAsDBDateTime();
    CDateTimeSpan bias = CDateTime::GetTimezoneBias();
    function += "&date=" + date + StringUtils::Format("-%02d00", bias.GetHours());
    function += "&uptime=" + GetSystemUpTime();
    function += "&disk_used=" + GetDiskUsed(m_strHome);
    function += "&disk_free=" + GetDiskFree(m_strHome);
    function += "&smart_status=Disks OK";

    StringUtils::Replace(function, ' ', '+');
    std::string xmlEncoded = EncodeExtra(function);
    std::string url = FormatUrl(m_PlayerInfo, xmlEncoded, "&format=xml");
    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);

    TiXmlDocument xml;
    xml.Parse(strXML.c_str());

    TiXmlElement* rootXmlNode = xml.RootElement();
    if (rootXmlNode)
    {
      TiXmlElement* responseNode = rootXmlNode->FirstChildElement("response");
      if (responseNode)
      {
        std::string result; // 'Operation Successful'
        XMLUtils::GetString(responseNode, "result", result);
        CLog::Log(LOGDEBUG, "**NW** - CNWClient::SendPlayerHealth - Response '%s'", result.c_str());
      }
    }
  }
*/
}

void CNWClient::SendNetworkInfo()
{
/*
  CLog::Log(LOGDEBUG, "**NW** - CNWClient::SendNetworkInfo()");
  if (m_HasNetwork)
  {
    // send networking information to the server.
    CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
    if (iface)
    {
      // yes, player-Edit really means send network info
      std::string function = "function=player-Edit&id=" + m_PlayerInfo.strPlayerID;
      function += "&network_ip_address=" + iface->GetCurrentIPAddress();
      function += "&network_default_gateway=" + iface->GetCurrentDefaultGateway();

      std::vector<std::string> nss = g_application.getNetwork().GetNameServers();
      if (nss.size() >= 1)
        function += "&network_dns_server_1=" + nss[0];
      else
        function += "&network_dns_server_1=0.0.0.0";
      if (nss.size() >= 2)
        function += "&network_dns_server_2=" + nss[1];
      else
        function += "&network_dns_server_2=0.0.0.0";
      function += "&network_subnet_mask=" + iface->GetCurrentNetmask();
      function += "&network_ethernet_mac_address=" + iface->GetMacAddress();

      std::vector<CNetworkInterface*> ifaces = g_application.getNetwork().GetInterfaceList();
      for (size_t i = 0; i < ifaces.size(); i++)
      {
        if (ifaces[i]->IsWireless())
        {
          function += "&network_wireless_mac_address=" + ifaces[i]->GetMacAddress();
          break;
        }
      }
      if (function.find("network_wireless_mac_address") == std::string::npos)
        function += "&network_wireless_mac_address=00:00:00:00:00:00";

      // make it network pretty
      std::string xmlEncoded = EncodeExtra(function);
      std::string url = FormatUrl(m_PlayerInfo, xmlEncoded, "&format=xml");
      XFILE::CCurlFile http;
      std::string strXML;
      http.Get(url, strXML);

      TiXmlDocument xml;
      xml.Parse(strXML.c_str());

      TiXmlElement* rootXmlNode = xml.RootElement();
      if (rootXmlNode)
      {
        TiXmlElement* responseNode = rootXmlNode->FirstChildElement("response");
        if (responseNode)
        {
          std::string result; // 'Operation Successful'
          XMLUtils::GetString(responseNode, "result", result);
          CLog::Log(LOGDEBUG, "**NW** - CNWClient::SendNetworkInfo - Response '%s'", result.c_str());
        }
      }
    }
  }
*/
}

void CNWClient::SendPlayerLog()
{
  // fetch xbmc.log and push it up
}

void CNWClient::GetActions()
{
  if (m_HasNetwork)
  {
    TVAPI_Actions actions;
    actions.apiKey = m_activate.apiKey;
    actions.apiSecret = m_activate.apiSecret;
    if (TVAPI_GetActionQueue(actions))
    {
      for (auto action: actions.actions)
      {
        if (action.action == kTVAPI_ActionHealth)
        {
          TVAPI_HealthReport health;
          health.apiKey = m_activate.apiKey;
          health.apiSecret = m_activate.apiSecret;
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
          //CSingleLock lock(m_reportLock);
          //SendFilesDownloaded();
          //ClearAction(actions, action.id);
        }
        else if (action.action == kTVAPI_ActionFilePlayed)
        {
          //CSingleLock lock(m_reportLock);
          //SendFilesPlayed();
          //ClearAction(actions, action.id);
        }
        else if (action.action == kTVAPI_ActionPlay)
        {
          //m_Player->Play();
          //ClearAction(actions, action.id);
        }
        else if (action.action == kTVAPI_ActionStop)
        {
          //m_Player->StopPlaying();
          //ClearAction(actions, action.id);
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
}

void CNWClient::ClearAction(TVAPI_Actions &actions, std::string id)
{
  if (m_HasNetwork)
  {
    // clear a server side requested action
    for (auto action: actions.actions)
    {
      if (action.id == id)
      {
        TVAPI_ActionStatus actionStatus;
        actionStatus.apiKey = m_activate.apiKey;
        actionStatus.apiSecret = m_activate.apiSecret;
        actionStatus.id = action.id;
        actionStatus.status = kTVAPI_ActionStatusCompleted;
        TVAPI_UpdateActionStatus(actionStatus);
        break;
      }
    }
  }
}

bool CNWClient::CreatePlaylist(std::string home, NWPlaylist &playList,
  const TVAPI_Playlist &playlist, const TVAPI_PlaylistItems &playlistItems)
{
  // convert server structures to player structure
  playList.id = std::stoi(playlist.id);
  playList.name = playlist.name;
  playList.type = playlist.type;
  // format is "2013-08-22"
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
              std::string localpath = kNWClient_DownloadVideoThumbNailsPath + std::to_string(asset.id) + video_extension;
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
  CNWClient *manager = (CNWClient*)ctx;
  manager->m_Player->MarkValidated(asset);
  if (!g_application.m_pPlayer->IsPlaying())
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "validating", asset.name, 500, false);

  if (wasDownloaded)
  {
    CSingleLock lock(manager->m_reportLock);
    LogDownLoad(manager->m_strHome, manager->m_PlayerInfo.id, std::to_string(asset.id));
  }
}

void CNWClient::ReportManagerCallBack(const void *ctx, bool status)
{
  CLog::Log(LOGDEBUG, "**NW** - CNWClient::ReportManagerCallBack()" );
  CNWClient *manager = (CNWClient*)ctx;
  manager->SendPlayerHealth();
}

void CNWClient::UpdatePlayerInfo(const std::string strPlayerID, const std::string strApiKey,const std::string strSecretKey)
{
  m_PlayerInfo.id = strPlayerID;
  m_PlayerInfo.apiKey = strApiKey;
  m_PlayerInfo.apiSecret = strSecretKey;
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

//m_activate.code = "HPPALSRK/A";

  std::string code = "";
  const std::string header = "Enter Authorization Code";

  if (CGUIKeyboardFactory::ShowAndGetInput(code, CVariant{header}, false))
  {
    TVAPI_Activate activate = m_activate;
    activate.code = code;
    if (TVAPI_DoActivate(activate))
    {
      m_activate = activate;
      m_status.apiKey = m_activate.apiKey;
      m_status.apiSecret = m_activate.apiSecret;
      return TVAPI_GetStatus(m_status);
    }
    else
    {
      m_activate.apiKey = "gMFQKKYS/Ib3Kyo/2oMA";
      m_activate.apiSecret = "HtqhPrk3JyvX5bDSay75OY1RHTvGAhxwg51Kh7KJ";
      m_status.apiKey = m_activate.apiKey;
      m_status.apiSecret = m_activate.apiSecret;
      return TVAPI_GetStatus(m_status);
    }
  }

  return false;
}

bool CNWClient::IsAuthorized()
{
  if (!m_activate.apiKey.empty() && !m_activate.apiSecret.empty())
  {
    m_status.apiKey = m_activate.apiKey;
    m_status.apiSecret = m_activate.apiSecret;
    return TVAPI_GetStatus(m_status);
  }
  else
    return false;
}
