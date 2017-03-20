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

#include "EmbyServices.h"

#include "EmbyUtils.h"
#include "EmbyClient.h"
#include "Application.h"
#include "URL.h"
#include "Util.h"
#include "GUIUserMessages.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogProgress.h"
#include "filesystem/DirectoryCache.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/DNSNameCache.h"
#include "network/GUIDialogNetworkSetup.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/md5.h"
#include "utils/sha1.hpp"
#include "utils/StringUtils.h"
#include "utils/StringHasher.h"
#include "utils/JobManager.h"

#include "utils/SystemInfo.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/Variant.h"

using namespace ANNOUNCEMENT;

//static const int NS_EMBY_SERVER_HTTP_PORT(8096);
//static const int NS_EMBY_SERVER_HTTPS_PORT(8920);
static const std::string NS_EMBY_URL("https://plex.tv");

class CEmbyServiceJob: public CJob
{
public:
  CEmbyServiceJob(double currentTime, std::string strFunction)
  : m_function(strFunction)
  , m_currentTime(currentTime)
  {
  }
  virtual ~CEmbyServiceJob()
  {
  }
  virtual bool DoWork()
  {
    if (m_function == "FoundNewClient")
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);

      // announce that we have a emby client and that recently added should be updated
      ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "UpdateRecentlyAdded");
      ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "UpdateRecentlyAdded");
    }
    return true;
  }
  virtual bool operator==(const CJob *job) const
  {
    return true;
  }
private:
  std::string    m_function;
  double         m_currentTime;
};


CEmbyServices::CEmbyServices()
: CThread("EmbyServices")
, m_playState(MediaServicesPlayerState::stopped)
, m_hasClients(false)
{
  // register our redacted protocol options with CURL
  // we do not want these exposed in mrmc.log.
  if (!CURL::HasProtocolOptionsRedacted(EmbyApiKeyHeader))
    CURL::SetProtocolOptionsRedacted(EmbyApiKeyHeader, "EMBYTOKEN");

  CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CEmbyServices::~CEmbyServices()
{
  CAnnouncementManager::GetInstance().RemoveAnnouncer(this);

  if (IsRunning())
    Stop();

  CancelJobs();
}

CEmbyServices& CEmbyServices::GetInstance()
{
  static CEmbyServices sEmbyServices;
  return sEmbyServices;
}

void CEmbyServices::Start()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    Stop();
  CThread::Create();
}

void CEmbyServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
  {
    m_bStop = true;
    m_processSleep.Set();
    StopThread();
  }

  g_directoryCache.Clear();
  CSingleLock lock2(m_clients_lock);
  m_clients.clear();
  m_playState = MediaServicesPlayerState::stopped;
  m_hasClients = false;
}

bool CEmbyServices::IsActive()
{
  return IsRunning();
}

bool CEmbyServices::IsEnabled()
{
  return (!CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYACESSTOKEN).empty() ||
           CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYBROADCAST));
}

bool CEmbyServices::HasClients() const
{
  return m_hasClients;
}

void CEmbyServices::GetClients(std::vector<CEmbyClientPtr> &clients) const
{
  CSingleLock lock(m_clients_lock);
  clients = m_clients;
}

CEmbyClientPtr CEmbyServices::FindClient(const std::string &path)
{
  CURL url(path);
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (client->IsSameClientHostName(url))
      return client;
  }

  return nullptr;
}

