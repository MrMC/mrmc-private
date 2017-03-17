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

#include "Application.h"
#include "URL.h"
#include "Util.h"
#include "GUIUserMessages.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogNumeric.h"
#include "dialogs/GUIDialogProgress.h"
#include "filesystem/DirectoryCache.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "network/DNSNameCache.h"
#include "network/GUIDialogNetworkSetup.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "profiles/dialogs/GUIDialogLockSettings.h"
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
#include "utils/XMLUtils.h"

#include "EmbyUtils.h"
#include "EmbyClient.h"

#include <regex>

using namespace ANNOUNCEMENT;

//static const int NS_EMBY_BROADCAST_PORT(7359);
//static const std::string NS_EMBY_BROADCAST_ADDRESS("255.255.255.255");
//static const std::string NS_EMBY_BROADCAST_SEARCH_MSG("who is EmbyServer?");
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
    if (m_function == "UpdateLibraries")
    {
      CLog::Log(LOGNOTICE, "CEmbyServiceJob: UpdateLibraries");
      CEmbyServices::GetInstance().UpdateLibraries(true);
    }
    else if (m_function == "FoundNewClient")
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
, m_playState(EmbyServicePlayerState::stopped)
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
  CSingleLock lock2(m_criticalClients);
  m_clients.clear();
  m_playState = EmbyServicePlayerState::stopped;
  m_hasClients = false;
}

bool CEmbyServices::IsActive()
{
  return IsRunning();
}

bool CEmbyServices::IsEnabled()
{
  return (!CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYUSERID).empty() ||
           CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYBROADCAST));
}

bool CEmbyServices::HasClients() const
{
  return m_hasClients;
}

void CEmbyServices::GetClients(std::vector<CEmbyClientPtr> &clients) const
{
  CSingleLock lock(m_criticalClients);
  clients = m_clients;
}

CEmbyClientPtr CEmbyServices::FindClient(const std::string &path)
{
  CURL url(path);
  CSingleLock lock(m_criticalClients);
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
      std::string path = "emby://";
      if (CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path))
      {
        CURL url(path);
        if (!url.GetHostName().empty() && !url.GetUserName().empty() && !url.GetPassWord().empty())
        {
          if (AuthenticateByName(url))
          {
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


    if (startThread || m_broadcast)
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
        CSingleLock lock(m_criticalClients);
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
        m_playState = EmbyServicePlayerState::playing;
        break;
      case "OnPause"_mkhash:
        m_playState = EmbyServicePlayerState::paused;
        break;
      case "OnStop"_mkhash:
        m_playState = EmbyServicePlayerState::stopped;
        break;
      default:
        break;
    }
  }
  else if ((flag & AnnouncementFlag::Other) && strcmp(sender, "emby") == 0)
  {
    if (strcmp(message, "UpdateLibrary") == 0)
    {
      AddJob(new CEmbyServiceJob(0, "UpdateLibraries"));
    }
    else if (strcmp(message, "ReloadProfiles") == 0)
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
  static const std::string SETTING_SERVICES_EMBYSERVERIP;
  static const std::string SETTING_SERVICES_EMBYACESSTOKEN;

  static const std::string SETTING_SERVICES_EMBYSIGNINPIN;
  static const std::string SETTING_SERVICES_EMBYHOMEUSER;
  static const std::string SETTING_SERVICES_EMBYBROADCAST;
  static const std::string SETTING_SERVICES_EMBYUPDATEMINS;
  */

  if (setting == NULL)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_EMBYBROADCAST)
  {
    m_broadcast = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYBROADCAST);
    // start or stop the service
    if (m_broadcast || (!m_userId.empty() && !m_accessToken.empty()))
      Start();
    else
      Stop();
  }
}

void CEmbyServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYUSERID, m_userId);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYSERVERIP, m_serverIP);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_EMBYACESSTOKEN, m_accessToken);
  CSettings::GetInstance().Save();
}

