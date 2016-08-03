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

#include "CGUIWindowMNDemand.h"
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

#define ONDEMAND_ITEM_LIST          1050
#define ONDEMAND_CATEGORY_LIST      50

PlayerSettings* CGUIWindowMNDemand::m_PlayerInfo = NULL;
float CGUIWindowMNDemand::m_Version;
CGUIWindowMNDemand *CGUIWindowMNDemand::m_MNDemand = NULL;
CCriticalSection CGUIWindowMNDemand::m_PlayerInfo_lock;
MNCategory CGUIWindowMNDemand::m_OnDemand;
NWMediaPlaylist CGUIWindowMNDemand::m_MediaPlayList;

CGUIWindowMNDemand::CGUIWindowMNDemand()
: CGUIWindow(WINDOW_MEMBERNET_DEMAND, "DialogNationWideOndemand.xml")
{
  m_loadType = KEEP_IN_MEMORY;
  m_MNDemand = this;
}

CGUIWindowMNDemand::~CGUIWindowMNDemand()
{
  m_MNDemand = NULL;
}

bool CGUIWindowMNDemand::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
    case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      bool selectAction = (message.GetParam1() == ACTION_SELECT_ITEM ||
                           message.GetParam1() == ACTION_MOUSE_LEFT_CLICK);
      if (selectAction && iControl == ONDEMAND_ITEM_LIST)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND_ITEM_LIST);
        OnMessage(msg);
        int listItem = msg.GetParam1();

        CGUIMessage msg1(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND_CATEGORY_LIST);
        OnMessage(msg1);
        int category = msg1.GetParam1();
        
        CFileItem item;
      
        NWAsset asset = m_MediaPlayList.groups[category].assets[listItem];
        CFileItemPtr pItem = CFileItemPtr(new CFileItem(asset.name));
        std::string path = asset.video_localpath;
        if(path.empty())
          path = asset.video_url;
        
        std::string thumb = asset.thumb_localpath;
        if(thumb.empty())
          thumb = asset.thumb_url;
        pItem->SetLabel2(asset.name);
        pItem->SetPath(CSpecialProtocol::TranslatePath(path));

        pItem->GetVideoInfoTag()->m_strTitle = asset.name;
        pItem->GetVideoInfoTag()->m_streamDetails.Reset();
        CMediaSettings::GetInstance().SetVideoStartWindowed(true);
        g_playlistPlayer.Add(PLAYLIST_VIDEO, pItem);
        g_playlistPlayer.SetRepeat(PLAYLIST_VIDEO, PLAYLIST::REPEAT_NONE, false);
        g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
        // do not call g_playlistPlayer.Play directly, we are not on main thread.
        KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_PLAYLISTPLAYER_PLAY, 0);
        return true;
      }
    }
//    case GUI_MSG_WINDOW_DEINIT:
//    {
//      CGUIWindow::OnMessage(message);
//      return true;
//    }
    case GUI_MSG_WINDOW_INIT:
    {
      CGUIWindow::OnMessage(message);
      FillAssets();
      return true;
    }
  }
  return CGUIWindow::OnMessage(message);
}

void CGUIWindowMNDemand::OnInitWindow()
{
  CGUIWindow::OnInitWindow();
}

void CGUIWindowMNDemand::OnWindowUnload()
{
}

void CGUIWindowMNDemand::SetInfo(PlayerSettings *playerInfo, const float version)
{
//  CSingleLock lock(m_PlayerInfo_lock);
  m_PlayerInfo = playerInfo;
  m_Version    = version;
}

