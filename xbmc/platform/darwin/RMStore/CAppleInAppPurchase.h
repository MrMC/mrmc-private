#pragma once
/*
 *      Copyright (C) 2019 Team MrMC
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
//#import <Foundation/Foundation.h>

typedef struct Product {
  std::string id;
  std::string title;
  std::string price;
  std::string expires;
} Product;

typedef std::vector<Product> ProductList;

class CAppleInAppPurchase
{
public:
  CAppleInAppPurchase();
  ~CAppleInAppPurchase();
  static CAppleInAppPurchase &GetInstance();
  bool IsActivated() { return m_bIsActivated; };
  void SetActivated(bool activated);
  bool IsDivxActivated() { return m_bIsDivxActivated; };
  void SetSubscribed(bool subscribed);
  void SetDivxActivated(bool activated);
  bool GetSubscribed();
  void RefreshReceipt();
  void RemoveTransactions();
  void RestoreTransactions();
  void VerifyPurchase();
  bool PurchaseProduct(std::string product);
  ProductList GetProducts();
  ProductList GetSubscriptions();
private:
  ProductList m_products;
  bool m_bIsActivated;
  bool m_bIsDivxActivated;
  bool checkUserDefaults(std::string product);
  void setUserDefaults(std::string product, bool set);

};
