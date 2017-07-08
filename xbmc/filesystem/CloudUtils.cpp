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

#include "utils/JSONVariantParser.h"
#include "utils/Base64.h"
#include "utils/Variant.h"

#include <stdlib.h>

std::string CCloudUtils::m_dropboxCSFR;

CCloudUtils::CCloudUtils()
{
  std::string clientInfoString = kOAuth2ClientInfo;
  CVariant clientInfo(CVariant::VariantTypeArray);
  CJSONVariantParser::Parse(clientInfoString, clientInfo);

}

CCloudUtils::~CCloudUtils()
{
}

std::string CCloudUtils::GetDropboxAppKey()
{
  std::string clientInfoString = kOAuth2ClientInfo;
  CVariant clientInfo(CVariant::VariantTypeArray);
  CJSONVariantParser::Parse(clientInfoString, clientInfo);

  for (auto variantItemIt = clientInfo.begin_array(); variantItemIt != clientInfo.end_array(); ++variantItemIt)
  {
    const auto &client = *variantItemIt;
    if (client["client"].asString() == "dropbox")
    {
      return client["client_id"].asString();
    }
  }

  //std::string AppKey = "p81ixbo7322dndd";
  //return AppKey;
  return "";
}

std::string CCloudUtils::GetDropboxCSRF()
{
  m_dropboxCSFR = GenerateRandom16Byte();
  return m_dropboxCSFR;
}

bool CCloudUtils::AuthDropbox(std::string authCode)
{
  
  return true;
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
