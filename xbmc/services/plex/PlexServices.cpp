/*
 *      Copyright (C) 2016 Team MrMC
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

#include "PlexServices.h"

#include "Application.h"
#include "URL.h"
#include "Util.h"
#include "GUIUserMessages.h"
#include "addons/Skin.h"
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
#include "network/WakeOnAccess.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "profiles/dialogs/GUIDialogLockSettings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/StringHasher.h"
#include "utils/JobManager.h"

#include "utils/JSONVariantParser.h"
#include "utils/SystemInfo.h"
#include "utils/Variant.h"
#include "utils/XMLUtils.h"

#include "PlexUtils.h"
#include "PlexClient.h"

#include <regex>

using namespace ANNOUNCEMENT;

static const int NS_PLEX_MEDIA_SERVER_PORT(32414);
static const std::string NS_BROADCAST_ADDR("239.0.0.250");
static const std::string NS_SEARCH_MSG("M-SEARCH * HTTP/1.1\r\n");
static const std::string NS_PLEXTV_URL("https://plex.tv");

class CPlexServiceJob: public CJob
{
public:
  CPlexServiceJob(double currentTime, std::string strFunction,std::string strUUID="")
  : m_function(strFunction)
  , m_strUUID(strUUID)
  , m_currentTime(currentTime)
  {
  }
  virtual ~CPlexServiceJob()
  {
  }
  virtual bool DoWork()
  {
    if (m_function == "UpdateLibraries")
    {
      CLog::Log(LOGNOTICE, "CPlexServiceJob: UpdateLibraries");
      CPlexServices::GetInstance().UpdateLibraries(true);
    }
    else if (m_function == "FoundNewClient")
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);

      // announce that we have a plex client and that recently added should be updated
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


CPlexServices::CPlexServices()
: CThread("PlexServices")
, m_gdmListener(nullptr)
, m_updateMins(0)
, m_playState(MediaServicesPlayerState::stopped)
, m_hasClients(false)
{
  // register our redacted items with CURL
  // we do not want these exposed in mrmc.log.
  if (!CURL::HasRedactedKey("X-Plex-Token"))
    CURL::SetRedactedKey("X-Plex-Token", "PLEXTOKEN");

  CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CPlexServices::~CPlexServices()
{
  CAnnouncementManager::GetInstance().RemoveAnnouncer(this);

  if (IsRunning())
    Stop();

  CancelJobs();
  SAFE_DELETE(m_gdmListener);
}

CPlexServices& CPlexServices::GetInstance()
{
  static CPlexServices sPlexServices;
  return sPlexServices;
}

void CPlexServices::Start()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    Stop();
  CThread::Create();
}

void CPlexServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
  {
    m_bStop = true;
    {
      // CPlexServices::Process controls create/delete life cycle
      // lock access to m_plextv.
      CSingleLock plextvLock(m_plextvCritical);
      if (m_plextv)
        m_plextv->Cancel();
    }
    m_processSleep.Set();
    StopThread();
  }

  g_directoryCache.Clear();
  CSingleLock lock2(m_criticalClients);
  m_clients.clear();
  m_gdmListener = nullptr;
  m_updateMins = 0;
  m_playState = MediaServicesPlayerState::stopped;
  m_hasClients = false;
}

bool CPlexServices::IsActive()
{
  return IsRunning();
}

bool CPlexServices::IsEnabled()
{
  return (!CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER).empty() ||
           CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER));
}

bool CPlexServices::HasClients() const
{
  return m_hasClients;
}

void CPlexServices::GetClients(std::vector<CPlexClientPtr> &clients) const
{
  CSingleLock lock(m_criticalClients);
  clients = m_clients;
}

CPlexClientPtr CPlexServices::FindClient(const std::string &path)
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

void CPlexServices::OnSettingAction(const CSetting *setting)
{
  if (setting == nullptr)
    return;

  bool startThread = false;
  std::string strMessage;
  std::string strSignIn = g_localizeStrings.Get(1240);
  std::string strSignOut = g_localizeStrings.Get(1241);
  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXSIGNIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNIN) == strSignIn)
    {
      // prompt is 'sign-in'
      std::string user;
      std::string pass;
      std::string module;
      bool saveDetails = false;
      if (CGUIDialogLockSettings::ShowAndGetUserAndPassword(user, pass, module, &saveDetails, true))
      {
        if (!user.empty() && !pass.empty())
        {
          if (GetPlexToken(user, pass))
          {
            // change prompt to 'sign-out'
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, strSignOut);
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, m_myHomeUser);
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, m_myHomeUserThumb);
            CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction manual sign-in ok");
            startThread = true;
          }
          else
          {
            strMessage = "Could not get authToken via manual sign-in";
            CLog::Log(LOGERROR, "CPlexServices: %s", strMessage.c_str());
          }
        }
        else
        {
          // opps, nuke'em all
          CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction user/pass are empty");
          m_authToken.clear();
        }
      }
    }
    else
    {
      // prompt is 'sign-out'
      // clear authToken and change prompt to 'sign-in'
      m_authToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, strSignIn);
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, "");
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, "");
      CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction sign-out ok");
      std::string serverType = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_TYPE);
      if (serverType == "plex")
      {
        CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"");
        CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
        CSettings::GetInstance().Save();
      }
    }
    SetUserSettings();

    if (startThread || m_useGDMServer)
      Start();
    else
      Stop();

  }
  else if (settingId == CSettings::SETTING_SERVICES_PLEXSIGNINPIN)
  {
    InitiateSignIn();
  }
  else if (settingId == CSettings::SETTING_SERVICES_PLEXHOMEUSER)
  {
    // user must be in 'sign-in' state so check for 'sign-out' label
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNIN) == strSignOut ||
        CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN) == strSignOut)
    {
      std::string homeUserName;
      if (GetMyHomeUsers(homeUserName))
      {
        m_myHomeUser = homeUserName;
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, m_myHomeUser);
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, m_myHomeUserThumb);
        SetUserSettings();
        CSingleLock lock(m_criticalClients);
        m_clients.clear();
        Start();
      }
    }
  }
}

void CPlexServices::InitiateSignIn()
{
  std::string strMessage;
  bool startThread = false;
  std::string strSignIn = g_localizeStrings.Get(1240);
  std::string strSignOut = g_localizeStrings.Get(1241);
  if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN) == strSignIn)
  {
    if (GetSignInPinCode())
    {
      // change prompt to 'sign-out'
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN, strSignOut);
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, m_myHomeUser);
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, m_myHomeUserThumb);
      CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction pin sign-in ok");
      startThread = true;
    }
    else
    {
      strMessage = "Could not get authToken via pin request sign-in";
      CLog::Log(LOGERROR, "CPlexServices: %s", strMessage.c_str());
    }
  }
  else
  {
    // prompt is 'sign-out'
    // clear authToken and change prompt to 'sign-in'
    m_authToken.clear();
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN, strSignIn);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, "");
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, "");
    CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction sign-out ok");
    std::string serverType = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_TYPE);
    if (serverType == "plex")
    {
      CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"");
      CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
      CSettings::GetInstance().Save();
    }
  }
  SetUserSettings();
  
  
  if (startThread || m_useGDMServer)
    Start();
  else
  {
    if (!strMessage.empty())
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
    Stop();
  }
}

void CPlexServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
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
  else if ((flag & AnnouncementFlag::Other) && strcmp(sender, "plex") == 0)
  {
    if (strcmp(message, "UpdateLibrary") == 0)
    {
      AddJob(new CPlexServiceJob(0, "UpdateLibraries"));
    }
    else if (strcmp(message, "ReloadProfiles") == 0)
    {
      // restart if we MrMC profiles has changed
      Stop();
      Start();
    }
  }
}

void CPlexServices::OnSettingChanged(const CSetting *setting)
{
  // All Plex settings so far
  /*
  static const std::string SETTING_SERVICES_PLEXSIGNIN;
  static const std::string SETTING_SERVICES_PLEXSIGNINPIN;
  static const std::string SETTING_SERVICES_PLEXHOMEUSER;
  static const std::string SETTING_SERVICES_PLEXGDMSERVER;
  static const std::string SETTING_SERVICES_PLEXMYPLEXAUTH;
  */

  if (setting == NULL)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXGDMSERVER)
  {
    m_useGDMServer = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER);
    // start or stop the service
    if (m_useGDMServer || MyPlexSignedIn())
      Start();
    else
      Stop();
  }
  else if (settingId == CSettings::SETTING_SERVICES_PLEXUPDATEMINS)
  {
    int oldUpdateMins = m_updateMins;
    m_updateMins = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_PLEXUPDATEMINS);
    if (IsRunning())
    {
      if (oldUpdateMins > 0 && m_updateMins == 0)
      {
        // switch to no caching
        g_directoryCache.Clear();
      }
      if (m_playState == MediaServicesPlayerState::stopped)
      {
        CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
        g_windowManager.SendThreadMessage(msg);
      }
    }
  }
}

void CPlexServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH, m_authToken);
  CSettings::GetInstance().Save();
}

void CPlexServices::GetUserSettings()
{
  m_authToken  = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH);
  m_updateMins  = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_PLEXUPDATEMINS);
  m_useGDMServer = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER);
  m_myHomeUser = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER);
  m_myHomeUserThumb = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB);
}

void CPlexServices::UpdateLibraries(bool forced)
{
  CSingleLock lock(m_criticalClients);
  bool clearDirCache = false;
  for (const auto &client : m_clients)
  {
    client->ParseSections(PlexSectionParsing::checkSection);
    if (forced || client->NeedUpdate())
    {
      client->ParseSections(PlexSectionParsing::updateSection);
      clearDirCache = true;
    }
  }
  if (clearDirCache)
  {
    g_directoryCache.Clear();
    if (m_playState == MediaServicesPlayerState::stopped)
    {
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);
    }
  }
}

bool CPlexServices::MyPlexSignedIn()
{
  return !m_authToken.empty();
}

void CPlexServices::Process()
{
  CLog::Log(LOGDEBUG, "CPlexServices::Process bgn");
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);

  // This gets started when network comes up but we need to
  // wait until gui is up before poking for emby servers
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

  {
    // CPlexServices::Stop checks m_plextv, lock access to it during create
    CSingleLock plextvLock(m_plextvCritical);
    m_plextv = new XFILE::CCurlFile();
    m_plextv->SetTimeout(20);
  //m_plextv.SetBufferSize(32768*10);
  }

  CStopWatch gdmTimer;
  gdmTimer.StartZero();
  while (!m_bStop)
  {
    if (g_sysinfo.HasInternet())
    {
      // check that we have any internet access (for iOS devices not on wifi)
      CLog::Log(LOGDEBUG, "CPlexServices::Process has gateway1");
      break;
    }
    else
    {
      // else check that we have any sort of network access (ie. local only)
      CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
      if (iface && iface->IsConnected())
      {
        in_addr_t router = inet_addr(iface->GetCurrentDefaultGateway().c_str());
        if (router != INADDR_NONE && g_application.getNetwork().PingHost(router, 0, 1000))
        {
          CLog::Log(LOGDEBUG, "CPlexServices::Process has gateway2");
          break;
        }
      }
    }
    std::string ip;
    if (CDNSNameCache::Lookup("plex.com", ip))
    {
      in_addr_t plexdotcom = inet_addr(ip.c_str());
      if (g_application.getNetwork().PingHost(plexdotcom, 0, 1000))
      {
        CLog::Log(LOGDEBUG, "CPlexServices::Process has gateway3");
        break;
      }
      if (gdmTimer.GetElapsedSeconds() > 5)
      {
        if (m_playState == MediaServicesPlayerState::stopped)
          CheckForGDMServers();
        gdmTimer.Reset();
      }
    }

    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  CPlexUtils::GetDefaultHeaders(m_plextv);
  int plextvTimeoutSeconds = 5;

  // try plex.tv first
  if (MyPlexSignedIn())
  {
    bool includeHttps = true;
    GetMyPlexServers(includeHttps);
    plextvTimeoutSeconds = 60 * 15;
  }
  // the via GDM
  CheckForGDMServers();

  CStopWatch plextvTimer, checkUpdatesTimer;
  gdmTimer.StartZero();
  plextvTimer.StartZero();
  checkUpdatesTimer.StartZero();
  while (!m_bStop)
  {
    // check for services every N seconds
    if (plextvTimer.GetElapsedSeconds() > plextvTimeoutSeconds)
    {
      // try plex.tv
      if (MyPlexSignedIn())
      {
        if (m_playState == MediaServicesPlayerState::stopped)
        {
          // if we get back servers, then
          // reduce the initial polling time
          bool foundSomething = false;
          foundSomething = GetMyPlexServers(true);
          if (foundSomething)
            plextvTimeoutSeconds = 60 * 15;
        }
      }
      plextvTimer.Reset();
    }

    if (gdmTimer.GetElapsedSeconds() > 5)
    {
      if (m_playState == MediaServicesPlayerState::stopped)
        CheckForGDMServers();
      gdmTimer.Reset();
    }

    if (m_updateMins > 0 && (checkUpdatesTimer.GetElapsedSeconds() > (60 * m_updateMins)))
    {
      if (m_playState == MediaServicesPlayerState::stopped)
        UpdateLibraries(false);
      checkUpdatesTimer.Reset();
    }

    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }

  if (m_gdmListener)
  {
    // before deleting listener, fetch and delete any sockets it uses.
    SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_gdmListener->GetFirstSocket();
    // we should not have to do the close,
    // delete 'should' do it.
    socket->Close();
    SAFE_DELETE(socket);
    SAFE_DELETE(m_gdmListener);
  }

  {
    // CPlexServices::Stop checks m_plextv, lock access to it during delete
    CSingleLock plextvLock(m_plextvCritical);
    SAFE_DELETE(m_plextv);
  }
  CLog::Log(LOGDEBUG, "CPlexServices::Process end");
}