void CEmbyServices::GetUserSettings()
{
  m_userId = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYUSERID);
  m_serverIP  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYSERVERIP);
  m_accessToken  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_EMBYACESSTOKEN);
  m_broadcast = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_EMBYBROADCAST);
}

void CEmbyServices::UpdateLibraries(bool forced)
{
  CSingleLock lock(m_criticalClients);
  bool clearDirCache = false;
  for (const auto &client : m_clients)
  {
    client->ParseViews(EmbyViewParsing::checkView);
    if (forced || client->NeedUpdate())
    {
      client->ParseViews(EmbyViewParsing::updateView);
      clearDirCache = true;
    }
  }
  if (clearDirCache)
  {
    g_directoryCache.Clear();
    if (m_playState == EmbyServicePlayerState::stopped)
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);
    }
  }
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

/*
  FindEmbyServersByBroadcast();

  CStopWatch broadcastTimer, serviceTimer, checkUpdatesTimer;
  broadcastTimer.StartZero();
  serviceTimer.StartZero();
  checkUpdatesTimer.StartZero();
  while (!m_bStop)
  {
    // check for services every N seconds
    if (serviceTimer.GetElapsedSeconds() > serviceTimeoutSeconds)
    {
      // try plex.tv
      if (EmbySignedIn())
      {
        if (m_playState == EmbyServicePlayerState::stopped)
        {
          // if we get back servers, then
          // reduce the initial polling time
          bool foundSomething = false;
          foundSomething = GetEmbyServers(true);
          if (foundSomething)
            serviceTimeoutSeconds = 60 * 15;
        }
      }
      serviceTimer.Reset();
    }

    if (broadcastTimer.GetElapsedSeconds() > 5)
    {
      if (m_playState == EmbyServicePlayerState::stopped)
        FindEmbyServersByBroadcast();
      broadcastTimer.Reset();
    }

    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  if (m_broadcastListener)
  {
    // before deleting listener, fetch and delete any sockets it uses.
    SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_broadcastListener->GetFirstSocket();
    // we should not have to do the close,
    // delete 'should' do it.
    socket->Close();
    SAFE_DELETE(socket);
    SAFE_DELETE(m_broadcastListener);
  }
*/
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

  CURL url2("emby/Users/AuthenticateByName");
  //url2.SetPort(8096);
  //url2.SetProtocol("http");
  url2.SetPort(8920);
  url2.SetProtocol("https");
  url2.SetHostName(url.GetHostName());

  std::string path = url2.Get();
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
  m_serverIP = url.GetHostName();
  m_accessToken = responseObj["AccessToken"].asString();

  return !m_accessToken.empty() && !m_userId.empty();
}

