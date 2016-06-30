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
#include "GUIUserMessages.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogNumeric.h"
#include "filesystem/CurlFile.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "profiles/dialogs/GUIDialogLockSettings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include "utils/JSONVariantParser.h"
#include "utils/Variant.h"
#include "utils/XMLUtils.h"

#include "PlexUtils.h"
#include "PlexClient.h"

#include <regex>

using namespace ANNOUNCEMENT;

#define NS_PLEX_MEDIA_SERVER_PORT 32414
#define NS_BROADCAST_ADDR "239.0.0.250"
#define NS_SEARCH_MSG "M-SEARCH * HTTP/1.1\r\n"

CPlexServices::CPlexServices()
: CThread("PlexServices")
{
}

CPlexServices::~CPlexServices()
{
  if (IsRunning())
    Stop();
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
    StopThread();
  CThread::Create();
}

void CPlexServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
    StopThread();
  m_clients.clear();
}

bool CPlexServices::IsActive()
{
  return IsRunning();
}

void CPlexServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
}

void CPlexServices::OnSettingAction(const CSetting *setting)
{
  if (setting == nullptr)
    return;

  bool startThread = false;
  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXSIGNIN)
  {
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNIN) == g_localizeStrings.Get(1240))
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
          m_myPlexUser = user;
          m_myPlexPass = pass;
          if (FetchPlexToken())
          {
            // change prompt to 'sign-out'
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, g_localizeStrings.Get(1241));
            CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERS, m_myHomeUser);
            CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction sign-in ok");
            startThread = true;
          }
          else
          {
            CLog::Log(LOGERROR, "CPlexServices: Could not get authToken");
          }
        }
        else
        {
          // opps, nuke'em all
          CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction user/pass are empty");
          m_myPlexUser.clear();
          m_myPlexPass.clear();
          m_myPlexToken.clear();
        }
      }
    }
    else
    {
      // prompt is 'sign-out'
      // clear user/pass/auth and change prompt to 'sign-in'
      m_myPlexUser.clear();
      m_myPlexPass.clear();
      m_myPlexToken.clear();
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXSIGNIN, g_localizeStrings.Get(1240));
      CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERS, "");
      CLog::Log(LOGDEBUG, "CPlexServices:OnSettingAction sign-out ok");
    }
    SetUserSettings();
    const CSetting *userSetting = CSettings::GetInstance().GetSetting(CSettings::SETTING_SERVICES_PLEXHOMEUSERS);
    ((CSettingBool*)userSetting)->SetEnabled(startThread);

    if (startThread)
      Start();
    else
      Stop();
    
  }
  else if (settingId == CSettings::SETTING_SERVICES_PLEXHOMEUSERS)
  {
    // user must be in 'sign-in' state so check for 'sign-out' label
    if (CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXSIGNIN) == g_localizeStrings.Get(1241))
    {
      std::string homeUserName;
      if (GetMyHomeUsers(homeUserName))
      {
        m_myHomeUser = homeUserName;
        CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXHOMEUSERS, m_myHomeUser);
        SetUserSettings();
        m_clients.clear();
        Start();
      }
    }
  }
}

void CPlexServices::OnSettingChanged(const CSetting *setting)
{
  // All Plex settings so far
  /*
   static const std::string SETTING_SERVICES_PLEXSIGNIN;
   static const std::string SETTING_SERVICES_PLEXGDMSERVER;
   static const std::string SETTING_SERVICES_PLEXMYPLEXUSER;
   static const std::string SETTING_SERVICES_PLEXMYPLEXPASS;
   static const std::string SETTING_SERVICES_PLEXMYPLEXAUTH;
   */

  if (setting == NULL)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXGDMSERVER)
  {
    m_useGDMServer = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER);
    // start or stop the service
    if (m_useGDMServer)
      Start();
    else
      Stop();
  }
}

void CPlexServices::SetUserSettings()
{
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER, m_myPlexUser);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS, m_myPlexPass);
  CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH, m_myPlexToken);
  CSettings::GetInstance().Save();
}

