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

#include "system.h"
#include "input/Key.h"
#include "GUIWindowAppStore.h"
#include "GUIInfoManager.h"
#include "guilib/WindowIDs.h"
#include "guilib/LocalizeStrings.h"
#include "pvr/PVRManager.h"
#include "utils/SystemInfo.h"
#include "utils/StringUtils.h"
#include "storage/MediaManager.h"
#include "guiinfo/GUIInfoLabels.h"
#include "guilib/GUIWindowManager.h"

#define CONTROL_SUB_INFO          94
#define CONTROL_SUB_PURCHASE      95
#define CONTROL_RESTORE_PURCHASE  96

#define CONTROL_SUB_LIST          940


CGUIWindowAppStore::CGUIWindowAppStore(void) :
    CGUIWindow(WINDOW_SETTINGS_APPSTORE, "AppStore.xml")
{
  m_loadType = KEEP_IN_MEMORY;
}

CGUIWindowAppStore::~CGUIWindowAppStore(void)
{
}

bool CGUIWindowAppStore::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_INIT:
    {
      CGUIWindow::OnMessage(message);
      return true;
    }
    break;

    case GUI_MSG_WINDOW_DEINIT:
    {
      CGUIWindow::OnMessage(message);
      return true;
    }
    break;

    case GUI_MSG_FOCUSED:
    {
      CGUIWindow::OnMessage(message);
      int focusedControl = GetFocusedControlID();
      if (focusedControl == CONTROL_SUB_INFO)
      {
        m_SubscriptionList = CInAppPurchase::GetInstance().GetSubscriptions();
        CFileItemList *subscriptionList = new CFileItemList;
        for (auto &sub :  m_SubscriptionList)
        {
          CFileItemPtr item(new CFileItem(sub.title));
          if (!sub.expires.empty())
            item->SetLabel(sub.title + " - Active until " + sub.expires);
          else
            item->SetLabel(sub.title + " - Active");
          subscriptionList->Add(item);
        }
        m_productItemList.clear();
        CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_SUB_LIST, 0, 0, subscriptionList);
        g_windowManager.SendThreadMessage(message);
      }
      else if (focusedControl == CONTROL_SUB_PURCHASE)
      {
        m_productItemList.clear();
        CFileItemList *productList = new CFileItemList;
        m_ProductList = CInAppPurchase::GetInstance().GetProducts();
        ProductList activeList;
        activeList = CInAppPurchase::GetInstance().GetSubscriptions();
        for (auto &sub :  m_ProductList)
        {
          bool addItem = true;
          for (auto &activeItem :  activeList)
          {
            // dont show active subscriptions
            if (sub.id == activeItem.id)
              addItem = false;
          }
          if (!addItem)
            continue;
          CFileItemPtr item(new CFileItem(sub.title));
          item->SetLabel(sub.title + " - " + sub.price);
          productList->Add(item);
          m_productItemList.push_back(sub);
        }
        m_SubscriptionList.clear();
        CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_SUB_LIST, 0, 0, productList);
        g_windowManager.SendThreadMessage(message);
      }
      else if (focusedControl == CONTROL_RESTORE_PURCHASE)
      {
        CFileItemList *fakeList = new CFileItemList;
        CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_SUB_LIST, 0, 0, fakeList);
        g_windowManager.SendThreadMessage(message);
      }
      return true;
    }
    break;

    case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      bool selectAction = (message.GetParam1() == ACTION_SELECT_ITEM ||
                           message.GetParam1() == ACTION_MOUSE_LEFT_CLICK);

      if (selectAction && iControl == CONTROL_SUB_LIST)
      {
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), CONTROL_SUB_LIST);
        OnMessage(msg);

        CSingleLock lock(m_critsection);
        int item = msg.GetParam1();
        if (m_productItemList.size() > 0 && (unsigned long)item < m_productItemList.size())
        {
          Product selectedProduct = m_productItemList[item];
          CInAppPurchase::GetInstance().PurchaseProduct(selectedProduct.id);
          SET_CONTROL_FOCUS(CONTROL_SUB_INFO, 0);
        }
        return true;
      }
      else if (iControl == CONTROL_RESTORE_PURCHASE)
      {
        CInAppPurchase::GetInstance().RestorePurchases();
        // focus "info" tab to refresh subscriptions
        SET_CONTROL_FOCUS(CONTROL_SUB_INFO, 0);
        return true;
      }
    }
  }
  return CGUIWindow::OnMessage(message);
}
