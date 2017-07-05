/*
 *      Copyright (C) 2017 Team MrMC
 *      https://github.com/MrMC
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

#include "TraktServices.h"

#include "Application.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogProgress.h"
#include "filesystem/ZipFile.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "interfaces/json-rpc/JSONUtils.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/log.h"
#include "utils/StringHasher.h"
#include "utils/SystemInfo.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"

#include "video/VideoInfoTag.h"
#include "video/VideoDatabase.h"

#include "services/emby/EmbyClient.h"
#include "services/emby/EmbyServices.h"

static char chars[] = "'/!";
void removeCharsFromString( std::string &str)
{
  for ( unsigned int i = 0; i < strlen(chars); ++i ) {
    str.erase( remove(str.begin(), str.end(), chars[i]), str.end() );
  }
}

using namespace ANNOUNCEMENT;

static const std::string NS_TRAKT_CLIENTID("fdf32abb08db6b163f31cb6be8b06ede301b4d883b7c050f88efcd82ca9a2dbc");
static const std::string NS_TRAKT_CLIENTSECRET("0cb37612e9c2fcdcb22f1dc7504465ebc34c155256c52597c0cd524bc082c7c7");

class CTraktServiceJob: public CJob
{
public:
  CTraktServiceJob(CFileItem &item, double percentage, std::string strFunction)
  : m_item(item)
  , m_function(strFunction)
  , m_percentage(percentage)
  {
  }
  CTraktServiceJob(std::string strFunction)
  : m_item(*new CFileItem)
  , m_function(strFunction)
  , m_percentage(0)
  {
  }
  virtual ~CTraktServiceJob()
  {
  }
  virtual bool DoWork()
  {
    using namespace StringHasher;
    switch(mkhash(m_function.c_str()))
    {
      case "OnPlay"_mkhash:
        CLog::Log(LOGDEBUG, "CTraktServiceJob::OnPlay currentTime = %f", m_currentTime);
        CTraktServices::ReportProgress(m_item, "start", m_percentage);
        break;
      case "OnSeek"_mkhash:
        // Trakt API only as start/pause/stop. It is unclear what
        // to do about if you are seeking, others seem to just do
        // start again. We can too.
        CLog::Log(LOGDEBUG, "CTraktServiceJob::OnSeek currentTime = %f", m_currentTime);
        CTraktServices::ReportProgress(m_item, "start", m_percentage);
        break;
      case "OnPause"_mkhash:
        CLog::Log(LOGDEBUG, "CTraktServiceJob::OnPause currentTime = %f", m_currentTime);
        CTraktServices::ReportProgress(m_item, "pause", m_percentage);
        break;
      case "TraktSetStopped"_mkhash:
        CTraktServices::ReportProgress(m_item, "stop", m_percentage);
        break;
      case "TraktSetWatched"_mkhash:
        CTraktServices::SetItemWatchedJob(m_item, true);
        break;
      case "TraktSetUnWatched"_mkhash:
        CTraktServices::SetItemWatchedJob(m_item, false);
        break;
      case "TraktPushWatched"_mkhash:
        CTraktServices::PushWatchedStatus();
        break;
      case "TraktPullWatched"_mkhash:
        CTraktServices::PullWatchedStatus();
        break;
      default:
        return false;
    }
    return true;
  }
  virtual bool operator==(const CJob *job) const
  {
    if (strcmp(job->GetType(),GetType()) == 0)
    {
      const CTraktServiceJob* rjob = dynamic_cast<const CTraktServiceJob*>(job);
      if (rjob)
      {
        return m_function == rjob->m_function;
      }
    }
    return false;
  }
  
private:
  CFileItem   m_item;
  std::string m_function;
  double      m_percentage;
  double      m_currentTime;
};


CTraktServices::CTraktServices()
{
  CAnnouncementManager::GetInstance().AddAnnouncer(this);
  GetUserSettings();
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPULLWATCHED, "Idle");
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPUSHWATCHED, "Idle");
}

CTraktServices::~CTraktServices()
{
  CAnnouncementManager::GetInstance().RemoveAnnouncer(this);
}

CTraktServices& CTraktServices::GetInstance()
{
  static CTraktServices sTraktServices;
  return sTraktServices;
}

bool CTraktServices::IsEnabled()
{
  return !CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN).empty();
}

void CTraktServices::OnSettingAction(const CSetting *setting)
{
  
  /*
   static const std::string SETTING_SERVICES_TRAKTSIGNINPIN;
   static const std::string SETTING_SERVICES_TRAKTPULLWATCHED;
   static const std::string SETTING_SERVICES_TRAKTPUSHWATCHED;
   static const std::string SETTING_SERVICES_TRAKTACESSTOKEN;
   static const std::string SETTING_SERVICES_TRAKTACESSREFRESHTOKEN;
   static const std::string SETTING_SERVICES_TRAKTACESSTOKENVALIDITY;
   */
  
  if (setting == nullptr)
    return;

  bool startThread = false;
  std::string strMessage;
  std::string strSignIn = g_localizeStrings.Get(1240);
  std::string strSignOut = g_localizeStrings.Get(1241);
  const std::string& settingId = setting->GetId();

  if (settingId == CSettings::SETTING_SERVICES_TRAKTSIGNINPIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTSIGNINPIN) == strSignIn)
    {
      if (GetSignInPinCode())
      {
        // change prompt to 'sign-out'
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTSIGNINPIN, strSignOut);
        CLog::Log(LOGDEBUG, "CTraktServices:OnSettingAction pin sign-in ok");
        startThread = true;
      }
      else
      {
        std::string strMessage = "Could not get authToken via pin request sign-in";
        CLog::Log(LOGERROR, "CTraktServices: %s", strMessage.c_str());
      }
      SetButtonsEnabled(true);
    }
    else
    {
      // prompt is 'sign-out'
      // clear authToken and change prompt to 'sign-in'
      m_authToken.clear();
      m_authTokenValidity = 0;
      m_refreshAuthToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTSIGNINPIN, strSignIn);
      CLog::Log(LOGDEBUG, "CTraktServices:OnSettingAction sign-out ok");
      SetButtonsEnabled(false);
    }
    SetUserSettings();
  }
  else if (settingId == CSettings::SETTING_SERVICES_TRAKTPULLWATCHED)
  {
    SetButtonsEnabled(false);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPULLWATCHED, "Running");
    AddJob(new CTraktServiceJob("TraktPullWatched"));
  }
  else if (settingId == CSettings::SETTING_SERVICES_TRAKTPUSHWATCHED)
  {
    SetButtonsEnabled(false);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPUSHWATCHED, "Running");
    AddJob(new CTraktServiceJob("TraktPushWatched"));
  }
}

void CTraktServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (!IsEnabled())
    return;
  
  if ((flag & AnnouncementFlag::Player) && strcmp(sender, "xbmc") == 0)
  {
    double percentage;
    CFileItem &item = g_application.CurrentFileItem();
    using namespace StringHasher;
    switch(mkhash(message))
    {
      // Announce of "OnStop" has wrong timestamp, so we pick it up from
      // CApplication::SaveFileState which calls our SaveFileState
      case "OnPlay"_mkhash:
        // "OnPlay" could be playback startup, or after pause. We really
        // cannot tell from the Announce message or passed CVariant data.
        // So we track the state internally and fetch the correct play time.
        // Note: m_resumePoint is ONLY good for playback startup.
        if (GetPlayState(item) == MediaServicesPlayerState::paused)
          percentage = 100.0 * g_application.GetTime() / g_application.GetTotalTime();
        else
        {
          percentage = 0;
          if (item.GetVideoInfoTag()->m_resumePoint.totalTimeInSeconds > 0)
            percentage = 100.0 * item.GetVideoInfoTag()->m_resumePoint.timeInSeconds / item.GetVideoInfoTag()->m_resumePoint.totalTimeInSeconds;
        }
        if (percentage < 0.0)
          percentage = 0.0;
        SetPlayState(item, MediaServicesPlayerState::playing);
        CLog::Log(LOGDEBUG, "CTraktServiceJob::Announce OnPlay currentSeconds = %f", percentage);
        AddJob(new CTraktServiceJob(item, percentage, message));
        break;
      case "OnPause"_mkhash:
        SetPlayState(item, MediaServicesPlayerState::paused);
        percentage = 100.0 * g_application.GetTime() / g_application.GetTotalTime();
        CLog::Log(LOGDEBUG, "CTraktServiceJob::Announce OnPause currentSeconds = %f", percentage);
        AddJob(new CTraktServiceJob(item, percentage, message));
        break;
      case "OnSeek"_mkhash:
        // Ahh, finally someone actually gives us the right playtime.
        percentage = 100.0 * JSONRPC::CJSONUtils::TimeObjectToMilliseconds(data["player"]["time"]) / g_application.GetTotalTime();
        CLog::Log(LOGDEBUG, "CTraktServiceJob::Announce OnSeek currentSeconds = %f", percentage);
        AddJob(new CTraktServiceJob(item, percentage, message));
        break;
      case "OnStop"_mkhash:
        SetPlayState(item, MediaServicesPlayerState::stopped);
        break;
      default:
        break;
    }
  }
  else if ((flag & AnnouncementFlag::Other) && strcmp(sender, "trakt") == 0)
  {
    if (strcmp(message, "ReloadProfiles") == 0)
    {
      // restart if MrMC profiles has changed
      GetUserSettings();
    }
  }
}

void CTraktServices::OnSettingChanged(const CSetting *setting)
{
  // All Trakt settings so far
  /*
  static const std::string SETTING_SERVICES_TRAKTSIGNINPIN;
  static const std::string SETTING_SERVICES_TRAKTACESSTOKEN;
  static const std::string SETTING_SERVICES_TRAKTACESSTOKENVALIDITY;
  */

  if (setting == NULL)
    return;
}

void CTraktServices::SetButtonsEnabled(bool visible)
{
  auto settingPush = static_cast<CSettingString*>(CSettings::GetInstance().GetSetting(CSettings::SETTING_SERVICES_TRAKTPUSHWATCHED));
  settingPush->SetEnabled(visible);
  auto settingPull = static_cast<CSettingString*>(CSettings::GetInstance().GetSetting(CSettings::SETTING_SERVICES_TRAKTPULLWATCHED));
  settingPull->SetEnabled(visible);
}

void CTraktServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN, m_authToken);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTACESSREFRESHTOKEN, m_refreshAuthToken);
  CSettings::GetInstance().SetInt(CSettings::SETTING_SERVICES_TRAKTACESSTOKENVALIDITY, m_authTokenValidity);
  CSettings::GetInstance().Save();
}

