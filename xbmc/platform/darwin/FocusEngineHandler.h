/*
 *      Copyright (C) 2017 Team MrMC
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

#include "guilib/Geometry.h"
#include "guilib/GUIControl.h"
#include "threads/CriticalSection.h"

class CAnimation;
class CGUIControl;

class CFocusEngineHandler
{
 public:
  static CFocusEngineHandler& GetInstance();

  void          Process();
  void          ClearAnimations();
  void          UpdateFocusedAnimation(float dx, float dy);
  CGUIControl  *GetFocusedControl();
  ORIENTATION   GetFocusedOrientation () const;

  const CRect   GetFocusedItemRect();
  const CPoint  GetFocusedItemCenter();

private:
  CFocusEngineHandler();
  CFocusEngineHandler(CFocusEngineHandler const& );
  CFocusEngineHandler& operator=(CFocusEngineHandler const&);

  CRect m_focusedRenderRect;
  CCriticalSection m_lock;
  CGUIControl *m_focusedControl;
  ORIENTATION m_focusedOrientation;
  std::vector<CAnimation> m_animations;
  static CFocusEngineHandler* m_instance;

};