void CGUIWindowMNDemand::FillAssets()
{
  if (1)
  {
    SendMessage(GUI_MSG_LABEL_RESET, GetID(), ONDEMAND_CATEGORY_LIST);
    CFileItemList stackItems;
    for (size_t i = 0; i < m_MediaPlayList.groups.size(); i++)
    {
      CFileItemPtr pItem = CFileItemPtr(new CFileItem(m_MediaPlayList.groups[i].name));
//      pItem->SetPath(m_OnDemand.items[i].video_url.c_str());
//      pItem->SetArt("thumb", m_OnDemand.items[i].thumb_localpath);
//      pItem->GetVideoInfoTag()->m_strPath = pItem->GetPath();
//      pItem->GetVideoInfoTag()->m_strTitle = pItem->GetLabel();
      stackItems.Add(pItem);
    }
    CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), ONDEMAND_CATEGORY_LIST, 0, 0, &stackItems);
    OnMessage(msg);
  }
  else
  {
    /// hack to display both lists, needs to be removed
    SendMessage(GUI_MSG_LABEL_RESET, GetID(), ONDEMAND_CATEGORY_LIST);
    CFileItemList stackItems;
    for (int i = 0; i < 20; i++)
    {
      std::string label = StringUtils::Format("test Category %i", i);
      CFileItemPtr pItem = CFileItemPtr(new CFileItem(label));
      pItem->SetPath("some path");
      pItem->SetArt("thumb", "some thumb path");
      pItem->GetVideoInfoTag()->m_strPath = pItem->GetPath();
      pItem->GetVideoInfoTag()->m_strTitle = label;
      stackItems.Add(pItem);
    }
    CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), ONDEMAND_CATEGORY_LIST, 0, 0, &stackItems);
    OnMessage(msg);
    
    SendMessage(GUI_MSG_LABEL_RESET, GetID(), ONDEMAND_ITEM_LIST);
    CFileItemList fakeListItems;
    for (int i = 0; i < 20; i++)
    {
      std::string label = StringUtils::Format("test Clip %i", i);
      CFileItemPtr pItem = CFileItemPtr(new CFileItem(label));
      pItem->SetPath("some path");
      pItem->SetArt("thumb", "some thumb path");
      pItem->GetVideoInfoTag()->m_strPath = pItem->GetPath();
      pItem->GetVideoInfoTag()->m_strTitle = label;
      pItem->GetVideoInfoTag()->m_duration = 3600; //seconds
      fakeListItems.Add(pItem);
    }
    

    CGUIMessage msg0(GUI_MSG_LABEL_BIND, GetID(), ONDEMAND_ITEM_LIST, 0, 0, &fakeListItems);
    OnMessage(msg0);
    //// ----------------- end of hack ----------------------
  }

  CGUIMessage msg1(GUI_MSG_SETFOCUS, GetID(), ONDEMAND_CATEGORY_LIST);
  OnMessage(msg1);
  
  CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND_CATEGORY_LIST);
  OnMessage(msg);
  
  int item = msg.GetParam1();
  SetCategoryItems(item);
}

void CGUIWindowMNDemand::SetControlLabel(int id, const char *format, int info)
{
  std::string tmpStr = StringUtils::Format(format, g_infoManager.GetLabel(info).c_str());
  SET_CONTROL_LABEL(id, tmpStr);
}

bool CGUIWindowMNDemand::OnAction(const CAction& action)
{
  if (action.GetID() == ACTION_PREVIOUS_MENU ||
      action.GetID() == ACTION_NAV_BACK)
  {
    if (g_application.m_pPlayer->IsPlaying())
      KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_STOP, 0);
    else
      g_windowManager.PreviousWindow();
    return true;
  }
  else if (GetFocusedControlID() == ONDEMAND_CATEGORY_LIST)
  {
    bool ret = CGUIWindow::OnAction(action);
    
    CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), ONDEMAND_CATEGORY_LIST);
    OnMessage(msg);
    
    int item = msg.GetParam1();
    SetCategoryItems(item);
    
    return ret;
  }
  return CGUIWindow::OnAction(action);
}

CGUIWindowMNDemand* CGUIWindowMNDemand::GetDialogMNDemand()
{
  return m_MNDemand;
}

void CGUIWindowMNDemand::GetDialogMNPlaylist(NWMediaPlaylist &mediaPlayList)
{
  mediaPlayList = m_MediaPlayList;
}

void CGUIWindowMNDemand::SetDialogMNPlaylist(const NWMediaPlaylist mediaPlayList)
{
  m_MediaPlayList = mediaPlayList;
}

void CGUIWindowMNDemand::SetCategoryItems(const int category)
{
  SendMessage(GUI_MSG_LABEL_RESET, GetID(), ONDEMAND_ITEM_LIST);
  CFileItemList ListItems;
  NWGroup group = m_MediaPlayList.groups[category];
  for (size_t i = 0; i < group.assets.size(); i++)
  {
    NWAsset asset = group.assets[i];
    CFileItemPtr pItem = CFileItemPtr(new CFileItem(asset.name));
    std::string path = asset.video_localpath;
    if(path.empty())
      path = asset.video_url;
    
    std::string thumb = asset.thumb_localpath;
    if(thumb.empty())
      thumb = asset.thumb_url;
    
    pItem->SetPath(path);
    pItem->SetArt("thumb", thumb);
    pItem->GetVideoInfoTag()->m_strPath = pItem->GetPath();
    pItem->GetVideoInfoTag()->m_strTitle = asset.name;
    //          pItem->GetVideoInfoTag()->m_duration = 3600; //seconds
    ListItems.Add(pItem);
  }
  CGUIMessage msg0(GUI_MSG_LABEL_BIND, GetID(), ONDEMAND_ITEM_LIST, 0, 0, &ListItems);
  OnMessage(msg0);

}
