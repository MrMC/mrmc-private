/*
 *  Copyright (C) 2016 RootCoder, LLC.
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

#include "NWPlayer.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "playlists/PlayList.h"
#include "FileItem.h"
#include "filesystem/File.h"
#include "guilib/GUIWindowManager.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/MediaSettings.h"
#include "video/VideoInfoTag.h"
#include "video/VideoThumbLoader.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

using namespace KODI::MESSAGING;

CNWPlayer::CNWPlayer()
 : CThread("CNWPlayer")
 , m_playing(false)
 , m_Override(false)
 , m_PlaybackTime(CDateTime::GetCurrentDateTime())
 , m_PlayBackDuration(0, 24, 0, 0)
 , m_PlayerCallBackFn(NULL)
 , m_PlayerCallBackCtx(NULL)
{
}

CNWPlayer::~CNWPlayer()
{
  m_PlayerCallBackFn = NULL;
  m_bStop = true;
  StopThread();
  Reset();
}

void CNWPlayer::Reset()
{
  CSingleLock lock(m_media_lock);
  StopPlaying();
  // new empty playlist
  m_playlist = NWPlaylist();
  CApplicationMessenger::GetInstance().SendMsg(TMSG_PLAYLISTPLAYER_CLEAR, PLAYLIST_VIDEO);
}

void CNWPlayer::Play()
{
  CSingleLock lock(m_player_lock);
  CLog::Log(LOGDEBUG, "**NW** - CNWPlayer::Play() playback enabled");
  CApplicationMessenger::GetInstance().SendMsg(TMSG_PLAYLISTPLAYER_CLEAR, PLAYLIST_VIDEO);
  m_playing = true;
}

void CNWPlayer::PlayPause()
{
  // PlayPause is a toggle (play or unpause)/pause.
  CSingleLock lock(m_player_lock);
  if (g_application.m_pPlayer->IsPaused())
    CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_UNPAUSE);
  else
  {
    if (!m_playing)
      m_playing = true;
    else
      CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_PAUSE);
  }
}

void CNWPlayer::PlayNext()
{
  CSingleLock lock(m_player_lock);
  // if m_playing is true, then just
  // doing the StopPlaying on player will cause
  // next pass in Process to sequence to next.
  if (m_playing)
    CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_STOP);
  else
    m_playing = true;

  g_playlistPlayer.Clear();
  g_playlistPlayer.Reset();
}

void CNWPlayer::Pause()
{
  // Pause is a toggle, pause/unpause
  CSingleLock lock(m_player_lock);
  CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_PAUSE);
}

bool CNWPlayer::IsPlaying()
{
  CSingleLock lock(m_player_lock);
  return m_playing;
}

void CNWPlayer::StopPlaying()
{
  CSingleLock lock(m_player_lock);
  m_playing = false;
  CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_STOP);
}

void CNWPlayer::OverridePlayBackWindow(bool override)
{
  m_Override = override;
}

void CNWPlayer::SetPlayBackTime(const CDateTime &time, const CDateTimeSpan &duration)
{
  m_PlaybackTime = time;
  m_PlayBackDuration = duration;
}

void CNWPlayer::MarkValidated(NWAsset &asset)
{
  CSingleLock lock(m_media_lock);
  for (size_t g = 0; g < m_playlist.groups.size(); g++)
  {
    if (m_playlist.groups[g].id == asset.group_id)
    {
      for (size_t a = 0; a < m_playlist.groups[g].assets.size(); a++)
      {
        if (m_playlist.groups[g].assets[a].id == asset.id)
        {
          m_playlist.groups[g].assets[a].valid = true;
          CLog::Log(LOGDEBUG, "**NW** - NW Asset - %s validated", m_playlist.groups[g].assets[a].video_localpath.c_str());
          break;
        }
      }
    }
  }
}

void CNWPlayer::QueueProgramInfo(NWPlaylist &playlist)
{
  CSingleLock lock(m_media_lock);
  m_playlist = playlist;
}

void CNWPlayer::RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn)
{
  m_PlayerCallBackFn = fn;
  m_PlayerCallBackCtx = ctx;
}

void CNWPlayer::Process()
{
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);
  CLog::Log(LOGDEBUG, "**NW** - CNWPlayer::Process Started");

  while (!m_bStop)
  {
    Sleep(100);

    CSingleLock player_lock(m_player_lock);
    
    int current = g_playlistPlayer.GetCurrentSong();
    int count = g_playlistPlayer.GetPlaylist(PLAYLIST_VIDEO).size();
    if (m_playing && (current == -1 || (count - current) < 2))
    {
      //CLog::Log(LOGDEBUG, "**NW** - CNWPlayer::playlist current(%d), count(%d)", current, count);

      CDateTime cur = CDateTime::GetCurrentDateTime();
      // playback can only occur in a datetime window.
      if (m_Override || cur >= m_PlaybackTime)
      {
        CDateTime end = m_PlaybackTime + m_PlayBackDuration;
        if (!m_Override && cur >= end)
        {
          // complicated but required. we get playback time as a
          // hh:mm:ss field, duration is in hours. So we have to
          // be able to span over the beginning or end of a 24 hour day.
          // for example. bgn at 6pm, end at 6am. bgn would be 18:00:00,
          // duration would be 12.
          CDateTime cur = CDateTime::GetCurrentDateTime();
          CDateTime bgn = m_PlaybackTime;
          m_PlaybackTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), bgn.GetHour(), bgn.GetMinute(), bgn.GetSecond());
          continue;
        }

        CSingleLock asset_lock(m_media_lock);
        if (!m_playlist.groups.empty())
        {
          int groupID = m_playlist.play_order.front();
          m_playlist.play_order.erase(m_playlist.play_order.begin());
          m_playlist.play_order.push_back(groupID);
          
          // check if we already have handled this group
          auto group = std::find_if(m_playlist.groups.begin(), m_playlist.groups.end(),
            [groupID](const NWGroup &existingGroup) { return existingGroup.id == groupID; });
          if (group != m_playlist.groups.end() && !group->assets.empty())
          {
            NWAsset asset = group->assets.front();
            group->assets.erase(group->assets.begin());
            group->assets.push_back(asset);
            asset_lock.Leave();

            // now play the asset if valid (downloaded and md5 checked)
            if (asset.valid)
            {
              CLog::Log(LOGDEBUG, "**NW** - CNWPlayer::queue group(%d), asset(%d)", group->id, asset.id);

              CFileItemPtr item(new CFileItem());
              item->SetLabel2(asset.name);
              item->SetPath(asset.video_localpath);
              
              item->GetVideoInfoTag()->m_strTitle = asset.name;
              item->GetVideoInfoTag()->m_streamDetails.Reset();
              item->GetVideoInfoTag()->m_iDbId = -1;
              item->GetVideoInfoTag()->m_iFileId = -1;
              if (XFILE::CFile::Exists(asset.thumb_localpath))
                item->SetArt("thumb", asset.thumb_localpath);

              CMediaSettings::GetInstance().SetVideoStartWindowed(true);

              auto fileItemList = new CFileItemList();
              fileItemList->Add(item);
              CApplicationMessenger::GetInstance().SendMsg(TMSG_PLAYLISTPLAYER_ADD, PLAYLIST_VIDEO, -1, static_cast<void*>(fileItemList));

              // play!
              if (g_playlistPlayer.GetPlaylist(PLAYLIST_VIDEO).size() == 1)
              {
                g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
                CApplicationMessenger::GetInstance().PostMsg(TMSG_PLAYLISTPLAYER_PLAY, 0);
              }

              if (m_PlayerCallBackFn)
                (*m_PlayerCallBackFn)(m_PlayerCallBackCtx, 0, asset);

              while (!m_bStop && !g_application.m_pPlayer->IsPlaying())
                Sleep(100);
            }
          }
        }
      }
    }
  }
  
  CLog::Log(LOGDEBUG, "**NW** - CNWPlayer::Process Stopped");
}

bool CNWPlayer::Exists(NWGroup &testgroup)
{
  for (auto group : m_playlist.groups)
  {
    if (group.id == testgroup.id)
      return true;
  }
  
  return false;
}
