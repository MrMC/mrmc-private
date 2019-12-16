/*
 *      Copyright (C) 2018 Team MrMC
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

#include "InAppPurchase.h"
#include "interfaces/AnnouncementManager.h"
#include "utils/log.h"

using namespace ANNOUNCEMENT;

CInAppPurchase::CInAppPurchase()
{
  CAnnouncementManager::GetInstance().AddAnnouncer(this);
}

CInAppPurchase::~CInAppPurchase()
{
  CAnnouncementManager::GetInstance().RemoveAnnouncer(this);
}

CInAppPurchase& CInAppPurchase::GetInstance()
{
  static CInAppPurchase inAppPurchase;
  return inAppPurchase;
}

void CInAppPurchase::SetActivated(bool activated)
{
  m_bIsActivated = activated;
}

void CInAppPurchase::SetDivxActivated(bool activated)
{
  m_bIsDivxActivated = activated;
}

void CInAppPurchase::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (flag == GUI && !strcmp(sender, "xbmc"))
  {
    if (strcmp(message, "OnCreated") == 0)
      VerifyPurchase();
  }
}

void CInAppPurchase::RefreshReceipt()
{
#if defined(TARGET_DARWIN_IOS)
  CAppleInAppPurchase::GetInstance().RefreshReceipt();
#elif defined(TARGET_ANDROID)

#endif
}

void CInAppPurchase::VerifyPurchase()
{
#if defined(TARGET_DARWIN_IOS)
  CAppleInAppPurchase::GetInstance().VerifyPurchase();
  SetActivated(CAppleInAppPurchase::GetInstance().IsActivated());
  SetDivxActivated(CAppleInAppPurchase::GetInstance().IsDivxActivated());
#elif defined(TARGET_ANDROID)

#endif
}

ProductList CInAppPurchase::GetSubscriptions()
{
  ProductList list;
#if defined(TARGET_DARWIN_IOS)
  list = CAppleInAppPurchase::GetInstance().GetSubscriptions();
#elif defined(TARGET_ANDROID)

#else
  Product fake;
  fake.id = "test.subscription";
  fake.title = "Lifetime Subscription";
  fake.price = "$14.99";
  list.push_back(fake);
#endif
  return list;
}
ProductList CInAppPurchase::GetProducts()
{
  ProductList list;
#if defined(TARGET_DARWIN_IOS)
  list = CAppleInAppPurchase::GetInstance().GetProducts();
#elif defined(TARGET_ANDROID)

#else
  Product fake;
  fake.id = "test.product";
  fake.title = "One year Subscription";
  fake.price = "$4.99";
  list.push_back(fake);
#endif
  return list;
}

void CInAppPurchase::RestorePurchases()
{
#if defined(TARGET_DARWIN_IOS)
  CAppleInAppPurchase::GetInstance().RestoreTransactions();
#elif defined(TARGET_ANDROID)

#endif
}

void CInAppPurchase::PurchaseProduct(std::string product)
{
#if defined(TARGET_DARWIN_IOS)
  CAppleInAppPurchase::GetInstance().PurchaseProduct(product);
#elif defined(TARGET_ANDROID)

#endif
  CLog::Log(LOGINFO, "CInAppPurchase::PurchaseProduct() - %s", product.c_str());
}
