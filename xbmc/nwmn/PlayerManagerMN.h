#pragma once

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

#include <queue>
#include <string>
#include "XBDateTime.h"
#include "filesystem/CurlFile.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "dialogs/GUIDialogProgress.h"
#include "interfaces/IAnnouncer.h"

#include "MNMedia.h"

typedef void (*PlayerCallBackFn)(const void *ctx, bool status);

class CLogManagerMN;
class CUpdateManagerMN;

class CPlayerManagerMN : public CThread, public ANNOUNCEMENT::IAnnouncer
{
public:
  CPlayerManagerMN();
  virtual ~CPlayerManagerMN();

  static CPlayerManagerMN* GetPlayerManager();
  void          SetSettings(PlayerSettings settings);
  void          Startup();
  void          FullUpdate();
  void          PlayPause();
  void          PausePlaying();
  void          StopPlaying();
  void          PlayNext();
  void          PlayPrevious();
  
  PlayerSettings GetSettings();
  std::vector<MNCategory> GetCategories();
  MNCategory     GetOndemand();

  void          RegisterPlayerCallBack(const void *ctx, PlayerCallBackFn fn);
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data);
  void          CreatePlaylist();
  void          CheckAssets();
  void          StartDialog();
  void          CloseDialog();

protected:
  virtual void  Process();
  
  bool          CheckAndDownloadAssets();

  std::string   m_strHome;
  bool          m_Startup;
  bool          m_HasNetwork;
  bool          m_CreatePlaylist;
  bool          m_CheckAssets;
  CDateTime     m_NextUpdateTime;
  CDateTimeSpan m_NextUpdateInterval;
  CDateTimeSpan m_NextReportInterval;
  CDateTime     m_NextDownloadTime;
  CDateTime     m_PlaybackTime;
  CDateTimeSpan m_PlaybackDuration;
  
  XFILE::CCurlFile m_http;
  std::string      m_http_title;
  int              m_http_count;
  PlayerSettings   m_settings;
  std::vector<MNCategory> m_categories;
  MNCategory       m_OnDemand;

  CLogManagerMN    *m_LogManager;
  CUpdateManagerMN *m_UpdateManager;

  static CCriticalSection m_player_lock;
  static CPlayerManagerMN *m_PlayerManager;
  CGUIDialogProgress* m_dlgProgress;

  PlayerCallBackFn m_PlayerCallBackFn;
  const void    *m_PlayerCallBackCtx;
};