void CTraktServices::GetUserSettings()
{
  m_authToken  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN);
  m_refreshAuthToken  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSREFRESHTOKEN);
  m_authTokenValidity  = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_TRAKTACESSTOKENVALIDITY);
}

bool CTraktServices::MyTraktSignedIn()
{
  return !m_authToken.empty();
}

bool CTraktServices::GetSignInPinCode()
{
  // on return, show user m_signInByPinCode so they can enter it at https://emby.media/pin
  bool rtn = false;
  std::string strMessage;
  
  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  
  CURL curl("https://trakt.tv");
  curl.SetFileName("oauth/device/code");
  curl.SetOption("format", "json");
  
  CVariant data;
  data["client_id"] = NS_TRAKT_CLIENTID;
  std::string jsonBody;
  if (!CJSONVariantWriter::Write(data, jsonBody, false))
    return rtn;
  std::string response;
  if (curlfile.Post(curl.Get(), jsonBody, response))
  {
#if defined(TRAKT_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CTraktServices:FetchSignInPin %s", response.c_str());
#endif
    CVariant reply;
    std::string verification_url;
    std::string user_code;
    int expires_in;
    int interval;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
    
    if (!reply.isObject() && !reply.isMember("user_code"))
      return rtn;
 
    m_deviceCode = reply["device_code"].asString();
    verification_url = reply["verification_url"].asString();
    expires_in = reply["expires_in"].asInteger();
    interval = reply["interval"].asInteger();
    user_code = reply["user_code"].asString();

    CGUIDialogProgress *waitPinReplyDialog;
    waitPinReplyDialog = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
    waitPinReplyDialog->SetHeading(g_localizeStrings.Get(2115));
    waitPinReplyDialog->SetLine(0, g_localizeStrings.Get(2117));
    std::string prompt = verification_url + g_localizeStrings.Get(2119) + user_code;
    waitPinReplyDialog->SetLine(1, prompt);
    
    waitPinReplyDialog->Open();
    waitPinReplyDialog->ShowProgressBar(true);
    
    CStopWatch dieTimer;
    dieTimer.StartZero();
    
    CStopWatch pingTimer;
    pingTimer.StartZero();
    
    m_authToken.clear();
    while (!waitPinReplyDialog->IsCanceled())
    {
      waitPinReplyDialog->SetPercentage(int(float(dieTimer.GetElapsedSeconds())/float(expires_in)*100));
      if (pingTimer.GetElapsedSeconds() > interval)
      {
        // wait for user to run and enter pin code
        rtn = GetSignInByPinReply();
        if (rtn)
          break;
        pingTimer.Reset();
        m_processSleep.WaitMSec(250);
        m_processSleep.Reset();
      }
      
      if (dieTimer.GetElapsedSeconds() > expires_in)
      {
        rtn = false;
        break;
      }
      waitPinReplyDialog->Progress();
    }
    waitPinReplyDialog->Close();
    
    if (m_authToken.empty())
    {
      strMessage = "Error extracting AcessToken";
      CLog::Log(LOGERROR, "CTraktServices::FetchSignInPin failed to get authToken");
      rtn = false;
    }
  }
  else
  {
    strMessage = "Could not connect to retreive AuthToken";
    CLog::Log(LOGERROR, "CTraktServices::FetchSignInPin failed %s", response.c_str());
  }
  if (!rtn)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Emby Services", strMessage, 3000, true);
  return rtn;
}

bool CTraktServices::GetSignInByPinReply()
{
  bool rtn = false;
  
  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  curlfile.SetSilent(true);
  
  CURL curl("https://trakt.tv");
  curl.SetFileName("oauth/device/token");
  curl.SetOption("format", "json");
  
  CVariant data;
  data["code"] = m_deviceCode;
  data["client_id"] = NS_TRAKT_CLIENTID;
  data["client_secret"] = NS_TRAKT_CLIENTSECRET;
  std::string jsondata;
  if (!CJSONVariantWriter::Write(data, jsondata, false))
    return rtn;
  std::string response;
  if (curlfile.Post(curl.Get(), jsondata, response))
  {
#if defined(TRAKT_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CTraktServices:AuthenticatePinReply %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
    if (reply.isObject() && reply.isMember("access_token"))
    {
      m_authToken = reply["access_token"].asString();
      m_refreshAuthToken = reply["refresh_token"].asString();
      m_authTokenValidity = reply["created_at"].asInteger() + reply["expires_in"].asInteger();
      SetUserSettings();
      rtn = true;
    }
  }
  return rtn;
}

const MediaServicesPlayerState CTraktServices::GetPlayState(CFileItem &item)
{
  // will create a new playstate if nothing matches
  CSingleLock lock(m_playStatesLock);
  std::string path = item.GetPath();
  for (const auto &playState : m_playStates)
  {
    if (path == playState.path)
    {
      return playState.state;
    }
  }
  TraktPlayState newPlayState;
  newPlayState.path = item.GetPath();
  m_playStates.push_back(newPlayState);
  return newPlayState.state;
}

void CTraktServices::SetPlayState(CFileItem &item, const MediaServicesPlayerState &state)
{
  // will erase existing playstate if matches and state is stopped
  CSingleLock lock(m_playStatesLock);
  std::string path = item.GetPath();
  for (auto playStateIt = m_playStates.begin(); playStateIt != m_playStates.end() ; ++playStateIt)
  {
    if (path == playStateIt->path)
    {
      if (state == MediaServicesPlayerState::stopped)
        m_playStates.erase(playStateIt);
      else
        playStateIt->state = state;
      break;
    }
  }
}

