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
//#include "NWReportManager.h"
//#include "NWUpdateManager.h"
#include "NWMediaManager.h"
#include "UtilitiesMN.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "network/Network.h"
#include "settings/Settings.h"
#include "utils/FileUtils.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

#include "guilib/GUIWindowManager.h"
#include "settings/MediaSourceSettings.h"
#include "storage/MediaManager.h"


CCriticalSection CNWClient::m_playerLock;
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
 , m_ReportManager(NULL)
 , m_UpdateManager(NULL)
 , m_ClientCallBackFn(NULL)
 , m_ClientCallBackCtx(NULL)
{
  CLog::Log(LOGDEBUG, "**NW** - NW version %f", kTVAPI_PlayerFloatVersion);

  // hardcode for now
  //m_activate.code = "GR7IDTYXOF";
  m_activate.code = "HPPALSRK/A";
  
  m_activate.application_id = StringUtils::CreateUUID();
  //m_activate.application_id = "137e4e4a-2224-49c9-b8f1-f833cec4a3a3";
 // if (!TVAPI_DoActivate(m_activate))
  {
    //m_activate.apiKey = "/3/NKO6ZFdRgum7fZkMi";
    //m_activate.apiSecret = "ewuDiXOIgZP7l9/Rxt/LDQbmAI1zJe0PQ5VZYnuy";
    m_activate.apiKey = "gMFQKKYS/Ib3Kyo/2oMA";
    m_activate.apiSecret = "HtqhPrk3JyvX5bDSay75OY1RHTvGAhxwg51Kh7KJ";
    
  }

  m_status.apiKey = m_activate.apiKey;
  m_status.apiSecret = m_activate.apiSecret;
  TVAPI_GetStatus(m_status);

  // default path to local red directory in home
  std::string home = "special://home/nwmn/";

  // look for a removable disk with an
  // existing red directory
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

  std::string download_path = m_strHome + kTVAPI_NDownloadPath;
  if (!XFILE::CFile::Exists(download_path))
    CUtil::CreateDirectoryEx(download_path);

  std::string download_log_path = m_strHome + kTVAPI_DownloadLogPath;
  if (!XFILE::CFile::Exists(download_log_path))
    CUtil::CreateDirectoryEx(download_log_path);

  std::string video_path = m_strHome + kTVAPI_DownloadVideoPath;
  if (!XFILE::CFile::Exists(video_path))
    CUtil::CreateDirectoryEx(video_path);

  std::string videothumbs_path = m_strHome + kTVAPI_DownloadVideoThumbNailsPath;
  if (!XFILE::CFile::Exists(videothumbs_path))
    CUtil::CreateDirectoryEx(videothumbs_path);
/*
  std::string music_path = m_strHome + kRedDownloadMusicPath;
  if (!XFILE::CFile::Exists(music_path))
    CUtil::CreateDirectoryEx(music_path);

  std::string musicthumbs_path = m_strHome + kRedDownloadMusicThumbNailsPath;
  if (!XFILE::CFile::Exists(musicthumbs_path))
    CUtil::CreateDirectoryEx(musicthumbs_path);
*/
  std::string webui_path = m_strHome + "webdata/";
  if (!XFILE::CFile::Exists(webui_path))
    CUtil::CreateDirectoryEx(webui_path);

  GetLocalPlayerInfo(m_PlayerInfo, m_strHome);
  SendPlayerStatus(kTVAPI_Status_Restarting);

  m_MediaManager = new CNWMediaManager();
  m_MediaManager->RegisterAssetUpdateCallBack(this, AssetUpdateCallBack);
  //m_ReportManager = new CNWReportManager(m_strHome, &m_PlayerInfo);
  //m_ReportManager->RegisterReportManagerCallBack(this, ReportManagerCallBack);
  //m_UpdateManager = new CNWUpdateManager(m_strHome);
  m_Player = new CNWPlayer();

  CSingleLock lock(m_playerLock);
  m_this = this;
}

CNWClient::~CNWClient()
{
  CSingleLock lock(m_playerLock);
  m_this = NULL;
  m_ClientCallBackFn = NULL;
  m_bStop = true;
  StopThread();

  SendPlayerStatus(kTVAPI_Status_Off);

  //CGUIDialogRedAbout* about = CGUIDialogRedAbout::GetDialogRedAbout();
  //about->SetInfo(NULL, kTVAPI_PlayerFloatVersion);

  SAFE_DELETE(m_Player);
  SAFE_DELETE(m_MediaManager);
  //SAFE_DELETE(m_ReportManager);
  //SAFE_DELETE(m_UpdateManager);
}