bool CEmbyServices::GetEmbyServers()
{
  bool rtn = false;

  std::vector<CEmbyClientPtr> clientsFound;

  EmbyServerInfo embyServerInfo = GetEmbyServerInfo(m_serverIP);
  if (!embyServerInfo.Id.empty())
  {
    CEmbyClientPtr client(new CEmbyClient());
    if (client->Init(m_userId, m_accessToken, embyServerInfo))
    {
      if (AddClient(client))
      {
        CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server found via GDM %s", client->GetServerName().c_str());
      }
      else if (GetClient(client->GetUuid()) == nullptr)
      {
        // lost client
        CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server was lost %s", client->GetServerName().c_str());
      }
      else if (UpdateClient(client))
      {
        // client exists and something changed
        CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers presence changed %s", client->GetServerName().c_str());
      }
    }
  }

/*
  std::string strResponse;
  CURL url(NS_PLEXTV_URL);
  if (includeHttps)
    url.SetFileName("pms/resources?includeHttps=1");
  else
    url.SetFileName("pms/resources?includeHttps=0");

  if (m_emby.Get(url.Get(), strResponse))
  {
#if defined(EMBY_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CPlexServices:GetMyPlexServers %d, %s", includeHttps, strResponse.c_str());
#endif
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      const TiXmlElement* DeviceNode = MediaContainer->FirstChildElement("Device");
      while (DeviceNode)
      {
        std::string provides = XMLUtils::GetAttribute(DeviceNode, "provides");
        if (provides == "server")
        {
          CEmbyClientPtr client(new CEmbyClient());
          if (client->Init(DeviceNode))
          {
            clientsFound.push_back(client);
            // always return true if we find anything
            rtn = true;
          }
        }
        DeviceNode = DeviceNode->NextSiblingElement("Device");
      }
    }
  }
  else
  {
    std::string strMessage = "Error getting Plex servers";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
    CLog::Log(LOGDEBUG, "CPlexServices::GetMyPlexServers failed %s, code %d", strResponse.c_str(), m_plextv.GetResponseCode());
    return false;
  }

  std::vector<CPlexClientPtr> lostClients;
  if (!clientsFound.empty())
  {
    for (const auto &client : clientsFound)
    {
      if (AddClient(client))
      {
        // new client
        CLog::Log(LOGNOTICE, "CPlexServices: Server found via plex.tv %s", client->GetServerName().c_str());
      }
      else if (GetClient(client->GetUuid()) == nullptr)
      {
        // lost client
        lostClients.push_back(client);
        CLog::Log(LOGNOTICE, "CPlexServices: Server was lost %s", client->GetServerName().c_str());
      }
      else if (UpdateClient(client))
      {
        // client exists and something changed
        CLog::Log(LOGNOTICE, "CPlexServices: Server presence changed %s", client->GetServerName().c_str());
      }
    }
    AddJob(new CPlexServiceJob(0, "FoundNewClient"));
  }

  if (!lostClients.empty())
  {
    for (const auto &lostclient : lostClients)
      RemoveClient(lostclient);
  }
*/

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
/*
void CEmbyServices::FindEmbyServersByBroadcast()
{
  if (m_broadcast)
  {
    if (!m_broadcastListener)
    {
      SOCKETS::CUDPSocket *socket = SOCKETS::CSocketFactory::CreateUDPSocket();
      if (socket)
      {
        CNetworkInterface *iface = g_application.getNetwork().GetFirstConnectedInterface();
        if (iface && iface->IsConnected())
        {
          SOCKETS::CAddress my_addr;
          my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
          if (!socket->Bind(my_addr, NS_EMBY_BROADCAST_PORT, 0))
          {
            CLog::Log(LOGERROR, "CEmbyServices:CheckEmbyServers Could not listen on port %d", NS_EMBY_BROADCAST_PORT);
            SAFE_DELETE(m_broadcastListener);
            m_broadcast = false;
            return;
          }

          if (socket)
          {
            socket->SetBroadCast(true);
            // create and add our socket to the 'select' listener
            m_broadcastListener = new SOCKETS::CSocketListener();
            m_broadcastListener->AddSocket(socket);
          }
        }
        else
        {
          SAFE_DELETE(socket);
        }
      }
      else
      {
        CLog::Log(LOGERROR, "CEmbyServices:CheckEmbyServers Could not create socket for GDM");
        m_broadcast = false;
        return;
      }
    }

    SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_broadcastListener->GetFirstSocket();
    if (socket)
    {
      SOCKETS::CAddress discoverAddress;
      discoverAddress.SetAddress(NS_EMBY_BROADCAST_ADDRESS.c_str(), NS_EMBY_BROADCAST_PORT);
      std::string discoverMessage = NS_EMBY_BROADCAST_SEARCH_MSG;
      int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
      if (packetSize < 0)
        CLog::Log(LOGERROR, "CEmbyServices::CheckEmbyServers:CheckforGDMServers discover send failed");
    }

    bool foundNewClient = false;
    static const int DiscoveryTimeoutMs = 1000;
    // listen for broadcast reply until we timeout
    if (socket && m_broadcastListener->Listen(DiscoveryTimeoutMs))
    {
      char buffer[1024] = {0};
      SOCKETS::CAddress sender;
      int packetSize = socket->Read(sender, 1024, buffer);
      if (packetSize > -1)
      {
        if (packetSize > 0)
        {
          CVariant data;
          data = CJSONVariantParser::Parse((const unsigned char*)buffer, packetSize);
          static const std::string ServerPropertyAddress = "Address";
          if (data.isObject() && data.isMember(ServerPropertyAddress))
          {
            EmbyServerInfo embyServerInfo = GetEmbyServerInfo(data[ServerPropertyAddress].asString());
            if (!embyServerInfo.Id.empty())
            {
              CEmbyClientPtr client(new CEmbyClient());
              if (client->Init(data, sender.Address()))
              {
                if (AddClient(client))
                {
                  CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server found via GDM %s", client->GetServerName().c_str());
                }
                else if (GetClient(client->GetUuid()) == nullptr)
                {
                  // lost client
                  CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers Server was lost %s", client->GetServerName().c_str());
                }
                else if (UpdateClient(client))
                {
                  // client exists and something changed
                  CLog::Log(LOGNOTICE, "CEmbyServices::CheckEmbyServers presence changed %s", client->GetServerName().c_str());
                }
              }
            }
          }
        }
      }
    }
    if (foundNewClient)
      AddJob(new CEmbyServiceJob(0, "FoundNewClient"));
  }
}
*/

EmbyServerInfo CEmbyServices::GetEmbyServerInfo(const std::string &ipAddress)
{
  EmbyServerInfo serverInfo;

  XFILE::CCurlFile emby;
  emby.SetTimeout(10);
  emby.SetRequestHeader("Cache-Control", "no-cache");
  emby.SetRequestHeader("Content-Type", "application/json");

  CURL url("emby/system/info/public");
  url.SetPort(8096);
  url.SetProtocol("http");
  url.SetHostName(ipAddress);

  std::string path = url.Get();
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
  serverInfo.Version = responseObj[ServerPropertyName].asString();
  serverInfo.ServerName = responseObj[ServerPropertyVersion].asString();
  serverInfo.WanAddress = responseObj[ServerPropertyWanAddress].asString();
  serverInfo.LocalAddress = responseObj[ServerPropertyLocalAddress].asString();
  serverInfo.OperatingSystem = responseObj[ServerPropertyOperatingSystem].asString();
  return serverInfo;
}

CEmbyClientPtr CEmbyServices::GetClient(std::string uuid)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == uuid)
      return client;
  }

  return nullptr;
}

