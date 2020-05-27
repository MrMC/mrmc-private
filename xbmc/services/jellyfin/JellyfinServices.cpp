/*
 *      Copyright (C) 2020 Team MrMC
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

#include "JellyfinServices.h"

#include "JellyfinUtils.h"
#include "JellyfinClient.h"
#include "Application.h"
#include "URL.h"
#include "Util.h"
#include "GUIUserMessages.h"
#include "addons/Skin.h"
#include "dialogs/GUIDialogBusy.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogSelect.h"
#include "filesystem/DirectoryCache.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/DNSNameCache.h"
#include "network/GUIDialogNetworkSetup.h"
#include "network/WakeOnAccess.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/md5.h"
#include "utils/sha1.hpp"
#include "utils/StringUtils.h"
#include "utils/StringHasher.h"
#include "utils/SystemInfo.h"
#include "utils/JobManager.h"

#include "utils/SystemInfo.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/Variant.h"

using namespace ANNOUNCEMENT;

static bool IsInSubNet(CURL url)
{
  bool rtn = false;
  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  in_addr_t localMask = ntohl(inet_addr(iface->GetCurrentNetmask().c_str()));
  in_addr_t testAddress = ntohl(inet_addr(url.GetHostName().c_str()));
  in_addr_t localAddress = ntohl(inet_addr(iface->GetCurrentIPAddress().c_str()));

  in_addr_t temp1 = testAddress & localMask;
  in_addr_t temp2 = localAddress & localMask;

  if (temp1 == temp2)
  {
    // now make sure it is a jellyfin server
    rtn = CJellyfinUtils::GetIdentity(url, 1);
  }
#if defined(JELLYFIN_DEBUG_VERBOSE)
  char buffer[256];
  std::string temp1IpAddress;
  if (inet_neta(temp1, buffer, sizeof(buffer)))
    temp1IpAddress = buffer;
  std::string temp2IpAddress;
  if (inet_neta(temp2, buffer, sizeof(buffer)))
    temp2IpAddress = buffer;
  CLog::Log(LOGDEBUG, "IsInSubNet = yes(%d), testAddress(%s), localAddress(%s)", rtn, temp1IpAddress.c_str(), temp2IpAddress.c_str());
#endif
  return rtn;
}

class CJellyfinServiceJob: public CJob
{
public:
  CJellyfinServiceJob(double currentTime, std::string strFunction,std::string strUUID="")
  : m_function(strFunction)
  , m_strUUID(strUUID)
  , m_currentTime(currentTime)
  {
  }
  virtual ~CJellyfinServiceJob()
  {
  }
  virtual bool DoWork()
  {
    if (m_function == "FoundNewClient")
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);

      // announce that we have a jellyfin client and that recently added should be updated
      CVariant data(CVariant::VariantTypeObject);
      data["uuid"] = m_strUUID;
      ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "UpdateRecentlyAdded",data);
      ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "UpdateRecentlyAdded",data);
    }
    return true;
  }
  virtual bool operator==(const CJob *job) const
  {
    return true;
  }
private:
  std::string    m_function;
  std::string    m_strUUID;
  double         m_currentTime;
};


CJellyfinServices::CJellyfinServices()
: CThread("JellyfinServices")
, m_playState(MediaServicesPlayerState::stopped)
, m_hasClients(false)
{
  // register our redacted items with CURL
  // we do not want these exposed in mrmc.log.
  if (!CURL::HasRedactedKey(JellyfinApiKeyHeader))
    CURL::SetRedactedKey(JellyfinApiKeyHeader, "JELLYFINTOKEN");

  CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CJellyfinServices::~CJellyfinServices()
{
  CAnnouncementManager::GetInstance().RemoveAnnouncer(this);

  if (IsRunning())
    Stop();

  CancelJobs();
}

CJellyfinServices& CJellyfinServices::GetInstance()
{
  static CJellyfinServices sJellyfinServices;
  return sJellyfinServices;
}

void CJellyfinServices::Start()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    Stop();
  CThread::Create();
}

void CJellyfinServices::Stop()
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

bool CJellyfinServices::IsActive()
{
  return IsRunning();
}

bool CJellyfinServices::IsEnabled()
{
  return (!CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINACESSTOKEN).empty());
}

bool CJellyfinServices::HasClients() const
{
  return m_hasClients;
}

void CJellyfinServices::GetClients(std::vector<CJellyfinClientPtr> &clients) const
{
  CSingleLock lock(m_clients_lock);
  clients = m_clients;
}

CJellyfinClientPtr CJellyfinServices::FindClient(const std::string &path)
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

CJellyfinClientPtr CJellyfinServices::FindClient(const CJellyfinClient *testclient)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (testclient == client.get())
      return client;
  }

  return nullptr;
}

void CJellyfinServices::OnSettingAction(const CSetting *setting)
{
  if (setting == nullptr)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_JELLYFINSIGNIN)
  {
    InitiateSignIn();
  }
}

void CJellyfinServices::InitiateSignIn()
{
  bool startThread = false;
  std::string strMessage;
  std::string strSignIn = g_localizeStrings.Get(2115);
  std::string strSignOut = g_localizeStrings.Get(2116);
  if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINSIGNIN) == strSignIn)
  {
    CURL curl(m_serverURL);
    curl.SetProtocol("jellyfin");
    std::string path = curl.Get();
    if (CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path))
    {
      CURL curl2(path);
      if (!curl2.GetHostName().empty() && !curl2.GetUserName().empty())
      {
        if (AuthenticateByName(curl2))
        {
          // never save the password
          curl2.SetPassword("");
          m_serverURL = curl2.Get();
          // change prompt to 'sign-out'
          CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_JELLYFINSIGNIN, strSignOut);
          CLog::Log(LOGDEBUG, "CJellyfinServices:OnSettingAction manual sign-in ok");
          startThread = true;
        }
        else
        {
          strMessage = "Could not get authToken via manual sign-in";
          CLog::Log(LOGERROR, "CJellyfinServices: %s", strMessage.c_str());
        }
      }
      else
      {
        // opps, nuke'em all
        CLog::Log(LOGDEBUG, "CJellyfinServices:OnSettingAction host/user are empty");
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
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_JELLYFINSIGNIN, strSignIn);
    CLog::Log(LOGDEBUG, "CJellyfinServices:OnSettingAction sign-out ok");
  }
  SetUserSettings();

  if (startThread)
    Start();
  else
    Stop();
}

void CJellyfinServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
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
  else if ((flag & AnnouncementFlag::Other) && strcmp(sender, "jellyfin") == 0)
  {
    if (strcmp(message, "UpdateLibrary") == 0)
    {
      std::string content = data["MediaServicesContent"].asString();
      std::string clientId = data["MediaServicesClientID"].asString();
      for (const auto &client : m_clients)
      {
        if (client->GetUuid() == clientId)
          client->UpdateLibrary(content);
      }
    }
    else if (strcmp(message, "ReloadProfiles") == 0)
    {
      // restart if we MrMC profiles has changed
      Stop();
      Start();
    }
  }
}

void CJellyfinServices::OnSettingChanged(const CSetting *setting)
{
  // All Jellyfin settings so far
  /*
  static const std::string SETTING_SERVICES_JELLYFINSIGNIN;
  static const std::string SETTING_SERVICES_JELLYFINUSERID;
  static const std::string SETTING_SERVICES_JELLYFINSERVERURL;
  static const std::string SETTING_SERVICES_JELLYFINACESSTOKEN;
  */

  if (setting == NULL)
    return;
}

void CJellyfinServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_JELLYFINUSERID, m_userId);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_JELLYFINSERVERURL, m_serverURL);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_JELLYFINACESSTOKEN, m_accessToken);
  CSettings::GetInstance().Save();
}

void CJellyfinServices::GetUserSettings()
{
  m_userId = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINUSERID);
  m_serverURL  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINSERVERURL);
  m_accessToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINACESSTOKEN);
}

void CJellyfinServices::Process()
{
  CLog::Log(LOGDEBUG, "CJellyfinServices::Process bgn");
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);

  // This gets started when network comes up but we need to
  // wait until gui is up before poking for jellyfin servers
  // there is no real indication that gui has something
  // in focus, so we wait another 0.75 secs before continuing.
  while (!m_bStop)
  {
    if (g_application.IsAppInitialized() && g_application.IsAppFocused())
    {
      m_processSleep.WaitMSec(750);
      m_processSleep.Reset();
      if (!g_application.IsPlayingSplash())
        break;
    }
    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  GetUserSettings();

  bool signInByManual;
  std::string strSignOut = g_localizeStrings.Get(2116);
  // if set to strSignOut, we are signed in by user/pass
  signInByManual = (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_JELLYFINSIGNIN) == strSignOut);

  while (!m_bStop)
  {
    if (g_sysinfo.HasInternet())
    {
      CLog::Log(LOGDEBUG, "CJellyfinServices::Process has gateway1");
      break;
    }
    if (g_application.getNetwork().IsConnected())
    {
      if (signInByManual)
      {
        std::string ip;
        if (CDNSNameCache::Lookup(m_serverURL, ip))
        {
          in_addr_t localjellyfindotcom = inet_addr(ip.c_str());
          if (g_application.getNetwork().PingHost(localjellyfindotcom, 0, 1000))
          {
            CLog::Log(LOGDEBUG, "CJellyfinServices::Process has network, manual signin");
            break;
          }
        }
      }
    }
    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  int serviceTimeoutSeconds = 5;
  if (signInByManual)
  {
    if (!m_accessToken.empty() && !m_userId.empty())
    {
      CURL curl(m_serverURL);
      CWakeOnAccess::GetInstance().WakeUpHost(curl.GetHostName(), "Jellyfin Server");
      GetJellyfinLocalServers(m_serverURL, m_userId, m_accessToken);
      serviceTimeoutSeconds = 60 * 15;
    }
  }

  while (!m_bStop)
  {
    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  CLog::Log(LOGDEBUG, "CJellyfinServices::Process end");
}

bool CJellyfinServices::AuthenticateByName(const CURL& url)
{
  XFILE::CCurlFile jellyfin;
  jellyfin.SetRequestHeader("Cache-Control", "no-cache");
  jellyfin.SetRequestHeader("Content-Type", "application/json");
  CJellyfinUtils::PrepareApiCall("", "", jellyfin);

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
  body["username"] = url.GetUserName();
  // Beginning April 1, 2018, only the "pw" param will be required.
  // Until then, all three are needed in order to support both newer and older servers.
  body["pw"] = password;
  body["password"] = passwordSha1;
  body["passwordMd5"] = passwordMd5;
  std::string requestBody;
  if (!CJSONVariantWriter::Write(body, requestBody, true))
    return false;

  CURL curl(CJellyfinUtils::ConstructFileName(url, "Users/AuthenticateByName"));
  curl.SetPort(url.GetPort());
  if (url.GetProtocol() == "jellyfins")
    curl.SetProtocol("https");
  else
    curl.SetProtocol("http");
  curl.SetHostName(url.GetHostName());

  std::string path = curl.Get();
  std::string response;
  if (!jellyfin.Post(path, requestBody, response) || response.empty())
  {
    std::string strMessage = "Could not connect to retreive JellyfinToken";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Jellyfin Services", strMessage, 3000, true);
    CLog::Log(LOGERROR, "CJellyfinServices:AuthenticateByName failed %d, %s", jellyfin.GetResponseCode(), response.c_str());
    return false;
  }

  CVariant responseObj;
  if (!CJSONVariantParser::Parse(response, responseObj))
    return false;
  if (!responseObj.isObject() ||
      !responseObj.isMember("AccessToken") ||
      !responseObj.isMember("User") ||
      !responseObj["User"].isMember("Id"))
    return false;

  m_userId = responseObj["User"]["Id"].asString();
  m_accessToken = responseObj["AccessToken"].asString();

  CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"jellyfin");
  CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
  CSettings::GetInstance().Save();
  return !m_accessToken.empty() && !m_userId.empty();
}

