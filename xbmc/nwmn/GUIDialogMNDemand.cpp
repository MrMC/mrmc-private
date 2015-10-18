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

#include "GUIDialogMNDemand.h"
//#include "PlayerManagerRed.h"

#include "GUIInfoManager.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "input/Key.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "FileItem.h"
#include "video/VideoInfoTag.h"
#include "Application.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/MediaSettings.h"
#include "messaging/ApplicationMessenger.h"

#define ONDEMAND_LIST      50


PlayerSettings* CGUIDialogMNDemand::m_PlayerInfo = NULL;
float CGUIDialogMNDemand::m_Version;
CGUIDialogMNDemand *CGUIDialogMNDemand::m_MNDemand = NULL;
CCriticalSection CGUIDialogMNDemand::m_PlayerInfo_lock;
MNCategory CGUIDialogMNDemand::m_OnDemand;

CGUIDialogMNDemand::CGUIDialogMNDemand()
: CGUIDialog(WINDOW_DIALOG_MN_DEMAND, "DialogNationWideOndemand.xml")
{
  m_loadType = KEEP_IN_MEMORY;
  m_MNDemand = this;
}

CGUIDialogMNDemand::~CGUIDialogMNDemand()
{
  m_MNDemand = NULL;
}

bool CGUIDialogMNDemand::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
    case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      bool selectAction = (message.GetParam1() == ACTION_SELECT_ITEM ||
                           message.GetParam1() == ACTION_MOUSE_LEFT_CLICK);
      if (selectAction && iControl == ONDEMAND_LIST)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND_LIST);
        OnMessage(msg);
        int listItem = msg.GetParam1();

        CFileItem item;
        item.SetLabel2(m_OnDemand.items[listItem].title);
        item.SetPath(CSpecialProtocol::TranslatePath(m_OnDemand.items[listItem].video_localpath));

        item.GetVideoInfoTag()->m_strTitle = m_OnDemand.items[listItem].title;
        item.GetVideoInfoTag()->m_streamDetails.Reset();
        CMediaSettings::GetInstance().SetVideoStartWindowed(false);
        g_playlistPlayer.Add(PLAYLIST_VIDEO, (CFileItemPtr) &item);
        return true;
      }
    }
    case GUI_MSG_WINDOW_DEINIT:
    {
      CGUIWindow::OnMessage(message);
      return true;
    }
    case GUI_MSG_WINDOW_INIT:
    {
      CGUIWindow::OnMessage(message);
      FillAssets();
      return true;
    }
  }
  return CGUIDialog::OnMessage(message);
}

void CGUIDialogMNDemand::OnInitWindow()
{
  CGUIWindow::OnInitWindow();
}

void CGUIDialogMNDemand::OnWindowUnload()
{
}

void CGUIDialogMNDemand::SetInfo(PlayerSettings *playerInfo, const float version)
{
//  CSingleLock lock(m_PlayerInfo_lock);
  m_PlayerInfo = playerInfo;
  m_Version    = version;
}

void CGUIDialogMNDemand::FillAssets()
{
  SendMessage(GUI_MSG_LABEL_RESET, GetID(), ONDEMAND_LIST);
  CFileItemList stackItems;
  for (size_t i = 0; i < m_OnDemand.items.size(); i++)
  {
    CFileItemPtr pItem = CFileItemPtr(new CFileItem(m_OnDemand.items[i].title));
    pItem->SetPath(m_OnDemand.items[i].video_url.c_str());
    pItem->SetArt("thumb", m_OnDemand.items[i].thumb_localpath);
    CVideoInfoTag* setInfo = pItem->GetVideoInfoTag();
    setInfo->m_strPath = pItem->GetPath();
    setInfo->m_strTitle = pItem->GetLabel();
    stackItems.Add(pItem);
  }
  CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), ONDEMAND_LIST, 0, 0, &stackItems);
  OnMessage(msg);

  CGUIMessage msg1(GUI_MSG_SETFOCUS, GetID(), ONDEMAND_LIST);
  OnMessage(msg1);
}

void CGUIDialogMNDemand::SetControlLabel(int id, const char *format, int info)
{
  std::string tmpStr = StringUtils::Format(format, g_infoManager.GetLabel(info).c_str());
  SET_CONTROL_LABEL(id, tmpStr);
}

bool CGUIDialogMNDemand::OnAction(const CAction& action)
{
  if (action.GetID() == ACTION_PREVIOUS_MENU ||
      action.GetID() == ACTION_NAV_BACK)
  {
    if (g_application.m_pPlayer->IsPlaying())
//      CApplicationMessenger::Get().MediaStop();
      g_application.StopPlaying();
    else
      Close();
    return true;
  }
  return CGUIDialog::OnAction(action);
}

CGUIDialogMNDemand* CGUIDialogMNDemand::GetDialogMNDemand()
{
  return m_MNDemand;
}

void CGUIDialogMNDemand::GetDialogMNCategory(MNCategory &category)
{
  category = m_OnDemand;
}

void CGUIDialogMNDemand::SetDialogMNCategory(const MNCategory &category)
{
  m_OnDemand = category;
}
