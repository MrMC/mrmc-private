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
#include "OAuth2ClientInfo.h"

#include "URL.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/Base64.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "filesystem/CurlFile.h"
#include "filesystem/ZipFile.h"
#include "settings/Settings.h"

#include <stdlib.h>

void testclientinfo(void)
{
  std::string clientInfoString = OAuth2ClientInfo::Decrypt();
  CVariant clientInfo(CVariant::VariantTypeArray);
  CJSONVariantParser::Parse(clientInfoString, clientInfo);

  CLog::Log(LOGDEBUG, "testclientinfo %s", clientInfoString.c_str());
}

void genclientinfo(void)
{
  const std::string GOOGLEAPI_CLIENTID = "173082143886-b1qhrbohloeugcm6u5kr08ujlr2o5lsn.apps.googleusercontent.com";
  const std::string GOOGLEAPI_CLIENTSECRET = "5FPzj6s-iVKSHXD9Lmt6jUbt";

  const std::string DROPBOXAPI_CLIENTID = "44h26vxxs0q78z9";
  const std::string DROPBOXAPI_CLIENTSECRET = "jgjs8a8q5lta9bd";

  CVariant oath2ClientInfo(CVariant::VariantTypeArray);
  CVariant oath2Client;
  oath2Client["client"] = "gdrive";
  oath2Client["client_id"] = GOOGLEAPI_CLIENTID;
  oath2Client["client_secret"] = GOOGLEAPI_CLIENTSECRET;
  oath2ClientInfo.append(oath2Client);

  oath2Client["client"] = "dropbox";
  oath2Client["client_id"] = DROPBOXAPI_CLIENTID;
  oath2Client["client_secret"] = DROPBOXAPI_CLIENTSECRET;
  oath2ClientInfo.append(oath2Client);
  std::string oath2ClientInfoString;
  CJSONVariantWriter::Write(oath2ClientInfo, oath2ClientInfoString, true);
  CLog::Log(LOGDEBUG, "genclientinfo %s", oath2ClientInfoString.c_str());
}

std::string CCloudUtils::m_dropboxAccessToken;
std::string CCloudUtils::m_dropboxAppID;
std::string CCloudUtils::m_dropboxAppSecret;
std::string CCloudUtils::m_googleAppID;
std::string CCloudUtils::m_googleAppSecret;
std::string CCloudUtils::m_googleAccessToken;
std::string CCloudUtils::m_googleRefreshToken;
void CCloudUtils::ParseAuth2()
{
  std::string clientInfoString = OAuth2ClientInfo::Decrypt();
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
    else if (client["client"].asString() == "gdrive")
    {
      m_googleAppID = client["client_id"].asString();
      m_googleAppSecret = client["client_secret"].asString();
    }
  }
  m_googleAccessToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_CLOUDGOOGLETOKEN);
  m_dropboxAccessToken = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_CLOUDDROPBOXTOKEN);
}

std::string CCloudUtils::GetDropboxAppKey()
{
  ParseAuth2();
  return m_dropboxAppID;
}

std::string CCloudUtils::GetGoogleAppKey()
{
  ParseAuth2();
  return m_googleAppID;
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
    CURL curl("https://api.dropbox.com/1/oauth2/token?grant_type=authorization_code&code=" + authCode);
    curl.SetUserName(m_dropboxAppID);
    curl.SetPassword(m_dropboxAppSecret);
    
    std::string response;
    XFILE::CCurlFile curlfile;
    if (curlfile.Post(curl.Get(), "", response))
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
  else if (service == "google")
  {
    CURL curl("https://www.googleapis.com/oauth2/v4/token");
    curl.SetProtocolOption("seekable", "0");
   
    std::string data, response;
    data += "redirect_uri=" + CURL::Encode("urn:ietf:wg:oauth:2.0:oob");
    data += "&code=" + CURL::Encode(authCode);
    data += "&client_secret=" + CURL::Encode(m_googleAppSecret);
    data += "&client_id=" + CURL::Encode(m_googleAppID);
    data += "&scope=&grant_type=authorization_code";
    XFILE::CCurlFile curlfile;
    bool ret = curlfile.Post(curl.Get(), data, response);
    if (ret)
    {
      CVariant resultObject;
      if (CJSONVariantParser::Parse(response, resultObject))
      {
        if (resultObject.isObject() || resultObject.isArray())
        {
          m_googleAccessToken = resultObject["access_token"].asString();
          m_googleRefreshToken = resultObject["refresh_token"].asString();
          CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_CLOUDGOOGLETOKEN, m_googleAccessToken);
          CSettings::GetInstance().SetString(CSettings::SETTING_SERVICES_CLOUDGOOGLEREFRESHTOKEN, m_googleRefreshToken);
          CSettings::GetInstance().Save();
          return true;
        }
      }
    }
//    CSettings::GetInstance().SetString(SETTING_SERVICES_CLOUDGOOGLEREFRESHTOKEN);
//    CSettings::GetInstance().SetInt(CSettings::SETTING_SERVICES_CLOUDGOOGLEREFRESHTIME);
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

