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

#include "GUIDialogRedAbout.h"
#include "PlayerManagerRed.h"

#include "GUIInfoManager.h"
#include "guilib/GUIWindowManager.h"
#include "guiinfo/GUIInfoLabels.h"
#include "input/Key.h"
#include "guilib/LocalizeStrings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#define PLAYER_ID          101
#define PLAYER_NAME        102
#define PLAYER_PROGRAM_ID  103
#define PLAYER_STATUS      104
#define PLAYER_UPDATE      105
#define NETWORK_IP         106
#define NETWORK_GATEWAY    107
#define NETWORK_DNS_1      108
#define NETWORK_DNS_2      109
#define NETWORK_SUBNET     110
#define NETWORK_DHCP       111
#define SOFTWARE_VERSION   112

PlayerInfo* CGUIDialogRedAbout::m_PlayerInfo = NULL;
float CGUIDialogRedAbout::m_Version;
CGUIDialogRedAbout *CGUIDialogRedAbout::m_RedAbout = NULL;
CCriticalSection CGUIDialogRedAbout::m_PlayerInfo_lock;

CGUIDialogRedAbout::CGUIDialogRedAbout()
: CGUIDialog(WINDOW_DIALOG_RED_ABOUT, "DialogRedAbout.xml")
{
  m_loadType = KEEP_IN_MEMORY;
  m_RedAbout = this;
}

CGUIDialogRedAbout::~CGUIDialogRedAbout()
{
  m_RedAbout = NULL;
}

bool CGUIDialogRedAbout::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
    case GUI_MSG_WINDOW_DEINIT:
    {
      CGUIWindow::OnMessage(message);
      return true;
    }
    case GUI_MSG_WINDOW_INIT:
    {
      CGUIWindow::OnMessage(message);
      return true;
    }
  }
  return CGUIDialog::OnMessage(message);
}

void CGUIDialogRedAbout::OnInitWindow()
{
  CGUIWindow::OnInitWindow();
  FillLabels();
}

void CGUIDialogRedAbout::OnWindowUnload()
{
}

void CGUIDialogRedAbout::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  static unsigned int lastTime_ms = 0;
  
  if (currentTime > lastTime_ms)
  {
    FillLabels();
    lastTime_ms = currentTime + 1000;
  }

  CGUIDialog::Process(currentTime, dirtyregions);
}

void CGUIDialogRedAbout::SetInfo(PlayerInfo *playerInfo, const float version)
{
  CSingleLock lock(m_PlayerInfo_lock);
  m_PlayerInfo = playerInfo;
  m_Version    = version;
}

void CGUIDialogRedAbout::FillLabels()
{
  SetControlLabel(NETWORK_IP,        "%s", NETWORK_IP_ADDRESS);
  SetControlLabel(NETWORK_GATEWAY,   "%s", NETWORK_GATEWAY_ADDRESS);
  
  SetControlLabel(NETWORK_DNS_1,     "%s", NETWORK_DNS1_ADDRESS);
  SetControlLabel(NETWORK_DNS_2,     "%s", NETWORK_DNS2_ADDRESS);
  SetControlLabel(NETWORK_SUBNET,    "%s", NETWORK_SUBNET_MASK);
  SetControlLabel(NETWORK_DHCP,      "%s", NETWORK_IS_DHCP);

  CSingleLock lock(m_PlayerInfo_lock);
  if (m_PlayerInfo)
  {
    SET_CONTROL_LABEL(PLAYER_ID,         m_PlayerInfo->strPlayerID);
    SET_CONTROL_LABEL(PLAYER_NAME,       m_PlayerInfo->strPlayerName);
    SET_CONTROL_LABEL(PLAYER_PROGRAM_ID, m_PlayerInfo->strProgramID);
    SET_CONTROL_LABEL(PLAYER_STATUS,     m_PlayerInfo->strStatus);
    SET_CONTROL_LABEL(PLAYER_UPDATE,     m_PlayerInfo->strUpdateInterval);
  }
  lock.Leave();
  
  std::string tmpStr = StringUtils::Format("%.1f", m_Version);
  SET_CONTROL_LABEL(SOFTWARE_VERSION,  tmpStr);
}

void CGUIDialogRedAbout::SetControlLabel(int id, const char *format, int info)
{
  std::string tmpStr = StringUtils::Format(format, g_infoManager.GetLabel(info).c_str());
  SET_CONTROL_LABEL(id, tmpStr);
}

bool CGUIDialogRedAbout::OnAction(const CAction& action)
{
  if (action.GetID() == ACTION_PREVIOUS_MENU ||
      action.GetID() == ACTION_NAV_BACK)
  {
    Close();
    return true;
  }
  return false;
}

CGUIDialogRedAbout* CGUIDialogRedAbout::GetDialogRedAbout()
{
  return m_RedAbout;
}