CNWClient* CNWClient::GetClient()
{
  CSingleLock lock(m_playerLock);
  return m_this;
}

void CNWClient::Startup()
{
  Create();
  m_MediaManager->Create();
  //m_ReportManager->Create();
  //m_UpdateManager->Create();
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

void CNWClient::SendReport()
{
  //m_ReportManager->SendReport();
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
  TVAPI_Machine machine;
  machine.apiKey = m_activate.apiKey;
  machine.apiSecret = m_activate.apiSecret;
  TVAPI_GetMachine(machine);
  
  m_PlayerInfo.id  = machine.id;
  m_PlayerInfo.name = machine.machine_name;
  m_PlayerInfo.member = machine.member;
  m_PlayerInfo.timezone = machine.timezone;
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
  m_PlayerInfo.status = machine.status;
  m_PlayerInfo.apiKey = machine.apiKey;
  m_PlayerInfo.apiSecret = machine.apiSecret;
  //m_PlayerInfo.intSettingsVersion;

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

/*
  CDBManagerRed database;

  if (!m_HasNetwork)
  {
    database.Open();
    if (!database.IsPlayerValid())
    {
      database.Close();
      return;
    }
    database.GetPlayer(m_PlayerInfo);
    database.Close();
    CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetPlayerInfo() (off-line)" );
  }
  else
  {
    CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetPlayerInfo() (on-line)" );

    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);

    TiXmlDocument xml;
    xml.Parse(strXML.c_str());

    TiXmlElement *rootXmlNode = xml.RootElement();
    if (rootXmlNode)
    {
      TiXmlElement *responseNode = rootXmlNode->FirstChildElement("response");
      if (responseNode)
      {
        TiXmlElement *settingsNode = responseNode->FirstChildElement("settings");
        if (settingsNode)
        {
          if (ParsePlayerInfo(m_PlayerInfo, settingsNode))
          {
            database.Open();
            database.ClearPlayer();
            database.SavePlayer(m_PlayerInfo);
            database.Close();
            SaveLocalPlayerInfo(*settingsNode, m_strHome);

            // Only check for app updates from server player xml.
            CheckForUpdate(m_PlayerInfo);
          }
        }
      }
    }
  }
  // now parse and fill in member vars.
  int update_interval = strtol(m_PlayerInfo.strUpdateInterval.c_str(), NULL, 10);
  m_NextUpdateInterval.SetDateTimeSpan(0, 0, update_interval, 0);
  CLog::Log(LOGINFO, "**NW** - CNWClient::GetPlayerInfo() update   %d", update_interval);

  int report_interval = 1;
  if (m_PlayerInfo.strReportInterval.size())
    report_interval = strtol(m_PlayerInfo.strUpdateInterval.c_str(), NULL, 10);
  m_NextReportInterval.SetDateTimeSpan(0, report_interval, 0, 0);
  m_ReportManager->SetReportInterval(m_NextReportInterval);

  if (!m_PlayerInfo.strDownloadStartTime.empty() && !m_PlayerInfo.strDownloadDuration.empty())
  {
    // complicated but required. we get download time as a
    // hh:mm:ss field, duration is in hours. So we have to
    // be able to span over the beginning or end of a 24 hour day.
    // for example. start at 6pm, end at 6am. start would be 18:00:00,
    // duration would be 12.
    // don't use SetFromDBTime, it does the wrong thing :)
    int hours, minutes, seconds;
    sscanf(m_PlayerInfo.strDownloadStartTime.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
    CDateTime cur = CDateTime::GetCurrentDateTime();
    m_NextDownloadTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), hours, minutes, seconds);

    int download_duration = 12;
    if (m_PlayerInfo.strDownloadDuration.size())
      download_duration = strtol(m_PlayerInfo.strDownloadDuration.c_str(), NULL, 10);
    m_NextDownloadDuration.SetDateTimeSpan(0, download_duration, 0, 0);
    m_MediaManager->SetDownloadTime(m_NextDownloadTime, m_NextDownloadDuration);
    CLog::Log(LOGINFO, "**NW** - CNWClient::GetPlayerInfo() download %s, %d", m_NextDownloadTime.GetAsDBDateTime().c_str(), download_duration);
  }

  m_Player->OverridePlayBackWindow(true);
  if (!m_PlayerInfo.strPlayStartTime.empty() && !m_PlayerInfo.strPlayDuration.empty())
  {
    int hours, minutes, seconds;
    sscanf(m_PlayerInfo.strPlayStartTime.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
    CDateTime cur = CDateTime::GetCurrentDateTime();
    m_PlaybackTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), hours, minutes, seconds);

    int playback_duration = 12;
    if (m_PlayerInfo.strPlayDuration.size())
      playback_duration = strtol(m_PlayerInfo.strPlayDuration.c_str(), NULL, 10);
    m_PlaybackDuration.SetDateTimeSpan(0, playback_duration, 0, 0);

    if (playback_duration > 0)
    {
      m_Player->OverridePlayBackWindow(false);
      m_Player->SetPlayBackTime(m_PlaybackTime, m_PlaybackDuration);
    }
    CLog::Log(LOGINFO, "**NW** - CNWClient::GetPlayerInfo() playback %s, %d", m_PlaybackTime.GetAsDBDateTime().c_str(), playback_duration);
  }
*/
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

    //m_ProgramInfo = NWGroupPlaylist();
    m_ProgramInfo.video_format = m_PlayerInfo.video_format;
    CreateGroupPlaylist(m_strHome, m_ProgramInfo, playlist, playlistItems);

    // queue all assets belonging to this mediagroup
    m_Player->QueueProgramInfo(m_ProgramInfo);
    for (auto group : m_ProgramInfo.groups)
      m_MediaManager->QueueAssetsForDownload(group.assets);

    rtn = true;
  }

/*
  std::string function = "function=programInfo&id=" + m_PlayerInfo.strProgramID;
  std::string url = FormatUrl(m_PlayerInfo, function);

  CDBManagerRed database;
  std::vector<RedMediaGroup> mediagroups;

  if (!m_HasNetwork)
  {
    database.Open();
    if (!database.IsPlayerValid())
    {
      database.Close();
      CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetProgamInfo() no program info (off-line)" );
      return false;
    }

    database.GetProgram(m_ProgramInfo);
    database.Close();

    CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetProgamInfo() using old program info (off-line)" );
  }
  else
  {
    CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetProgamInfo() fetching program info (on-line)" );

    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);

    TiXmlDocument xml;
    xml.Parse(strXML.c_str());

    while(true)
    {
      TiXmlElement* rootXmlNode = xml.RootElement();
      if (!rootXmlNode)
        break;

      TiXmlElement* responseNode = rootXmlNode->FirstChildElement("response");
      if (!responseNode)
        break;

      TiXmlElement* programNode = responseNode->FirstChildElement("program");
      if (!programNode)
        break;

      // compare the date to previous data, if newer or 1st time
      // reset player and load new info, else skip checking the rest
      std::string date;
      XMLUtils::GetString(programNode, "date", date);
      CDateTime programDate;
      programDate.SetFromDBDateTime(date);
      if (!m_FullUpdate && m_ProgramDateStamp.IsValid() && programDate <= m_ProgramDateStamp)
      {
        CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetProgamInfo() using old program info (on-line)" );
        break;
      }
      CLog::Log(LOGDEBUG, "**NW** - CNWClient::GetProgamInfo() using new program info (on-line)" );

      m_ProgramDateStamp = programDate;

      m_ProgramInfo.strDate = date;
      m_ProgramInfo.strProgramID = ((TiXmlElement*) programNode)->Attribute("id");

      // new program list, purge active program.
      m_Player->Reset();
      m_MediaManager->ClearAssets();
      m_ProgramInfo.zones.clear();

      TiXmlElement* zonesNode = programNode->FirstChildElement("zones");
      if (!zonesNode)
        break;
      TiXmlNode *pZoneNode = NULL;
      while ((pZoneNode = zonesNode->IterateChildren(pZoneNode)) != NULL)
      {
        RedMediaZone zone;

        zone.strId = ((TiXmlElement*) pZoneNode)->Attribute("id");
        XMLUtils::GetString(pZoneNode, "left", zone.strLeft);
        XMLUtils::GetString(pZoneNode, "top", zone.strTop);
        XMLUtils::GetString(pZoneNode, "width", zone.strWidth);
        XMLUtils::GetString(pZoneNode, "height", zone.strHeight);

        TiXmlElement* playlistsNode = pZoneNode->FirstChildElement("playlists");
        if (!playlistsNode)
          break;

        TiXmlNode *pPlaylistNode = NULL;

        while ((pPlaylistNode = playlistsNode->IterateChildren(pPlaylistNode)) != NULL)
        {
          RedMediaPlaylist playlist;

          playlist.strID = ((TiXmlElement*) pPlaylistNode)->Attribute("id");
          XMLUtils::GetString(pZoneNode, "name", playlist.strName);
          XMLUtils::GetString(pZoneNode, "lastUpdated", playlist.strLastUpdated);

          TiXmlElement* mediaGroupsNode = pPlaylistNode->FirstChildElement("mediaGroups");
          if (!mediaGroupsNode)
            break;

          playlist.intMediaGroupsCount = (int) atoi(((TiXmlElement*) mediaGroupsNode)->Attribute("count"));

          TiXmlNode *pMediaGroupsNode = NULL;

          while ((pMediaGroupsNode = mediaGroupsNode->IterateChildren(pMediaGroupsNode)) != NULL)
          {
            RedMediaGroup mediagroup;
            mediagroup.id = ((TiXmlElement*) pMediaGroupsNode)->Attribute("id");
            mediagroup.name = ((TiXmlElement*) pMediaGroupsNode)->Attribute("name");
            mediagroup.playbackType = ((TiXmlElement*) pMediaGroupsNode)->Attribute("playback");
            mediagroup.playlistId = playlist.strID;
            mediagroup.assetIndex = "0";

            std::string date;
            XMLUtils::GetString(pMediaGroupsNode, "startDate", date);
            mediagroup.startDate.SetFromDBDateTime(date);


            XMLUtils::GetString(pMediaGroupsNode, "endDate", date);
            mediagroup.endDate.SetFromDBDateTime(date);

            TiXmlElement* assetsNode = pMediaGroupsNode->FirstChildElement("assets");
            if (!assetsNode)
              break;

            TiXmlNode *pAssetNode = NULL;
            while ((pAssetNode = assetsNode->IterateChildren(pAssetNode)) != NULL)
            {
              RedMediaAsset asset;

              asset.id = ((TiXmlElement*) pAssetNode)->Attribute("id");
              XMLUtils::GetString(pAssetNode, "url", asset.url);
              XMLUtils::GetString(pAssetNode, "md5", asset.md5);
              XMLUtils::GetString(pAssetNode, "size", asset.size);
              XMLUtils::GetString(pAssetNode, "title", asset.name);
              XMLUtils::GetString(pAssetNode, "type", asset.type);
              XMLUtils::GetString(pAssetNode, "thumbnail", asset.thumbnail_url);
              asset.basename = URIUtils::GetFileName(asset.url);
              asset.localpath = URIUtils::AddFileToFolder(m_strHome, kRedDownloadMusicPath + asset.basename);
              asset.thumbnail_basename = URIUtils::GetFileName(asset.thumbnail_url);
              asset.thumbnail_localpath = URIUtils::AddFileToFolder(m_strHome, kRedDownloadMusicThumbNailsPath + asset.thumbnail_basename);
              asset.mediagroup_id = mediagroup.id;

              TiXmlElement* pMetadataNode = pAssetNode->FirstChildElement("metadata");
              if (pMetadataNode)
              {
                TiXmlNode *pRecordNode = NULL;
                while ((pRecordNode = pMetadataNode->IterateChildren(pRecordNode)) != NULL)
                {
                  std::string key;
                  std::string value;
                  XMLUtils::GetString(pRecordNode, "key", key);
                  XMLUtils::GetString(pRecordNode, "value", value);

                  if (!key.empty() && !value.empty())
                  {
                    if (StringUtils::EqualsNoCase(key, "artist"))
                      asset.artist = value;
                    else if (StringUtils::EqualsNoCase(key, "year"))
                      asset.year = value;
                    else if (StringUtils::EqualsNoCase(key, "genre"))
                      asset.genre = value;
                    else if (StringUtils::EqualsNoCase(key, "composer"))
                      asset.composer = value;
                    else if (StringUtils::EqualsNoCase(key, "album"))
                      asset.album = value;
                    else if (StringUtils::EqualsNoCase(key, "tracknumber"))
                      asset.tracknumber = value;
                  }
                }
              }
 
              //CLog::Log(LOGDEBUG, "CNWClient::GetProgamInfo[%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n]",
              //          asset.url.c_str(),asset.md5.c_str(),asset.name.c_str(),asset.basename.c_str(),
              //          asset.artist.c_str(),asset.year.c_str(),asset.genre.c_str(),asset.composer.c_str(),
              //          asset.album.c_str(),asset.tracknumber.c_str());

              mediagroup.assets.push_back(asset);
            }

            // if requested, initial ramdomize of this group assets
            mediagroup.lastPlayedId.clear();
            if (mediagroup.playbackType == "random" && mediagroup.assets.size() > 3)
              std::random_shuffle(mediagroup.assets.begin(), mediagroup.assets.end());

            playlist.MediaGroups.push_back(mediagroup);
          } // while groups node
          zone.playlists.push_back(playlist);
        } // while playlists node
        m_ProgramInfo.zones.push_back(zone);
      } // while zones node

      database.Open();
      database.SaveProgram(m_ProgramInfo);
      database.Close();

      // force ourselves out of the fake while loop
      break;
    }
  }

  for (std::vector<RedMediaZone>::iterator rzit = m_ProgramInfo.zones.begin(); rzit != m_ProgramInfo.zones.end(); ++rzit)
  {
    for (std::vector<RedMediaPlaylist>::iterator rpit = rzit->playlists.begin(); rpit != rzit->playlists.end(); ++rpit)
    {
      for (std::vector<RedMediaGroup>::iterator mgit = rpit->MediaGroups.begin(); mgit != rpit->MediaGroups
           .end(); ++mgit)
      {
        // queue all assets belonging to this mediagroup
        m_Player->QueueMediaGroup(*mgit);
        m_MediaManager->QueueAssetsForDownload(mgit->assets);
      }
    }
  }

  // if this is the inital start up, override
  // download window in MediaManager
  if (m_Startup)
  {
    m_Startup = false;
    m_MediaManager->OverrideDownloadWindow();
  }
*/
  return rtn;
}