bool CEmbyServices::ClientIsLocal(std::string path)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (StringUtils::StartsWithNoCase(client->GetUrl(), path))
      return client->IsLocal();
  }
  
  return false;
}

bool CEmbyServices::AddClient(CEmbyClientPtr foundClient)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    // do not add existing clients
    if (client->GetUuid() == foundClient->GetUuid())
      return false;
  }

  // only add new clients that are present
  if (foundClient->GetPresence() && foundClient->ParseViews(EmbyViewParsing::newView))
  {
    m_clients.push_back(foundClient);
    m_hasClients = !m_clients.empty();
    return true;
  }

  return false;
}

bool CEmbyServices::RemoveClient(CEmbyClientPtr lostClient)
{
  CSingleLock lock(m_criticalClients);
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

bool CEmbyServices::UpdateClient(CEmbyClientPtr updateClient)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == updateClient->GetUuid())
    {
      // client needs updating
      if (client->GetPresence() != updateClient->GetPresence())
      {
        client->SetPresence(updateClient->GetPresence());
        // update any gui lists here.
        for (const auto &item : client->GetViewItems())
        {
          std::string name = client->FindViewName(item->GetPath());
          if (!name.empty())
          {
            item->SetLabel(client->FormatContentTitle(name));
            CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, item);
            g_windowManager.SendThreadMessage(msg);
          }
        }
        return true;
      }
      // no need to look further but an update was not needed
      return false;
    }
  }

  return false;
}
