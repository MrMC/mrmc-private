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

// lastFullAppStoreVersion is the 3.9.7 tvOS MrMC
const float lastFullAppStoreVersion = 200526.1603;

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
  NSUbiquitousKeyValueStore* kVStore = [NSUbiquitousKeyValueStore defaultStore];
  if (subscribed)
  {
    RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
    if (appReceipt)
    {
      NSDate *expiry = [appReceipt activeAutoRenewableSubscriptionOfProductIdentifierExpiry:@YEAR_PURCHASE];
      [kVStore setObject:expiry forKey:@MRMC_SUBSCRIBED];
      CLog::Log(LOGDEBUG, "CAppleInAppPurchase::SetSubscribed(): (%s)", subscribed ? "true":"false");
    }
  }
  else
  {
    if ([kVStore objectForKey:@MRMC_SUBSCRIBED])
      [kVStore removeObjectForKey:@MRMC_SUBSCRIBED];
  }
  [kVStore synchronize];
}

bool CAppleInAppPurchase::GetSubscribed()
{
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::GetSubscribed()");
  NSUbiquitousKeyValueStore* kVStore = [NSUbiquitousKeyValueStore defaultStore];
  if ([kVStore objectForKey:@MRMC_SUBSCRIBED])
  {
    NSDate *expiry = [kVStore objectForKey:@MRMC_SUBSCRIBED];
    if ([expiry compare:[NSDate date]] == NSOrderedDescending)
      return true;
  }
  return false;
}

void CAppleInAppPurchase::RefreshReceipt()
{
  [[RMStore defaultStore] refreshReceiptOnSuccess:^
   {
     VerifyPurchase(false);
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::RestoreTransactions()  - refresh - complete");
   }failure:^(NSError *error)
   {
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::RestoreTransactions()  - refresh - Did not complete");
   }];
}

void CAppleInAppPurchase::RemoveTransactions()
{
  // Test Only, to be removed
  RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
  [persistence removeTransactions];
}

void CAppleInAppPurchase::RestoreTransactions()
{
  RemoveTransactions();
  [[RMStore defaultStore] restoreTransactionsOnSuccess:^(NSArray *transactions)
  {
    if ([SKPaymentQueue defaultQueue].transactions.count > 0)
    {
       for (SKPaymentTransaction *transaction in transactions)
       {
         if (transaction.transactionState == SKPaymentTransactionStateRestored)
         {
           RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
           [persistence persistTransactionProductID:transaction.payment.productIdentifier];
           CLog::Log(LOGNOTICE, "CAppleInAppPurchase::RestoreTransactions() - SKPaymentTransactionStateRestored for %s", [transaction.payment.productIdentifier UTF8String]);
         }
       }
    }
    VerifyPurchase(false);
  } failure:^(NSError *error)
  {
    CLog::Log(LOGDEBUG, "CAppleInAppPurchase::RestoreTransactions() - failed");
  }];
  CLog::Log(LOGNOTICE, "CAppleInAppPurchase::RestoreTransactions()  - refresh - complete");
}

