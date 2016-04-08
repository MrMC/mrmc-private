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

#include "system.h"

#include "PlayerManagerRed.h"
#include "ReportManagerRed.h"
#include "UpdateManagerRed.h"
#include "PlayBackManagerRed.h"
#include "MediaManagerRed.h"
#include "UtilitiesRed.h"
#include "DBManagerRed.h"
#include "GUIDialogRedAbout.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "Util.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/SpecialProtocol.h"
#include "network/Network.h"
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

// http://ec2-54-235-246-85.compute-1.amazonaws.com/api/1/test

CCriticalSection CPlayerManagerRed::m_player_lock;
CPlayerManagerRed *CPlayerManagerRed::m_PlayerManager = NULL;

CPlayerManagerRed::CPlayerManagerRed()
 : CThread("CPlayerManagerRed")
 , m_Startup(true)
 , m_HasNetwork(true)
 , m_FullUpdate(false)
 , m_NextUpdateTime(CDateTime::GetCurrentDateTime())
 , m_NextUpdateInterval(0, 0, 5, 0)
 , m_NextReportInterval(0, 6, 0, 0)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_NextDownloadDuration(0, 6, 0, 0)
 , m_MediaManager(NULL)
 , m_ReportManager(NULL)
 , m_UpdateManager(NULL)
 , m_PlaybackManager(NULL)
 , m_PlayerCallBackFn(NULL)
 , m_PlayerCallBackCtx(NULL)
{
  CLog::Log(LOGDEBUG, "**RED** - RED version %f", kRedPlayerFloatVersion);

  // default path to local red directory in home
  std::string home = "special://home/red/";

  // look for a removable disk with an
  // existing red directory
  VECSOURCES sources;
  g_mediaManager.GetRemovableDrives(sources);
  for (size_t indx = 0; indx < sources.size(); indx++)
  {
    std::string test_dir = sources[indx].strPath + "/red/";
    if (XFILE::CFile::Exists(test_dir))
    {
      // found one, use it.
      home = test_dir;
      break;
    }
  }
  CSpecialProtocol::SetREDPath(home);
  CLog::Log(LOGDEBUG, "**RED** - RED special:// path - %s", home.c_str());

    // now we can ref to either location using the same special path.
  m_strHome = "special://red/";
  if (!XFILE::CFile::Exists(m_strHome))
    CUtil::CreateDirectoryEx(m_strHome);

  std::string download_path = m_strHome + kRedDownloadPath;
  if (!XFILE::CFile::Exists(download_path))
    CUtil::CreateDirectoryEx(download_path);

  std::string update_path = m_strHome + kRedDownloadUpdatePath;
  if (!XFILE::CFile::Exists(update_path))
    CUtil::CreateDirectoryEx(update_path);

  std::string music_path = m_strHome + kRedDownloadMusicPath;
  if (!XFILE::CFile::Exists(music_path))
    CUtil::CreateDirectoryEx(music_path);

  std::string musicthumbs_path = m_strHome + kRedDownloadMusicThumbNailsPath;
  if (!XFILE::CFile::Exists(musicthumbs_path))
    CUtil::CreateDirectoryEx(musicthumbs_path);

  std::string webui_path = m_strHome + "webdata/";
  if (!XFILE::CFile::Exists(webui_path))
    CUtil::CreateDirectoryEx(webui_path);

  GetLocalPlayerInfo(m_PlayerInfo, m_strHome);
  SendPlayerStatus(kRedStatus_Restarting);

  m_MediaManager = new CMediaManagerRed();
  m_MediaManager->RegisterAssetUpdateCallBack(this, AssetUpdateCallBack);
  m_ReportManager = new CReportManagerRed(m_strHome, &m_PlayerInfo);
  m_ReportManager->RegisterReportManagerCallBack(this, ReportManagerCallBack);
  m_UpdateManager = new CUpdateManagerRed(m_strHome);
  m_PlaybackManager = new CPlayBackManagerRed();

  CGUIDialogRedAbout* about = CGUIDialogRedAbout::GetDialogRedAbout();
  about->SetInfo(&m_PlayerInfo, kRedPlayerFloatVersion);

  CSingleLock lock(m_player_lock);
  m_PlayerManager = this;
}