JellyfinServerInfo CJellyfinServices::GetJellyfinLocalServerInfo(const std::string url)
{
  JellyfinServerInfo serverInfo;

  XFILE::CCurlFile jellyfin;
  jellyfin.SetRequestHeader("Cache-Control", "no-cache");
  jellyfin.SetRequestHeader("Content-Type", "application/json");

  CURL curl(url);
  curl.SetFileName(CJellyfinUtils::ConstructFileName(curl, "system/info/public"));
  bool useHttps = curl.GetProtocol() == "jellyfins";
  if (useHttps)
    curl.SetProtocol("https");
  else
    curl.SetProtocol("http");
  // do not need user/pass for server info
  curl.SetUserName("");
  curl.SetPassword("");

  std::string path = curl.Get();
  std::string response;
  if (!jellyfin.Get(path, response) || response.empty())
  {
    std::string strMessage = "Could not connect to retreive JellyfinServerInfo";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Jellyfin Services", strMessage, 3000, true);
    CLog::Log(LOGERROR, "CJellyfinServices:GetJellyfinServerInfo failed %d, %s", jellyfin.GetResponseCode(), response.c_str());
    return serverInfo;
  }

  static const std::string ServerPropertyId = "Id";
  static const std::string ServerPropertyName = "ServerName";
  static const std::string ServerPropertyVersion = "Version";
  static const std::string ServerPropertyWanAddress = "WanAddress";
  static const std::string ServerPropertyLocalAddress = "LocalAddress";
  static const std::string ServerPropertyOperatingSystem = "OperatingSystem";
  CVariant responseObj;
  if (!CJSONVariantParser::Parse(response, responseObj))
    return serverInfo;
  if (!responseObj.isObject() ||
      !responseObj.isMember(ServerPropertyId) ||
      !responseObj.isMember(ServerPropertyName) ||
      !responseObj.isMember(ServerPropertyVersion))
    return serverInfo;

  serverInfo.UserId = m_userId;
  serverInfo.AccessToken = m_accessToken;
  // servers found by broadcast are always local ("Linked")
  serverInfo.UserType= "Linked";
  serverInfo.ServerId = responseObj[ServerPropertyId].asString();
  if (curl.GetShareName().empty())
    serverInfo.ServerURL = curl.GetWithoutFilename();
  else
    serverInfo.ServerURL = curl.GetWithoutFilename() + curl.GetShareName();
  serverInfo.ServerName = responseObj[ServerPropertyName].asString();
  // jellyfin does use WanAddress and it might be missing
  if (responseObj.isMember(ServerPropertyWanAddress))
    serverInfo.WanAddress = responseObj[ServerPropertyWanAddress].asString();
  if (responseObj.isMember(ServerPropertyLocalAddress))
    serverInfo.LocalAddress = responseObj[ServerPropertyLocalAddress].asString();
  else
    serverInfo.LocalAddress = curl.GetWithoutFilename();
  return serverInfo;
}

