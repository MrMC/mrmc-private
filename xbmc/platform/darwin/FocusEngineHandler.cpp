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
: m_state(FocusEngineState::Idle)
, m_focusedOrientation(UNDEFINED)
, m_shouldZoom(true)
, m_shouldSlide(true)
{
}

void CFocusEngineHandler::Process()
{
  CSingleLock lock(m_lock);

  FocusEngineFocus focus;
  UpdateFocus(focus);
  if (m_focus.itemFocus != focus.itemFocus)
  {
    if (m_focus.itemFocus)
    {
      m_focus.itemFocus->ResetAnimation(ANIM_TYPE_CONDITIONAL);
      m_focus.itemFocus->ClearDynamicAnimations();
    }
    m_focus = focus;
  }

  if (m_focus.itemFocus)
  {
    switch(m_state)
    {
      case FocusEngineState::Idle:
        break;
      case FocusEngineState::Clear:
        m_focus.itemFocus->ResetAnimation(ANIM_TYPE_CONDITIONAL);
        m_focus.itemFocus->ClearDynamicAnimations();
        m_focusAnimate = FocusEngineAnimate();
        m_state = FocusEngineState::Idle;
        break;
      case FocusEngineState::Update:
        {
          CRect rect = focus.itemFocus->GetSelectionRenderRect();
          if (!rect.IsEmpty())
          {
            FocusEngineAnimate focusAnimate = m_focusAnimate;
            std::vector<CAnimation> animations;
            if (m_shouldSlide && (fabs(focusAnimate.slideX) > 0.0f || fabs(focusAnimate.slideY) > 0.0f))
            {
              float screenDX =   focusAnimate.slideX  * 10.0f;
              float screenDY = (-focusAnimate.slideY) * 10.0f;
              TiXmlElement node("animation");
              node.SetAttribute("reversible", "false");
              node.SetAttribute("effect", "slide");
              node.SetAttribute("start", "0, 0");
              std::string temp = StringUtils::Format("%d, %d", MathUtils::round_int(screenDX), MathUtils::round_int(screenDY));
              node.SetAttribute("end", temp);
              //node.SetAttribute("time", "10");
              node.SetAttribute("condition", "true");
              TiXmlText text("conditional");
              node.InsertEndChild(text);

              CAnimation anim;
              anim.Create(&node, rect, 0);
              animations.push_back(anim);
            }

            if (m_shouldZoom && (focusAnimate.zoomX > 0 && focusAnimate.zoomY > 0))
            {
              TiXmlElement node("animation");
              node.SetAttribute("reversible", "false");
              node.SetAttribute("effect", "zoom");
              node.SetAttribute("start", "100, 100");
              std::string temp = StringUtils::Format("%f, %f", focusAnimate.zoomX, focusAnimate.zoomY);
              node.SetAttribute("end", temp);
              node.SetAttribute("center", "auto");
              node.SetAttribute("condition", "true");
              TiXmlText text("conditional");
              node.InsertEndChild(text);

              CAnimation anim;
              anim.Create(&node, rect, 0);
              animations.push_back(anim);
            }
            m_focus.itemFocus->ResetAnimation(ANIM_TYPE_CONDITIONAL);
            m_focus.itemFocus->SetDynamicAnimations(animations);
          }
          m_state = FocusEngineState::Idle;
        }
        break;
    }
  }
}

void CFocusEngineHandler::ClearAnimation()
{
  CSingleLock lock(m_lock);
  m_state = FocusEngineState::Clear;
}

void CFocusEngineHandler::UpdateAnimation(FocusEngineAnimate &focusAnimate)
{
  m_focusAnimate = focusAnimate;
  m_state = FocusEngineState::Update;
}

void CFocusEngineHandler::setShouldZoom(bool shouldZoom)
{
  m_shouldZoom = shouldZoom;
}

void CFocusEngineHandler::setShouldSlide(bool shouldSlide)
{
  m_shouldSlide = shouldSlide;
}

void CFocusEngineHandler::UpdateFocus(FocusEngineFocus &focus)
{
  focus.window = g_windowManager.GetWindow(g_windowManager.GetFocusedWindow());
  if (!focus.window)
    return;

  focus.rootFocus = focus.window->GetFocusedControl();
  if (!focus.rootFocus)
    return;

  if (focus.rootFocus->GetControlType() == CGUIControl::GUICONTROL_UNKNOWN)
    return;

  if (!focus.rootFocus->HasFocus())
    return;

  switch(focus.rootFocus->GetControlType())
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
        focus.itemFocus = focus.rootFocus->GetSelectionControl();
      }
      break;
    case CGUIControl::GUICONTAINER_FIXEDLIST:
      {
        focus.itemFocus = focus.rootFocus->GetSelectionControl();
      }
      break;
  }
}

void CFocusEngineHandler::InvalidateFocus(CGUIControl *control)
{
  CSingleLock lock(m_lock);
  if (m_focus.rootFocus == control || m_focus.itemFocus == control)
  {
    m_focus = FocusEngineFocus();
  }
}

const CRect
CFocusEngineHandler::GetFocusRect()
{
  FocusEngineFocus focus;
  UpdateFocus(focus);
  if (focus.itemFocus)
  {
    CRect focusedRenderRect = focus.itemFocus->GetSelectionRenderRect();
    return focusedRenderRect;
  }

  return CRect();
}

bool CFocusEngineHandler::GetShowFocusRect()
{
  return showFocusRect;
}

ORIENTATION CFocusEngineHandler::GetFocusOrientation()
{
  FocusEngineFocus focus;
  UpdateFocus(focus);
  if (focus.itemFocus)
  {
    switch(focus.itemFocus->GetControlType())
    {
      case CGUIControl::GUICONTROL_LISTGROUP:
        return focus.rootFocus->GetOrientation();
        break;
      case CGUIControl::GUICONTROL_BUTTON:
      case CGUIControl::GUICONTROL_IMAGE:
        {
          CGUIControl *parentFocusedControl = focus.itemFocus->GetParentControl();
          if (parentFocusedControl)
            return parentFocusedControl->GetOrientation();
        }
        break;
      default:
        break;
    }
    return focus.itemFocus->GetOrientation();
  }
  return UNDEFINED;
}
