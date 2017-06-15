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

 // https://developer.apple.com/library/content/documentation/General/Conceptual/AppleTV_PG/WorkingwiththeAppleTVRemote.html#//apple_ref/doc/uid/TP40015241-CH5-SW5

#include "FocusEngineHandler.h"

#include "guilib/GUIBaseContainer.h"
#include "guilib/GUIControl.h"
#include "guilib/GUIListItem.h"
#include "guilib/GUIScrollBarControl.h"
#include "guilib/GUIControlGroupList.h"
#include "guilib/GUIFixedListContainer.h"
#include "guilib/GUIMultiSelectText.h"
#include "guilib/GUIListLabel.h"
#include "guilib/GUIWindowManager.h"
#include "guiinfo/GUIInfoLabels.h"

#include "threads/Atomics.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/MathUtils.h"


static std::atomic<long> sg_focusenginehandler_lock {0};
CFocusEngineHandler* CFocusEngineHandler::m_instance = nullptr;

CFocusEngineHandler&
CFocusEngineHandler::GetInstance()
{
  CAtomicSpinLock lock(sg_focusenginehandler_lock);
  if (!m_instance)
    m_instance = new CFocusEngineHandler();

  return *m_instance;
}

CFocusEngineHandler::CFocusEngineHandler()
: m_focusedControl(nullptr)
, m_focusedOrientation(UNDEFINED)
{
}

void CFocusEngineHandler::Process()
{
  CSingleLock lock(m_lock);

  // find and update the real focused control
  CGUIWindow* pWindow = g_windowManager.GetWindow(g_windowManager.GetFocusedWindow());
  if (!pWindow)
    return;

  CGUIControl *focusedControl = pWindow->GetFocusedControl();
  if (!focusedControl)
    return;

  if (focusedControl->GetControlType() == CGUIControl::GUICONTROL_UNKNOWN)
    return;

  if (!focusedControl->HasFocus())
    return;
  
  if (focusedControl->GetID() <= 0)
    return;

  if (!m_focusedControl)
  {
    m_focusedControl = focusedControl->GetSelectionControl();
  }
  else if (m_focusedControl->GetID() != focusedControl->GetSelectionControl()->GetID())
  {
    m_focusedControl->ResetAnimation(ANIM_TYPE_CONDITIONAL);
    m_focusedControl->ClearDynamicAnimations();
    m_focusedControl = focusedControl->GetSelectionControl();
  }

  if (m_focusedControl)
  {
    if (m_animations.size() && !m_focusedControl->IsAnimating(ANIM_TYPE_CONDITIONAL))
    {
      m_focusedControl->ResetAnimation(ANIM_TYPE_CONDITIONAL);
      m_focusedControl->SetDynamicAnimations(m_animations);
      m_animations.clear();
    }
  }
}

void CFocusEngineHandler::ClearAnimations()
{
  CSingleLock lock(m_lock);
  m_animations.clear();
}

void CFocusEngineHandler::UpdateFocusedAnimation(float dx, float dy)
{
  CSingleLock lock(m_lock);

  ORIENTATION orientation = GetFocusedOrientation();
  //if (orientation == UNDEFINED)
  //  return;

  CRect rect = CFocusEngineHandler::GetInstance().GetFocusedItemRect();
  if (rect.IsEmpty())
    return;

  //float screenDX =   dx  * (0.1 * rect.Width());
  //float screenDY = (-dy) * (0.1 * rect.Height());
  float screenDX =   dx  * 10.0f;
  float screenDY = (-dy) * 10.0f;

  TiXmlElement node("animation");
  node.SetAttribute("reversible", "false");
  node.SetAttribute("effect", "slide");
  node.SetAttribute("start", "0, 0");
  std::string temp = StringUtils::Format("%d, %d", MathUtils::round_int(screenDX), MathUtils::round_int(screenDY));
  node.SetAttribute("end", temp);
  /*
  if (orientation == HORIZONTAL)
  {
    std::string temp = StringUtils::Format("%d, %d", MathUtils::round_int(screenDX), MathUtils::round_int(screenDY));
    node.SetAttribute("end", temp);
  }
  else if (orientation == VERTICAL)
  {
    std::string temp = StringUtils::Format("%d, %d", MathUtils::round_int(screenDX), MathUtils::round_int(screenDY));
    node.SetAttribute("end", temp);
  }
  */
  //node.SetAttribute("time", "10");
  std::string condition = StringUtils::Format("Control.HasFocus(%d)", m_focusedControl->GetID());
  node.SetAttribute("condition", condition);
  //node.SetAttribute("condition", "true");
  TiXmlText text("conditional");
  node.InsertEndChild(text);

  CAnimation anim;
  anim.Create(&node, rect, 0);
  std::vector<CAnimation> animations;
  animations.push_back(anim);

  m_animations = animations;
}