void CNWClient::NotifyAssetDownload(NWAsset &asset)
{
  CLog::Log(LOGDEBUG, "**NW** - CNWClient::NotifyAssetDownload");
/*
  std::string function = "function=player-NotifyAssetDownload&id=" + m_PlayerInfo.strPlayerID;
  function += "&asset_id=" + asset.id;

  std::string url = FormatUrl(m_PlayerInfo, function, "&format=xml");
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
      CLog::Log(LOGDEBUG, "**NW** - CNWClient::NotifyAssetDownload - Response '%s'", result.c_str());
    }
  }
*/
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
    /*
     During the update interval, query the player-GetActions method and perform the following:
     info (can be multiple info actions):
      a) SendFilesDownloaded - parse the Program Info, and compare to list of locally
        downloaded files and then sent up an API call.
      b) SendPlayerHealth - send up the health stats.
      c) SendPlayerNetworkInfo - send up the player network information.
      d) SendFilePlayed - send up file played report
      e) SendPlayerLog - send up file played report

     command:
      a) PlayerRestart - restart the system (reboot the box)
      b) PlayerStop - stop playback on the blackbird player
      c) PlayerStart - start playback on the blackbird player
    */

/*
    std::string function = "function=player-GetActions&id=" + m_PlayerInfo.strPlayerID;
    std::string url = FormatUrl(m_PlayerInfo, function);

    XFILE::CCurlFile http;
    std::string strXML;
    http.Get(url, strXML);

    TiXmlDocument xml;
    xml.Parse(strXML.c_str());

    TiXmlElement *rootXmlNode = xml.RootElement();
    if (rootXmlNode)
    {
      TiXmlElement *responseNode = rootXmlNode->FirstChildElement("response");
      if (responseNode)
      {
        std::string playerId;
        XMLUtils::GetString(responseNode, "playerId", playerId);
        if (playerId == m_PlayerInfo.strPlayerID)
        {
          TiXmlElement *actionsNode = responseNode->FirstChildElement("actions");
          if (actionsNode)
          {
            std::string info_str;
            XMLUtils::GetString(responseNode, "info", info_str);
            if (!info_str.empty())
            {
              std::vector<std::string> info_actions = StringUtils::Split(info_str, ",");
              for (size_t i = 0; i < info_actions.size(); i++)
              {
                if (info_actions[i] == "SendFilesDownloaded")
                {
                  SendFilesDownloaded();
                  ClearAction(info_actions[i]);
                }
                else if (info_actions[i] == "SendPlayerHealth")
                {
                  SendPlayerHealth();
                  ClearAction(info_actions[i]);
                }
                else if (info_actions[i] == "SendPlayerNetworkInfo")
                {
                  SendNetworkInfo();
                  ClearAction(info_actions[i]);
                }
                else if (info_actions[i] == "SendFilePlayed")
                {
                  m_ReportManager->SendReport();
                  ClearAction(info_actions[i]);
                }
                else if (info_actions[i] == "SendPlayerLog")
                {
                  SendPlayerLog();
                  ClearAction(info_actions[i]);
                }
                else
                {
                  // unknown action
                  ClearAction(info_actions[i]);
                }
              }
            }

            std::string command_str;
            XMLUtils::GetString(responseNode, "command", command_str);
            if (!command_str.empty())
            {
              if (command_str == "PlayerRestart")
              {
                // flip the order so server action gets cleared
                ClearAction(command_str);
//                CApplicationMessenger::Get().Restart();
              }
              else if (command_str == "PlayerStop")
              {
                m_Player->StopPlaying();
                ClearAction(command_str);
              }
              else if (command_str == "PlayerStart")
              {
                m_Player->Play();
                ClearAction(command_str);
              }
            }
          }
        }
      }
    }
*/
  }
}

