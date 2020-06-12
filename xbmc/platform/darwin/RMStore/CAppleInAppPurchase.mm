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
#include "CAppleInAppPurchase.h"
#include "InAppPurchase.h"
#include "RMStore.h"
#include "RMAppReceipt.h"
#include "RMStoreKeychainPersistence.h"
#include "RMStoreAppReceiptVerifier.h"
#import  "utils/log.h"



// list of IDs
#define YEAR_PURCHASE          "tv.mrmc.mrmc.tvos.year"         // real ID for a year subscription
#define LIFETIME_PURCHASE      "tv.mrmc.mrmc.tvos.lifetime"     // real ID for a lifetime subscription
#define FREE_LIFETIME_PURCHASE "tv.mrmc.mrmc.tvos.freelifetime" // real ID for a free lifetime subscription, only for MrMC touch migration
#define LIFETIME_DIVX_PURCHASE "tv.mrmc.mrmc.tvos.divx"         // real ID for a lifetime divx subscription
#define TVOS_FAKE_PURCHASE     "tv.mrmc.mrmc.tvos.tvosfake"     // fake id to write it in keychain for an easy check
#define IOS_UPGRADE_PURCHASE   "tv.mrmc.mrmc.tvos.iosupgrade"   // this ID will be saved by MrMC Touch ver 3.*

#define MRMC_ACTIVATED         "isMrMCActivated"                // we write this into defaults so its easy to pull up without pinging the store
#define MRMC_DIVX_ACTIVATED    "isMrMCdivxActivated"            // we write this into defaults so its easy to pull up without pinging the store
#define MRMC_SUBSCRIBED        "isMrMCSubscribed"               // we write this into defaults so its easy to pull up without pinging the store

id<RMStoreReceiptVerifier> _receiptVerifier;
RMStoreKeychainPersistence *_persistence;


CAppleInAppPurchase::CAppleInAppPurchase()
{
  _receiptVerifier = [[RMStoreAppReceiptVerifier alloc] init];
  [RMStore defaultStore].receiptVerifier = _receiptVerifier;

  _persistence = [[RMStoreKeychainPersistence alloc] init];
  [RMStore defaultStore].transactionPersistor = _persistence;

  NSArray *_products = @[@YEAR_PURCHASE,
                        @LIFETIME_PURCHASE,
                        @FREE_LIFETIME_PURCHASE,
                        @LIFETIME_DIVX_PURCHASE];
  [[RMStore defaultStore] requestProducts:[NSSet setWithArray:_products] success:^(NSArray *products, NSArray *invalidProductIdentifiers)
  {
    int count_out = [products count];
    for (int k = 0; k < count_out; k++)
    {
      Product product;
      SKProduct *sKproduct = products[k];
      if ([sKproduct.productIdentifier isEqualToString:@FREE_LIFETIME_PURCHASE])
        // we dont want this ID to be sent back to App Store window as its not for sale :)
        // but we need to requestProducts: for all 
        continue;
      product.title = [sKproduct.localizedTitle UTF8String];
      product.price = [[RMStore localizedPriceOfProduct:sKproduct] UTF8String];
      product.id    = [sKproduct.productIdentifier UTF8String];
      CLog::Log(LOGDEBUG, "requestProducts: - add : %s", [sKproduct.productIdentifier UTF8String]);
      m_products.push_back(product);
    }
  } failure:^(NSError *error)
  {
    CLog::Log(LOGDEBUG, "requestProducts: - Failed");
  }];
}

CAppleInAppPurchase::~CAppleInAppPurchase()
{
}

CAppleInAppPurchase& CAppleInAppPurchase::GetInstance()
{
  static CAppleInAppPurchase inAppPurchase;
  return inAppPurchase;
}

void CAppleInAppPurchase::SetActivated(bool activated)
{
  m_bIsActivated = activated;
  CLog::Log(LOGNOTICE, "CAppleInAppPurchase::SetActivated() - %s", activated ? "true":"false");
  setUserDefaults(MRMC_ACTIVATED, activated);
  CInAppPurchase::GetInstance().SetActivated(m_bIsActivated);
}

void CAppleInAppPurchase::SetDivxActivated(bool activated)
{
  m_bIsDivxActivated = activated;
  CLog::Log(LOGNOTICE, "CAppleInAppPurchase::SetDivxActivated() - %s", activated ? "true":"false");
  setUserDefaults(MRMC_DIVX_ACTIVATED, activated);
  CInAppPurchase::GetInstance().SetDivxActivated(m_bIsDivxActivated);
}