void CPlexServices::GetUserSettings()
{
  // false is disabled, true is auto
  m_useGDMServer = CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXGDMSERVER);
  
  m_myPlexUser = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXUSER);
  m_myPlexPass = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXPASS);
  m_myPlexToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_PLEXMYPLEXAUTH);
}

void CPlexServices::Process()
{
  bool hasPlexServers = false;
  GetUserSettings();

  // testing sign-in by Pin
  if (false)
  {
    // fetch a ping request code (should show it to user)
    FetchSignInPin();

    CStopWatch dieTimer;
    dieTimer.StartZero();

    CEvent m_wakeEvent;
    m_wakeEvent.Set();
    while (!m_bStop)
    {
      // wait for user to run and enter pin code
      // at https://plex.tv/pin
      if (WaitForSignInByPin())
        break;

      if (dieTimer.GetElapsedSeconds() > 60 * 5)
        break;

      m_wakeEvent.WaitMSec(2 * 1000);
      m_wakeEvent.Reset();
    }
  }

  CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
  if (iface)
  {
    SOCKETS::CUDPSocket *socket = nullptr;
    SOCKETS::CSocketListener listener;
    if (m_useGDMServer)
    {
      socket = SOCKETS::CSocketFactory::CreateUDPSocket();
      if (socket)
      {
        SOCKETS::CAddress my_addr;
        my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
        if (!socket->Bind(my_addr, NS_PLEX_MEDIA_SERVER_PORT, 0))
        {
          CLog::Log(LOGERROR, "CPlexServices: Could not listen on port %d", NS_PLEX_MEDIA_SERVER_PORT);
          SAFE_DELETE(socket);
        }

        if (socket)
        {
          socket->SetBroadCast(true);
          // add our socket to the 'select' listener
          listener.AddSocket(socket);
          // do an initial broadcast to get things rolling
          SendDiscoverBroadcast(socket);
        }
      }
      else
        CLog::Log(LOGERROR, "CPlexServices: Could not create socket for GDM");
    }

    // try plex.tv
    if (!m_myPlexToken.empty() && !hasPlexServers)
      hasPlexServers = FetchMyPlexServers();

    CStopWatch idleTimer;
    idleTimer.StartZero();
    while (!m_bStop)
    {
      // recheck services every N seconds
      if (idleTimer.GetElapsedMilliseconds() > 5000)
      {
        // try plex.tv
        if (!m_myPlexToken.empty() && !hasPlexServers)
          hasPlexServers = FetchMyPlexServers();

        // check GDM
        if (socket)
          SendDiscoverBroadcast(socket);

        idleTimer.Reset();
      }

      // listen for GDM reply until we timeout
      if (socket && listener.Listen(250))
      {
        char buffer[1024] = {0};
        SOCKETS::CAddress sender;
        int packetSize = socket->Read(sender, 1024, buffer);
        if (packetSize > -1)
        {
          std::string buf(buffer, packetSize);
          if (buf.find("200 OK") != std::string::npos)
          {
            CPlexClient newClient(buf, sender.Address());
            if (AddClient(newClient))
            {
              CLog::Log(LOGNOTICE, "CPlexServices: Server found via GDM %s", sender.Address());
            }
            else if (GetClient(newClient.m_uuid))
            {
              GetClient(newClient.m_uuid)->ParseData(buf, sender.Address());
              CLog::Log(LOGDEBUG, "CPlexServices: Server updated via GDM %s", sender.Address());
            }
          }
        }
      }
      usleep(250 * 1000);
    }

    if (socket)
      SAFE_DELETE(socket);
  }
}

bool CPlexServices::FetchPlexToken()
{
  bool rtn = false;
  XFILE::CCurlFile plex;
  CPlexUtils::GetDefaultHeaders(plex);

  CURL url("https://plex.tv/users/sign_in.json");
  url.SetUserName(m_myPlexUser);
  url.SetPassword(m_myPlexPass);

  std::string strResponse;
  std::string strPostData;
  if (plex.Post(url.Get(), strPostData, strResponse))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: myPlex %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    CVariant user = reply["user"];
    m_myPlexToken = user["authentication_token"].asString();

    std::string homeUserName;
    if (GetMyHomeUsers(homeUserName))
      m_myHomeUser = homeUserName;

    rtn = true;
  }
  else
  {
    CLog::Log(LOGERROR, "CPlexServices:FetchPlexToken failed %s", strResponse.c_str());
  }

  return rtn;
}