bool CPlexServices::GetPlexToken(std::string user, std::string pass)
{
  bool rtn = false;
  XFILE::CCurlFile plex;
  plex.SetTimeout(20);
  CPlexUtils::GetDefaultHeaders(&plex);

  CURL url(NS_PLEXTV_URL + "/users/sign_in.json");
  url.SetUserName(user);
  url.SetPassword(pass);

  std::string response;
  std::string strPostData;
  if (plex.Post(url.Get(), strPostData, response))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: myPlex %s", strResponse.c_str());

    CVariant reply;
    if (!CJSONVariantParser::Parse(response, reply))
      return rtn;

    CVariant user = reply["user"];
    m_authToken = user["authentication_token"].asString();

    std::string homeUserName;
    if (GetMyHomeUsers(homeUserName))
      m_myHomeUser = homeUserName;

    rtn = true;
  }
  else
  {
    std::string strMessage = "Could not connect to retreive PlexToken";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
    CLog::Log(LOGERROR, "CPlexServices:FetchPlexToken failed %s", response.c_str());
  }

  return rtn;
}

bool CPlexServices::GetMyPlexServers(bool includeHttps)
{
  bool rtn = false;

  std::vector<PlexServerInfo> serversFound;

  if (MyPlexSignedIn())
    m_plextv->SetRequestHeader("X-Plex-Token", m_authToken);

  std::string strResponse;
  CURL url(NS_PLEXTV_URL);
  if (includeHttps)
    url.SetFileName("pms/resources?includeHttps=1");
  else
    url.SetFileName("pms/resources?includeHttps=0");

  if (m_plextv->Get(url.Get(), strResponse))
  {
#if defined(PLEX_DEBUG_VERBOSE)
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
          PlexServerInfo plexServerInfo = ParsePlexDeviceNode(DeviceNode);
          serversFound.push_back(plexServerInfo);
        }
        DeviceNode = DeviceNode->NextSiblingElement("Device");
      }
    }
  }
  else
  {
    std::string strMessage = "Error getting Plex servers";
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
    CLog::Log(LOGDEBUG, "CPlexServices::GetMyPlexServers failed %s, code %d", strResponse.c_str(), m_plextv->GetResponseCode());
    return false;
  }

  std::vector<CPlexClientPtr> lostClients;
  if (!serversFound.empty())
  {
    for (const auto &server : serversFound)
    {
      // ignore clients we know about.
      if (GetClient(server.uuid))
        continue;

      // new client that we do not know about, create and add it.
      CPlexClientPtr client(new CPlexClient());
      if (client->Init(server))
      {
        // always return true if we find anything
        rtn = true;
        if (AddClient(client))
        {
          // new client
          CLog::Log(LOGNOTICE, "CPlexServices: Server found via plex.tv %s", client->GetServerName().c_str());
          AddJob(new CPlexServiceJob(0, "FoundNewClient", client->GetUuid()));
        }
        else if (GetClient(client->GetUuid()) == nullptr)
        {
          // lost client
          lostClients.push_back(client);
          CLog::Log(LOGNOTICE, "CPlexServices: Server was lost %s", client->GetServerName().c_str());
        }
      }
    }
  }

  if (!lostClients.empty())
  {
    for (const auto &lostclient : lostClients)
      RemoveClient(lostclient);
  }

  return rtn;
}

