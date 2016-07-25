/*
 *  Copyright (C) 2016 Team MN
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

#include "NWTVAPI.h"

#include "URL.h"
#include "filesystem/CurlFile.h"
#include "utils/Variant.h"
#include "utils/JSONVariantParser.h"
#include "utils/log.h"


bool TVAPI_DoActivate(NWActivate &activate)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  CURL nwmn_activate(kTVAPI_URLBASE + "activate");
  nwmn_activate.SetProtocolOption("seekable", "0");
  nwmn_activate.SetProtocolOption("auth", "basic");
  nwmn_activate.SetProtocolOption("Content-Type", "application/x-www-form-urlencoded");
  nwmn_activate.SetOption("code", activate.code);
  nwmn_activate.SetOption("application_id", activate.application_id);
  std::string strResponse;
  if (nwmn.Post(nwmn_activate.Get(), "", strResponse))
  {
    CLog::Log(LOGDEBUG, "testNationwide5_0 %s", strResponse.c_str());
    //{"message":"Operation Successful","key":"\/3\/NKO6ZFdRgum7fZkMi","secret":"ewuDiXOIgZP7l9\/Rxt\/LDQbmAI1zJe0PQ5VZYnuy"}

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    std::string message = reply["message"].asString();
    if (message == "Operation Successful")
    {
      activate.apiKey = reply["key"].asString();
      activate.apiSecret = reply["secret"].asString();
      CLog::Log(LOGDEBUG, "testNationwide5_0 apiKey = %s", activate.apiKey.c_str());
      CLog::Log(LOGDEBUG, "testNationwide5_0 apiSecret = %s", activate.apiSecret.c_str());
      return true;
    }
  }
  return false;
}

bool TVAPI_GetStatus(NWStatus &status)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  CURL nwmn_status(kTVAPI_URLBASE + "status");
  nwmn_status.SetProtocolOption("seekable", "0");
  nwmn_status.SetProtocolOption("auth", "basic");
  nwmn_status.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_status.SetUserName(status.apiKey);
  nwmn_status.SetPassword(status.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_status.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "testNationwide5_0 %s", strResponse.c_str());
    //{"key":"fGyb157LNrPsP4DOVin1","unique_id":"My Application Id","activation_date":"2016-07-08 14:19:51","status":"1","status_text":"Active","machine_id":"1757"}

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    status.key             = reply["key"].asString();
    status.unique_id       = reply["unique_id"].asString();
    status.machine_id      = reply["machine_id"].asString();
    status.status          = reply["status"].asString();
    status.status_text     = reply["status_text"].asString();
    status.activation_date = reply["activation_date"].asString();
    return true;
  }

  return false;
}

bool TVAPI_GetMachine(NWMachine &machine)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  CURL nwmn_machine(kTVAPI_URLBASE + "machine");
  nwmn_machine.SetProtocolOption("seekable", "0");
  nwmn_machine.SetProtocolOption("auth", "basic");
  nwmn_machine.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_machine.SetUserName(machine.apiKey);
  nwmn_machine.SetPassword(machine.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_machine.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "testNationwide5_0 %s", strResponse.c_str());
    //{"id":"1757","member":"1560","machine_name":"jww-test","description":"Description from JSON","playlist_id":"1096","status":null,"vendor":"0","hardware":"0","timezone":"America\/Puerto_Rico","serial_number":"421232123212321232124","warranty_number":"0","video_format":"720","allow_new_content":"1","allow_software_update":"1","update_interval":"daily","update_time":"2400","location":{"id":"83400001","name":"All Maytag","address":"401 S. Broadway","address2":"","zip_code":"73034","state":"OK","city":"Edmond","phone":"(405) 359-9274","fax":"(405) 359-0022"},"network":{"macaddress":"","macaddress_wireless":"","dhcp":"1","ipaddress":"","subnet":"","router":"","dns_1":"","dns_2":""},"settings":{"network":"1","pairing":"1","about":"1","hdmibrightness":"1","tvresolution":"1","updatesoftware":"0","language":"1","legal":"1"},"menu":{"membernettv":"1","vendorcommercials":"0","hdcontent":"1","membercommercials":"0","movietrailers":"1","promotionalcampaigns":"1","nationwidebroadcasts":"0","imaginationwidehd":"1","primemediacommercialfactory":"1"},"membernet_software":{"id":"20","version":"MNTV 2.1","cfbundleversion":"2.1.1","url":"http:\/\/test.nationwidemember.com\/resources\/tv\/versions\/membernet_2_1.tar.gz"},"apple_software":{"id":"20","version":"MNTV 2.1","url":"http:\/\/test.nationwidemember.com\/resources\/tv\/versions\/membernet_2_1.tar.gz"}}
    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    machine.id                    = reply["id"].asString();
    machine.member                = reply["member"].asString();
    machine.machine_name          = reply["machine_name"].asString();
    machine.description           = reply["description"].asString();
    machine.playlist_id           = reply["playlist_id"].asString();
    machine.status                = reply["status"].asString();
    machine.vendor                = reply["vendor"].asString();
    machine.hardware              = reply["hardware"].asString();
    machine.timezone              = reply["timezone"].asString();
    machine.serial_number         = reply["serial_number"].asString();
    machine.warranty_number       = reply["warranty_number"].asString();
    machine.video_format          = reply["video_format"].asString();
    machine.allow_new_content     = reply["allow_new_content"].asString();
    machine.allow_software_update = reply["allow_software_update"].asString();
    machine.update_time           = reply["update_time"].asString();

    CVariant location             = reply["location"];
    machine.location.id           = location["id"].asString();
    machine.location.name         = location["name"].asString();
    machine.location.address      = location["address"].asString();
    machine.location.address2     = location["address2"].asString();
    machine.location.city         = location["city"].asString();
    machine.location.state        = location["state"].asString();
    machine.location.phone        = location["phone"].asString();
    machine.location.fax          = location["fax"].asString();
    
    CVariant network              = reply["network"];
    machine.network.macaddress    = network["macaddress"].asString();
    machine.network.macaddress_wireless = network["macaddress_wireless"].asString();
    machine.network.dhcp          = network["dhcp"].asString();
    machine.network.ipaddress     = network["ipaddress"].asString();
    machine.network.subnet        = network["subnet"].asString();
    machine.network.router        = network["router"].asString();
    machine.network.dns_1         = network["dns_1"].asString();
    machine.network.dns_2         = network["dns_2"].asString();

    CVariant settings             = reply["settings"];
    machine.settings.network      = settings["network"].asString();
    machine.settings.pairing      = settings["pairing"].asString();
    machine.settings.network      = settings["network"].asString();
    machine.settings.about        = settings["about"].asString();
    machine.settings.hdmibrightness = settings["hdmibrightness"].asString();
    machine.settings.tvresolution = settings["tvresolution"].asString();
    machine.settings.updatesoftware = settings["updatesoftware"].asString();
    machine.settings.language     = settings["language"].asString();
    machine.settings.legal        = settings["legal"].asString();

    CVariant menu                 = reply["menu"];
    machine.menu.membernettv      = menu["membernettv"].asString();
    machine.menu.vendorcommercials = menu["vendorcommercials"].asString();
    machine.menu.hdcontent        = menu["hdcontent"].asString();
    machine.menu.membercommercials = menu["membercommercials"].asString();
    machine.menu.movietrailers    = menu["movietrailers"].asString();
    machine.menu.promotionalcampaigns = menu["promotionalcampaigns"].asString();
    machine.menu.nationwidebroadcasts = menu["nationwidebroadcasts"].asString();
    machine.menu.imaginationwidehd = menu["imaginationwidehd"].asString();
    machine.menu.primemediacommercialfactory = menu["primemediacommercialfactory"].asString();

    CVariant membernet_software   = reply["membernet_software"];
    machine.membernet_software.id = membernet_software["id"].asString();
    machine.membernet_software.version = membernet_software["version"].asString();
    machine.membernet_software.cfbundleversion = membernet_software["cfbundleversion"].asString();
    machine.membernet_software.url = membernet_software["url"].asString();

    CVariant apple_software       = reply["apple_software"];
    machine.apple_software.id     = apple_software["id"].asString();
    machine.apple_software.version = apple_software["version"].asString();
    machine.apple_software.url    = apple_software["url"].asString();
  }

  return false;
}








