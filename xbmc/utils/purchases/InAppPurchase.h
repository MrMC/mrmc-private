#pragma once
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
#include <vector>
#include <string>
#include "interfaces/IAnnouncer.h"

#if defined(TARGET_DARWIN_IOS)
#include "platform/darwin/RMStore/CAppleInAppPurchase.h"
#else
typedef struct Product {
  std::string id;
  std::string title;
  std::string price;
  std::string expires;
} Product;
#endif



typedef std::vector<Product> ProductList;

class CInAppPurchase: public ANNOUNCEMENT::IAnnouncer
{
public:
  CInAppPurchase();
  ~CInAppPurchase();
  static CInAppPurchase &GetInstance();
  bool IsActivated() { return m_bIsActivated; };
  void SetActivated(bool activated);
  bool IsDivxActivated();
  void SetDivxActivated(bool activated);
  void RefreshReceipt();
  ProductList GetProducts();
  ProductList GetSubscriptions();
  void VerifyPurchase();
  void RestorePurchases();
  void PurchaseProduct(std::string product);

  // IAnnouncer callbacks
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data);

private:
  bool m_bIsActivated;
  bool m_bIsDivxActivated;
};
