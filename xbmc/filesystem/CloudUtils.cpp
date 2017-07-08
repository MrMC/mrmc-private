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

#include "CloudUtils.h"

#include "URL.h"
#include "utils/JSONVariantParser.h"
#include "utils/Base64.h"
#include "utils/Variant.h"
#include "filesystem/CurlFile.h"
#include "filesystem/ZipFile.h"
#include "settings/Settings.h"

#include <stdlib.h>

std::string CCloudUtils::m_dropboxCSFR;
std::string CCloudUtils::m_dropboxAccessToken;
std::string CCloudUtils::m_dropboxAppID;
std::string CCloudUtils::m_dropboxAppSecret;

CCloudUtils::CCloudUtils()
{

}

CCloudUtils::~CCloudUtils()
{
}

void CCloudUtils::ParseAuth2()
{
  std::string clientInfoString = kOAuth2ClientInfo;
  CVariant clientInfo(CVariant::VariantTypeArray);
  CJSONVariantParser::Parse(clientInfoString, clientInfo);
  for (auto variantItemIt = clientInfo.begin_array(); variantItemIt != clientInfo.end_array(); ++variantItemIt)
  {
    const auto &client = *variantItemIt;
    if (client["client"].asString() == "dropbox")
    {
      m_dropboxAppID = client["client_id"].asString();
      m_dropboxAppSecret = client["client_secret"].asString();
    }
  }
  m_dropboxAccessToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_CLOUDDROPBOXTOKEN);
}

std::string CCloudUtils::GetDropboxAppKey()
{
  ParseAuth2();
  return m_dropboxAppID;
}

std::string CCloudUtils::GetDropboxCSRF()
{
  return GenerateRandom16Byte();
}

std::string CCloudUtils::GetAccessToken(std::string service)
{
  ParseAuth2();
  if (service == "dropbox")
    return m_dropboxAccessToken;
  
  return "";
}

bool CCloudUtils::AuthorizeCloud(std::string service, std::string authCode)
{
  ParseAuth2();
  if (service == "dropbox")
  {
    std::string url = "https://api.dropbox.com/1/oauth2/token?grant_type=authorization_code&code=" + authCode;

    CURL curl(url);
    curl.SetUserName(m_dropboxAppID);
    curl.SetPassword(m_dropboxAppSecret);
    
    XFILE::CCurlFile db;
    std::string response, data;
    if (db.Post(curl.Get(),data,response))
    {
      CVariant resultObject;
      if (CJSONVariantParser::Parse(response, resultObject))
      {
        if (resultObject.isObject() || resultObject.isArray())
        {
          m_dropboxAccessToken = resultObject["access_token"].asString();
          CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_CLOUDDROPBOXTOKEN, m_dropboxAccessToken);
          CSettings::GetInstance().Save();
          return true;
        }
      }
    }
  }
  return false;
}

std::string CCloudUtils::GenerateRandom16Byte()
{
  unsigned char buf[16];
  int i;
  srand(time(NULL));
  for (i = 0; i < 16; i++) {
    buf[i] = rand() % 256;
  }
  return Base64::Encode((const char*)buf, sizeof(buf));
}