CPlayerManagerRed::~CPlayerManagerRed()
{
  CSingleLock lock(m_player_lock);
  m_PlayerManager = NULL;
  m_PlayerCallBackFn = NULL;
  StopThread();

  SendPlayerStatus(kRedStatus_Off);

  CGUIDialogRedAbout* about = CGUIDialogRedAbout::GetDialogRedAbout();
  about->SetInfo(NULL, kRedPlayerFloatVersion);

  SAFE_DELETE(m_PlaybackManager);
  SAFE_DELETE(m_ReportManager);
  SAFE_DELETE(m_UpdateManager);
  SAFE_DELETE(m_MediaManager);
}

CPlayerManagerRed* CPlayerManagerRed::GetPlayerManager()
{
  CSingleLock lock(m_player_lock);
  return m_PlayerManager;
}

void CPlayerManagerRed::Startup()
{
  Create();
  m_MediaManager->Create();
  m_ReportManager->Create();
  m_UpdateManager->Create();
  m_PlaybackManager->Create();
}

void CPlayerManagerRed::FullUpdate()
{
  SendPlayerStatus(kRedStatus_Restarting);
  m_FullUpdate = true;
}

void CPlayerManagerRed::SendReport()
{
  m_ReportManager->SendReport();
}

void CPlayerManagerRed::GetStats(CDateTime &NextUpdateTime, CDateTime &NextDownloadTime, CDateTimeSpan &NextDownloadDuration)
{
  NextUpdateTime = m_NextUpdateTime;
  NextDownloadTime = m_NextDownloadTime;
  NextDownloadDuration = m_NextDownloadDuration;
}

void CPlayerManagerRed::PlayPause()
{
  m_PlaybackManager->PlayPause();
}

void CPlayerManagerRed::PausePlaying()
{
  m_PlaybackManager->Pause();
}

void CPlayerManagerRed::StopPlaying()
{
  m_PlaybackManager->StopPlaying();
}

void CPlayerManagerRed::PlayNext()
{
  m_PlaybackManager->PlayNext();
}

std::string CPlayerManagerRed::GetPlayerStatus()
{
  return m_PlayerInfo.strStatus;
}

bool CPlayerManagerRed::SendPlayerStatus(const std::string status)
{
  CLog::Log(LOGINFO, "**RED** - CPlayerManagerRed::SendPlayerStatus() %s", status.c_str());
  std::string function = "function=player-UpdateStatus&id=" + m_PlayerInfo.strPlayerID + "&status=" + status;
  std::string url = FormatUrl(m_PlayerInfo, function);

  m_PlayerInfo.strStatus = status;

  if (!m_HasNetwork)
    return false;

  XFILE::CCurlFile http;
  std::string strXML;
  http.Get(url, strXML);

  TiXmlDocument xml;
  xml.Parse(strXML.c_str());

  TiXmlElement* rootXmlNode = xml.RootElement();
  if (!rootXmlNode)
    return false;

  TiXmlElement* responseNode = rootXmlNode->FirstChildElement("response");
  if (!responseNode)
    return false;

  std::string result;
  XMLUtils::GetString(responseNode, "result", result);

  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendPlayerStatus - Response '%s'", result.c_str());
  if (StringUtils::EqualsNoCase(result, "Operation Successful"))
  {
    std::string return_status;
    XMLUtils::GetString(responseNode, "status", return_status);
    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendPlayerStatus - Return Status '%s'", return_status.c_str());
    return true;
  }
  return false;
}

void CPlayerManagerRed::RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn)
{
  m_PlayerCallBackFn = fn;
  m_PlayerCallBackCtx = ctx;
}

