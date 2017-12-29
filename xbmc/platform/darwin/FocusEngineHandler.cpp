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

#include "guilib/GUIControl.h"
#include "guilib/GUIListGroup.h"
#include "guilib/GUIListLabel.h"
#include "guilib/GUIBaseContainer.h"
#include "guilib/GUIWindowManager.h"
#include "threads/Atomics.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/XBMCTinyXML.h"


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
: m_focusZoom(true)
, m_focusSlide(true)
, m_showFocusRect(false)
, m_showVisibleRects(false)
, m_state(FocusEngineState::Idle)
, m_focusedOrientation(UNDEFINED)
{
}

void CFocusEngineHandler::Process()
{
  FocusEngineFocus focus;
  UpdateFocus(focus);
  if (m_focus.itemFocus != focus.itemFocus ||
      m_focus.window != focus.window       ||
      m_focus.windowID != focus.windowID)
  {
    if (m_focus.itemFocus)
    {
      m_focus.itemFocus->ResetAnimation(ANIM_TYPE_DYNAMIC);
      m_focus.itemFocus->ClearDynamicAnimations();
    }
    CSingleLock lock(m_focusLock);
    // itemsVisible will start cleared
    m_focus = focus;
  }
  //UpdateVisible(m_focus);

  if (m_focus.itemFocus)
  {
    CSingleLock lock(m_stateLock);
    switch(m_state)
    {
      case FocusEngineState::Idle:
        break;
      case FocusEngineState::Clear:
        m_focus.itemFocus->ResetAnimation(ANIM_TYPE_DYNAMIC);
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
            // handle control slide
            if (m_focusSlide && (fabs(focusAnimate.slideX) > 0.0f || fabs(focusAnimate.slideY) > 0.0f))
            {
              float screenDX =   focusAnimate.slideX  * focusAnimate.maxScreenSlideX;
              float screenDY = (-focusAnimate.slideY) * focusAnimate.maxScreenSlideY;
              TiXmlElement node("animation");
              node.SetAttribute("reversible", "false");
              node.SetAttribute("effect", "slide");
              node.SetAttribute("start", "0, 0");
              std::string temp = StringUtils::Format("%f, %f", screenDX, screenDY);
              node.SetAttribute("end", temp);
              //node.SetAttribute("time", "10");
              node.SetAttribute("condition", "true");
              TiXmlText text("dynamic");
              node.InsertEndChild(text);

              CAnimation anim;
              anim.Create(&node, rect, 0);
              animations.push_back(anim);
            }
            // handle control zoom
            if (m_focusZoom && (focusAnimate.zoomX > 0.0f && focusAnimate.zoomY > 0.0f))
            {
              TiXmlElement node("animation");
              node.SetAttribute("reversible", "false");
              node.SetAttribute("effect", "zoom");
              node.SetAttribute("start", "100, 100");
              float aspectRatio = rect.Width()/ rect.Height();
              //CLog::Log(LOGDEBUG, "FocusEngineState::Update: aspectRatio(%f)", aspectRatio);
              if (aspectRatio > 2.5f)
              {
                CRect newRect(rect);
                newRect.x1 -= 2;
                newRect.y1 -= 2;
                newRect.x2 += 8;
                newRect.y2 += 8;
                // format is end="x,y,width,height"
                std::string temp = StringUtils::Format("%f, %f, %f, %f",
                  newRect.x1, newRect.y1, newRect.Width(), newRect.Height());
                node.SetAttribute("end", temp);
              }
              else
              {
                std::string temp = StringUtils::Format("%f, %f", focusAnimate.zoomX, focusAnimate.zoomY);
                node.SetAttribute("end", temp);
              }
              node.SetAttribute("center", "auto");
              node.SetAttribute("condition", "true");
              TiXmlText text("dynamic");
              node.InsertEndChild(text);

              CAnimation anim;
              anim.Create(&node, rect, 0);
              animations.push_back(anim);
            }
            m_focus.itemFocus->ResetAnimation(ANIM_TYPE_DYNAMIC);
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
  CSingleLock lock(m_stateLock);
  m_state = FocusEngineState::Clear;
}

void CFocusEngineHandler::UpdateAnimation(FocusEngineAnimate &focusAnimate)
{
  CSingleLock lock(m_stateLock);
  m_focusAnimate = focusAnimate;
  m_state = FocusEngineState::Update;
}

void CFocusEngineHandler::EnableFocusZoom(bool enable)
{
  CSingleLock lock(m_stateLock);
  m_focusZoom = enable;
}

void CFocusEngineHandler::EnableFocusSlide(bool enable)
{
  CSingleLock lock(m_stateLock);
  m_focusSlide = enable;
}

void CFocusEngineHandler::InvalidateFocus(CGUIControl *control)
{
  CSingleLock lock(m_focusLock);
  if (m_focus.rootFocus == control || m_focus.itemFocus == control)
    m_focus = FocusEngineFocus();

  auto foundControl = std::find_if(m_focus.items.begin(), m_focus.items.end(),
      [&](FocusabilityItem item)
      { return item.control == control;
  });
  if (foundControl != m_focus.items.end())
    m_focus.items.erase(foundControl);
}

const int
CFocusEngineHandler::GetFocusWindowID()
{
  CSingleLock lock(m_focusLock);
  return m_focus.windowID;
}

const CRect
CFocusEngineHandler::GetFocusRect()
{
  FocusEngineFocus focus;
  // skip finding focused window, use current
  CSingleLock lock(m_focusLock);
  focus.window = m_focus.window;
  focus.windowID = m_focus.windowID;
  lock.Leave();
  if (focus.window && focus.windowID != 0 && focus.windowID != WINDOW_INVALID)
  {
    UpdateFocus(focus);
    if (focus.itemFocus)
    {
      CRect focusedRenderRect = focus.itemFocus->GetSelectionRenderRect();
      return focusedRenderRect;
    }
  }
  return CRect();
}

bool CFocusEngineHandler::ShowFocusRect()
{
  return m_showFocusRect;
}

bool CFocusEngineHandler::ShowVisibleRects()
{
  return m_showVisibleRects;
}

ORIENTATION CFocusEngineHandler::GetFocusOrientation()
{
  FocusEngineFocus focus;
  // skip finding focused window, use current
  CSingleLock lock(m_focusLock);
  focus.window = m_focus.window;
  focus.windowID = m_focus.windowID;
  lock.Leave();
  if (focus.window && focus.windowID != 0 && focus.windowID != WINDOW_INVALID)
  {
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
  }
  return UNDEFINED;
}

void CFocusEngineHandler::UpdateFocus(FocusEngineFocus &focus)
{
  // if focus.window is valid, use it and
  // skip finding focused window else invalidate focus
  if (!focus.window || focus.windowID == 0 || focus.windowID == WINDOW_INVALID)
  {
    focus.windowID = g_windowManager.GetActiveWindowID();
    focus.window = g_windowManager.GetWindow(focus.windowID);
    if (!focus.window)
      return;
    if(focus.windowID == 0 || focus.windowID == WINDOW_INVALID)
    {
      focus = FocusEngineFocus();
      return;
    }
  }

  focus.rootFocus = focus.window->GetFocusedControl();
  if (!focus.rootFocus)
    return;

  if (focus.rootFocus->GetControlType() == CGUIControl::GUICONTROL_UNKNOWN)
    return;

  if (!focus.rootFocus->HasFocus())
    return;

  switch(focus.rootFocus->GetControlType())
  {
    // include all known types of controls
    // we do not really need to do this but compiler
    // will generate a warning if a new one is added.
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

void CFocusEngineHandler::GetFocusabilityItems(std::vector<FocusabilityItem> &items)
{
  // skip finding focused window, use current
  CSingleLock lock(m_focusLock);
  if (m_focus.window && m_focus.windowID != 0 && m_focus.windowID != WINDOW_INVALID)
  {
    if (m_focus.rootFocus)
      items = m_focus.items;
    else
      items.clear();
  }
}

void CFocusEngineHandler::AppendFocusability(const CFocusabilityTracker &focusabilityTracker)
{
  auto items = focusabilityTracker.GetItems();
  for (auto it = items.begin(); it != items.end(); ++it)
  {
    //CRect renderRect = (*it)->GetRenderRect();
    //CLog::Log(LOGDEBUG, "focusableTracker: %p, %f,%f %f x %f",
    //  *it, renderRect.x1, renderRect.y1, renderRect.Width(), renderRect.Height());
    if (!(*it).control->HasProcessed() || !(*it).control->IsVisible())
      continue;
    AppendFocusabilityItem(*it);
  }
}

void CFocusEngineHandler::AppendFocusabilityItem(FocusabilityItem &item)
{
  // skip finding focused window, use current
  CSingleLock lock(m_focusLock);
  if (m_focus.window && m_focus.windowID != 0 && m_focus.windowID != WINDOW_INVALID)
  {
    auto foundControl = std::find_if(m_focus.items.begin(), m_focus.items.end(),
        [&](FocusabilityItem a)
        { return a.control == item.control;
    });
    if (foundControl == m_focus.items.end())
    {
      // missing from our list, add it in
      m_focus.items.push_back(item);
      // always sort the control list by control pointer address
      // we could play games with trying to inset at the right place
      // but these arrays are short and it just does not matter much.
      std::sort(m_focus.items.begin(), m_focus.items.end(),
        [] (FocusabilityItem const& a, FocusabilityItem const& b)
      {
          return a.control < b.control;
      });
    }
    else
    {
      // if existing, just update renderRect
      (*foundControl).renderRect = item.control->GetRenderRect();
    }
  }
}

void CFocusEngineHandler::UpdateFocusabilityItemRenderRects()
{
  // use current focused window
  CSingleLock lock(m_focusLock);
  if (m_focus.window && m_focus.windowID != 0 && m_focus.windowID != WINDOW_INVALID)
  {
    for (auto it = m_focus.items.begin(); it != m_focus.items.end(); ++it)
    {
      // grr, some animation effects will leave button boxes showing,
      // the control eventually gets marked unprocessed or hidden so
      // deal with that quirk here.
      if (!(*it).control->HasProcessed() || !(*it).control->IsVisible())
      {
        (*it).renderRect = CRect();
        continue;
      }
      switch((*it).control->GetControlType())
      {
        default:
          (*it).renderRect = (*it).control->GetRenderRect();
          break;
        case CGUIControl::GUICONTROL_BUTTON:
          {
            // bottons with ControlID of zero are only
            // touch navigable, ignore them.
            if ((*it).control->GetID() == 0)
              (*it).renderRect = CRect();
          }
          break;
        case CGUIControl::GUICONTROL_LISTLABEL:
          {
            (*it).renderRect = (*it).control->GetRenderRect();
            /*
            CGUIControl *parent = (*it).control->GetParentControl();
            if (parent)
            {
              (*it).renderRect = parent->GetRenderRect();
              CGUIControlGroup *groupControl = dynamic_cast<CGUIControlGroup*>(parent);
              if (groupControl)
              {
                CRect region = (*it).renderRect;
                CRect zeroRect = CRect(0, 0, 0, 0);
                std::vector<CGUIControl *> children;
                groupControl->GetChildren(children);
                for (auto childit = children.begin(); childit != children.end(); ++childit)
                {
                  // never trust renderRects that are not processed
                  // this means they have not been mapped to display
                  if (!(*childit)->HasProcessed() || !(*childit)->IsVisible())
                    continue;
                  CRect childRenderRect = (*childit)->GetRenderRect();
                  if (childRenderRect != zeroRect)
                    region.ArithmeticUnion( childRenderRect );
                }
                // not sure why this happens, it's the ".." in a file list
                if (region.x1 < (*it).renderRect.x1)
                  region.x1 = (*it).renderRect.x1;
                (*it).renderRect = region;
              }
            }
            */
          }
          break;
      }
    }
  }
}
