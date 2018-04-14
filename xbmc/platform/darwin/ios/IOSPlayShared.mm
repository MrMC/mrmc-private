/*
 *      Copyright (C) 2015 Team MrMC
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

#import "system.h"

#import "IOSPlayShared.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "FileItem.h"
#include "DatabaseManager.h"
#include "guilib/GUIWindowManager.h"
#include "pictures/GUIWindowSlideShow.h"
#include "video/VideoThumbLoader.h"
#include "video/VideoInfoTag.h"
#include "video/dialogs/GUIDialogVideoInfo.h"
#include "video/windows/GUIWindowVideoNav.h"
#include "video/windows/GUIWindowVideoBase.h"
#include "filesystem/File.h"
#include "utils/LiteUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <mach/mach_host.h>
#import <sys/sysctl.h>

std::string CIOSPlayShared::m_url;
bool        CIOSPlayShared::m_handleUrl;

CIOSPlayShared::CIOSPlayShared()
{
}

CIOSPlayShared::~CIOSPlayShared()
{
}

CIOSPlayShared &CIOSPlayShared::GetInstance()
{
  static CIOSPlayShared sTopShelf;
  return sTopShelf;
}

void CIOSPlayShared::HandlePlaybackUrl(const std::string& url, const bool run)
{
  m_handleUrl = run;
  m_url       = url;
}

bool CIOSPlayShared::RunPlayback()
{
  bool rtn = false;
  if (m_handleUrl)
  {
    m_handleUrl = false;
    CFileItemPtr pItem(new CFileItem(m_url,false));

    if (pItem->IsAudio() || pItem->IsVideo())
    {
      int playlist = pItem->IsAudio() ? PLAYLIST_MUSIC : PLAYLIST_VIDEO;
      g_playlistPlayer.ClearPlaylist(playlist);
      g_playlistPlayer.SetCurrentPlaylist(playlist);
      g_application.PlayMedia(*pItem, playlist);
    }
    else if (pItem->IsPicture())
    {
      CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)g_windowManager.GetWindow(WINDOW_SLIDESHOW);
      if (!pSlideShow)
        return false;
      if (g_application.m_pPlayer->IsPlayingVideo())
        g_application.StopPlaying();

      pSlideShow->Reset();
      pSlideShow->Add(pItem.get());
      pSlideShow->Select(pItem->GetPath());

      g_windowManager.ActivateWindow(WINDOW_SLIDESHOW);
    }
  }
  return rtn;
}
