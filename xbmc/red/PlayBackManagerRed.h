#pragma once

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

#include "XBDateTime.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"

#include "RedMedia.h"

typedef void (*PlayBackCallBackFn)(const void *ctx, int msg, RedMediaAsset &asset);

class CPlayBackManagerRed : public CThread
{
public:
  CPlayBackManagerRed();
  virtual ~CPlayBackManagerRed();

  void          Reset();
  void          Play();
  void          PlayPause();
  void          PlayNext();
  void          Pause();
  bool          IsPlaying();
  void          StopPlaying();
  void          OverridePlayBackWindow(bool override);
  void          SetPlayBackTime(const CDateTime &time, const CDateTimeSpan &duration);
  void          ValidateAsset(RedMediaAsset &asset, bool valid);
  void          QueueMediaGroup(RedMediaGroup &mediagroup);
  void          RegisterPlayBackCallBack(const void *ctx, PlayBackCallBackFn fn);

protected:
  virtual void  Process();
  bool          Exists(RedMediaGroup &mediagroup);

  bool                  m_playing;
  CCriticalSection      m_media_lock;
  CCriticalSection      m_player_lock;
  std::vector<RedMediaGroup> m_mediagroups;
  bool                  m_Override;
  CDateTime             m_PlaybackTime;
  CDateTimeSpan         m_PlayBackDuration;

  PlayBackCallBackFn    m_PlayBackCallBackFn;
  const void           *m_PlayBackCallBackCtx;
};