void CEmbyServices::OnSettingAction(const CSetting *setting)
{
  if (setting == nullptr)
    return;

  bool startThread = false;
  std::string strMessage;
  std::string strSignIn = g_localizeStrings.Get(2109);
  std::string strSignOut = g_localizeStrings.Get(2110);
  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_EMBYSIGNIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSIGNIN) == strSignIn)
    {
      CURL curl(m_serverURL);
      curl.SetProtocol("emby");
      std::string path = curl.Get();
      if (CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path))
      {
        CURL curl2(path);
        if (!curl2.GetHostName().empty() && !curl2.GetUserName().empty() && !curl2.GetPassWord().empty())
        {
          if (AuthenticateByName(curl2))
          {
            // never save the password
            curl2.SetPassword("");
            m_serverURL = curl2.Get();
            // change prompt to 'sign-out'
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSIGNIN, strSignOut);
            CLog::Log(LOGDEBUG, "CEmbyServices:OnSettingAction manual sign-in ok");
            startThread = true;
          }
          else
          {
            strMessage = "Could not get authToken via manual sign-in";
            CLog::Log(LOGERROR, "CEmbyServices: %s", strMessage.c_str());
          }
        }
        else
        {
          // opps, nuke'em all
          CLog::Log(LOGDEBUG, "CEmbyServices:OnSettingAction host/user/pass are empty");
          m_userId.clear();
          m_accessToken.clear();
        }
      }
    }
    else
    {
      // prompt is 'sign-out'
      // clear authToken and change prompt to 'sign-in'
      m_userId.clear();
      m_accessToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSIGNIN, strSignIn);
      CLog::Log(LOGDEBUG, "CEmbyServices:OnSettingAction sign-out ok");
    }
    SetUserSettings();

    if (startThread)
      Start();
    else
      Stop();
  }
  else if (settingId == CSettings::SETTING_SERVICES_EMBYSIGNINPIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN) == strSignIn)
    {
      if (PostSignInPinCode())
      {
        // change prompt to 'sign-out'
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN, strSignOut);
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYHOMEUSER, m_myHomeUser);
        CLog::Log(LOGDEBUG, "CEmbyServices:OnSettingAction pin sign-in ok");
        startThread = true;
      }
      else
      {
        std::string strMessage = "Could not get authToken via pin request sign-in";
        CLog::Log(LOGERROR, "CEmbyServices: %s", strMessage.c_str());
      }
    }
    else
    {
      // prompt is 'sign-out'
      // clear authToken and change prompt to 'sign-in'
      m_userId.clear();
      m_accessToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN, strSignIn);
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYHOMEUSER, "");
      CLog::Log(LOGDEBUG, "CEmbyServices:OnSettingAction sign-out ok");
    }
    SetUserSettings();


    if (startThread)
      Start();
    else
    {
      if (!strMessage.empty())
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Emby Services", strMessage, 3000, true);
      Stop();
    }
  }
  else if (settingId == CSettings::SETTING_SERVICES_EMBYHOMEUSER)
  {
/*
    // user must be in 'sign-in' state so check for 'sign-out' label
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSIGNIN) == strSignOut ||
        CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN) == strSignOut)
    {
      std::string homeUserName;
      if (GetMyHomeUsers(homeUserName))
      {
        m_myHomeUser = homeUserName;
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYHOMEUSER, m_myHomeUser);
        SetUserSettings();
        CSingleLock lock(m_clients_lock);
        m_clients.clear();
        Start();
      }
    }
*/
  }
}

void CEmbyServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if ((flag & AnnouncementFlag::Player) && strcmp(sender, "xbmc") == 0)
  {
    using namespace StringHasher;
    switch(mkhash(message))
    {
      case "OnPlay"_mkhash:
        m_playState = MediaServicesPlayerState::playing;
        break;
      case "OnPause"_mkhash:
        m_playState = MediaServicesPlayerState::paused;
        break;
      case "OnStop"_mkhash:
        m_playState = MediaServicesPlayerState::stopped;
        break;
      default:
        break;
    }
  }
  else if ((flag & AnnouncementFlag::Other) && strcmp(sender, "emby") == 0)
  {
    if (strcmp(message, "ReloadProfiles") == 0)
    {
      // restart if we MrMC profiles has changed
      Stop();
      Start();
    }
  }
}

void CEmbyServices::OnSettingChanged(const CSetting *setting)
{
  // All Emby settings so far
  /*
  static const std::string SETTING_SERVICES_EMBYSIGNIN;
  static const std::string SETTING_SERVICES_EMBYUSERID;
  static const std::string SETTING_SERVICES_EMBYSERVERURL;
  static const std::string SETTING_SERVICES_EMBYACESSTOKEN;

  static const std::string SETTING_SERVICES_EMBYSIGNINPIN;
  static const std::string SETTING_SERVICES_EMBYHOMEUSER;
  static const std::string SETTING_SERVICES_EMBYUPDATEMINS;
  */

  if (setting == NULL)
    return;
}

void CEmbyServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYUSERID, m_userId);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSERVERURL, m_serverURL);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYACESSTOKEN, m_accessToken);
  CSettings::GetInstance().Save();
}

void CEmbyServices::GetUserSettings()
{
  m_userId = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYUSERID);
  m_serverURL  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSERVERURL);
  m_accessToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYACESSTOKEN);
}

void CEmbyServices::Process()
{
  CLog::Log(LOGDEBUG, "CEmbyServices::Process bgn");
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);

  GetUserSettings();

  while (!m_bStop)
  {
    if (g_sysinfo.HasInternet())
    {
      CLog::Log(LOGDEBUG, "CEmbyServices::Process has gateway1");
      break;
    }

    std::string ip;
    if (CDNSNameCache::Lookup("connect.mediabrowser.tv", ip))
    {
      in_addr_t embydotcom = inet_addr(ip.c_str());
      if (g_application.getNetwork().PingHost(embydotcom, 0, 1000))
      {
        CLog::Log(LOGDEBUG, "CEmbyServices::Process has gateway2");
        break;
      }
    }
    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  int serviceTimeoutSeconds = 5;
  if (!m_accessToken.empty() && !m_userId.empty())
  {
    GetEmbyServers();
    serviceTimeoutSeconds = 60 * 15;
  }
   while (!m_bStop)
   {
    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  CLog::Log(LOGDEBUG, "CEmbyServices::Process end");
}

bool CEmbyServices::AuthenticateByName(const CURL& url)
{
  XFILE::CCurlFile emby;
  emby.SetTimeout(10);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");
  CEmbyUtils::PrepareApiCall("", "", emby);

  std::string password = url.GetPassWord();
  uuids::sha1 sha1;
  sha1.process_bytes(password.c_str(), password.size());

  unsigned int hash[5];
  sha1.get_digest(hash);

  std::string passwordSha1;
  for (const auto hashPart : hash)
    passwordSha1 += StringUtils::Format("%08x", hashPart);

  std::string passwordMd5 = XBMC::XBMC_MD5::GetMD5(password);

  CVariant body;
  body["Username"] = url.GetUserName();
  body["password"] = passwordSha1;
  body["passwordMd5"] = passwordMd5;
  const std::string requestBody = CJSONVariantWriter::Write(body, true);

  CURL curl("emby/Users/AuthenticateByName");
  curl.SetPort(url.GetPort());
  if (url.GetProtocol() == "embys")
    curl.SetProtocol("https");
  else
    curl.SetProtocol("http");
  curl.SetHostName(url.GetHostName());

  std::string path = curl.Get();
  std::string response;
  if (!emby.Post(path, requestBody, response) || response.empty())
  {
    std::string strMessage = "Could not connect to retreive EmbyToken";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Emby Services", strMessage, 3000, true);
    CLog::Log(LOGERROR, "CEmbyServices:AuthenticateByName failed %d, %s", emby.GetResponseCode(), response.c_str());
    return false;
  }

  CVariant responseObj = CJSONVariantParser::Parse(response);
  if (!responseObj.isObject() ||
      !responseObj.isMember("AccessToken") ||
      !responseObj.isMember("User") ||
      !responseObj["User"].isMember("Id"))
    return false;

  m_userId = responseObj["User"]["Id"].asString();
  m_accessToken = responseObj["AccessToken"].asString();

  return !m_accessToken.empty() && !m_userId.empty();
}

bool CEmbyServices::GetEmbyServers()
{
  bool rtn = false;

  std::vector<CEmbyClientPtr> clientsFound;

  EmbyServerInfo embyServerInfo = GetEmbyServerInfo(m_serverURL);
  if (!embyServerInfo.Id.empty())
  {
    CEmbyClientPtr client(new CEmbyClient());
    if (client->Init(m_userId, m_accessToken, embyServerInfo))
    {
      if (AddClient(client))
      {
        CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server found %s", client->GetServerName().c_str());
      }
      else if (GetClient(client->GetUuid()) == nullptr)
      {
        // lost client
        CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server was lost %s", client->GetServerName().c_str());
      }
    }
  }
  return rtn;
}

bool CEmbyServices::PostSignInPinCode()
{
  // on return, show user m_signInByPinCode so they can enter it at https://emby.media/pin

  bool rtn = false;

  XFILE::CCurlFile emby;
  // use a lower default timeout
  emby.SetTimeout(10);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");

  CURL url("https://connect.mediabrowser.tv/service/pin");

  CVariant data;
  data["deviceId"] = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID);
  std::string jsonBody = CJSONVariantWriter::Write(data, false);
  std::string response;
  std::string strMessage;
  if (emby.Post(url.Get(), jsonBody, response))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CEmbyServices:FetchSignInPin %s", response.c_str());
#endif
    CVariant reply;
    reply = CJSONVariantParser::Parse(response);
    if (reply.isObject() && reply.isMember("Pin"))
    {
      m_signInByPinCode = reply["Pin"].asString();
      if (m_signInByPinCode.empty())
        strMessage = "Failed to get Pin Code";
      rtn = !m_signInByPinCode.empty();
    }

    CGUIDialogProgress *waitPinReplyDialog;
    waitPinReplyDialog = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
    waitPinReplyDialog->SetHeading(g_localizeStrings.Get(2115));
    waitPinReplyDialog->SetLine(0, g_localizeStrings.Get(2117));
    std::string prompt = g_localizeStrings.Get(2118) + m_signInByPinCode;
    waitPinReplyDialog->SetLine(1, prompt);

    waitPinReplyDialog->Open();
    waitPinReplyDialog->ShowProgressBar(false);

    CStopWatch dieTimer;
    dieTimer.StartZero();
    int timeToDie = 60 * 5;

    CStopWatch pingTimer;
    pingTimer.StartZero();

    m_userId.clear();
    m_accessToken.clear();
    while (!waitPinReplyDialog->IsCanceled())
    {
      waitPinReplyDialog->SetPercentage(int(float(dieTimer.GetElapsedSeconds())/float(timeToDie)*100));

      if (pingTimer.GetElapsedSeconds() > 1)
      {
        // wait for user to run and enter pin code
        // at https://emby.media/pin
        if (GetSignInByPinReply())
          break;
        pingTimer.Reset();
      }

      if (dieTimer.GetElapsedSeconds() > timeToDie)
      {
        rtn = false;
        break;
      }
      waitPinReplyDialog->Progress();
    }
    waitPinReplyDialog->Close();

    if (m_accessToken.empty())
    {
      strMessage = "Error extracting AcessToken";
      CLog::Log(LOGERROR, "CPlexServices:FetchSignInPin failed to get authToken");
      m_signInByPinCode = "";
      rtn = false;
    }
    else
    {
      rtn = true;
/*
      std::string homeUserName;
      if (GetMyHomeUsers(homeUserName))
      {
        m_myHomeUser = homeUserName;
        rtn = true;
      }
      else
        rtn = false;
*/
    }
  }
  else
  {
    strMessage = "Could not connect to retreive AuthToken";
    CLog::Log(LOGERROR, "CEmbyServices:FetchSignInPin failed %s", response.c_str());
  }
  if (!rtn)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Emby Services", strMessage, 3000, true);
  return rtn;
}