bool CPlexServices::FetchMyPlexServers()
{
  bool rtn = false;

  XFILE::CCurlFile plex;
  CPlexUtils::GetDefaultHeaders(plex);
  if (!m_myPlexToken.empty())
    plex.SetRequestHeader("X-Plex-Token", m_myPlexToken);

  std::string strResponse;
  CURL url("https://plex.tv/api/resources");
  if (plex.Get(url.Get(), strResponse))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: servers %s", strResponse.c_str());

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
          CPlexClient newClient(DeviceNode);

          if (AddClient(newClient))
          {
            CLog::Log(LOGNOTICE, "CPlexServices: Server found via plex.tv %s", newClient.GetServerName().c_str());
            rtn = true;
          }
          else if (GetClient(newClient.m_uuid))
          {
            //GetClient(newClient.m_uuid)->ParseData(data, host);
            CLog::Log(LOGDEBUG, "CPlexServices: Server updated via plex.tv %s", newClient.GetServerName().c_str());
            rtn = true;
          }
        }
        DeviceNode = DeviceNode->NextSiblingElement("Device");
      }
    }
  }
  else
  {
    CLog::Log(LOGDEBUG, "CPlexServices:FetchMyPlexServers failed %s", strResponse.c_str());
  }

  return rtn;
}

bool CPlexServices::FetchSignInPin()
{
  // on return, show user m_signInByPinCode so they can enter it at https://plex.tv/pin

  bool rtn = false;

  std::string id;
  std::string code;
  std::string clientid;
  CDateTime   expiresAt;

  XFILE::CCurlFile plex;
  // use a lower default timeout
  plex.SetTimeout(3);
  plex.SetRequestHeader("X-Plex-Client-Identifier", CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID));

  CURL url("https://plex.tv/pins.xml");

  std::string strResponse;
  if (plex.Post(url.Get(), "", strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexServices:FetchSignInPin %s", strResponse.c_str());
    /*
    <pin>
      <client-identifier>a36023fe-930c-4f07-9dbb-88ac8cb91ccf</client-identifier>
      <code>5YFG</code>
      <expires-at type="datetime">2016-06-30T03:56:36Z</expires-at>
      <id type="integer">28975394</id>
      <user-id type="integer" nil="true"/>
      <auth-token type="NilClass" nil="true"/>
      <auth_token nil="true"></auth_token>
    </pin>
    */

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
      rtn = !m_signInByPinId.empty() && !m_signInByPinCode.empty();
    }
  }
  else
  {
    CLog::Log(LOGERROR, "CPlexServices:FetchSignInPin failed %s", strResponse.c_str());
  }

  return rtn;
}

bool CPlexServices::WaitForSignInByPin()
{
  // repeat called until we timeout or get authToken
  bool rtn = false;

  std::string authToken;

  XFILE::CCurlFile plex;
  plex.SetRequestHeader("Content-Type", "application/xml; charset=utf-8");
  plex.SetRequestHeader("Content-Length", "0");
  plex.SetRequestHeader("X-Plex-Client-Identifier", CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_UUID));

  std::string path = "https://plex.tv/pins/" + m_signInByPinId + ".xml";
  CURL url(path);

  std::string strResponse;
  if (plex.Get(url.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "CPlexServices:WaitForSignInByPin %s", strResponse.c_str());

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
          authToken = elem->GetText();
      }
      m_myPlexToken = authToken;
      rtn = !m_myPlexToken.empty();
    }
  }
  else
  {
    CLog::Log(LOGERROR, "CPlexServices:WaitForSignInByPin failed %s", strResponse.c_str());
  }

  return rtn;
}

void CPlexServices::SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket)
{
  SOCKETS::CAddress discoverAddress;
  discoverAddress.SetAddress(NS_BROADCAST_ADDR, NS_PLEX_MEDIA_SERVER_PORT);
  std::string discoverMessage = NS_SEARCH_MSG;
  int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
  if (packetSize < 0)
    CLog::Log(LOGERROR, "CPlexServices: discover send failed");
}