bool CPlexServices::GetSignInPinCode()
{
  // on return, show user m_signInByPinCode so they can enter it at https://plex.tv/link

  bool rtn = false;

  std::string id;
  std::string code;
  std::string clientid;
  CDateTime   expiresAt;

  XFILE::CCurlFile plex;
  // use a lower default timeout
  plex.SetTimeout(20);
  CPlexUtils::GetDefaultHeaders(&plex);

  CURL url(NS_PLEXTV_URL + "/pins.xml");

  std::string strResponse;
  std::string strMessage;
  if (plex.Post(url.Get(), "", strResponse))
  {
#if defined(PLEX_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CPlexServices:FetchSignInPin %s", strResponse.c_str());
#endif

    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* pinNode = xml.RootElement();
    if (pinNode)
    {
      for (TiXmlElement *elem = pinNode->FirstChildElement(); elem; elem = elem->NextSiblingElement())
      {
        if (elem->GetText() == nullptr)
          continue;

        if (elem->ValueStr() == "id")
          id = elem->GetText();
        else if (elem->ValueStr() == "code")
          code = elem->GetText();
        else if (elem->ValueStr() == "client-identifier")
          clientid = elem->GetText();
        else if (elem->ValueStr() == "expires-at")
        {
          std::string date = elem->GetText();
          date = std::regex_replace(date, std::regex("T"), " ");
          date = std::regex_replace(date, std::regex("Z"), "");
          expiresAt.SetFromDBDateTime(date);
        }
      }
      m_signInByPinId = id;
      m_signInByPinCode = code;
      strMessage = "Failed to get ID or Code";
      rtn = !m_signInByPinId.empty() && !m_signInByPinCode.empty();
    }

    CGUIDialogProgress *waitPinReplyDialog;
    waitPinReplyDialog = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
    waitPinReplyDialog->SetHeading(g_localizeStrings.Get(1246));
    waitPinReplyDialog->SetLine(0, g_localizeStrings.Get(1248));
    std::string prompt = g_localizeStrings.Get(1249) + code;
    waitPinReplyDialog->SetLine(1, prompt);

    waitPinReplyDialog->Open();
    waitPinReplyDialog->ShowProgressBar(true);

    CStopWatch dieTimer;
    dieTimer.StartZero();
    int timeToDie = 60 * 5;

    CStopWatch pingTimer;
    pingTimer.StartZero();

    m_authToken.clear();
    while (!waitPinReplyDialog->IsCanceled())
    {
      waitPinReplyDialog->SetPercentage(int(float(dieTimer.GetElapsedSeconds())/float(timeToDie)*100));
      waitPinReplyDialog->Progress();
      if (pingTimer.GetElapsedSeconds() > 1)
      {
        // wait for user to run and enter pin code
        // at https://plex.tv/link
        if (GetSignInByPinReply())
          break;
        pingTimer.Reset();
      }

      if (dieTimer.GetElapsedSeconds() > timeToDie)
      {
        rtn = false;
        break;
      }
    }
    waitPinReplyDialog->Close();

    if (m_authToken.empty())
    {
      strMessage = "Error extracting AuthToken";
      CLog::Log(LOGERROR, "CPlexServices:FetchSignInPin failed to get authToken");
      rtn = false;
    }
    else
    {
      std::string homeUserName;
      if (GetMyHomeUsers(homeUserName))
      {
        m_myHomeUser = homeUserName;
        rtn = true;
      }
      else
        rtn = false;
    }
  }
  else
  {
    strMessage = "Could not connect to retreive AuthToken";
    CLog::Log(LOGERROR, "CPlexServices:FetchSignInPin failed %s", strResponse.c_str());
  }
  if (!rtn)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
  return rtn;
}