void CAppleInAppPurchase::VerifyPurchase(bool checkSavedUserDefaults /*true*/)
{

  // quick check if app has been activated on this device
  // much quicker than going through receipts every time
  // if checkSavedUserDefaults is false, we want to go over receipts and save the new information
  if (checkSavedUserDefaults)
  {
    if (GetSubscribed() || checkUserDefaults(MRMC_ACTIVATED))
    {
      SetActivated(true);
      if (checkUserDefaults(MRMC_DIVX_ACTIVATED))
        SetDivxActivated(true);
      return;
    }
  }

  if (![RMStore canMakePayments])
    return;

  RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;

  // dump all purchased products, just so we can debug if needed
  NSSet* purchasedProducts = [persistence purchasedProductIdentifiers];
  for (id product in purchasedProducts)
  {
    NSString *expiryDate = nil;
    if ([[product description] isEqual:@YEAR_PURCHASE])
    {
      RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
      if (appReceipt)
      {
        NSDate *expiry = [appReceipt activeAutoRenewableSubscriptionOfProductIdentifierExpiry:@YEAR_PURCHASE];
        NSDateFormatter *dateFormat = [[NSDateFormatter alloc] init];
        [dateFormat setDateFormat:@"EEE, dd MMM yyyy HH:mm"];
        [dateFormat setLocale:[NSLocale currentLocale]];
        expiryDate = [dateFormat stringFromDate:expiry];
      }
    }
    if (expiryDate)
      CLog::Log(LOGDEBUG, "CAppleInAppPurchase::VerifyPurchase() - purchasedProductIdentifiers - %s - Expires: %s", [[product description] UTF8String], [expiryDate UTF8String]);
    else
      CLog::Log(LOGDEBUG, "CAppleInAppPurchase::VerifyPurchase() - purchasedProductIdentifiers - %s", [[product description] UTF8String]);
  }
  // --- Below real purchases --- //
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
    if (!appReceipt)
    {
      [[RMStore defaultStore] refreshReceiptOnSuccess:^
       {
         RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
         CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - YEAR_PURCHASE");
         bool isActive = [appReceipt containsActiveAutoRenewableSubscriptionOfProductIdentifier:@YEAR_PURCHASE forDate:[NSDate date]];
         SetSubscribed(isActive);
       }failure:^(NSError *error)
       {
         CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase()  - YEAR_PURCHASE - Did not complete");
         SetSubscribed(false);
       }];
    }
    else
    {
      bool isActive = [appReceipt containsActiveAutoRenewableSubscriptionOfProductIdentifier:@YEAR_PURCHASE forDate:[NSDate date]];
      SetSubscribed(isActive);
      CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - YEAR_PURCHASE");
    }
  }

  // Check if we have a divx subscription
  if ([persistence isPurchasedProductOfIdentifier:@LIFETIME_DIVX_PURCHASE])
  {
    SetDivxActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - LIFETIME_DIVX_PURCHASE");
  }
  // --- End real purchases --- //

  // --- Below transfer and fake purchases --- //
  // Check if we have a FREE lifetime subscription, for users that purchased before 4.0.0
  if ([persistence isPurchasedProductOfIdentifier:@FREE_LIFETIME_PURCHASE])
  {
    SetActivated(true);
    SetDivxActivated(true);
    CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - LIFETIME_PURCHASE");
    return;
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
  [kVStore synchronize];
  NSDictionary *kvd = [kVStore dictionaryRepresentation];
  NSArray *arr = [kvd allKeys];
  for (NSUInteger i=0; i < arr.count; i++)
  {
    NSString *key = [arr objectAtIndex:i];
    CLog::Log(LOGDEBUG, "CAppleInAppPurchase::VerifyPurchase() - TouchTransfer - %s", [[key description] UTF8String]);
  }
  if (([kVStore boolForKey:@IOS_UPGRADE_PURCHASE] ||
      [persistence isPurchasedProductOfIdentifier:@IOS_UPGRADE_PURCHASE]) &&
      ![persistence isPurchasedProductOfIdentifier:@FREE_LIFETIME_PURCHASE])
  {
    PurchaseProduct(FREE_LIFETIME_PURCHASE);
  }

  // Check if we have purchase receipt, that means user purchased before 4.0.0
  RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
  if (!appReceipt)
  {
    [[RMStore defaultStore] refreshReceiptOnSuccess:^
     {
       RMAppReceipt *appReceipt = [RMAppReceipt bundleReceipt];
       if ([[appReceipt originalAppVersion] floatValue] <= lastFullAppStoreVersion)
       {
         CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - Receipt verified");
         RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
         [persistence persistTransactionProductID:@TVOS_FAKE_PURCHASE];
         SetActivated(true);
         SetDivxActivated(true);
         return;
       }
     }failure:^(NSError *error)
     {
       CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - Did not complete");
       SetActivated(false);
       SetDivxActivated(false);
     }];
  }
  else
  {
    if ([[appReceipt originalAppVersion] floatValue] <= lastFullAppStoreVersion)
    {
      CLog::Log(LOGNOTICE, "CAppleInAppPurchase::VerifyPurchase() - Receipt verified");
      RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
      [persistence persistTransactionProductID:@TVOS_FAKE_PURCHASE];
      SetActivated(true);
      SetDivxActivated(true);
      return;
    }
  }
  // --- End transfer and fake purchases --- //
}

bool CAppleInAppPurchase::PurchaseProduct(std::string product)
{
  NSString* productID = [NSString stringWithUTF8String:product.c_str()];
  [[RMStore defaultStore] addPayment:productID success:^(SKPaymentTransaction *transaction)
   {
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::PurchaseProduct() - Completed %s", [productID UTF8String]);
     VerifyPurchase(false);
     return;
   } failure:^(SKPaymentTransaction *transaction, NSError *error)
   {
     if (transaction.transactionState == SKPaymentTransactionStateRestored ||
         transaction.transactionState == SKPaymentTransactionStatePurchased)
     {
       if ([transaction.payment.productIdentifier isEqualToString:productID])
       {
         RMStoreKeychainPersistence *persistence = [RMStore defaultStore].transactionPersistor;
         [persistence persistTransactionProductID:productID];
         CLog::Log(LOGNOTICE, "CAppleInAppPurchase::PurchaseProduct() - SKPaymentTransactionStateRestored for %s", [productID UTF8String]);
       }
     }
     CLog::Log(LOGNOTICE, "CAppleInAppPurchase::PurchaseProduct() - Failure");
     VerifyPurchase(false);
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
    if ([subscriptions[k]  isEqual: @TVOS_FAKE_PURCHASE])
    {
      sub.title = [@"Lifetime upgrade from past purchase" UTF8String];
      sub.price = [@"Free" UTF8String];
      sub.id    = [@TVOS_FAKE_PURCHASE UTF8String];
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
        if (expiryDate)
        {
          sub.expires = [expiryDate UTF8String];
          if ([expiry compare:[NSDate date]] == NSOrderedDescending)
            ret.push_back(sub);
        }
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
  NSUbiquitousKeyValueStore* kVStore = [NSUbiquitousKeyValueStore defaultStore];
  [kVStore synchronize];
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::checkUserDefaults(): - %s", product.c_str());
  if ([kVStore objectForKey:strID])
    return [kVStore boolForKey:strID];
  return false;
}

void CAppleInAppPurchase::setUserDefaults(std::string product, bool set)
{
  NSString* strID = [NSString stringWithUTF8String:product.c_str()];
  NSUbiquitousKeyValueStore* kVStore = [NSUbiquitousKeyValueStore defaultStore];
  if (set)
    [kVStore setBool:YES forKey:strID];
  else
  {
    if ([kVStore objectForKey:strID])
      [kVStore removeObjectForKey:strID];
  }
  [kVStore synchronize];
  CLog::Log(LOGDEBUG, "CAppleInAppPurchase::setUserDefaults(): - %s (%s)", product.c_str(), set ? "true":"false");
}