CPlexClient* CPlexServices::GetClient(std::string uuid)
{
  for (std::vector<CPlexClient>::iterator s_it = m_clients.begin(); s_it != m_clients.end(); ++s_it)
  {
    if (s_it->GetUuid() == uuid)
      return &(*s_it);
  }
  return nullptr;
}

bool CPlexServices::AddClient(CPlexClient client)
{
  // do not add existing clients
  for (std::vector<CPlexClient>::iterator s_it = m_clients.begin(); s_it != m_clients.end(); ++s_it)
  {
    if (s_it->GetUuid() == client.GetUuid())
    return false;
  }

  client.ParseSections();
  m_clients.push_back(client);

  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
  g_windowManager.SendThreadMessage(msg);
  
  // announce that we have a plex client and that recently added should be updated
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::VideoLibrary, "xbmc", "UpdateRecentlyAdded");
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "UpdateRecentlyAdded");

  return true;
}

bool CPlexServices::GetMyHomeUsers(std::string &homeUserName)
{
  bool rtn = false;

  XFILE::CCurlFile plex;
  CPlexUtils::GetDefaultHeaders(plex);
  if (!m_myPlexToken.empty())
    plex.SetRequestHeader("X-Plex-Token", m_myPlexToken);

  std::string strResponse;
  CURL url("https://plex.tv/api/home/users");
  if (plex.Get(url.Get(), strResponse))
  {
    //CLog::Log(LOGDEBUG, "CPlexServices: servers %s", strResponse.c_str());

    TiXmlDocument xml;
    CFileItemList plexUsers;
    xml.Parse(strResponse.c_str());

    TiXmlElement* MediaContainer = xml.RootElement();
    if (MediaContainer)
    {
      std::string users = XMLUtils::GetAttribute(MediaContainer, "size");
      if (atoi(users.c_str()) > 1)
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
          plexUser->SetIconImage(XMLUtils::GetAttribute(UserNode, "thumb"));
          plexUsers.Add(plexUser);
          UserNode = UserNode->NextSiblingElement("User");
        }
      }
    }
    CGUIDialogSelect *dialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    if (dialog == NULL)
      return false;

    dialog->Reset();
    dialog->SetHeading("Choose User");
    dialog->SetItems(plexUsers);
    dialog->SetMultiSelection(false);
    dialog->SetUseDetails(true);
    dialog->Open();

    if (!dialog->IsConfirmed())
      return false;

    const CFileItemPtr item = dialog->GetSelectedItem();

    if (item == NULL || !item->HasProperty("uuid"))
      return false;

    std::string pinUrl = "/switch";
    if (item->GetProperty("protected").asBoolean())
    {
      std::string pin;
      if( !CGUIDialogNumeric::ShowAndGetNumber(pin, "Enter pin") )
        return false;
      pinUrl = "/switch?pin=" + pin;
    }
    
    XFILE::CCurlFile plex;
    CPlexUtils::GetDefaultHeaders(plex);
    if (!m_myPlexToken.empty())
      plex.SetRequestHeader("X-Plex-Token", m_myPlexToken);

    std::string uuid = item->GetProperty("uuid").asString();
    CURL url("https://plex.tv/api/v2/home/users/" + uuid + pinUrl);

    CPlexUtils::GetDefaultHeaders(plex);
    std::string strResponse;
    plex.Post(url.Get(), "", strResponse);

    TiXmlDocument xml1;
    xml1.Parse(strResponse.c_str());

    TiXmlElement* userContainer = xml1.RootElement();
    if (userContainer)
    {
      std::string token = XMLUtils::GetAttribute(userContainer, "authToken");
      homeUserName = XMLUtils::GetAttribute(userContainer, "title");
      // each user gets its own token
      if (!token.empty())
        m_myPlexToken = token;
      rtn = !homeUserName.empty();
    }
  }
  else
  {
    CLog::Log(LOGDEBUG, "CPlexServices:FetchMyPlexServers failed %s", strResponse.c_str());
  }

  return rtn;
}