void CTraktServices::SetItemWatchedJob(CFileItem &item, bool watched, bool setLastWatched)
{
  CVariant data;
  CDateTime now = CDateTime::GetUTCDateTime();
  CDateTime lastPlayed = item.GetVideoInfoTag()->m_lastPlayed;
  
  if (setLastWatched && lastPlayed.IsValid())
    now = lastPlayed;

  if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->m_type == MediaTypeMovie)
  {
    std::string timenow = now.GetAsW3CDateTime(true);
    CVariant movie;
    
    movie["title"]      = item.GetVideoInfoTag()->m_strTitle;
    movie["year"]       = item.GetVideoInfoTag()->GetYear();
    movie["watched_at"] = now.GetAsW3CDateTime(true);
    movie["ids"]        = ParseIds(item.GetVideoInfoTag()->GetUniqueIDs(), item.GetVideoInfoTag()->m_type);
    data["movies"].push_back(movie);
  }
  else if (item.HasVideoInfoTag() &&
           (item.GetVideoInfoTag()->m_type == MediaTypeTvShow || item.GetVideoInfoTag()->m_type == MediaTypeSeason))
  {
    CFileItem showItem;
    if (!item.IsMediaServiceBased())
    {
      CVideoDatabase videodatabase;
      if (!videodatabase.Open())
        return;
      
      std::string basePath = StringUtils::Format("videodb://tvshows/titles/%i/%i/%i",item.GetVideoInfoTag()->m_iIdShow, item.GetVideoInfoTag()->m_iSeason, item.GetVideoInfoTag()->m_iDbId);
      videodatabase.GetTvShowInfo(basePath, *showItem.GetVideoInfoTag(), item.GetVideoInfoTag()->m_iIdShow);
      videodatabase.Close();
      
    }
    else
    {
      if(item.HasProperty("PlexItem"))
      {
           // plex dosnt have any tvdb or imdb IDs so we just send the show name and the season
          // less reliable but seems to work
      }
      else if (item.HasProperty("EmbyItem"))
      {
        CEmbyClientPtr client = CEmbyServices::GetInstance().FindClient(item.GetPath());
        if (client && client->GetPresence())
        {
          CVariant paramsseries;
          std::string seriesId = item.GetProperty("EmbySeriesID").asString();
          paramsseries = client->FetchItemById(seriesId);
          CVariant paramsProvID = paramsseries["Items"][0]["ProviderIds"];
          if (paramsProvID.isObject())
          {
            for (CVariant::iterator_map it = paramsProvID.begin_map(); it != paramsProvID.end_map(); it++)
            {
              std::string strFirst = it->first;
              StringUtils::ToLower(strFirst);
              showItem.GetVideoInfoTag()->SetUniqueID(it->second.asString(),strFirst);
            }
          }
        }
      }
    }
    CVariant show;
    show["title"]      = item.GetVideoInfoTag()->m_strShowTitle;
    show["year"]       = item.GetVideoInfoTag()->GetYear();
    show["watched_at"] = now.GetAsW3CDateTime(true);
    show["ids"]        = ParseIds(showItem.GetVideoInfoTag()->GetUniqueIDs(), item.GetVideoInfoTag()->m_type);
    
    if(item.GetVideoInfoTag()->m_type == MediaTypeSeason)
    {
      CVariant season;
      season["number"] = item.GetVideoInfoTag()->m_iSeason;
      show["seasons"].push_back(season);
    }
    data["shows"].push_back(show);
  }
  else if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->m_type == MediaTypeEpisode)
  {
    /// https://api.trakt.tv/shows/top-gear/seasons/24
    // to get a list of episodes, there we will find what we need
    
    std::string showName = item.GetVideoInfoTag()->m_strShowTitle;
    removeCharsFromString(showName);
    StringUtils::Replace(showName, " ", "-");
    std::string showNameT;
    std::string episodesUrl = StringUtils::Format("https://api.trakt.tv/shows/%s/seasons/%i",showName.c_str(),item.GetVideoInfoTag()->m_iSeason);
    CVariant episodes = GetTraktCVariant(episodesUrl);
    CVariant episodeIds;
    CVariant episode;
    if (episodes.isArray())
    {
      for (CVariant::iterator_array it = episodes.begin_array(); it != episodes.end_array(); it++)
      {
        CVariant &episodeItem = *it;
        if (episodeItem["number"].asInteger() == item.GetVideoInfoTag()->m_iEpisode)
        {
          episodeIds = episodeItem["ids"];
          break;
        }
      }
    }
    episode["watched_at"] = now.GetAsW3CDateTime(true);
    episode["ids"]        = episodeIds;
    data["episodes"].push_back(episode);
    
  }

  std::string unwatched = watched ? "":"/remove";
  // send it to server
  ServerChat("https://api.trakt.tv/sync/history" + unwatched,data);
}

void CTraktServices::SetItemWatched(CFileItem &item)
{
  AddJob(new CTraktServiceJob(item, 100, "TraktSetWatched"));
}

void CTraktServices::SetItemUnWatched(CFileItem &item)
{
  AddJob(new CTraktServiceJob(item, 0, "TraktSetUnWatched"));
}

