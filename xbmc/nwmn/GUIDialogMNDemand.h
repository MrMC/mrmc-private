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
#include "guilib/GUIDialog.h"
#include "MNMedia.h"

class  CPlayerManagerMN;
struct MNMediaAsset;

class CGUIDialogMNDemand : public CGUIDialog
{
public:
  CGUIDialogMNDemand();
  virtual ~CGUIDialogMNDemand();

  virtual bool  OnMessage(CGUIMessage& message);
  virtual void  OnInitWindow();
  virtual void  OnWindowUnload();
  virtual bool  OnAction(const CAction& action);
  void          FillAssets();
  static  void  SetInfo(PlayerSettings *playerInfo, const float version);
  void          SetControlLabel(int id, const char *format, int info);
  static  CGUIDialogMNDemand* GetDialogMNDemand();
  static void   GetDialogMNCategory(MNCategory &category);
  static void   SetDialogMNCategory(const MNCategory &category);


protected:
  static float       m_Version;
  static PlayerSettings *m_PlayerInfo;
  static CGUIDialogMNDemand *m_MNDemand;
  static CCriticalSection m_PlayerInfo_lock;
  static MNCategory  m_OnDemand;
};