bool CJellyfinServices::GetJellyfinLocalServers(const std::string &serverURL, const std::string &userId, const std::string &accessToken)
{
  bool rtn = false;

  std::vector<CJellyfinClientPtr> clientsFound;

  JellyfinServerInfo jellyfinServerInfo = GetJellyfinLocalServerInfo(serverURL);
  if (!jellyfinServerInfo.ServerId.empty())
  {
    jellyfinServerInfo.UserId = userId;
    jellyfinServerInfo.AccessToken = accessToken;
    CJellyfinClientPtr client(new CJellyfinClient());
    if (client->Init(jellyfinServerInfo))
    {
      if (AddClient(client))
      {
        CLog::Log(LOGNOTICE, "CJellyfinServices::CheckJellyfinServers Server found %s", client->GetServerName().c_str());
      }
      else if (GetClient(client->GetUuid()) == nullptr)
      {
        // lost client
        CLog::Log(LOGNOTICE, "CJellyfinServices::CheckJellyfinServers Server was lost %s", client->GetServerName().c_str());
      }
    }
  }
  return rtn;
}

bool CJellyfinServices::PostSignInPinCode()
{
  // on return, show user m_signInByPinCode so they can enter it at https://jellyfin.media/pin
  bool rtn = false;
  std::string strMessage;

  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");

  CURL curl("https://connect.jellyfin.media");
  curl.SetFileName("service/pin");
  curl.SetOption("format", "json");

  CVariant data;
  data["deviceId"] = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID);
  std::string jsonBody;
  if (!CJSONVariantWriter::Write(data, jsonBody, false))
    return rtn;
  std::string response;
  if (curlfile.Post(curl.Get(), jsonBody, response))
  {
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinServices:FetchSignInPin %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
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
    waitPinReplyDialog->ShowProgressBar(true);

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
      waitPinReplyDialog->Progress();
      if (pingTimer.GetElapsedSeconds() > 1)
      {
        // wait for user to run and enter pin code
        // at https://jellyfin.media/pin
        if (GetSignInByPinReply())
          break;
        pingTimer.Reset();
        m_processSleep.WaitMSec(250);
        m_processSleep.Reset();
      }

      if (dieTimer.GetElapsedSeconds() > timeToDie)
      {
        rtn = false;
        break;
      }
    }
    waitPinReplyDialog->Close();

    if (m_accessToken.empty())
    {
      strMessage = "Error extracting AcessToken";
      CLog::Log(LOGERROR, "CJellyfinServices::PostSignInPinCode failed to get authToken");
      m_signInByPinCode = "";
      rtn = false;
    }
  }
  else
  {
    strMessage = "Could not connect to retreive AuthToken";
    CLog::Log(LOGERROR, "CJellyfinServices:FetchSignInPin failed %s", response.c_str());
  }
  if (!rtn)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Jellyfin Services", strMessage, 3000, true);
  else
  {
    // Jellyfin signed in, save server type for home selection
    CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"jellyfin");
    CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
    CSettings::GetInstance().Save();
  }
  return rtn;
}

