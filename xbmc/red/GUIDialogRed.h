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

class  CPlayerManagerRed;
struct RedMediaAsset;

class CGUIDialogRed : public CGUIDialog
{
public:
  CGUIDialogRed();
  virtual ~CGUIDialogRed();

  virtual bool  OnMessage(CGUIMessage& message);
  virtual void  OnInitWindow();
  virtual void  OnWindowUnload();

  void          Refresh();
  void          SendReport();

protected:
  virtual void  Process(unsigned int currentTime, CDirtyRegionList &dirtyregions);
  static  void  PlayerCallBack(const void *ctx, bool status);
  static  void  PlaybackCallBack(const void *ctx, int msg, RedMediaAsset &asset);

  bool          m_RefreshRunning;
  CPlayerManagerRed *m_PlayerManager;
};
