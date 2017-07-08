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

#include <string>

const std::string kOAuth2ClientInfo = "[{\"client\":\"gdrive\",\"client_id\":\"173082143886-b1qhrbohloeugcm6u5kr08ujlr2o5lsn.apps.googleusercontent.com\",\"client_secret\":\"5FPzj6s-iVKSHXD9Lmt6jUbt\"},{\"client\":\"dropbox\",\"client_id\":\"44h26vxxs0q78z9\",\"client_secret\":\"jgjs8a8q5lta9bd\"}]";


class CCloudUtils
{
public:
  CCloudUtils();
  virtual ~CCloudUtils();
  
  static void        ParseAuth2();
  static std::string GetDropboxAppKey();
  static std::string GetDropboxCSRF();
  static bool        AuthorizeCloud(std::string service, std::string authCode);
  static std::string GetAccessToken(std::string service);
private:
  static std::string GenerateRandom16Byte();
  
  static std::string m_dropboxCSFR;
  static std::string m_dropboxAppID;
  static std::string m_dropboxAppSecret;
  static std::string m_dropboxAccessToken;
};
