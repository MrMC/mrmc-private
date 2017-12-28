#pragma once

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

#include "Geometry.h"
#include "guilib/GUIControl.h"

#include <vector>

typedef struct
{
  int order = 0;                    // draw order
  CRect renderRect;                 // renderRect in display coordinates
  CGUIControl *control = nullptr;   // control item reference
} FocusabilityItem;

class CFocusabilityTracker
{
public:
  CFocusabilityTracker();
 ~CFocusabilityTracker();

  void Clear();

  bool IsEnabled();
  void SetEnabled(bool enable);
  void Append(CGUIControl *control);
  const std::vector<FocusabilityItem>& GetItems() const;

private:
  int m_order = 0;
  bool m_enable = true;
  std::vector<FocusabilityItem> items;
};