bool CJellyfinServices::GetSignInByPinReply()
{
  // repeat called until we timeout or get authToken
  bool rtn = false;
  std::string strMessage;

  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");

  CURL curl("https://connect.jellyfin.media");
  curl.SetFileName("service/pin");
  curl.SetOption("format", "json");
  curl.SetOption("pin", m_signInByPinCode);
  curl.SetOption("deviceId", CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID));

  std::string response;
  if (curlfile.Get(curl.Get(), response))
  {
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinServices:WaitForSignInByPin %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
    if (reply.isObject() && reply.isMember("IsConfirmed") && reply["IsConfirmed"].asString() == "true")
    {
      std::string pin = reply["Pin"].asString();
      std::string deviceId = reply["DeviceId"].asString();
      std::string id = reply["Id"].asString();
      //std::string isConfirmed = reply["IsConfirmed"].asString();
      //std::string isExpired = reply["IsExpired"].asString();
      //std::string accessToken = reply["AccessToken"].asString();
      if (!deviceId.empty() && !pin.empty())
        rtn = AuthenticatePinReply(deviceId, pin);
    }
  }

  if (!rtn)
  {
    CLog::Log(LOGERROR, "CJellyfinServices:WaitForSignInByPin failed %s", response.c_str());
  }
  return rtn;
}

bool CJellyfinServices::AuthenticatePinReply(const std::string &deviceId, const std::string &pin)
{
  bool rtn = false;

  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");

  CURL curl("https://connect.jellyfin.media");
  curl.SetFileName("service/pin/authenticate");
  curl.SetOption("format", "json");

  CVariant data;
  data["pin"] = pin;
  data["deviceId"] = deviceId;
  std::string jsondata;
  if (!CJSONVariantWriter::Write(data, jsondata, false))
    return rtn;
  std::string response;
  if (curlfile.Post(curl.Get(), jsondata, response))
  {
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinServices:AuthenticatePinReply %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
    if (reply.isObject() && reply.isMember("AccessToken"))
    {
      // pin connects are parsed as UserId/AccessToken
      // user/pass connects are parsed as ConnectUserId/ConnectAccessToken
      const std::string connectUserId = reply["UserId"].asString();
      const std::string connectAccessToken = reply["AccessToken"].asString();
      JellyfinServerInfoVector servers;
      servers = GetConnectServerList(connectUserId, connectAccessToken);
      if (!servers.empty())
      {
        m_userId = connectUserId;
        m_accessToken = connectAccessToken;
        rtn = true;
      }
    }
  }
  return rtn;
}

