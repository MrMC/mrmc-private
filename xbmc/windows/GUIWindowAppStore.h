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

#include "guilib/GUIWindow.h"
#include "utils/purchases/InAppPurchase.h"

class CGUIWindowAppStore : public CGUIWindow
{
public:
  CGUIWindowAppStore(void);
  virtual ~CGUIWindowAppStore(void);
  virtual bool OnMessage(CGUIMessage& message);
private:
  ProductList                  m_ProductList;
  ProductList                  m_SubscriptionList;
  CFileItemList*               m_Purchases;
  CCriticalSection             m_critsection;
  ProductList                  m_productItemList;
};

