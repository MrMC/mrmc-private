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

#include "GUIDialogMN.h"
#include "GUIDialogMNDemand.h"
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


#define PLAYLIST          90101
#define ONDEMAND          90102
#define MEDIAUPDATE       90103
#define ABOUT             90105
#define SWUPDATE          90107
#define RESTART           90108
#define NETWORKSET        90116
#define NETWORKTEST       90126
#define DISPLAY720        90114
#define DISPLAY1080       90124
#define SETVERTICAL       90134
#define SETHORIZONTAL     90144
#define ABOUTDIALOG       90200

#define PINGDIALOG        90145
#define GOOGLESERVER      90147
#define NWSEREVER         90148
#define PROXYMNSERVER     90149



CGUIDialogMN::CGUIDialogMN()
: CGUIDialog(WINDOW_DIALOG_MN, "DialogNationWide.xml")
, CThread("MNwindow")
, m_RefreshRunning(false)
, m_AboutUp(false)
, m_testServersPopup(false)
, m_PlayerManager(NULL)
{
  Create();
  m_loadType = KEEP_IN_MEMORY;
}

CGUIDialogMN::~CGUIDialogMN()
{
  StopThread();
  Close();
}

bool CGUIDialogMN::OnMessage(CGUIMessage& message)
{
  if  (message.GetMessage() == GUI_MSG_CLICKED)
  {
    int iControl = message.GetSenderId();

    if (iControl == RESTART)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), RESTART);
      OnMessage(msg);

//      CApplicationMessenger::Get().Restart();
      return true;
    }
    else if (iControl == ABOUT)
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
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SWUPDATE);
      OnMessage(msg);
      Refresh();
      return true;
    }
    else if (iControl == SWUPDATE)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SWUPDATE);
      OnMessage(msg);
      
      // do the SWUPDATE thing

      return true;
    }

    else if (iControl == NETWORKSET)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), NETWORKSET);
      OnMessage(msg);

#if defined(TARGET_ANDROID)
      OpenAndroidSettings()
#elseif defined(TARGET_LINUX) // no way to do this only on Openelec?
      CStdString cmd;
      cmd = StringUtils::Format("RunAddon(service.openelec.settings)");
      CApplicationMessenger::Get().ExecBuiltIn(cmd, false);
#endif
      return true;
    }

    else if (iControl == NETWORKTEST)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), NETWORKTEST);
      OnMessage(msg);
      
      m_testServers = true;
      
      return true;
    }
    
    else if (iControl == DISPLAY720)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), DISPLAY720);
      OnMessage(msg);
      
      // set 720P here
      const std::string strResolution = "00128000720060.00000p";
      SetResolution(strResolution);
      return true;
    }
    
    else if (iControl == DISPLAY1080)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), DISPLAY1080);
      OnMessage(msg);
      
      // set 1080p here
      const std::string strResolution = "00192001080060.00000p";
      SetResolution(strResolution);
      return true;
    }
    
    else if (iControl == SETVERTICAL)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SETVERTICAL);
      OnMessage(msg);
      
      int string = CSkinSettings::GetInstance().TranslateBool("NW_Vertical");
      CSkinSettings::GetInstance().SetBool(string, true);
      CSettings::GetInstance().Save();
      
      return true;
    }
    else if (iControl == SETHORIZONTAL)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SETHORIZONTAL);
      OnMessage(msg);
      
      CSkinSettings::GetInstance().Reset("NW_Vertical");
      CSettings::GetInstance().Save();
      
      return true;
    }
    else if (iControl == ONDEMAND)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND);
      OnMessage(msg);
      
      //fill in on demand window here
      CGUIDialogMNDemand::SetDialogMNCategory(m_PlayerManager->GetOndemand());
      g_windowManager.ActivateWindow(WINDOW_DIALOG_MN_DEMAND);
      return true;
    }
    
  }
  else if (message.GetMessage() == GUI_MSG_WINDOW_DEINIT)
  {
    // below prevents window from exiting into home
    // CGUIDialog::OnMessage(message);
    return true;
  }

  return CGUIDialog::OnMessage(message);
}


bool CGUIDialogMN::OnAction(const CAction &action)
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
  }
  
  return CGUIDialog::OnAction(action);

}

void CGUIDialogMN::OnInitWindow()
{
   // below needs to be called once we run the update, it disables buttons in skin
   //DisableButtonsOnRefresh(true);

   m_PlayerManager = new CPlayerManagerMN();
   m_PlayerManager->RegisterPlayerCallBack(this, PlayerCallBack);
   m_PlayerManager->Startup();
  
   CGUIWindow::OnInitWindow();

}

void CGUIDialogMN::OnWindowUnload()
{
  m_PlayerManager = NULL;
  SAFE_DELETE(m_PlayerManager);
}

void CGUIDialogMN::Refresh()
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIDialogMN::Refresh()");
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->Startup();
}

void CGUIDialogMN::OnStartup()
{
  
}

void  CGUIDialogMN::Process()
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

void CGUIDialogMN::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  CGUIDialog::Process(currentTime, dirtyregions);
}

void CGUIDialogMN::PlayerCallBack(const void *ctx, bool status)
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIDialogMN::PlayerCallBack() player running" );
  CGUIDialogMN *dlog = (CGUIDialogMN*)ctx;
  dlog->m_RefreshRunning = false;

  
  CGUIWindow *pWindow = (CGUIWindow*)g_windowManager.GetWindow(WINDOW_DIALOG_MN);
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

    CLog::Log(LOGDEBUG, "**MN** - CGUIDialogMN::Process %s", NextDownloadTime.GetAsDBDateTime().c_str());
  }
}

void CGUIDialogMN::PlaybackCallBack(const void *ctx, int msg, MNMediaAsset &asset)
{
  CLog::Log(LOGDEBUG, "**MN** - CGUIDialogMN::PlaybackCallBack(): playing \'%s\'", asset.title.c_str());
}

void CGUIDialogMN::DisableButtonsOnRefresh(bool disable)
{
  if (disable)
  {
    CONTROL_DISABLE(PLAYLIST);
    CONTROL_DISABLE(ONDEMAND);
    CONTROL_DISABLE(SWUPDATE);
    CONTROL_DISABLE(MEDIAUPDATE);
  }
  else
  {
    CONTROL_ENABLE(PLAYLIST);
    CONTROL_ENABLE(ONDEMAND);
    CONTROL_ENABLE(SWUPDATE);
    CONTROL_ENABLE(MEDIAUPDATE);
  }
}


void CGUIDialogMN::TestServers()
{
  SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' --> testing");
  SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' --> testing");
  SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' --> testing");
  
  if (PingMNServer("http://www.google.com"))
    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is reachable");
  else
    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is not reachable");
  
  if (PingMNServer("http://www.nationwidemember.com"))
    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is reachable");
  else
    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is not reachable");
  
  if (PingMNServer("http://proxy.membernettv.com"))
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is reachable");
  else
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is not reachable");
}

void CGUIDialogMN::SetResolution(const std::string &strResolution)
{
  // format: SWWWWWHHHHHRRR.RRRRRP, where,
  //  S = screen, W = width, H = height, R = refresh, P = interlace
  RESOLUTION res = CDisplaySettings::GetResolutionFromString(strResolution);
//  const char *strMode = CDisplaySettings::Get().GetResolutionInfo(res).strMode.c_str();
  CDisplaySettings::GetInstance().SetCurrentResolution(res, true);
  CLog::Log(LOGERROR,"resolution:res(%d), parameter(%s)", res, strResolution.c_str());
}