bool CPlexServices::GetSignInByPinReply()
{
  // repeat called until we timeout or get authToken
  bool rtn = false;
  std::string strMessage;
  XFILE::CCurlFile plex;
  plex.SetTimeout(20);
  CPlexUtils::GetDefaultHeaders(&plex);

  std::string path = NS_PLEXTV_URL + "/pins/" + m_signInByPinId + ".xml";
  CURL url(path);

  std::string strResponse;
  if (plex.Get(url.Get(), strResponse))
  {
#if defined(PLEX_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CPlexServices:WaitForSignInByPin %s", strResponse.c_str());
#endif
    TiXmlDocument xml;
    xml.Parse(strResponse.c_str());

    TiXmlElement* pinNode = xml.RootElement();
    if (pinNode)
    {
      for (TiXmlElement *elem = pinNode->FirstChildElement(); elem; elem = elem->NextSiblingElement())
      {
        if (elem->GetText() == nullptr)
          continue;

        if (elem->ValueStr() == "auth_token")
          m_authToken = elem->GetText();
      }
      rtn = !m_authToken.empty();
    }
  }
  else
  {
    CLog::Log(LOGERROR, "CPlexServices:WaitForSignInByPin failed %s", strResponse.c_str());
  }
  return rtn;
}

void CPlexServices::CheckForGDMServers()
{
  if (m_useGDMServer)
  {
    if (!m_gdmListener)
    {
      SOCKETS::CUDPSocket *socket = SOCKETS::CSocketFactory::CreateUDPSocket();
      if (socket)
      {
        CNetworkInterface *iface = g_application.getNetwork().GetFirstConnectedInterface();
        if (iface && iface->IsConnected())
        {
          SOCKETS::CAddress my_addr;
          my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
          if (!socket->Bind(my_addr, NS_PLEX_MEDIA_SERVER_PORT, 0))
          {
            CLog::Log(LOGERROR, "CPlexServices:CheckforGDMServers Could not listen on port %d", NS_PLEX_MEDIA_SERVER_PORT);
            SAFE_DELETE(m_gdmListener);
            m_useGDMServer = false;
            return;
          }

          if (socket)
          {
            socket->SetBroadCast(true);
            // create and add our socket to the 'select' listener
            m_gdmListener = new SOCKETS::CSocketListener();
            m_gdmListener->AddSocket(socket);
          }
        }
        else
        {
          SAFE_DELETE(socket);
        }
      }
      else
      {
        CLog::Log(LOGERROR, "CPlexServices:CheckforGDMServers Could not create socket for GDM");
        m_useGDMServer = false;
        return;
      }
    }

    SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_gdmListener->GetFirstSocket();
    if (socket)
    {
      SOCKETS::CAddress discoverAddress;
      discoverAddress.SetAddress(NS_BROADCAST_ADDR.c_str(), NS_PLEX_MEDIA_SERVER_PORT);
      std::string discoverMessage = NS_SEARCH_MSG;
      int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
      if (packetSize < 0)
        CLog::Log(LOGERROR, "CPlexServices:CPlexServices:CheckforGDMServers discover send failed");
    }

    bool foundNewClient = false;
    std::string uuid;
    // listen for GDM reply until we timeout
    if (socket && m_gdmListener->Listen(250))
    {
      char buffer[1024] = {0};
      SOCKETS::CAddress sender;
      int packetSize = socket->Read(sender, 1024, buffer);
      if (packetSize > -1)
      {
        std::string buf(buffer, packetSize);
        if (buf.find("200 OK") != std::string::npos)
        {
          CPlexClientPtr client(new CPlexClient());
          if (client->Init(buf, sender.Address()))
          {
            if (AddClient(client))
            {
              CLog::Log(LOGNOTICE, "CPlexServices:CheckforGDMServers Server found via GDM %s", client->GetServerName().c_str());
              uuid = client->GetUuid();
            }
            else if (GetClient(client->GetUuid()) == nullptr)
            {
              // lost client
              CLog::Log(LOGNOTICE, "CPlexServices:CheckforGDMServers Server was lost %s", client->GetServerName().c_str());
            }
          }
        }
      }
    }
    if (foundNewClient)
      AddJob(new CPlexServiceJob(0, "FoundNewClient",uuid));
  }
}