CGUIControl  *CFocusEngineHandler::GetFocusedControl()
{
  CSingleLock lock(m_lock);
  return m_focusedControl;
}

ORIENTATION CFocusEngineHandler::GetFocusedOrientation () const
{
  CSingleLock lock(m_lock);
  if (m_focusedControl)
  {
    switch(m_focusedControl->GetControlType())
    {
      case CGUIControl::GUICONTROL_BUTTON:
      case CGUIControl::GUICONTROL_IMAGE:
        {
          CGUIControl *parentFocusedControl = m_focusedControl->GetParentControl();
          if (parentFocusedControl)
            return parentFocusedControl->GetOrientation();
        }
        break;
      default:
        break;
    }
    return m_focusedControl->GetOrientation();
  }
  return UNDEFINED;
}

const CRect
CFocusEngineHandler::GetFocusedItemRect()
{
  CGUIWindow* pWindow = g_windowManager.GetWindow(g_windowManager.GetFocusedWindow());
  if (!pWindow)
    return CRect();

  CGUIControl *focusedControl = pWindow->GetFocusedControl();
  if (!focusedControl)
    return CRect();

  CRect focusedRenderRect;
  switch(focusedControl->GetControlType())
  {
    case CGUIControl::GUICONTROL_UNKNOWN:
      CLog::Log(LOGDEBUG, "GetFocusedItem: GUICONTROL_UNKNOWN");
      break;
    case CGUIControl::GUICONTROL_BUTTON:
    case CGUIControl::GUICONTROL_CHECKMARK:
    case CGUIControl::GUICONTROL_FADELABEL:
    case CGUIControl::GUICONTROL_IMAGE:
    case CGUIControl::GUICONTROL_BORDEREDIMAGE:
    case CGUIControl::GUICONTROL_LARGE_IMAGE:
    case CGUIControl::GUICONTROL_LABEL:
    case CGUIControl::GUICONTROL_PROGRESS:
    case CGUIControl::GUICONTROL_RADIO:
    case CGUIControl::GUICONTROL_RSS:
    case CGUIControl::GUICONTROL_SELECTBUTTON:
    case CGUIControl::GUICONTROL_SPIN:
    case CGUIControl::GUICONTROL_SPINEX:
    case CGUIControl::GUICONTROL_TEXTBOX:
    case CGUIControl::GUICONTROL_TOGGLEBUTTON:
    case CGUIControl::GUICONTROL_VIDEO:
    case CGUIControl::GUICONTROL_SLIDER:
    case CGUIControl::GUICONTROL_SETTINGS_SLIDER:
    case CGUIControl::GUICONTROL_MOVER:
    case CGUIControl::GUICONTROL_RESIZE:
    case CGUIControl::GUICONTROL_EDIT:
    case CGUIControl::GUICONTROL_VISUALISATION:
    case CGUIControl::GUICONTROL_RENDERADDON:
    case CGUIControl::GUICONTROL_MULTI_IMAGE:
    case CGUIControl::GUICONTROL_LISTGROUP:
    case CGUIControl::GUICONTROL_GROUPLIST:
    case CGUIControl::GUICONTROL_LISTLABEL:
    case CGUIControl::GUICONTROL_GROUP:
    case CGUIControl::GUICONTROL_SCROLLBAR:
    case CGUIControl::GUICONTROL_MULTISELECT:
    case CGUIControl::GUICONTAINER_LIST:
    case CGUIControl::GUICONTAINER_WRAPLIST:
    case CGUIControl::GUICONTAINER_EPGGRID:
    case CGUIControl::GUICONTAINER_PANEL:
      {
        // returned rect is in screen coordinates.
        focusedRenderRect = focusedControl->GetSelectionRenderRect();
        if (focusedRenderRect != m_focusedRenderRect)
        {
          m_focusedRenderRect = focusedRenderRect;
          //CLog::Log(LOGDEBUG, "GetFocusedItem: itemRect, t(%f) l(%f) w(%f) h(%f)",
          //  focusedItem.x1, focusedItem.y1, focusedItem.Width(), focusedItem.Height());
        }
      }
      break;
    case CGUIControl::GUICONTAINER_FIXEDLIST:
      {
        CGUIFixedListContainer *fixedListContainer = (CGUIFixedListContainer*)focusedControl;
        // returned rect is in screen coordinates.
        focusedRenderRect = fixedListContainer->GetSelectionRenderRect();
        if (focusedRenderRect != m_focusedRenderRect)
        {
          m_focusedRenderRect = focusedRenderRect;
          //CLog::Log(LOGDEBUG, "GetFocusedItem: itemRect, t(%f) l(%f) w(%f) h(%f)",
          //  focusedItem.x1, focusedItem.y1, focusedItem.Width(), focusedItem.Height());
        }
        
      }
      break;
  }

  return m_focusedRenderRect;
}

const CPoint
CFocusEngineHandler::GetFocusedItemCenter()
{
  return GetFocusedItemRect().Center();
}