JellyfinServerInfoVector CJellyfinServices::GetConnectServerList(const std::string &connectUserId, const std::string &connectAccessToken)
{
  JellyfinServerInfoVector servers;

  CGUIDialogBusy *busyDialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
  if (busyDialog)
    busyDialog->Open();
  
  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");

  CURL curl("https://connect.jellyfin.media");
  curl.SetFileName("service/servers");
  curl.SetOption("format", "json");
  curl.SetOption("userId", connectUserId);
  curl.SetProtocolOptions("&X-Connect-UserToken=" + connectAccessToken);

  std::string response;
  if (curlfile.Get(curl.Get(), response))
  {
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinServices:GetConnectServerList %s", response.c_str());
#endif
    CVariant vservers;
    if (!CJSONVariantParser::Parse(response, vservers))
    {
      if (busyDialog)
        busyDialog->Close();
      return servers;
    }
    if (vservers.isArray())
    {
      for (auto serverObjectIt = vservers.begin_array(); serverObjectIt != vservers.end_array(); ++serverObjectIt)
      {
        const auto server = *serverObjectIt;
        JellyfinServerInfo serverInfo;
        serverInfo.UserId = connectUserId;
        serverInfo.AccessToken = connectAccessToken;

        serverInfo.UserType= server["UserType"].asString();
        serverInfo.ServerId = server["SystemId"].asString();
        serverInfo.AccessKey= server["AccessKey"].asString();
        serverInfo.ServerName= server["Name"].asString();
        serverInfo.WanAddress= server["Url"].asString();
        serverInfo.LocalAddress= server["LocalAddress"].asString();
        if (IsInSubNet(CURL(serverInfo.LocalAddress)))
        {
          serverInfo.ServerURL= serverInfo.LocalAddress;
        }
        else
        {
          // jellyfin does use WanAddress and it might be missing
          if (!serverInfo.WanAddress.empty())
            serverInfo.ServerURL= serverInfo.WanAddress;
          else
            serverInfo.ServerURL= serverInfo.LocalAddress;
        }
        if (ExchangeAccessKeyForAccessToken(serverInfo))
          servers.push_back(serverInfo);
      }
    }
  }
  if (busyDialog)
    busyDialog->Close();
  return servers;
}

bool CJellyfinServices::ExchangeAccessKeyForAccessToken(JellyfinServerInfo &connectServerInfo)
{
  bool rtn = false;

  XFILE::CCurlFile curlfile;
  curlfile.SetRequestHeader("Cache-Control", "no-cache");
  curlfile.SetRequestHeader("Content-Type", "application/json");
  CJellyfinUtils::PrepareApiCall(connectServerInfo.UserId, connectServerInfo.AccessKey, curlfile);

  CURL curl(connectServerInfo.ServerURL);
  curl.SetFileName(CJellyfinUtils::ConstructFileName(curl, "Connect/Exchange"));
  curl.SetOption("format", "json");
  curl.SetOption("ConnectUserId", connectServerInfo.UserId);

  std::string response;
  if (curlfile.Get(curl.Get(), response))
  {
#if defined(JELLYFIN_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CJellyfinServices:ExchangeAccessKeyForAccessToken %s", response.c_str());
#endif
    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;
    if (reply.isObject() && reply.isMember("AccessToken"))
    {
      connectServerInfo.UserId = reply["LocalUserId"].asString();
      connectServerInfo.AccessToken = reply["AccessToken"].asString();
      rtn = true;
    }
  }
  return rtn;
}


CJellyfinClientPtr CJellyfinServices::GetClient(std::string uuid)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == uuid)
      return client;
  }

  return nullptr;
}

CJellyfinClientPtr CJellyfinServices::GetFirstClient()
{
  CSingleLock lock(m_clients_lock);
  if (m_clients.size() > 0)
    return m_clients[0];
  else
    return nullptr;
}

bool CJellyfinServices::ClientIsLocal(std::string path)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    if (StringUtils::StartsWithNoCase(client->GetUrl(), path))
      return client->IsLocal();
  }

  return false;
}

bool CJellyfinServices::AddClient(CJellyfinClientPtr foundClient)
{
  CSingleLock lock(m_clients_lock);
  for (const auto &client : m_clients)
  {
    // do not add existing clients
    if (client->GetUuid() == foundClient->GetUuid())
      return false;
  }

  // only add new clients that are present
  if (foundClient->GetPresence())
  {
    std::string uuid = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
    if (uuid.empty() || uuid == foundClient->GetUuid() || (g_SkinInfo && !g_SkinInfo->IsDynamicHomeCompatible()))
      foundClient->FetchViews();
    m_clients.push_back(foundClient);
    m_hasClients = !m_clients.empty();
    AddJob(new CJellyfinServiceJob(0, "FoundNewClient",foundClient->GetUuid()));
    return true;
  }

  return false;
}

bool CJellyfinServices::RemoveClient(CJellyfinClientPtr lostClient)
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

bool CJellyfinServices::ParseCurrentServerSections()
{
  std::string uuid = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
  CJellyfinClientPtr client = GetClient(uuid);
  if (client)
  {
    client->FetchViews();
    return true;
  }
  return false;
}