void CPlayerManagerRed::RegisterPlayBackCallBack(const void *ctx, PlayBackCallBackFn fn)
{
  m_PlaybackManager->RegisterPlayBackCallBack(ctx, fn);
}

void CPlayerManagerRed::Process()
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::Process Started");

  while (!m_bStop)
  {
    Sleep(100);

    CDateTime time = CDateTime::GetCurrentDateTime();
    if (m_FullUpdate || time >= m_NextUpdateTime)
    {
      m_NextUpdateTime = time + m_NextUpdateInterval;

      m_HasNetwork = PingRedServer(m_PlayerInfo.strApiURL);
      if (m_FullUpdate)
        SendNetworkInfo();

      GetPlayerInfo();
      GetActions();
      if (GetProgamInfo())
      {
        m_FullUpdate = false;
        m_PlaybackManager->Play();
      }

      if (m_PlayerCallBackFn)
        (*m_PlayerCallBackFn)(m_PlayerCallBackCtx, true);

      if (m_PlaybackManager->IsPlaying())
        SendPlayerStatus(kRedStatus_On);
      else
        SendPlayerStatus(kRedStatus_Off);
    }
  }

  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::Process Stopped");
}

void CPlayerManagerRed::GetPlayerInfo()
{
  std::string function = "function=playerInfo&id=" + m_PlayerInfo.strPlayerID;
  std::string url = FormatUrl(m_PlayerInfo, function);

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
    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetPlayerInfo() (off-line)" );
  }
  else
  {
    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetPlayerInfo() (on-line)" );

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
  CLog::Log(LOGINFO, "**RED** - CPlayerManagerRed::GetPlayerInfo() update   %d", update_interval);

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
    CLog::Log(LOGINFO, "**RED** - CPlayerManagerRed::GetPlayerInfo() download %s, %d", m_NextDownloadTime.GetAsDBDateTime().c_str(), download_duration);
  }

  m_PlaybackManager->OverridePlayBackWindow(true);
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
      m_PlaybackManager->OverridePlayBackWindow(false);
      m_PlaybackManager->SetPlayBackTime(m_PlaybackTime, m_PlaybackDuration);
    }
    CLog::Log(LOGINFO, "**RED** - CPlayerManagerRed::GetPlayerInfo() playback %s, %d", m_PlaybackTime.GetAsDBDateTime().c_str(), playback_duration);
  }
}

bool CPlayerManagerRed::GetProgamInfo()
{
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
      CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetProgamInfo() no program info (off-line)" );
      return false;
    }

    database.GetProgram(m_ProgramInfo);
    database.Close();

    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetProgamInfo() using old program info (off-line)" );
  }
  else
  {
    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetProgamInfo() fetching program info (on-line)" );

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
        CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetProgamInfo() using old program info (on-line)" );
        break;
      }
      CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::GetProgamInfo() using new program info (on-line)" );

      m_ProgramDateStamp = programDate;

      m_ProgramInfo.strDate = date;
      m_ProgramInfo.strProgramID = ((TiXmlElement*) programNode)->Attribute("id");

      // new program list, purge active program.
      m_PlaybackManager->Reset();
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
              /*
              CLog::Log(LOGDEBUG, "CPlayerManagerRed::GetProgamInfo[%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n,%s\n]",
                        asset.url.c_str(),asset.md5.c_str(),asset.name.c_str(),asset.basename.c_str(),
                        asset.artist.c_str(),asset.year.c_str(),asset.genre.c_str(),asset.composer.c_str(),
                        asset.album.c_str(),asset.tracknumber.c_str());
              */
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
        m_PlaybackManager->QueueMediaGroup(*mgit);
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

  return true;
}


void CPlayerManagerRed::NotifyAssetDownload(RedMediaAsset &asset)
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::NotifyAssetDownload");

  //function=player-NotifyAssetDownload&id=8&asset_id=1&format=xml&security=fb6120de31afc72bed400861be5bfbc8&apiKey=cFpN1RnsW9YulGb2Vhvy

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
      CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::NotifyAssetDownload - Response '%s'", result.c_str());
    }
  }
}