void CAppleInAppPurchase::SetSubscribed(bool subscribed)
{
  m_bIsActivated = subscribed;
  CInAppPurchase::GetInstance().SetActivated(m_bIsActivated);
  if (subscribed)
  {
    RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
    if (appReceipt)
    {
      NSDate *expiry = [appReceipt activeAutoRenewableSubscriptionOfProductIdentifierExpiry:@YEAR_PURCHASE];
      NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
      [defaults setObject:expiry forKey:@MRMC_SUBSCRIBED];
      [defaults synchronize];
      CLog::Log(LOGDEBUG, "CAppleInAppPurchase::SetSubscribed(): (%s)", subscribed ? "true":"false");
    }
  }
}

bool CAppleInAppPurchase::GetSubscribed()
{
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::GetSubscribed()");
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  if ([defaults objectForKey:@MRMC_SUBSCRIBED])
  {
    NSDate *expiry = [defaults objectForKey:@MRMC_SUBSCRIBED];
    if ([expiry compare:[NSDate date]] == NSOrderedDescending)
      return true;
  }
  return false;
}

void CAppleInAppPurchase::RefreshReceipt()
{
  [[RMStore defaultStore] refreshReceipt];
}

void CAppleInAppPurchase::RemoveTransactions()
{
  // Test Only, to be removed
  RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
  [persistence removeTransactions];
}

void CAppleInAppPurchase::RestoreTransactions()
{
  [[RMStore defaultStore] restoreTransactionsOnSuccess:^(NSArray *transactions)
  {
    VerifyPurchase();
  } failure:^(NSError *error)
  {
    CLog::Log(LOGDEBUG, "CAppleInAppPurchase::RestoreTransactions() - failed");
  }];
}

void CAppleInAppPurchase::VerifyPurchase()
{

  // quick check if app has been activated on this device
  // much quicker than going through receipts every time
  if (GetSubscribed() || checkUserDefaults(MRMC_ACTIVATED))
  {
    SetActivated(true);
    if (checkUserDefaults(MRMC_DIVX_ACTIVATED))
      SetDivxActivated(true);
    return;
  }

  if (![RMStore canMakePayments])
    return;

  RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;

  // dump all purchased products, just so we can debug if needed
  NSSet* purchasedProducts = [persistence purchasedProductIdentifiers];
  for (id product in purchasedProducts)
  {
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - purchasedProductIdentifiers - %s", [[product description] UTF8String]);
  }

  [persistence dumpProducts];

  // Check if we have a FREE lifetime subscription, for users that purchased before 4.0.0
  if ([persistence isPurchasedProductOfIdentifier:@FREE_LIFETIME_PURCHASE])
  {
    SetActivated(true);
    SetDivxActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - LIFETIME_PURCHASE");
    return;
  }

  // Check if we have a lifetime subscription
  if ([persistence isPurchasedProductOfIdentifier:@LIFETIME_PURCHASE])
  {
    SetActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - LIFETIME_PURCHASE");
  }

  // Check if we have a yearly subscription
  if ([persistence isPurchasedProductOfIdentifier:@YEAR_PURCHASE])
  {
    RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
    bool isActive = false;
    if (appReceipt)
    {
      isActive =  [appReceipt containsActiveAutoRenewableSubscriptionOfProductIdentifier:@YEAR_PURCHASE forDate:[NSDate date]];
      SetSubscribed(isActive);
    }
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - YEAR_PURCHASE");
  }

  // Check if we have a fake subscription, thats for the users that migrated from 3.*
  // if this is false, it could be the first 4.* app run and we will write it in a keychain below
  if ([persistence isPurchasedProductOfIdentifier:@TVOS_FAKE_PURCHASE])
  {
    SetActivated(true);
    SetDivxActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - TVOS_FAKE_PURCHASE");
    return;
  }

  // Check if we have a IOS_UPGRADE_PURCHASE set, this will only be set by MrMC Touch v 3.* just before we release 4.0
  // this allows users of MrMC Touch to migrate to MrMC universal app for free
  NSUbiquitousKeyValueStore* kVStore = [NSUbiquitousKeyValueStore defaultStore];
  if ([kVStore boolForKey:@IOS_UPGRADE_PURCHASE] ||
      [persistence isPurchasedProductOfIdentifier:@IOS_UPGRADE_PURCHASE])
  {

    [[RMStore defaultStore] addPayment:@FREE_LIFETIME_PURCHASE success:^(SKPaymentTransaction *transaction)
     {
       [persistence removeProductOfIdentifier:@IOS_UPGRADE_PURCHASE];
       CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - FREE_LIFETIME_PURCHASE");
       SetActivated(true);
       SetDivxActivated(true);
       return;
     } failure:^(SKPaymentTransaction *transaction, NSError *error)
     {
       CLog::Log(LOGNOTICE, "FREE_LIFETIME_PURCHASE Did not complete");
     }];
  }

  // Check if we have a divx subscription
  if ([persistence isPurchasedProductOfIdentifier:@LIFETIME_DIVX_PURCHASE])
  {
    SetDivxActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - LIFETIME_DIVX_PURCHASE");
  }

  // Check if we have purchase receipt, that means user purchased before 4.0.0
  RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
  CLog::Log(LOGDEBUG, "Path is: %s", [[[[NSBundle mainBundle] appStoreReceiptURL] path] UTF8String]);

  if (!appReceipt)
  {
    [[RMStore defaultStore] refreshReceiptOnSuccess:^
     {
       RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];

       if ([[appReceipt originalAppVersion] floatValue] < 4.0)
       {
         RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
         [persistence persistTransactionProductID:@TVOS_FAKE_PURCHASE];
         CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - TVOS_FAKE_PURCHASE - Receipt verified");
         SetActivated(true);
         SetDivxActivated(true);
         return;
       }
     }failure:^(NSError *error)
     {
       CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() Did not complete");
       SetActivated(false);
       SetDivxActivated(false);
     }];
  }
  else
  {
    if ([[appReceipt originalAppVersion] floatValue] < 4.0)
    {
      RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
      [persistence persistTransactionProductID:@TVOS_FAKE_PURCHASE];
      CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - TVOS_FAKE_PURCHASE - Receipt verified");
      SetActivated(true);
      SetDivxActivated(true);
      return;
    }
  }
}

