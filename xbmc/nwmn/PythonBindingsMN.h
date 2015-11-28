/*
 *  Copyright (C) 2015 Dream-Team 
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

#ifndef __XBMC__PythonBindingsMN__
#define __XBMC__PythonBindingsMN__

#include "GUIDialogMN.h"

class CPythonBindingsMN
{
public:
  CPythonBindingsMN(void);
  virtual ~CPythonBindingsMN(void);
  static void       Refresh();
  static void       PlayPause();
  static void       PausePlaying();
  static void       StopPlaying();
  static void       PlayNext();
  static void       PlayPrevious();
};

#endif /* defined(__XBMC__PythonBindingsMN__) */