void CTraktServices::ReportProgress(CFileItem &item, const std::string &status, double percentage)
{
  // if we are music, do not report
  if (item.IsAudio() || item.IsPVR())
    return;

  if (!status.empty())
  {
    CLog::Log(LOGDEBUG, "CTraktServices::ReportProgress status = %s, percentage = %f", status.c_str(), percentage);

    CVariant data;
    if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->m_type == MediaTypeEpisode)
    {
      /// https://api.trakt.tv/shows/top-gear/seasons/24
      // to get a list of episodes, there we will find what we need

      std::string showName = item.GetVideoInfoTag()->m_strShowTitle;
      removeCharsFromString(showName);
      StringUtils::Replace(showName, " ", "-");
      std::string episodesUrl = StringUtils::Format("https://api.trakt.tv/shows/%s/seasons/%i",
        showName.c_str(), item.GetVideoInfoTag()->m_iSeason);
      CVariant episodes = GetTraktCVariant(episodesUrl);

      if (episodes.isArray())
      {
        for (CVariant::iterator_array it = episodes.begin_array(); it != episodes.end_array(); it++)
        {
          CVariant &episodeItem = *it;
          if (episodeItem["number"].asInteger() == item.GetVideoInfoTag()->m_iEpisode)
          {
            data["episode"] = episodeItem;
            break;
          }
        }
      }
      data["progress"] = percentage;
      data["app_version"] = CSysInfo::GetVersion();
      data["app_date"] = CSysInfo::GetBuildDate();
    }
    else if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->m_type == MediaTypeMovie)
    {
      data["movie"]["title"] = item.GetVideoInfoTag()->m_strTitle;
      data["movie"]["year"] = item.GetVideoInfoTag()->GetYear();
      data["movie"]["ids"] = ParseIds(item.GetVideoInfoTag()->GetUniqueIDs(), item.GetVideoInfoTag()->m_type);
      data["progress"] = percentage;
      data["app_version"] = CSysInfo::GetVersion();
      data["app_date"] = CSysInfo::GetBuildDate();
    }

    if (!data.isNull())
    {
      // now that we have "data" talk to trakt server
      // If the progress is above 80%, the video will be scrobbled
      // and we will get a 409 on stop. Ignore it :)
      ServerChat("https://api.trakt.tv/scrobble/" + status, data);
    }
  }
}

void CTraktServices::SaveFileState(CFileItem &item, double currentTime, double totalTime)
{
  double percentage = 100.0 * currentTime / totalTime;
  AddJob(new CTraktServiceJob(item, percentage, "TraktSetStopped"));
}

CVariant CTraktServices::ParseIds(const std::map<std::string, std::string> &Ids, const std::string &type)
{
  CVariant variantIDs;
  for (std::map<std::string, std::string>::const_iterator i = Ids.begin(); i != Ids.end(); ++i)
  {
    if (i->first == "unknown")
    {
      std::string id = i->second;
      if (StringUtils::StartsWithNoCase(id, "tt"))
        variantIDs["imdb"] = id;
      else if (isdigit(*id.c_str()) && type == MediaTypeMovie)
        variantIDs["tmdb"] = atoi(id.c_str());
      else if (isdigit(*id.c_str()) && (type == MediaTypeEpisode || type == MediaTypeSeason || type == MediaTypeTvShow))
        variantIDs["tvdb"] = atoi(id.c_str());
      else
        variantIDs["slug"] = id;
    }
    else
      variantIDs[i->first] = i->second;
  }
  return variantIDs;
}

