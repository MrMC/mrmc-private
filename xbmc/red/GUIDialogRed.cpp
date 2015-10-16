/*
 *  Copyright (C) 2014 Team RED
 *
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

#include "GUIDialogRed.h"
#include "GUIDialogRedAbout.h"
#include "DBManagerRed.h"
#include "PlayerManagerRed.h"
#include "UtilitiesRed.h"

#include "messaging/ApplicationMessenger.h"
#include "URL.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "guilib/GUIWindowManager.h"


#define STATUS_LABEL          100
#define LOADING_IMAGE         110
#define REBOOT_BUTTON         140
#define REFRESH_BUTTON        141
#define SYS_SETTTINGS_BUTTON  142
#define SEND_REPORT_BUTTON    143
#define ABOUT_BUTTON          144

CGUIDialogRed::CGUIDialogRed()
: CGUIDialog(WINDOW_DIALOG_RED, "DialogRed.xml")
, m_RefreshRunning(false)
, m_PlayerManager(NULL)
{
  m_loadType = KEEP_IN_MEMORY;
}

CGUIDialogRed::~CGUIDialogRed()
{
}

bool CGUIDialogRed::OnMessage(CGUIMessage& message)
{
  if  (message.GetMessage() == GUI_MSG_CLICKED)
  {
    int iControl = message.GetSenderId();

    if (iControl == REBOOT_BUTTON)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), REBOOT_BUTTON);
      OnMessage(msg);

//      CApplicationMessenger::Get().Restart();
      return true;
    }
    else if (iControl == REFRESH_BUTTON)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), REFRESH_BUTTON);
      OnMessage(msg);
      if (!m_RefreshRunning)
      {
        // refresh playlist/assets here
        Refresh();
      }
      else
      {
        // cancel playlist/assets update here
      }
      return true;
    }
    else if (iControl == SYS_SETTTINGS_BUTTON)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SYS_SETTTINGS_BUTTON);
      OnMessage(msg);
      
      OpenAndroidSettings();

      return true;
    }

    else if (iControl == SEND_REPORT_BUTTON)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SEND_REPORT_BUTTON);
      OnMessage(msg);
      
      m_PlayerManager->SendReport();
      return true;
    }

    else if (iControl == ABOUT_BUTTON)
    {
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SEND_REPORT_BUTTON);
      OnMessage(msg);
      
      
      g_windowManager.ActivateWindow(WINDOW_DIALOG_RED_ABOUT);
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

void CGUIDialogRed::OnInitWindow()
{
  m_PlayerManager = new CPlayerManagerRed();
  m_PlayerManager->RegisterPlayerCallBack(this, PlayerCallBack);
  m_PlayerManager->RegisterPlayBackCallBack(this, PlaybackCallBack);
  m_PlayerManager->Startup();
  
  Refresh();

  CGUIWindow::OnInitWindow();
}

void CGUIDialogRed::OnWindowUnload()
{
  SAFE_DELETE(m_PlayerManager);
}

void CGUIDialogRed::Refresh()
{
  m_RefreshRunning = true;
  m_PlayerManager->FullUpdate();
  
}

void CGUIDialogRed::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  if (!m_RefreshRunning)
    SET_CONTROL_LABEL(REFRESH_BUTTON,g_localizeStrings.Get(32007));
  else
    SET_CONTROL_LABEL(REFRESH_BUTTON,g_localizeStrings.Get(32013));

  CGUIDialog::Process(currentTime, dirtyregions);
}

void CGUIDialogRed::PlayerCallBack(const void *ctx, bool status)
{
  CLog::Log(LOGDEBUG, "**RED** - CGUIDialogRed::PlayerCallBack() player running" );
  CGUIDialogRed *dlog = (CGUIDialogRed*)ctx;
  dlog->m_RefreshRunning = false;

  
  CGUIWindow *pWindow = (CGUIWindow*)g_windowManager.GetWindow(WINDOW_DIALOG_RED);
  if (pWindow && 0)
  {
    CDateTime NextUpdateTime;
    CDateTime NextDownloadTime;
    CDateTimeSpan NextDownloadDuration;
    dlog->m_PlayerManager->GetStats(NextUpdateTime, NextDownloadTime, NextDownloadDuration);
    
    CDateTime end = NextDownloadTime + NextDownloadDuration;
    pWindow->SetProperty("line1", StringUtils::Format("dl bgn: %s", NextDownloadTime.GetAsDBDateTime().c_str()));
    pWindow->SetProperty("line2", StringUtils::Format("dl end: %s", end.GetAsDBDateTime().c_str()));
    pWindow->SetProperty("line3", StringUtils::Format("update: %s", NextUpdateTime.GetAsDBDateTime().c_str()));

    CLog::Log(LOGDEBUG, "**RED** - CGUIDialogRed::Process %s", NextDownloadTime.GetAsDBDateTime().c_str());
  }
}

void CGUIDialogRed::PlaybackCallBack(const void *ctx, int msg, RedMediaAsset &asset)
{
  CLog::Log(LOGDEBUG, "**RED** - CGUIDialogRed::PlaybackCallBack(): playing \'%s\'", asset.name.c_str());
  //CGUIDialogRed *dlog = (CGUIDialogRed*)ctx;
  CDBManagerRed database;
  database.Open();
  database.AddAssetPlayback(asset);
  database.Close();
}
