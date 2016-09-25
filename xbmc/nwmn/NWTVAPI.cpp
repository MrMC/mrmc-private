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
#include "utils/JSONVariantParser.h"
#include "utils/Variant.h"
#include "utils/URIUtils.h"
#include "utils/log.h"


bool TVAPI_DoActivate(TVAPI_Activate &activate)
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
    CLog::Log(LOGDEBUG, "TVAPI_DoActivate %s", strResponse.c_str());
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

bool TVAPI_GetStatus(TVAPI_Status &status)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(5);

  CURL nwmn_status(kTVAPI_URLBASE + "status");
  nwmn_status.SetProtocolOption("seekable", "0");
  nwmn_status.SetProtocolOption("auth", "basic");
  nwmn_status.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_status.SetUserName(status.apiKey);
  nwmn_status.SetPassword(status.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_status.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "TVAPI_GetStatus %s", strResponse.c_str());
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

bool TVAPI_GetMachine(TVAPI_Machine &machine)
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
    CLog::Log(LOGDEBUG, "TVAPI_GetMachine %s", strResponse.c_str());
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
    machine.update_interval       = reply["update_interval"].asString();

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

    return true;
  }

  return false;
}

bool TVAPI_GetPlaylists(TVAPI_Playlists &playlists)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  std::string sub_url;
  sub_url = kTVAPI_URLBASE + "playlist";
  sub_url += "?_page=" + std::to_string(1) + "&_perPage=50";

  CURL nwmn_machine(sub_url);
  nwmn_machine.SetProtocolOption("seekable", "0");
  nwmn_machine.SetProtocolOption("auth", "basic");
  nwmn_machine.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_machine.SetUserName(playlists.apiKey);
  nwmn_machine.SetPassword(playlists.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_machine.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "TVAPI_GetPlaylists %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    int page = 1;
    int total = reply["total"].asInteger();
    int sub_total = 0;
    playlists.playlists.clear();
    while(true)
    {
      int curPage = reply["page"].asInteger();
      int perPage = reply["perPage"].asInteger();

      CVariant results(CVariant::VariantTypeArray);
      results = reply["results"];
      for (size_t i = 0; i < results.size(); i++, sub_total++)
      {
        CVariant result = results[i];

        TVAPI_PlaylistInfo playListInfo;
        playListInfo.id = result["id"].asString();
        playListInfo.name = result["name"].asString();
        playListInfo.type = result["type"].asString();
        playListInfo.updated_date = result["updated_date"].asString();
        playListInfo.layout = result["layout"].asString();
        playListInfo.member_id = result["member_id"].asString();
        playListInfo.member_name = result["member_name"].asString();
        playListInfo.nmg_managed = result["nmg_managed"].asString();
        playlists.playlists.push_back(playListInfo);

        CLog::Log(LOGDEBUG, "TVAPI_GetPlaylists %d, %s", sub_total, playListInfo.name.c_str());
      }
      
      CLog::Log(LOGDEBUG, "TVAPI_GetPlaylists page = %d, perPage = %d, sub_total = %d, total = %d", curPage, perPage, sub_total, total);

      if (sub_total < total)
      {
        sub_url = kTVAPI_URLBASE + "playlist";
        sub_url += "?_page=" + std::to_string(++page) + "&_perPage=25";
        nwmn_machine = CURL(sub_url);
        if (nwmn.Get(nwmn_machine.Get(), strResponse))
        {
          reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());
          continue;
        }
      }

      // if we get here, we are done
      break;
    }
    
    return true;
  }

  return false;
}

bool TVAPI_GetPlaylist(TVAPI_Playlist &playlist, std::string playlist_id)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  std::string url;
  url = kTVAPI_URLBASE + "playlist/" + playlist_id;

  CURL nwmn_machine(url);
  nwmn_machine.SetProtocolOption("seekable", "0");
  nwmn_machine.SetProtocolOption("auth", "basic");
  nwmn_machine.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_machine.SetUserName(playlist.apiKey);
  nwmn_machine.SetPassword(playlist.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_machine.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "TVAPI_GetPlaylist %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    playlist.id = reply["id"].asString();
    playlist.name = reply["name"].asString();
    playlist.type = reply["type"].asString();
    playlist.layout = reply["layout"].asString();
    playlist.member_id = reply["member_id"].asString();
    playlist.nmg_managed = reply["nmg_managed"].asString();
    playlist.updated_date = reply["updated_date"].asString();

    if (playlist.type == "smart")
    {
      CVariant categories(CVariant::VariantTypeArray);
      categories = reply["categories"];
      for (size_t i = 0; i < categories.size(); ++i)
      {
        TVAPI_CategoryId category;
        category.id = categories[i]["id"].asString();
        category.name = categories[i]["name"].asString();
        playlist.categories.push_back(category);
      }
    }
    else if (playlist.type == "custom")
    {
      CVariant files(CVariant::VariantTypeArray);
      files = reply["files"];
      for (size_t i = 0; i < files.size(); ++i)
      {
        TVAPI_FileId file;
        file.id = files[i]["id"].asString();
        playlist.files.push_back(file);
      }
    }

    return true;
  }

  return false;
}