void CNWClient::ClearAction(std::string action)
{
  if (m_HasNetwork)
  {
    // clear a server side requested action
  }
}

bool CNWClient::CreateGroupPlaylist(std::string home, NWGroupPlaylist &groupPlayList,
  const TVAPI_Playlist &playlist, const TVAPI_PlaylistItems &playlistItems)
{
  // convert server structures to player structure
  groupPlayList.id = std::stoi(playlist.id);
  groupPlayList.name = playlist.name;
  groupPlayList.type = playlist.type;
  // format is "2013-08-22"
  groupPlayList.updated_date = playlist.updated_date;
  groupPlayList.groups.clear();
  groupPlayList.play_order.clear();

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
    groupPlayList.play_order.push_back(group.id);

    // check if we already have handled this group
    auto it = std::find_if(groupPlayList.groups.begin(), groupPlayList.groups.end(),
      [group](const NWGroup &existingGroup) { return existingGroup.id == group.id; });
    if (it == groupPlayList.groups.end())
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
            if (file.type == groupPlayList.video_format)
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
              asset.video_localpath = URIUtils::AddFileToFolder(home, kTVAPI_DownloadVideoPath + asset.video_basename);
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
              asset.thumb_localpath = URIUtils::AddFileToFolder(home, kTVAPI_DownloadVideoThumbNailsPath + asset.thumb_basename);
            }
            group.assets.push_back(asset);
          }
        }
      }
      // add the new group regardless of it there are assets present
      // player will skip over this group if there are no assets.
      groupPlayList.groups.push_back(group);
    }
  }

  return false;
}

