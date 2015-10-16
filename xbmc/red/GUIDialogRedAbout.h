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


#include <string>

#include "cores/IPlayerCallback.h"
#include "guilib/GUIDialog.h"
#include "RedMedia.h"

class  CPlayerManagerRed;
struct RedMediaAsset;

class CGUIDialogRedAbout : public CGUIDialog
{
public:
  CGUIDialogRedAbout();
  virtual ~CGUIDialogRedAbout();

  virtual bool  OnMessage(CGUIMessage& message);
  virtual void  OnInitWindow();
  virtual void  OnWindowUnload();
  virtual bool  OnAction(const CAction& action);
  void          FillLabels();
  static  void  SetInfo(PlayerInfo *playerInfo, const float version);
  void          SetControlLabel(int id, const char *format, int info);
  static  CGUIDialogRedAbout* GetDialogRedAbout();

protected:
  virtual void  Process(unsigned int currentTime, CDirtyRegionList &dirtyregions);

  static float       m_Version;
  static PlayerInfo *m_PlayerInfo;
  static CGUIDialogRedAbout *m_RedAbout;
  static CCriticalSection m_PlayerInfo_lock;
};
