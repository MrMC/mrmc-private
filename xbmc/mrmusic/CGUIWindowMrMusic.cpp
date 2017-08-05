/*
 *  Copyright (C) 2017 RootCoder, LLC.
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
 *  along with this app; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"

#include "CGUIWindowMrMusic.h"

#include "Application.h"
#include "URL.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "filesystem/CurlFile.h"
#include "input/Key.h"
#include "network/Network.h"
#include "settings/DisplaySettings.h"
#include "settings/SkinSettings.h"
#include "settings/Settings.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/Environment.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#define MAINLIST          9000

#define CONTROLS          90101
#define PLAYQUEUE         90102
#define SOURCEFOLDER      90103
#define PLAYLIST          90104
#define USB               90105
#define SDCARD            90106
#define NETWORK           90107
#define OPTIONS           90108
#define HELP              90109
#define EXIT              90110




CGUIWindowMrMusic::CGUIWindowMrMusic()
: CGUIWindow(WINDOW_MR_MUSIC, "HomeMrMusic.xml")
, CThread("MrMusicwindow")
, m_RefreshRunning(false)
, m_AboutUp(false)
, m_testServersPopup(false)
, m_testServers(false)
, m_client(NULL)
{
//  Create();
  m_loadType = KEEP_IN_MEMORY;
}

CGUIWindowMrMusic::~CGUIWindowMrMusic()
{
//  StopThread();
//  Close();
}

bool CGUIWindowMrMusic::OnMessage(CGUIMessage& message)
{
    //CONTROLS
    //PLAYQUEUE
    //SOURCEFOLDER
    //PLAYLIST
    //USB
    //SDCARD
    //NETWORK
    //OPTIONS
    //HELP
    //EXIT
  if  (message.GetMessage() == GUI_MSG_CLICKED)
  {
    int iList = message.GetSenderId();
    
    bool selectAction = (message.GetParam1() == ACTION_SELECT_ITEM ||
                         message.GetParam1() == ACTION_MOUSE_LEFT_CLICK);
    
    if (selectAction && iList == MAINLIST)
    {
    
      CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iList);
      OnMessage(msg);
      
      int iControl = msg.GetParam1() + 90101; // ofsett for our list items
      
      if (iControl == CONTROLS)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), CONTROLS);
        OnMessage(msg);
        
        return true;
      }
      else if (iControl == PLAYQUEUE)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), PLAYQUEUE);
        OnMessage(msg);

        return true;
      }
      else if (iControl == SOURCEFOLDER)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SOURCEFOLDER);
        OnMessage(msg);

        return true;
      }
      else if (iControl == PLAYLIST)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), PLAYLIST);
        OnMessage(msg);

        return true;
      }
      else if (iControl == USB)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), USB);
        OnMessage(msg);

        return true;
      }
      else if (iControl == SDCARD)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), SDCARD);
        OnMessage(msg);

        return true;
      }
      else if (iControl == NETWORK)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), NETWORK);
        OnMessage(msg);
        
        return true;
      }
      else if (iControl == OPTIONS)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), OPTIONS);
        OnMessage(msg);

        return true;
      }
      else if (iControl == HELP)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), HELP);
        OnMessage(msg);
        
        return true;
      }
      else if (iControl == EXIT)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), EXIT);
        OnMessage(msg);
        
        return true;
      }
    }
  }
  else if (message.GetMessage() == GUI_MSG_WINDOW_DEINIT)
  {
    // below prevents window from exiting into home
    // CGUIWindow::OnMessage(message);
    return true;
  }
  else if (message.GetMessage() == GUI_MSG_NOTIFY_ALL)
  {
//    if (message.GetParam1() == STARTCLIENT)

  }
  return CGUIWindow::OnMessage(message);
}


bool CGUIWindowMrMusic::OnAction(const CAction &action)
{

  if (action.GetButtonCode() == KEY_BUTTON_BACK || action.GetID() == ACTION_PREVIOUS_MENU || action.GetID() == ACTION_NAV_BACK)
  {
    
    if (m_AboutUp)
    {
//      SET_CONTROL_HIDDEN(ABOUTDIALOG);
      m_AboutUp = false;
      return true;
    }

    if (m_testServersPopup)
    {
//      SET_CONTROL_HIDDEN(PINGDIALOG);
      m_testServersPopup = false;
      //return true;
    }
    
    if (m_client && g_application.m_pPlayer->IsPlaying())
    {
//      m_client->StopPlaying();
      return true;
    }
    // this return blocks any other "back/esc" action, prevents us closing MN main screen
    return true;
  }
  
  return CGUIWindow::OnAction(action);

}

void CGUIWindowMrMusic::OnInitWindow()
{
//  CGUIMessage reload(GUI_MSG_NOTIFY_ALL, GetID(), 0, STARTCLIENT, 0);
//  g_windowManager.SendThreadMessage(reload, GetID());
  
  CGUIWindow::OnInitWindow();
}

void CGUIWindowMrMusic::OnWindowLoaded()
{
  CGUIWindow::OnWindowLoaded();
}

void CGUIWindowMrMusic::OnWindowUnload()
{
//  SAFE_DELETE(m_client);
}


void CGUIWindowMrMusic::OnStartup()
{
  
}

void  CGUIWindowMrMusic::Process()
{
//  while (!m_bStop)
//  {
//    Sleep(500);
//    if (m_testServers)
//    {
//      m_testServers = false;
//      m_testServersPopup = true;
//      SET_CONTROL_VISIBLE(PINGDIALOG);
//      TestServers();
//    }
//  }
}

void CGUIWindowMrMusic::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  CGUIWindow::Process(currentTime, dirtyregions);
}


void CGUIWindowMrMusic::DisableButtonsOnRefresh(bool disable)
{
  if (disable)
  {
//    CONTROL_DISABLE(PLAYLIST);
//    CONTROL_DISABLE(ONDEMAND);
//    CONTROL_DISABLE(MEDIAUPDATE);
  }
  else
  {
//    CONTROL_ENABLE(PLAYLIST);
//    CONTROL_ENABLE(ONDEMAND);
//    CONTROL_ENABLE(MEDIAUPDATE);
  }
}


void CGUIWindowMrMusic::TestServers()
{
//  SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' --> testing");
//  SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' --> testing");
//  SET_CONTROL_LABEL(PROXYMNSERVER, "");
//  
//  if (PingMNServer("http://www.google.com"))
//    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is reachable");
//  else
//    SET_CONTROL_LABEL(GOOGLESERVER, "'www.google.com' is not reachable");
//  
//  if (PingMNServer("http://www.nationwidemember.com"))
//    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is reachable");
//  else
//    SET_CONTROL_LABEL(NWSEREVER, "'www.nationwidemember.com' is not reachable");
  /*
  if (PingMNServer("http://proxy.membernettv.com"))
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is reachable");
  else
    SET_CONTROL_LABEL(PROXYMNSERVER, "'proxy.membernettv.com' is not reachable");
  */
}

bool CGUIWindowMrMusic::PingMNServer(const std::string& apiURL)
{
  CURL url(apiURL.c_str());
  std::string http_path = url.GetProtocol().c_str();
  http_path += "://" + url.GetHostName();

  XFILE::CCurlFile http;
  CURL http_url(http_path.c_str());
  bool found = http.Open(http_url);
  if (!found && (errno == EACCES))
    found = true;
  http.Close();
  
  if (found)
    CLog::Log(LOGDEBUG, "**MN** - PingMNServer: network=yes");
  else
    CLog::Log(LOGDEBUG, "**MN** - PingMNServer: network=no");

  return found;
}

void CGUIWindowMrMusic::SetResolution(const std::string &strResolution)
{
  // format: SWWWWWHHHHHRRR.RRRRRP, where,
  //  S = screen, W = width, H = height, R = refresh, P = interlace
  RESOLUTION res = CDisplaySettings::GetResolutionFromString(strResolution);
//  const char *strMode = CDisplaySettings::Get().GetResolutionInfo(res).strMode.c_str();
  CDisplaySettings::GetInstance().SetCurrentResolution(res, true);
  CLog::Log(LOGDEBUG,"resolution:res(%d), parameter(%s)", res, strResolution.c_str());
}
