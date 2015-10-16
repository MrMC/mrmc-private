/*
 *  Copyright (C) 2014 Team RED
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

#include "PlayBackManagerRed.h"
#include "UtilitiesRed.h"

#include "Application.h"
#include "messaging/ApplicationMessenger.h"
#include "FileItem.h"
#include "filesystem/File.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/MediaSettings.h"
#include "video/VideoInfoTag.h"
#include "video/VideoThumbLoader.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

using namespace KODI::MESSAGING;

CPlayBackManagerRed::CPlayBackManagerRed()
 : CThread("CPlayBackManagerRed")
 , m_playing(false)
 , m_Override(false)
 , m_PlaybackTime(CDateTime::GetCurrentDateTime())
 , m_PlayBackDuration(0, 24, 0, 0)
 , m_PlayBackCallBackFn(NULL)
 , m_PlayBackCallBackCtx(NULL)
{
}

CPlayBackManagerRed::~CPlayBackManagerRed()
{
  m_PlayBackCallBackFn = NULL;
  StopThread();
  Reset();
}

void CPlayBackManagerRed::Reset()
{
  CSingleLock lock(m_media_lock);
  StopPlaying();
  m_mediagroups.clear();
}

void CPlayBackManagerRed::Play()
{
  CSingleLock lock(m_player_lock);
  CLog::Log(LOGDEBUG, "**RED** - CPlayBackManagerRed::Play() playback enabled");
  m_playing = true;
}

void CPlayBackManagerRed::PlayPause()
{
  // PlayPause is a toggle (play or unpause)/pause.
  CSingleLock lock(m_player_lock);
  if (g_application.m_pPlayer->IsPaused())
    CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_UNPAUSE);
  else
  {
    if (!m_playing)
      m_playing = true;
    else
      CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_PAUSE);
  }
}

void CPlayBackManagerRed::PlayNext()
{
  CSingleLock lock(m_player_lock);
  // if m_playing is true, then just
  // doing the StopPlaying on player will cause
  // next pass in Process to sequence to next.
  if (m_playing)
    CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
  else
    m_playing = true;
}

void CPlayBackManagerRed::Pause()
{
  // Pause is a toggle, pause/unpause
  CSingleLock lock(m_player_lock);
  CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_PAUSE);
}

bool CPlayBackManagerRed::IsPlaying()
{
  CSingleLock lock(m_player_lock);
  return m_playing;
}

void CPlayBackManagerRed::StopPlaying()
{
  CSingleLock lock(m_player_lock);
  m_playing = false;
  CApplicationMessenger::GetInstance().SendMsg(TMSG_MEDIA_STOP);
}

void CPlayBackManagerRed::OverridePlayBackWindow(bool override)
{
  m_Override = override;
}

void CPlayBackManagerRed::SetPlayBackTime(const CDateTime &time, const CDateTimeSpan &duration)
{
  m_PlaybackTime = time;
  m_PlayBackDuration = duration;
}

void CPlayBackManagerRed::ValidateAsset(RedMediaAsset &asset, bool valid)
{
  CSingleLock lock(m_media_lock);
  if (!m_mediagroups.empty())
  {
    for (size_t g = 0; g < m_mediagroups.size(); g++)
    {
      if (m_mediagroups[g].id == asset.mediagroup_id)
      {
        for (size_t a = 0; a < m_mediagroups[g].assets.size(); a++)
        {
          if (m_mediagroups[g].assets[a].id == asset.id)
          {
            m_mediagroups[g].assets[a].valid = valid;
            CLog::Log(LOGDEBUG, "**RED** - Red Asset - %s validated", m_mediagroups[g].assets[a].localpath.c_str());
            break;
          }
        }
      }
    }
  }
}

void CPlayBackManagerRed::QueueMediaGroup(RedMediaGroup &mediagroup)
{
  CSingleLock lock(m_media_lock);
  if (!Exists(mediagroup))
    m_mediagroups.push_back(mediagroup);
}

void CPlayBackManagerRed::RegisterPlayBackCallBack(const void *ctx, PlayBackCallBackFn fn)
{
  m_PlayBackCallBackFn = fn;
  m_PlayBackCallBackCtx = ctx;
}

void CPlayBackManagerRed::Process()
{
  CLog::Log(LOGDEBUG, "**RED** - CPlayBackManagerRed::Process Started");

  while (!m_bStop)
  {
    Sleep(100);

    CSingleLock player_lock(m_player_lock);
    if (m_playing && !g_application.m_pPlayer->IsPlaying() && !g_application.m_pPlayer->IsPaused())
    {
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
        if (!m_mediagroups.empty())
        {
          RedMediaGroup group = m_mediagroups.front();
          if (!group.assets.empty())
          {
            // check mediagroup start/end date
            CDateTime time = CDateTime::GetCurrentDateTime();
            if (time > group.startDate && time < group.endDate)
            {
              size_t assetIndex = strtol(group.assetIndex.c_str(), NULL, 10);
              if (assetIndex >= group.assets.size())
              {
                if (group.playbackType == "random" && group.assets.size() > 3)
                {
                  // random shuffle the assset list
                  std::random_shuffle(group.assets.begin(), group.assets.end());
                  std::random_shuffle(&group.assets[0], &group.assets[group.assets.size()-1]);

                  // check that the 1st asset has not played in the last 3 playback cycles
                  // check that the 2nd asset has not played in the last 2 playback cycles
                  // check that the 3rd asset has not played in the last 1 playback cycles
                  size_t asset_indx = 0;
                  std::vector<std::string>::iterator lastPlayedIds;
                  for(lastPlayedIds = group.lastPlayedId.begin(); lastPlayedIds != group.lastPlayedId.end(); ++lastPlayedIds)
                  {
                    // randomize list until the id does not match
                    // notice we increment into asset list to check 1st, 2nd and 3rd.
                    while(*lastPlayedIds == group.assets[asset_indx].id)
                      std::random_shuffle(&group.assets[asset_indx], &group.assets[group.assets.size()-1]);
                    asset_indx++;
                    // paranoid check, we should never have this condition
                    // as we must have four or more assets and the lastPlayedId
                    // is limited to three items.
                    if (asset_indx >= group.assets.size())
                      break;
                  }
                }
                assetIndex = 0;
              }
              // fetch the indexed asset and bump the index
              RedMediaAsset asset = group.assets[assetIndex++];
              group.assetIndex = StringUtils::Format("%zu", assetIndex);
              // remember the asset id for this group for the next three cycle
              group.lastPlayedId.push_back(asset.id);
              if (group.lastPlayedId.size() > 3)
                group.lastPlayedId.erase(group.lastPlayedId.begin());

              // sequence to next media group for next cycle
              m_mediagroups.erase(m_mediagroups.begin());
              m_mediagroups.push_back(group);
              
              std::string bgn_str = group.startDate.GetAsLocalizedDateTime(false, false);
              std::string end_str = group.endDate.GetAsLocalizedDateTime(false, false);
              std::string time_str = time.GetAsLocalizedDateTime(false, false);
              CLog::Log(LOGDEBUG, "**RED** - CPlayBackManagerRed::valid asset(%s), media(%s) time(%s), bgn(%s), end(%s)",
                asset.id.c_str(), group.id.c_str(), time_str.c_str(), bgn_str.c_str(), end_str.c_str());

              asset_lock.Leave();
   
              // now play the asset if valid (downloaded and md5 checked)
              if (asset.valid)
              {
                CFileItem item;
                item.SetLabel2(asset.name);
                item.SetPath(asset.localpath);
                
                g_playlistPlayer.ClearPlaylist(g_playlistPlayer.GetCurrentPlaylist());
                g_playlistPlayer.Reset();
                
                int iPlayList = PLAYLIST_MUSIC;
                
                if (IsRedVideo(asset)) // Video file
                {
                  item.GetVideoInfoTag()->m_strTitle = asset.name;
                  item.GetVideoInfoTag()->m_streamDetails.Reset();
                  item.GetVideoInfoTag()->m_artist.push_back(asset.artist); // this sets musicvideo
                  item.GetVideoInfoTag()->m_strAlbum = asset.album;
                  item.GetVideoInfoTag()->m_genre.push_back(asset.genre);
                  item.GetVideoInfoTag()->m_iYear = strtol(asset.year.c_str(), NULL, 10);
                  item.GetVideoInfoTag()->m_iDbId = -1;
                  item.GetVideoInfoTag()->m_iFileId = -1;
                  CMediaSettings::GetInstance().SetVideoStartWindowed(true);
                  iPlayList = PLAYLIST_VIDEO;
                }
      
                
                else //Audio file
                {
                  item.GetMusicInfoTag()->SetTitle(asset.name);
                  item.GetMusicInfoTag()->SetArtist(asset.artist);
                  item.GetMusicInfoTag()->SetAlbum(asset.album);
                  item.GetMusicInfoTag()->SetYear(strtol(asset.year.c_str(), NULL, 10));
                  item.GetMusicInfoTag()->SetTrackNumber(strtol(asset.tracknumber.c_str(), NULL, 10));
                  item.GetMusicInfoTag()->SetGenre(asset.genre);
                  
                }
                
                if (XFILE::CFile::Exists(asset.thumbnail_localpath))
                  item.SetArt("thumb", asset.thumbnail_localpath);
                else
                  item.SetArt("thumb", asset.thumbnail_url);
                
                g_playlistPlayer.Add(iPlayList, (CFileItemPtr) &item);
                g_playlistPlayer.SetCurrentPlaylist(iPlayList);
                
                // play!
                g_playlistPlayer.Play();
                
                if (m_PlayBackCallBackFn)
                  (*m_PlayBackCallBackFn)(m_PlayBackCallBackCtx, 0, asset);
              }
            }
            else
            {
              // sequence to next group for next cycle
              m_mediagroups.erase(m_mediagroups.begin());
              m_mediagroups.push_back(group);
              asset_lock.Leave();
            }
          }
        }
      }
    }
  }
  
  CLog::Log(LOGDEBUG, "**RED** - CPlayBackManagerRed::Process Stopped");
}

bool CPlayBackManagerRed::Exists(RedMediaGroup &mediagroup)
{
  for (size_t index = 0; index < m_mediagroups.size(); index++)
  {
    if (mediagroup.id == m_mediagroups[index].id)
      return true;
  }
  
  return false;
}