bool CAppleInAppPurchase::PurchaseProduct(std::string product)
{
  NSString* productID = [NSString stringWithUTF8String:product.c_str()];
  [[RMStore defaultStore] addPayment:productID success:^(SKPaymentTransaction *transaction)
   {
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::PurchaseProduct() - Completed %s", [productID UTF8String]);
     VerifyPurchase();
     return;
   } failure:^(SKPaymentTransaction *transaction, NSError *error)
   {
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::PurchaseProduct() - Did not complete");
     VerifyPurchase();
   }];
  return false;
}

ProductList CAppleInAppPurchase::GetProducts()
{
  return m_products;
}

ProductList CAppleInAppPurchase::GetSubscriptions()
{
  ProductList ret;
  RMStore *store = [RMStore defaultStore];
  RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
  NSArray* subscriptions = [persistence purchasedProductIdentifiers].allObjects;
  int count_out = [subscriptions count];
  for (int k = 0; k < count_out; k++)
  {
    Product sub;
    if ([subscriptions[k]  isEqual: @"tv.mrmc.mrmc.tvos.tvosfake"])
    {
      sub.title = [@"Lifetime upgrade from past purchase" UTF8String];
      sub.price = [@"Free" UTF8String];
      sub.id    = [@"tv.mrmc.mrmc.tvos.tvosfake" UTF8String];
      CLog::Log(LOGDEBUG, "CAppleInAppPurchase::GetSubscriptions(): - add : %s", "tv.mrmc.mrmc.tvos.tvosfake");
      ret.push_back(sub);
      continue;
    }
    SKProduct *sKsub = [store productForIdentifier:subscriptions[k]];
    if (!sKsub)
      continue;
    sub.title = [sKsub.localizedTitle UTF8String];
    sub.price = [[RMStore localizedPriceOfProduct:sKsub] UTF8String];
    sub.id    = [sKsub.productIdentifier UTF8String];
    if ([subscriptions[k]  isEqual:@YEAR_PURCHASE])
    {
      RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
      if (appReceipt)
      {
        NSDate *expiry = [appReceipt activeAutoRenewableSubscriptionOfProductIdentifierExpiry:@YEAR_PURCHASE];
        NSDateFormatter *dateFormat = [[NSDateFormatter alloc] init];
        [dateFormat setDateFormat:@"EEE, dd MMM yyyy HH:mm"];
        [dateFormat setLocale:[NSLocale currentLocale]];
        NSString *expiryDate = [dateFormat stringFromDate:expiry];
        sub.expires = [expiryDate UTF8String];
        if ([expiry compare:[NSDate date]] == NSOrderedDescending)
          ret.push_back(sub);
      }
    }
    else
      ret.push_back(sub);
    CLog::Log(LOGDEBUG, "CAppleInAppPurchase::GetSubscriptions(): - add : %s", [sKsub.productIdentifier UTF8String]);

  }
  return ret;
}

bool CAppleInAppPurchase::checkUserDefaults(std::string product)
{
  NSString* strID = [NSString stringWithUTF8String:product.c_str()];
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::checkUserDefaults(): - %s", product.c_str());
  if ([defaults objectForKey:strID])
    return [defaults boolForKey:strID];
  return false;
}

void CAppleInAppPurchase::setUserDefaults(std::string product, bool set)
{
  NSString* strID = [NSString stringWithUTF8String:product.c_str()];
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  if (set)
    [defaults setBool:YES forKey:strID];
  else
  {
    if ([defaults objectForKey:strID])
      [defaults removeObjectForKey:strID];
  }
  [defaults synchronize];
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::setUserDefaults(): - %s (%s)", product.c_str(), set ? "true":"false");
}