CPlexClientPtr CPlexServices::GetClient(std::string uuid)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == uuid)
      return client;
  }

  return nullptr;
}

CPlexClientPtr CPlexServices::GetFirstClient()
{
  CSingleLock lock(m_criticalClients);
  if (m_clients.size() > 0)
    return m_clients[0];
  else
    return nullptr;
}

bool CPlexServices::ClientIsLocal(std::string path)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (StringUtils::StartsWithNoCase(client->GetUrl(), path))
      return client->IsLocal();
  }
  
  return false;
}

PlexServerInfo CPlexServices::ParsePlexDeviceNode(const TiXmlElement* DeviceNode)
{
  PlexServerInfo serverInfo;

  serverInfo.uuid = XMLUtils::GetAttribute(DeviceNode, "clientIdentifier");
  serverInfo.owned = XMLUtils::GetAttribute(DeviceNode, "owned");
  serverInfo.presence = XMLUtils::GetAttribute(DeviceNode, "presence");
  serverInfo.platform = XMLUtils::GetAttribute(DeviceNode, "platform");
  serverInfo.serverName = XMLUtils::GetAttribute(DeviceNode, "name");
  serverInfo.accessToken = XMLUtils::GetAttribute(DeviceNode, "accessToken");
  serverInfo.httpsRequired = XMLUtils::GetAttribute(DeviceNode, "httpsRequired");
  serverInfo.publicAdrressMatch = XMLUtils::GetAttribute(DeviceNode, "publicAddressMatches") == "1" ? true : false;

  const TiXmlElement* ConnectionNode = DeviceNode->FirstChildElement("Connection");
  while (ConnectionNode)
  {
    PlexConnection connection;
    connection.port = XMLUtils::GetAttribute(ConnectionNode, "port");
    connection.address = XMLUtils::GetAttribute(ConnectionNode, "address");
    connection.protocol = XMLUtils::GetAttribute(ConnectionNode, "protocol");
    connection.external = XMLUtils::GetAttribute(ConnectionNode, "local") == "0" ? 1 : 0;
    connection.uri = XMLUtils::GetAttribute(ConnectionNode, "uri");
    serverInfo.connections.push_back(connection);

    ConnectionNode = ConnectionNode->NextSiblingElement("Connection");
  }
  // sort so that all external=0 are first. These are the local connections.
  std::sort(serverInfo.connections.begin(), serverInfo.connections.end(),
    [] (PlexConnection const& a, PlexConnection const& b) { return a.external < b.external; });

  return serverInfo;
}