void CPlayerManagerRed::SendFilesDownloaded()
{
  if (m_HasNetwork)
  {
    // compare program list to files on disk, send what we have on disk

    CDBManagerRed database;
    database.Open();
    std::vector<RedMediaAsset> assets; // this holds all downloaded assest at the time of a query
    database.GetAllDownloadedAssets(assets);
    database.Close();

    // below if we need all files that are on local disc
    CFileItemList items;
    XFILE::CDirectory::GetDirectory(m_strHome + kRedDownloadMusicPath, items, "", XFILE::DIR_FLAG_NO_FILE_DIRS);

    for (int i=0; i < items.Size(); i++)
    {
      std::string itemName = URIUtils::GetFileName(items[i]->GetPath());
      CLog::Log(LOGDEBUG, "**RED** - SendFilesDownloaded() - %s", itemName.c_str());
    }
  }
}

void CPlayerManagerRed::SendPlayerHealth()
{
  if (m_HasNetwork)
  {
    CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendPlayerHealth");
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
        CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendPlayerHealth - Response '%s'", result.c_str());
      }
    }
  }
}

void CPlayerManagerRed::SendNetworkInfo()
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendNetworkInfo()");
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
          CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::SendNetworkInfo - Response '%s'", result.c_str());
        }
      }
    }
  }
}

void CPlayerManagerRed::SendPlayerLog()
{
  // fetch xbmc.log and push it up
}

void CPlayerManagerRed::GetActions()
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
                m_PlaybackManager->StopPlaying();
                ClearAction(command_str);
              }
              else if (command_str == "PlayerStart")
              {
                m_PlaybackManager->Play();
                ClearAction(command_str);
              }
            }
          }
        }
      }
    }
  }
}

void CPlayerManagerRed::ClearAction(std::string action)
{
  if (m_HasNetwork)
  {
    // clear a server side requested action
  }
}

void CPlayerManagerRed::AssetUpdateCallBack(const void *ctx, RedMediaAsset &asset, bool wasDownloaded)
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::AssetUpdateCallBack()" );
  CPlayerManagerRed *manager = (CPlayerManagerRed*)ctx;
  manager->m_PlaybackManager->ValidateAsset(asset, true);

  if (wasDownloaded)
  {
    // mark it downloaded in DB
    SetDownloadedAsset(asset.id);
    manager->NotifyAssetDownload(asset);
  }
}

void CPlayerManagerRed::ReportManagerCallBack(const void *ctx, bool status)
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::ReportManagerCallBack()" );
  CPlayerManagerRed *manager = (CPlayerManagerRed*)ctx;
  manager->SendPlayerHealth();
}

void CPlayerManagerRed::UpdatePlayerInfo(const std::string strPlayerID, const std::string strApiKey,const std::string strSecretKey, const std::string strApiURL )
{
  m_PlayerInfo.strPlayerID = strPlayerID;
  m_PlayerInfo.strApiKey = strApiKey;
  m_PlayerInfo.strSecretKey = strSecretKey;
  m_PlayerInfo.strApiURL = strApiURL;
}

void CPlayerManagerRed::ForceLocalPlayerUpdate()
{
  GetLocalPlayerInfo(m_PlayerInfo, m_strHome);
}

void CPlayerManagerRed::CheckForUpdate(PlayerInfo &player)
{
#if defined(TARGET_ANDROID)
  if (!player.strUpdateUrl.empty() && !player.strUpdateMD5.empty())
  {
    RedMediaUpdate update = {};
    sscanf(player.strUpdateVersion.c_str(), "%f", &update.version);

    if (update.version > kRedPlayerFloatVersion)
    {
      CLog::Log(LOGDEBUG, "**RED** - CPlayerManagerRed::CheckForUpdate(), version %f, version %f found.", kRedPlayerFloatVersion, update.version);

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