void CTraktServices::PullWatchedStatus()
{
  CLog::Log(LOGDEBUG, "CTraktServices::PullWatchedStatus() - Started");
  CVideoDatabase videodb;
  
  if (!videodb.Open())
    return;
  
  if (!videodb.HasContent())
  {
    videodb.Close();
    return;
  }
  
  CFileItemList items;
  videodb.GetMoviesNav("videodb://movies/titles/", items);

  CVariant data = GetTraktCVariant("https://api.trakt.tv/sync/watched/movies");
  if (data.isArray())
  {
    for (CVariant::iterator_array it = data.begin_array(); it != data.end_array(); it++)
    {
      CVariant &movieItem = *it;
      if (items.Size() > 0)
      {
        for (int i=0; i < items.Size(); i++)
        {
          CFileItem pItem = *new CFileItem(*items[i]);
          std::string itemIMDB = pItem.GetVideoInfoTag()->GetUniqueID("imdb");
          std::string itemTMDB = pItem.GetVideoInfoTag()->GetUniqueID("tmdb");
          if (itemIMDB == movieItem["movie"]["ids"]["imdb"].asString() ||
              itemTMDB == movieItem["movie"]["ids"]["tmdb"].asString())
          {
            CDateTime date;
            date.SetFromW3CDate(movieItem["last_watched_at"].asString());
            videodb.ClearBookMarksOfFile(pItem.GetVideoInfoTag()->GetPath(), CBookmark::RESUME);
            videodb.SetPlayCount(pItem, pItem.GetVideoInfoTag()->m_playCount + 1 , date);
          }
        }
      }
    }
  }
  // CommitTransaction in case GetTvShowsByWhere below fails
  videodb.CommitTransaction();
  
  // get tvshows
  CVideoDatabase::Filter filter;
  items.Clear();
  videodb.GetTvShowsByWhere("videodb://tvshows/titles/", filter, items);

  CVariant dataShows = GetTraktCVariant("https://api.trakt.tv/sync/watched/shows");
  if (dataShows.isArray())
  {
    for (CVariant::iterator_array it = dataShows.begin_array(); it != dataShows.end_array(); it++)
    {
      CVariant &movieItem = *it;
      if (items.Size() > 0)
      {
        for (int i=0; i < items.Size(); i++)
        {
          CFileItem pItem = *new CFileItem(*items[i]);
          std::string itemDefault = pItem.GetVideoInfoTag()->GetUniqueID();
          
          if (!itemDefault.empty() &&
              ((itemDefault == movieItem["show"]["ids"]["tvdb"].asString() ||
               itemDefault == movieItem["show"]["ids"]["tmdb"].asString() ||
               itemDefault == movieItem["show"]["ids"]["imdb"].asString()) &&
               pItem.GetVideoInfoTag()->GetYear() == movieItem["show"]["year"].asInteger()))
          {
            CFileItemList episodes;
            CVideoDatabase::Filter filter1(videodb.PrepareSQL("idShow=%i", pItem.GetVideoInfoTag()->m_iDbId));
            videodb.GetEpisodesByWhere("videodb://tvshows/titles/", filter1, episodes);
            for (int i = 0; i < episodes.Size(); i++)
            {
              CFileItem pItemEpisode = *new CFileItem(*episodes[i]);
              CVariant dataSeasons = movieItem["seasons"];
              for (CVariant::iterator_array is = dataSeasons.begin_array(); is != dataSeasons.end_array(); is++)
              {
                CVariant &seasonItem = *is;
                if(seasonItem["number"].asInteger() == pItemEpisode.GetVideoInfoTag()->m_iSeason)
                {
                  CVariant dataEpisodes = seasonItem["episodes"];
                  for (CVariant::iterator_array ie = dataEpisodes.begin_array(); ie != dataEpisodes.end_array(); ie++)
                  {
                    CVariant &episodeItem = *ie;
                    if(episodeItem["number"].asInteger() == pItemEpisode.GetVideoInfoTag()->m_iEpisode)
                    {
                      CLog::Log(LOGDEBUG, "CTraktServices::PullWatchedStatus() - Matched [%s] season %i episode %i",
                                movieItem["show"]["title"].asString().c_str(),
                                pItemEpisode.GetVideoInfoTag()->m_iSeason,
                                pItemEpisode.GetVideoInfoTag()->m_iEpisode);
                      CDateTime date;
                      date.SetFromW3CDate(episodeItem["last_watched_at"].asString());
                      videodb.ClearBookMarksOfFile(pItem.GetVideoInfoTag()->GetPath(), CBookmark::RESUME);
                      videodb.SetPlayCount(pItemEpisode, pItem.GetVideoInfoTag()->m_playCount + 1 , date);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  // close database
  videodb.CommitTransaction();
  videodb.Close();
  CLog::Log(LOGDEBUG, "CTraktServices::PullWatchedStatus() - Done");
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPULLWATCHED, "Idle");
  GetInstance().SetButtonsEnabled(true);
}

void CTraktServices::PushWatchedStatus()
{
  CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Started");
  
  CVariant data;
  CVideoDatabase videodb;
  
  if (!videodb.Open())
    return;
  
  if (!videodb.HasContent())
    return;
  
  CFileItemList movieItems;
  videodb.GetMoviesNav("videodb://movies/titles/", movieItems);

  if (movieItems.Size() > 0)
  {
    for (int i=0; i < movieItems.Size(); i++)
    {
      CFileItem pItem = *new CFileItem(*movieItems[i]);
      if (pItem.HasVideoInfoTag() &&
          pItem.GetVideoInfoTag()->m_type == MediaTypeMovie &&
          pItem.GetVideoInfoTag()->m_playCount > 0)
      {
        CDateTime date = pItem.GetVideoInfoTag()->m_lastPlayed;
        std::string timenow = date.GetAsW3CDateTime(true);
        CVariant movie;
        movie["title"]      = pItem.GetVideoInfoTag()->m_strTitle;
        movie["year"]       = pItem.GetVideoInfoTag()->GetYear();
        movie["watched_at"] = timenow;
        movie["ids"]        = ParseIds(pItem.GetVideoInfoTag()->GetUniqueIDs(), pItem.GetVideoInfoTag()->m_type);
        data["movies"].push_back(movie);
        CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Movie [%s]", pItem.GetVideoInfoTag()->m_strTitle.c_str());
      }
    }
  }
  
  CFileItemList showItems;
  CVideoDatabase::Filter filter;
  videodb.GetTvShowsByWhere("videodb://tvshows/titles/", filter, showItems);

  if (showItems.Size() > 0)
  {
    for (int i=0; i < showItems.Size(); i++)
    {
      CFileItem sItem = *new CFileItem(*showItems[i]);
      
      CFileItem showItem;
      std::string basePath = StringUtils::Format("videodb://tvshows/titles/%i/%i",sItem.GetVideoInfoTag()->m_iDbId, sItem.GetVideoInfoTag()->m_iSeason);
      videodb.GetTvShowInfo(basePath, *showItem.GetVideoInfoTag(), sItem.GetVideoInfoTag()->m_iIdShow);
      
      if (sItem.HasVideoInfoTag() &&
          sItem.GetVideoInfoTag()->m_type == MediaTypeTvShow &&
          sItem.GetVideoInfoTag()->m_playCount > 0)
      {
        CVariant show;
        CDateTime date = sItem.GetVideoInfoTag()->m_lastPlayed;
        std::string timenow = date.GetAsW3CDateTime(true);
        show["title"]      = sItem.GetVideoInfoTag()->m_strShowTitle;
        show["year"]       = sItem.GetVideoInfoTag()->GetYear();
        show["watched_at"] = timenow;
        show["ids"]        = ParseIds(showItem.GetVideoInfoTag()->GetUniqueIDs(), sItem.GetVideoInfoTag()->m_type);
        data["shows"].push_back(show);
        CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Show [%s]", sItem.GetVideoInfoTag()->m_strShowTitle.c_str());
      }
      else
      {
        std::string strPath = StringUtils::Format("videodb://tvshows/titles/%i/", sItem.GetVideoInfoTag()->m_iDbId);
        CFileItemList seasonItems;
        videodb.GetSeasonsNav(strPath, seasonItems, -1, -1, -1, -1, sItem.GetVideoInfoTag()->m_iIdShow, false);
        if (seasonItems.Size() > 0)
        {
          for (int i=0; i < seasonItems.Size(); i++)
          {
            CFileItem seItem = *new CFileItem(*seasonItems[i]);
            if (seItem.HasVideoInfoTag() &&
                seItem.GetVideoInfoTag()->m_type == MediaTypeSeason &&
                seItem.GetVideoInfoTag()->m_playCount > 0)
            {
              CVariant show;
              CDateTime date = seItem.GetVideoInfoTag()->m_lastPlayed;
              std::string timenow = date.GetAsW3CDateTime(true);
              show["title"]      = sItem.GetVideoInfoTag()->m_strShowTitle;
              show["year"]       = sItem.GetVideoInfoTag()->GetYear();
              show["watched_at"] = timenow;
              show["ids"]        = ParseIds(showItem.GetVideoInfoTag()->GetUniqueIDs(), sItem.GetVideoInfoTag()->m_type);
              CVariant season;
              season["number"] = seItem.GetVideoInfoTag()->m_iSeason;
              show["seasons"].push_back(season);
              data["shows"].push_back(show);
              CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Show [%s] Season [%i]", sItem.GetVideoInfoTag()->m_strShowTitle.c_str(), seItem.GetVideoInfoTag()->m_iSeason);
            }
            else
            {
              CFileItemList episodes;
              CVideoDatabase::Filter filter1(videodb.PrepareSQL("idShow=%i", sItem.GetVideoInfoTag()->m_iDbId));
              CVideoDatabase::Filter filter2;
//              CVideoDbUrl videoUrl;
//              videoUrl.FromString("videodb://tvshows/titles/");
//              videoUrl.AddOption("season", seItem.GetVideoInfoTag()->m_iSeason);
              videodb.GetEpisodesByWhere(StringUtils::Format("videodb://tvshows/titles/%i/%i",sItem.GetVideoInfoTag()->m_iDbId,seItem.GetVideoInfoTag()->m_iSeason), filter2, episodes);
//              videodb.GetEpisodesNav("videodb://tvshows/titles/", episodes, -1, -1, -1, -1, sItem.GetVideoInfoTag()->m_iDbId, seItem.GetVideoInfoTag()->m_iSeason);
              if (episodes.Size() > 0)
              {
                int watched = 0;
                for (int i=0; i < episodes.Size(); i++)
                {
                  CFileItem eItem = *new CFileItem(*episodes[i]);
                  if(eItem.GetVideoInfoTag()->m_playCount > 0)
                    watched++;
                }
                if (watched > 0)
                {
                  std::string showName = seItem.GetVideoInfoTag()->m_strShowTitle;
                  removeCharsFromString(showName);
                  StringUtils::Replace(showName, " ", "-");
                  StringUtils::Replace(showName, ":", "");
                  StringUtils::Replace(showName, "(", "");
                  StringUtils::Replace(showName, ")", "");
                  std::string showNameT;
                  std::string episodesUrl = StringUtils::Format("https://api.trakt.tv/shows/%s/seasons/%i",showName.c_str(),seItem.GetVideoInfoTag()->m_iSeason);
                  CVariant ep = GetTraktCVariant(episodesUrl);
                  CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() get trakt episodes [%s]",episodesUrl.c_str());
                  if (ep.isArray())
                  {
                    for (int i=0; i < episodes.Size(); i++)
                    {
                      CFileItem eItem = *new CFileItem(*episodes[i]);
                      if (eItem.HasVideoInfoTag() &&
                          eItem.GetVideoInfoTag()->m_type == MediaTypeEpisode &&
                          eItem.GetVideoInfoTag()->m_playCount > 0)
                      {
                        for (CVariant::iterator_array it = ep.begin_array(); it != ep.end_array(); it++)
                        {
                          CVariant &episodeItem = *it;
                          if (episodeItem["number"].asInteger() == eItem.GetVideoInfoTag()->m_iEpisode)
                          {
                            CDateTime date = eItem.GetVideoInfoTag()->m_lastPlayed;
                            std::string timenow = date.GetAsW3CDateTime(true);
                            CVariant episode;
                            episode["watched_at"] = timenow;
                            episode["ids"]        = episodeItem["ids"];
                            data["episodes"].push_back(episode);
                            CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Show [%s] Season [%i] episode[%i]", sItem.GetVideoInfoTag()->m_strShowTitle.c_str(), seItem.GetVideoInfoTag()->m_iSeason,eItem.GetVideoInfoTag()->m_iEpisode);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  ServerChat("https://api.trakt.tv/sync/history", data);
  CLog::Log(LOGDEBUG, "CTraktServices::PushWatchedStatus() - Done");
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_TRAKTPUSHWATCHED, "Idle");
  GetInstance().SetButtonsEnabled(true);
}

CVariant CTraktServices::GetTraktCVariant(const std::string &url)
{
  // check if token expired
  CTraktServices::GetInstance().CheckAccessToken();
  
  XFILE::CCurlFile trakt;
  trakt.SetRequestHeader("Cache-Control", "no-cache");
  trakt.SetRequestHeader("Content-Type", "application/json");
  trakt.SetRequestHeader("Accept-Encoding", "gzip");
  trakt.SetRequestHeader("trakt-api-version", "2");
  trakt.SetRequestHeader("trakt-api-key", NS_TRAKT_CLIENTID);
  trakt.SetRequestHeader("Authorization", "Bearer " + CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN));
  
  CURL curl(url);
  // this is key to get back gzip encoded content
  curl.SetProtocolOption("seekable", "0");
  // we always want json back
  curl.SetProtocolOptions(curl.GetProtocolOptions() + "&format=json");
  std::string response;
  if (trakt.Get(curl.Get(), response))
  {
    if (trakt.GetContentEncoding() == "gzip")
    {
      std::string buffer;
      if (XFILE::CZipFile::DecompressGzip(response, buffer))
        response = std::move(buffer);
      else
        return CVariant(CVariant::VariantTypeNull);
    }
    CVariant resultObject;
    if (CJSONVariantParser::Parse(response, resultObject))
    {
      if (resultObject.isObject() || resultObject.isArray())
        return resultObject;
    }
  }
  return CVariant(CVariant::VariantTypeNull);
}

void CTraktServices::ServerChat(const std::string &url, const CVariant &data)
{
  // check if token expired
  CTraktServices::GetInstance().CheckAccessToken();
  
  XFILE::CCurlFile curlfile;
  curlfile.SetSilent(true);
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  curlfile.SetRequestHeader("trakt-api-version", "2");
  curlfile.SetRequestHeader("trakt-api-key", NS_TRAKT_CLIENTID);
  curlfile.SetRequestHeader("Authorization", "Bearer " + CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN));
  
  CURL curl(url);
  
  std::string jsondata;
  if (!CJSONVariantWriter::Write(data, jsondata, false))
    return;
  std::string response;
  if (curlfile.Post(curl.Get(), jsondata, response))
  {
#if defined(TRAKT_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CTraktServices::ServerChat %s", curl.Get().c_str());
    CLog::Log(LOGDEBUG, "CTraktServices::ServerChat - response %s", response.c_str());
#endif
  }
  else
  {
    // If the same item was just scrobbled, a 409 HTTP status code
    // will returned to avoid scrobbling a duplicate
    if (curlfile.GetResponseCode() != 409)
      CLog::Log(LOGDEBUG, "CTraktServices::ServerChat - failed, response %s", response.c_str());
  }

}

void CTraktServices::RefreshAccessToken()
{
  https://api.trakt.tv/oauth/token
  
  XFILE::CCurlFile curlfile;
  curlfile.SetSilent(true);
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  curlfile.SetRequestHeader("trakt-api-version", "2");
  curlfile.SetRequestHeader("trakt-api-key", NS_TRAKT_CLIENTID);
  curlfile.SetRequestHeader("Authorization", "Bearer " + CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSTOKEN));
  
  CURL curl("https://api.trakt.tv/oauth/token");
  
  CVariant data;
  data["refresh_token"] = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_TRAKTACESSREFRESHTOKEN);
  data["client_id"] = NS_TRAKT_CLIENTID;
  data["client_secret"] = NS_TRAKT_CLIENTSECRET;
  data["redirect_uri"] = "urn:ietf:wg:oauth:2.0:oob";
  data["grant_type"] = "refresh_token";
  std::string jsondata;
  if (!CJSONVariantWriter::Write(data, jsondata, false))
    return;
  std::string response;
  if (curlfile.Post(curl.Get(), jsondata, response))
  {
#if defined(TRAKT_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CTraktServices::ServerChat %s", curl.Get().c_str());
    CLog::Log(LOGDEBUG, "CTraktServices::ServerChat - response %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return;
    if (reply.isObject() && reply.isMember("access_token"))
    {
      m_authToken = reply["access_token"].asString();
      m_refreshAuthToken = reply["refresh_token"].asString();
      m_authTokenValidity = reply["created_at"].asInteger() + reply["expires_in"].asInteger();
      SetUserSettings();
    }
  }
}

void CTraktServices::CheckAccessToken()
{
  CDateTime now = CDateTime::GetUTCDateTime();
  time_t tNow = 0;
  time_t tExpiry = (time_t)CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_TRAKTACESSTOKENVALIDITY);
  now.GetAsTime(tNow);
  if (tNow > tExpiry)
  {
    RefreshAccessToken();
    CLog::Log(LOGDEBUG, "CTraktServices::CheckAccessToken() refreshed");
  }
  
}