bool TVAPI_GetPlaylistItems(TVAPI_PlaylistItems &playlistItems, std::string playlist_id)
{
  XFILE::CCurlFile nwmn;
  nwmn.SetTimeout(30);

  std::string url;
  url = kTVAPI_URLBASE + "playlist/" + playlist_id + "/files";

  CURL nwmn_machine(url);
  nwmn_machine.SetProtocolOption("seekable", "0");
  nwmn_machine.SetProtocolOption("auth", "basic");
  nwmn_machine.SetProtocolOption("Cache-Control", "no-cache");
  nwmn_machine.SetUserName(playlistItems.apiKey);
  nwmn_machine.SetPassword(playlistItems.apiSecret);
  std::string strResponse;

  if (nwmn.Get(nwmn_machine.Get(), strResponse))
  {
    CLog::Log(LOGDEBUG, "TVAPI_GetPlaylistItems %s", strResponse.c_str());

    CVariant reply;
    reply = CJSONVariantParser::Parse((const unsigned char*)strResponse.c_str(), strResponse.size());

    CVariant playlist = reply["playlist"];
    playlistItems.id = playlist["id"].asString();
    playlistItems.name = playlist["name"].asString();
    playlistItems.type = playlist["type"].asString();

    CVariant results(CVariant::VariantTypeArray);
    results = reply["results"];
    for (size_t i = 0; i < results.size(); ++i)
    {
      CVariant result = results[i];

      TVAPI_PlaylistItem item;
      item.id = result["id"].asString();
      item.name = result["name"].asString();
      item.tv_category_id = result["tv_category_id"].asString();
      item.description = result["description"].asString();
      item.created_date = result["created_date"].asString();
      item.updated_date = result["updated_date"].asString();
      item.completion_date = result["completion_date"].asString();
      item.theatricalrelease = result["theatricalrelease"].asString();
      item.dvdrelease = result["dvdrelease"].asString();
      item.download = result["download"].asString();
      CVariant availability = result["availability"];
      item.availability_to = availability["to"].asString();
      item.availability_from = availability["from"].asString();

      CVariant files = result["files"];
      if (files.isObject())
      {
        for (CVariant::const_iterator_map it = files.begin_map(); it != files.end_map(); ++it)
        {
          CVariant fileobj = it->second;
          if (fileobj.isObject())
          {
            if (it->first == "720" || it->first == "1080" || it->first == "4K")
            {
              TVAPI_PlaylistFile file;
              file.type = it->first;
              file.path = fileobj["path"].asString();
              file.size = fileobj["size"].asString();
              file.width = fileobj["width"].asString();
              file.height = fileobj["height"].asString();
              file.etag = fileobj["etag"].asString();
              file.mime_type = fileobj["mime_type"].asString();
              file.created_date = fileobj["created_date"].asString();
              file.updated_date = fileobj["updated_date"].asString();
              item.files.push_back(file);
            }
          }
        }
        // sort from low rez to high rez
        std::sort(item.files.begin(), item.files.end(),
          [] (TVAPI_PlaylistFile const& a, TVAPI_PlaylistFile const& b)
          {
            return std::stoi(a.type) < std::stoi(b.type);
          });

        // now find the 'thumb'
        for (CVariant::const_iterator_map it = files.begin_map(); it != files.end_map(); ++it)
        {
          CVariant fileobj = it->second;
          if (fileobj.isObject() && it->first == "thumb")
          {
            item.thumb.type = it->first;
            item.thumb.path = fileobj["path"].asString();
            item.thumb.size = fileobj["size"].asString();
            item.thumb.width = fileobj["width"].asString();
            item.thumb.height = fileobj["height"].asString();
            item.thumb.etag = fileobj["etag"].asString();
            item.thumb.mime_type = fileobj["mime_type"].asString();
            item.thumb.created_date = fileobj["created_date"].asString();
            item.thumb.updated_date = fileobj["updated_date"].asString();
          }
        }

        // and find the 'poster'
        for (CVariant::const_iterator_map it = files.begin_map(); it != files.end_map(); ++it)
        {
          CVariant fileobj = it->second;
          if (fileobj.isObject() && it->first == "thumb")
          {
            item.poster.type = it->first;
            item.poster.path = fileobj["path"].asString();
            item.poster.size = fileobj["size"].asString();
            item.poster.width = fileobj["width"].asString();
            item.poster.height = fileobj["height"].asString();
            item.poster.etag = fileobj["etag"].asString();
            item.poster.mime_type = fileobj["mime_type"].asString();
            item.poster.created_date = fileobj["created_date"].asString();
            item.poster.updated_date = fileobj["updated_date"].asString();
          }
        }

      }
      playlistItems.items.push_back(item);
    }

    return true;
  }

  return false;
}