bool CPlexServices::HasClient(const std::string &uuid)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    if (client->GetUuid() == uuid)
      return true;
  }
  return false;
}

bool CPlexServices::AddClient(CPlexClientPtr foundClient)
{
  CSingleLock lock(m_criticalClients);
  for (const auto &client : m_clients)
  {
    // do not add existing clients
    if (client->GetUuid() == foundClient->GetUuid())
      return false;
  }

  CWakeOnAccess::GetInstance().WakeUpHost(foundClient->GetHost(), "Plex Server");

  // only add new clients that are present
  if (foundClient->GetPresence())
  {
    std::string uuid = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
    if (uuid.empty() || uuid == foundClient->GetUuid() || (g_SkinInfo && !g_SkinInfo->IsDynamicHomeCompatible()))
      foundClient->ParseSections(PlexSectionParsing::newSection);
    m_clients.push_back(foundClient);
    m_hasClients = !m_clients.empty();
    return true;
  }

  return false;
}

bool CPlexServices::RemoveClient(CPlexClientPtr lostClient)
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

bool CPlexServices::GetMyHomeUsers(std::string &homeUserName)
{
  bool rtn = false;

  std::string strMessage;
  XFILE::CCurlFile plex;
  plex.SetTimeout(20);
  //plex.SetBufferSize(32768*10);
  CPlexUtils::GetDefaultHeaders(&plex);
  if (MyPlexSignedIn())
    plex.SetRequestHeader("X-Plex-Token", m_authToken);

  std::string strResponse;
  CURL url(NS_PLEXTV_URL + "/api/home/users");
  if (plex.Get(url.Get(), strResponse))
  {
#if defined(PLEX_DEBUG_VERBOSE)
    CLog::Log(LOGDEBUG, "CPlexServices:GetMyHomeUsers %s", strResponse.c_str());
#endif

    TiXmlDocument xml;
    CFileItemList plexUsers;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      std::string users = XMLUtils::GetAttribute(MediaContainer, "size");
      if (atoi(users.c_str()) == 1)
      {
        // if we only have one user show the name of it
        const TiXmlElement* UserNode = MediaContainer->FirstChildElement("User");
        homeUserName = XMLUtils::GetAttribute(UserNode, "title");
        // plex service is enabled and home user has been selected,save server type for home selection
        CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"plex");
        CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
        CSettings::GetInstance().Save();
        return true;
      }
      else if (atoi(users.c_str()) > 1)
      {
        const TiXmlElement* UserNode = MediaContainer->FirstChildElement("User");
        while (UserNode)
        {
          CFileItemPtr plexUser(new CFileItem());
          // set m_bIsFolder to true to indicate we are tvshow list
          plexUser->m_bIsFolder = true;
          plexUser->SetProperty("title", XMLUtils::GetAttribute(UserNode, "title"));
          plexUser->SetProperty("uuid", XMLUtils::GetAttribute(UserNode, "uuid"));
          plexUser->SetProperty("id", XMLUtils::GetAttribute(UserNode, "id"));
          plexUser->SetProperty("protected", XMLUtils::GetAttribute(UserNode, "protected"));
          plexUser->SetLabel(XMLUtils::GetAttribute(UserNode, "title"));
          plexUser->SetIconImage(XMLUtils::GetAttribute(UserNode, "thumb") + "&s=512");
          plexUser->SetArt("thumb", XMLUtils::GetAttribute(UserNode, "thumb") + "&s=512");
          plexUsers.Add(plexUser);
          UserNode = UserNode->NextSiblingElement("User");
        }
      }
      else
      {
        return false;
      }
    }
    plexUsers.SetContent("users");
    CGUIDialogSelect *dialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    if (dialog == NULL)
      return false;

    dialog->Reset();
    dialog->SetHeading("Users");
    dialog->SetItems(plexUsers);
    dialog->SetMultiSelection(false);
    dialog->SetUseDetails(true);
    dialog->Open();

    if (!dialog->IsConfirmed())
      return false;

    const CFileItemPtr item = dialog->GetSelectedFileItem();

    if (item == NULL || !item->HasProperty("id"))
      return false;

    std::string pinUrl = "/switch";
    if (item->GetProperty("protected").asBoolean())
    {
      std::string pin;
      if( !CGUIDialogNumeric::ShowAndGetNumber(pin, "Enter pin", 0, true) )
        return false;
      pinUrl = "/switch?pin=" + pin;
    }

    XFILE::CCurlFile plex;
    plex.SetTimeout(20);
    CPlexUtils::GetDefaultHeaders(&plex);
    if (MyPlexSignedIn())
      plex.SetRequestHeader("X-Plex-Token", m_authToken);

    std::string id = item->GetProperty("id").asString();
    CURL url(NS_PLEXTV_URL + "/api/home/users/" + id + pinUrl);

    CPlexUtils::GetDefaultHeaders(&plex);
    std::string strResponse;
    plex.Post(url.Get(), "", strResponse);

    TiXmlDocument xml1;
    xml1.Parse(strResponse.c_str());

    TiXmlElement* userContainer = xml1.RootElement();
    if (userContainer)
    {
      m_authToken = XMLUtils::GetAttribute(userContainer, "authToken");
      m_myHomeUserThumb = XMLUtils::GetAttribute(userContainer, "thumb") + "&s=512";
      homeUserName = XMLUtils::GetAttribute(userContainer, "title");
      rtn = !homeUserName.empty() && !m_authToken.empty();
    }
    else
    {
      strMessage = "Couldn't get home users";
    }
  }
  else
  {
    strMessage = "Could not connect to retreive Home users";
    CLog::Log(LOGDEBUG, "CPlexServices:GetMyHomeUsers failed %s", strResponse.c_str());
  }

  if (!rtn)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Plex Services", strMessage, 3000, true);
  else
  {
    // plex service is enabled and home user has been selected,save server type for home selection
    CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_TYPE,"plex");
    CSettings::GetInstance().SetString(CSettings::SETTING_GENERAL_SERVER_UUID,"");
    CSettings::GetInstance().Save();
  }
  return rtn;
}

std::string CPlexServices::PickHomeUser()
{
  std::string homeUserName;
  if (GetMyHomeUsers(homeUserName))
  {
    m_myHomeUser = homeUserName;
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSER, m_myHomeUser);
    CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERTHUMB, m_myHomeUserThumb);
    SetUserSettings();
    CSingleLock lock(m_criticalClients);
    m_clients.clear();
    Start();
    return homeUserName;
  }
  return "";
}

bool CPlexServices::ParseCurrentServerSections()
{
  std::string uuid = CSettings::GetInstance().GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
  CPlexClientPtr client = GetClient(uuid);;
  if (client)
  {
    client->ParseSections(PlexSectionParsing::updateSection);
    return true;
  }
  return false;
}
