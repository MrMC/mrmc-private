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


#include <string>

#include "cores/IPlayerCallback.h"
#include "guilib/GUIWindow.h"
#include "MNMedia.h"
#include "NWTVAPI.h"

class  CPlayerManagerMN;
struct MNMediaAsset;

class CGUIWindowMNDemand : public CGUIWindow
{
public:
  CGUIWindowMNDemand();
  virtual ~CGUIWindowMNDemand();

  virtual bool  OnMessage(CGUIMessage& message);
  virtual void  OnInitWindow();
  virtual void  OnWindowUnload();
  virtual bool  OnAction(const CAction& action);
  void          FillAssets();
  static  void  SetInfo(PlayerSettings *playerInfo, const float version);
  void          SetControlLabel(int id, const char *format, int info);
  static  CGUIWindowMNDemand* GetDialogMNDemand();
  static void   GetDialogMNPlaylist(NWMediaPlaylist &mediaPlayList);
  static void   SetDialogMNPlaylist(const NWMediaPlaylist mediaPlayList);


protected:
  static float       m_Version;
  static PlayerSettings *m_PlayerInfo;
  static CGUIWindowMNDemand *m_MNDemand;
  static CCriticalSection m_PlayerInfo_lock;
  static MNCategory  m_OnDemand;
  static NWMediaPlaylist m_MediaPlayList;
  void SetCategoryItems(const int category);
};