bool CEmbyServices::GetSignInByPinReply()
{
  // repeat called until we timeout or get authToken
  bool rtn = false;
  std::string strMessage;
  XFILE::CCurlFile emby;
  emby.SetTimeout(10000);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");

  std::string path = "https://connect.mediabrowser.tv/service/pin";
  CURL url(path);
  url.SetOption("pin", m_signInByPinCode);
  url.SetOption("deviceId", CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID));;

  std::string response;
  CStopWatch pollTimer;
  pollTimer.StartZero();
  while (!m_bStop)
  {
    if (emby.Get(url.Get(), response))
    {
  #if defined(EMBY_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CEmbyServices:WaitForSignInByPin %s", response.c_str());
  #endif
      CVariant reply;
      reply = CJSONVariantParser::Parse(response);
      if (reply.isObject() && reply.isMember("AccessToken"))
      {
        m_userId = reply["Id"].asString();
        m_accessToken = reply["AccessToken"].asString();
        rtn = !m_accessToken.empty();
        if (rtn || (pollTimer.GetElapsedSeconds() > 60))
          break;
        m_processSleep.WaitMSec(250);
        m_processSleep.Reset();
      }
    }
  }
  
  if (!rtn)
  {
    CLog::Log(LOGERROR, "CEmbyServices:WaitForSignInByPin failed %s", response.c_str());
  }
  return rtn;
}

