/*
 *  Copyright (C) 2014 Team MN
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

#include "system.h"

#include "CGUIWindowMN.h"
#include "CGUIWindowMNDemand.h"
#include "PlayerManagerMN.h"
#include "network/Network.h"
#include "UtilitiesMN.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "URL.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "guilib/GUIWindowManager.h"
#include "input/Key.h"
#include "settings/DisplaySettings.h"
#include "settings/SkinSettings.h"
#include "settings/Settings.h"

#include "NWTVAPI.h"


#define PLAYLIST          90101
#define ONDEMAND          90102
#define MEDIAUPDATE       90103
#define ABOUT             90105
#define NETWORKSET        90116
#define NETWORKTEST       90126
#define SETVERTICAL       90134
#define SETHORIZONTAL     90144
#define ABOUTDIALOG       90200

#define PINGDIALOG        90145
#define GOOGLESERVER      90147
#define NWSEREVER         90148
#define PROXYMNSERVER     90149



CGUIWindowMN::CGUIWindowMN()
: CGUIWindow(WINDOW_MEMBERNET, "DialogNationWide.xml")
, CThread("MNwindow")
, m_RefreshRunning(false)
, m_AboutUp(false)
, m_testServersPopup(false)
, m_PlayerManager(NULL)
, m_MediaPlayList(*new NWMediaPlaylist)
{
  Create();
  m_loadType = KEEP_IN_MEMORY;
}

CGUIWindowMN::~CGUIWindowMN()
{
  StopThread();
  Close();
}

bool CGUIWindowMN::OnMessage(CGUIMessage& message)
{
  if  (message.GetMessage() == GUI_MSG_CLICKED)
  {
    int iControl = message.GetSenderId();
    
    if (iControl == ABOUT)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ABOUT);
      OnMessage(msg);
      
      m_AboutUp = true;
      SET_CONTROL_VISIBLE(ABOUTDIALOG);
      
      std::string strIPAddress;
      CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();
      if (iface)
        strIPAddress = iface->GetCurrentIPAddress();
      
      PlayerSettings settings = m_PlayerManager->GetSettings();
      // Fill in about popup
      SET_CONTROL_LABEL(90210, StringUtils::Format("Machine Name: %s",
                                                   CSettings::GetInstance().GetString("services.devicename").c_str()));
      SET_CONTROL_LABEL(90211, StringUtils::Format("Serial Number: %s",
                                                   settings.strMachine_sn.c_str()));
      SET_CONTROL_LABEL(90212, StringUtils::Format("Location ID: %s",
                                                   settings.strLocation_id.c_str()));
      SET_CONTROL_LABEL(90213, StringUtils::Format("Machine ID: %s",
                                                   settings.strMachine_id.c_str()));
      SET_CONTROL_LABEL(90214, StringUtils::Format("MNTV Software Version: %s",
                                                   settings.strSettings_cf_bundle_version.c_str()));
      SET_CONTROL_LABEL(90215, StringUtils::Format("IP Address:  %s",
                                                   strIPAddress.c_str()));
      SET_CONTROL_LABEL(90216, StringUtils::Format("Ethernet MAC: %s",
                                                   settings.strMachine_ethernet_id.c_str()));
      SET_CONTROL_LABEL(90217, StringUtils::Format("Wireless MAC: %s",
                                                   settings.strMachine_wireless_id.c_str()));
      SET_CONTROL_LABEL(90218, StringUtils::Format("Free Space: %s/%s",
                                                   GetDiskFree("/").c_str(),GetDiskTotal("/").c_str()));
      SET_CONTROL_LABEL(90219, StringUtils::Format("System Uptime: %s",
                                                   GetSystemUpTime().c_str()));
      return true;
    }
    else if (iControl == PLAYLIST)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), PLAYLIST);
      OnMessage(msg);
      if (!m_RefreshRunning)
      {
//        m_RefreshRunning = true;
        CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
        if (MNPlayerManager)
          MNPlayerManager->CreatePlaylist();
      }
      return true;
    }
    else if (iControl == MEDIAUPDATE)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), MEDIAUPDATE);
      OnMessage(msg);
      Refresh();
      return true;
    }
    else if (iControl == NETWORKTEST)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), NETWORKTEST);
      OnMessage(msg);
      
      m_testServers = true;
      
      return true;
    }
    else if (iControl == SETVERTICAL)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SETVERTICAL);
      OnMessage(msg);
      CSettings::GetInstance().SetBool(CSettings::MN_VERTICAL, true);
      CSettings::GetInstance().Save();
      SET_CONTROL_FOCUS(SETHORIZONTAL,0);
      
      return true;
    }
    else if (iControl == SETHORIZONTAL)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SETHORIZONTAL);
      OnMessage(msg);
      CSettings::GetInstance().SetBool(CSettings::MN_VERTICAL, false);
      CSettings::GetInstance().Save();
      SET_CONTROL_FOCUS(SETVERTICAL,0);
      
      return true;
    }
    else if (iControl == ONDEMAND)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND);
      OnMessage(msg);
      
      //fill in on demand window here
      CGUIWindowMNDemand::SetDialogMNPlaylist(m_MediaPlayList);
      g_windowManager.ActivateWindow(WINDOW_MEMBERNET_DEMAND);
      return true;
    }
    
  }
  else if (message.GetMessage() == GUI_MSG_WINDOW_DEINIT)
  {
    // below prevents window from exiting into home
    // CGUIWindow::OnMessage(message);
    return true;
  }

  return CGUIWindow::OnMessage(message);
}


bool CGUIWindowMN::OnAction(const CAction &action)
{

  if (action.GetButtonCode() == KEY_BUTTON_BACK || action.GetID() == ACTION_PREVIOUS_MENU || action.GetID() == ACTION_NAV_BACK)
  {
    
    if (m_AboutUp)
    {
      SET_CONTROL_HIDDEN(ABOUTDIALOG);
      m_AboutUp = false;
      return true;
    }
    
    if (m_testServersPopup)
    {
      SET_CONTROL_HIDDEN(PINGDIALOG);
      m_testServersPopup = false;
      return true;
    }
    
    if (g_application.m_pPlayer->IsPlaying())
    {
      m_PlayerManager->StopPlaying();
      return true;
    }
    // this return blocks any other "back/esc" action, prevents us closing MN main screen
    return true;
  }
  
  return CGUIWindow::OnAction(action);

}

void CGUIWindowMN::OnInitWindow()
{
  // below needs to be called once we run the update, it disables buttons in skin
  //DisableButtonsOnRefresh(true)
  
//  if (!m_PlayerManager)
//  {
//    m_PlayerManager = new CPlayerManagerMN();
//    m_PlayerManager->RegisterPlayerCallBack(this, PlayerCallBack);
//    m_PlayerManager->Startup();
//  }
  NWActivate activate;
  activate.apiKey = "/3/NKO6ZFdRgum7fZkMi";
  activate.apiSecret = "ewuDiXOIgZP7l9/Rxt/LDQbmAI1zJe0PQ5VZYnuy";
  
  NWStatus status;
  status.apiKey = activate.apiKey;
  status.apiSecret = activate.apiSecret;
  TVAPI_GetStatus(status);

  NWMachine machine;
  machine.apiKey = activate.apiKey;
  machine.apiSecret = activate.apiSecret;
  TVAPI_GetMachine(machine);
  
  NWPlaylist playlist;
  playlist.apiKey = activate.apiKey;
  playlist.apiSecret = activate.apiSecret;
  TVAPI_GetPlaylist(playlist, machine.playlist_id);

  NWPlaylistItems playlistItems;
  playlistItems.apiKey = activate.apiKey;
  playlistItems.apiSecret = activate.apiSecret;
  TVAPI_GetPlaylistItems(playlistItems, machine.playlist_id);

  m_MediaPlayList = NWMediaPlaylist();
  TVAPI_CreateMediaPlaylist(m_MediaPlayList, playlist, playlistItems);

  CGUIWindow::OnInitWindow();
}

void CGUIWindowMN::OnWindowLoaded()
{
  CGUIWindow::OnWindowLoaded();
}

void CGUIWindowMN::OnWindowUnload()
{
  m_PlayerManager = NULL;
  SAFE_DELETE(m_PlayerManager);
}

void CGUIWindowMN::Refresh()
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIWindowMN::Refresh()");
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->Startup();
}

void CGUIWindowMN::OnStartup()
{
  
}

void  CGUIWindowMN::Process()
{
  while (!m_bStop)
  {
    Sleep(500);
    if (m_testServers)
    {
      m_testServers = false;
      m_testServersPopup = true;
      SET_CONTROL_VISIBLE(PINGDIALOG);
      TestServers();
    }
  }
}

void CGUIWindowMN::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  CGUIWindow::Process(currentTime, dirtyregions);
}

void CGUIWindowMN::PlayerCallBack(const void *ctx, bool status)
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIWindowMN::PlayerCallBack() player running" );
  CGUIWindowMN *dlog = (CGUIWindowMN*)ctx;
  dlog->m_RefreshRunning = false;

  
  CGUIWindow *pWindow = (CGUIWindow*)g_windowManager.GetWindow(WINDOW_MEMBERNET);
  if (pWindow && 0)
  {
    CDateTime NextUpdateTime;
    CDateTime NextDownloadTime;
    CDateTimeSpan NextDownloadDuration;
//    dlog->m_PlayerManager->GetStats(NextUpdateTime, NextDownloadTime, NextDownloadDuration);
    
    CDateTime end = NextDownloadTime + NextDownloadDuration;
    pWindow->SetProperty("line1", StringUtils::Format("dl bgn: %s", NextDownloadTime.GetAsDBDateTime().c_str()));
    pWindow->SetProperty("line2", StringUtils::Format("dl end: %s", end.GetAsDBDateTime().c_str()));
    pWindow->SetProperty("line3", StringUtils::Format("update: %s", NextUpdateTime.GetAsDBDateTime().c_str()));

    CLog::Log(LOGDEBUG, "**MN** - CGUIWindowMN::Process %s", NextDownloadTime.GetAsDBDateTime().c_str());
  }
}

void CGUIWindowMN::PlaybackCallBack(const void *ctx, int msg, MNMediaAsset &asset)
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIWindowMN::PlaybackCallBack(): playing \'%s\'", asset.title.c_str());
}

void CGUIWindowMN::DisableButtonsOnRefresh(bool disable)
{
  if (disable)
  {
    CONTROL_DISABLE(PLAYLIST);
    CONTROL_DISABLE(ONDEMAND);
    CONTROL_DISABLE(MEDIAUPDATE);
  }
  else
  {
    CONTROL_ENABLE(PLAYLIST);
    CONTROL_ENABLE(ONDEMAND);
    CONTROL_ENABLE(MEDIAUPDATE);
  }
}


void CGUIWindowMN::TestServers()
{
  SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' --> testing");
  SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' --> testing");
  SET_CONTROL_LABEL(PROXYMNSERVER, "");
  
  if (PingMNServer("http://www.google.com"))
    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is reachable");
  else
    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is not reachable");
  
  if (PingMNServer("http://www.nationwidemember.com"))
    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is reachable");
  else
    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is not reachable");
  /*
  if (PingMNServer("http://proxy.membernettv.com"))
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is reachable");
  else
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is not reachable");
  */
}

void CGUIWindowMN::SetResolution(const std::string &strResolution)
{
  // format: SWWWWWHHHHHRRR.RRRRRP, where,
  //  S = screen, W = width, H = height, R = refresh, P = interlace
  RESOLUTION res = CDisplaySettings::GetResolutionFromString(strResolution);
//  const char *strMode = CDisplaySettings::Get().GetResolutionInfo(res).strMode.c_str();
  CDisplaySettings::GetInstance().SetCurrentResolution(res, true);
  CLog::Log(LOGERROR,"resolution:res(%d), parameter(%s)", res, strResolution.c_str());
}