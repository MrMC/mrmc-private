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
class CGUIWindow;
class CGUIControl;

typedef enum FocusEngineState
{
  Idle,
  Clear,
  Update,
} FocusEngineState;

typedef struct
{
  CGUIWindow  *window = nullptr;
  CGUIControl *rootFocus = nullptr;
  CGUIControl *itemFocus = nullptr;
} FocusEngineFocus;

typedef struct
{
  float zoomX = -1.0f;
  float zoomY = -1.0f;
  float slideX = 0.0f;
  float slideY = 0.0f;
} FocusEngineAnimate;

class CFocusEngineHandler
{
 public:
  static CFocusEngineHandler& GetInstance();

  void          Process();
  void          ClearAnimation();
  void          UpdateAnimation(FocusEngineAnimate &focusAnimate);
  void          UpdateFocus(FocusEngineFocus &focus);
  void          InvalidateFocus(CGUIControl *control);
  const CRect   GetFocusRect();
  bool          GetShowFocusRect();
  ORIENTATION   GetFocusOrientation();

private:
  CFocusEngineHandler();
  CFocusEngineHandler(CFocusEngineHandler const& );
  CFocusEngineHandler& operator=(CFocusEngineHandler const&);

  bool showFocusRect = false;
  CCriticalSection m_lock;
  FocusEngineState m_state;
  FocusEngineFocus m_focus;
  ORIENTATION m_focusedOrientation;
  FocusEngineAnimate m_focusAnimate;
  static CFocusEngineHandler* m_instance;

};