EmbyServerInfo CEmbyServices::GetEmbyServerInfo(const std::string url)
{
  EmbyServerInfo serverInfo;

  XFILE::CCurlFile emby;
  emby.SetTimeout(10);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");

  CURL curl(url);
  curl.SetFileName("emby/system/info/public");
  bool useHttps = curl.GetProtocol() == "embys";
  if (useHttps)
    curl.SetProtocol("https");
  else
    curl.SetProtocol("http");
  // do not need user/pass for server info
  curl.SetUserName("");
  curl.SetPassword("");

  std::string path = curl.Get();
  std::string response;
  if (!emby.Get(path, response) || response.empty())
  {
    std::string strMessage = "Could not connect to retreive EmbyServerInfo";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Emby Services", strMessage, 3000, true);
    CLog::Log(LOGERROR, "CEmbyServices:GetEmbyServerInfo failed %d, %s", emby.GetResponseCode(), response.c_str());
    return serverInfo;
  }

  static const std::string ServerPropertyId = "Id";
  static const std::string ServerPropertyName = "ServerName";
  static const std::string ServerPropertyVersion = "Version";
  static const std::string ServerPropertyWanAddress = "WanAddress";
  static const std::string ServerPropertyLocalAddress = "LocalAddress";
  static const std::string ServerPropertyOperatingSystem = "OperatingSystem";
  CVariant responseObj = CJSONVariantParser::Parse(response);
  if (!responseObj.isObject() ||
      !responseObj.isMember(ServerPropertyId) ||
      !responseObj.isMember(ServerPropertyName) ||
      !responseObj.isMember(ServerPropertyVersion) ||
      !responseObj.isMember(ServerPropertyWanAddress) ||
      !responseObj.isMember(ServerPropertyLocalAddress) ||
      !responseObj.isMember(ServerPropertyOperatingSystem))
    return serverInfo;

  serverInfo.Id = responseObj[ServerPropertyId].asString();
  serverInfo.Version = responseObj[ServerPropertyVersion].asString();
  serverInfo.ServerURL = curl.GetWithoutFilename();
  serverInfo.ServerName = responseObj[ServerPropertyName].asString();
  serverInfo.WanAddress = responseObj[ServerPropertyWanAddress].asString();
  serverInfo.LocalAddress = responseObj[ServerPropertyLocalAddress].asString();
  serverInfo.OperatingSystem = responseObj[ServerPropertyOperatingSystem].asString();
  return serverInfo;
}

CEmbyClientPtr CEmbyServices::GetClient(std::string uuid)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == uuid)
      return client;
  }

  return nullptr;
}

bool CEmbyServices::ClientIsLocal(std::string path)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (StringUtils::StartsWithNoCase(client->GetUrl(), path))
      return client->IsLocal();
  }
  
  return false;
}

bool CEmbyServices::AddClient(CEmbyClientPtr foundClient)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    // do not add existing clients
    if (client->GetUuid() == foundClient->GetUuid())
      return false;
  }

  // only add new clients that are present
  if (foundClient->GetPresence() && foundClient->ParseViews())
  {
    m_clients.push_back(foundClient);
    m_hasClients = !m_clients.empty();
    return true;
  }

  return false;
}

bool CEmbyServices::RemoveClient(CEmbyClientPtr lostClient)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == lostClient->GetUuid())
    {
      // this is silly but can not figure out how to erase
      // just given 'client' :)
      m_clients.erase(std::find(m_clients.begin(), m_clients.end(), client));
      m_hasClients = !m_clients.empty();

      // client is gone, remove it from any gui lists here.
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);
      return true;
    }
  }

  return false;
}