void CNWClient::AssetUpdateCallBack(const void *ctx, NWAsset &asset, bool wasDownloaded)
{
  //CLog::Log(LOGDEBUG, "**NW** - CNWClient::AssetUpdateCallBack()" );
  CNWClient *manager = (CNWClient*)ctx;
  manager->m_Player->ValidateAsset(asset, true);

  if (wasDownloaded)
  {
    // mark it downloaded in DB
    SetDownloadedAsset(asset.id);
    manager->NotifyAssetDownload(asset);
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

void CNWClient::ForceLocalPlayerUpdate()
{
  GetLocalPlayerInfo(m_PlayerInfo, m_strHome);
}

void CNWClient::CheckForUpdate(NWPlayerInfo &player)
{
#if defined(TARGET_ANDROID)
  if (!player.strUpdateUrl.empty() && !player.strUpdateMD5.empty())
  {
    RedMediaUpdate update = {};
    sscanf(player.strUpdateVersion.c_str(), "%f", &update.version);

    if (update.version > kRedPlayerFloatVersion)
    {
      CLog::Log(LOGDEBUG, "**NW** - CNWClient::CheckForUpdate(), version %f, version %f found.", kRedPlayerFloatVersion, update.version);

      update.url = player.strUpdateUrl;
      update.key = player.strUpdateKey;
      update.md5 = player.strUpdateMD5;
      update.size = player.strUpdateSize;
      update.name = player.strUpdateName;
      update.date.SetFromDBDateTime(player.strUpdateDate);
      std::string home = CSpecialProtocol::TranslatePath(m_strHome);
      std::string localpath = home + kRedDownloadUpdatePath + URIUtils::GetFileName(update.url).c_str();
      update.localpath = CSpecialProtocol::TranslatePath(localpath);

      m_UpdateManager->SetDownloadTime(m_NextDownloadTime, m_NextDownloadDuration);
      m_UpdateManager->QueueUpdateForDownload(update);
    }
  }
#endif
}