/*
 *  Copyright (C) 2016 RootCoder, LLC.
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
 *  along with this app; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "NWClientUtilities.h"

#include "filesystem/File.h"
#include "utils/StringUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

// ---------------------------------------------
// ---------------------------------------------
bool HasLocalPlayer(std::string home)
{
  return XFILE::CFile::Exists(home + kNWClient_PlayerFileName);
}

bool LoadLocalPlayer(std::string home, NWPlayerInfo &playerInfo)
{
  CXBMCTinyXML xmlDoc;
  if (!xmlDoc.LoadFile(home + kNWClient_PlayerFileName))
    return false;

  TiXmlElement *rootElement = xmlDoc.RootElement();
  if (!rootElement)
    return false;

  XMLUtils::GetString(rootElement, "id", playerInfo.id);
  XMLUtils::GetString(rootElement, "name", playerInfo.name);
  XMLUtils::GetString(rootElement, "member", playerInfo.member);
  XMLUtils::GetString(rootElement, "timezone", playerInfo.timezone);
  XMLUtils::GetString(rootElement, "playlist_id", playerInfo.playlist_id);
  XMLUtils::GetString(rootElement, "video_format", playerInfo.video_format);
  XMLUtils::GetString(rootElement, "update_time", playerInfo.update_time);
  XMLUtils::GetString(rootElement, "update_interval", playerInfo.update_interval);
  XMLUtils::GetString(rootElement, "status", playerInfo.status);
  XMLUtils::GetString(rootElement, "apiKey", playerInfo.apiKey);
  XMLUtils::GetString(rootElement, "apiSecret", playerInfo.apiSecret);
  XMLUtils::GetInt(   rootElement, "intSettingsVersion", playerInfo.intSettingsVersion);

  XMLUtils::GetString(rootElement, "strUpdateUrl", playerInfo.strUpdateUrl);
  XMLUtils::GetString(rootElement, "strUpdateKey", playerInfo.strUpdateKey);
  XMLUtils::GetString(rootElement, "strUpdateMD5", playerInfo.strUpdateMD5);
  XMLUtils::GetString(rootElement, "strUpdateSize", playerInfo.strUpdateSize);
  XMLUtils::GetString(rootElement, "strUpdateName", playerInfo.strUpdateName);
  XMLUtils::GetString(rootElement, "strUpdateDate", playerInfo.strUpdateDate);
  XMLUtils::GetString(rootElement, "strUpdateVersion", playerInfo.strUpdateVersion);

  return true;
}

bool SaveLocalPlayer(std::string home, const NWPlayerInfo &playerInfo)
{
  CXBMCTinyXML xmlDoc;

  TiXmlElement rootElement("player");
  TiXmlNode *rootNode = xmlDoc.InsertEndChild(rootElement);
  if (rootNode)
  {
    XMLUtils::SetString(rootNode, "id", playerInfo.id);
    XMLUtils::SetString(rootNode, "name", playerInfo.name);
    XMLUtils::SetString(rootNode, "member", playerInfo.member);
    XMLUtils::SetString(rootNode, "timezone", playerInfo.timezone);
    XMLUtils::SetString(rootNode, "playlist_id", playerInfo.playlist_id);
    XMLUtils::SetString(rootNode, "video_format", playerInfo.video_format);
    XMLUtils::SetString(rootNode, "update_time", playerInfo.update_time);
    XMLUtils::SetString(rootNode, "update_interval", playerInfo.update_interval);
    XMLUtils::SetString(rootNode, "status", playerInfo.status);
    XMLUtils::SetString(rootNode, "apiKey", playerInfo.apiKey);
    XMLUtils::SetString(rootNode, "apiSecret", playerInfo.apiSecret);
    XMLUtils::SetInt(   rootNode, "intSettingsVersion", playerInfo.intSettingsVersion);

    XMLUtils::SetString(rootNode, "strUpdateUrl", playerInfo.strUpdateUrl);
    XMLUtils::SetString(rootNode, "strUpdateKey", playerInfo.strUpdateKey);
    XMLUtils::SetString(rootNode, "strUpdateMD5", playerInfo.strUpdateMD5);
    XMLUtils::SetString(rootNode, "strUpdateSize", playerInfo.strUpdateSize);
    XMLUtils::SetString(rootNode, "strUpdateName", playerInfo.strUpdateName);
    XMLUtils::SetString(rootNode, "strUpdateDate", playerInfo.strUpdateDate);
    XMLUtils::SetString(rootNode, "strUpdateVersion", playerInfo.strUpdateVersion);

    return xmlDoc.SaveFile(home + kNWClient_PlayerFileName);
  }
  
  return false;
}

bool HasLocalPlaylist(std::string home)
{
  return XFILE::CFile::Exists(home + kNWClient_PlaylistFileName);
}

bool LoadLocalPlaylist(std::string home, NWPlaylist &playList)
{
  CXBMCTinyXML xmlDoc;
  if (!xmlDoc.LoadFile(home + kNWClient_PlaylistFileName))
    return false;

  TiXmlElement *rootElement = xmlDoc.RootElement();
  if (!rootElement)
    return false;

  XMLUtils::GetInt(   rootElement, "id", playList.id);
  XMLUtils::GetString(rootElement, "name", playList.name);
  XMLUtils::GetString(rootElement, "type", playList.type);
  XMLUtils::GetString(rootElement, "video_format", playList.video_format);
  XMLUtils::GetString(rootElement, "updated_date", playList.updated_date);

  std::string play_order;
  XMLUtils::GetString(rootElement, "play_order", play_order);
  std::vector<std::string> orders = StringUtils::Split(play_order, ',');
  playList.play_order.clear();
  for (auto order: orders)
  {
    if (!order.empty())
      playList.play_order.push_back(std::stoi(order));
  }

  playList.groups.clear();

  // no groups is a failure
  const TiXmlElement *groupElement = rootElement->FirstChildElement("group");
  if (!groupElement)
    return false;

  while (groupElement != NULL)
  {
    NWGroup group;
    XMLUtils::GetInt(   groupElement, "id", group.id);
    XMLUtils::GetString(groupElement, "name", group.name);
    XMLUtils::GetInt(   groupElement, "next_asset_index", group.next_asset_index);

    // no assets is not a failure
    const TiXmlElement *assetElement = groupElement->FirstChildElement("asset");
    while (assetElement != nullptr)
    {
      NWAsset asset;
      XMLUtils::GetInt(   assetElement, "id", asset.id);
      XMLUtils::GetInt(   assetElement, "group_id", asset.group_id);
      XMLUtils::GetString(assetElement, "name", asset.name);
      XMLUtils::GetString(assetElement, "type", asset.type);

      XMLUtils::GetString(assetElement, "video_url", asset.video_url);
      XMLUtils::GetString(assetElement, "video_md5", asset.video_md5);
      XMLUtils::GetInt(   assetElement, "video_size", asset.video_size);
      XMLUtils::GetString(assetElement, "video_basename", asset.video_basename);
      XMLUtils::GetString(assetElement, "video_localpath", asset.video_localpath);

      XMLUtils::GetString(assetElement, "thumb_url", asset.thumb_url);
      XMLUtils::GetString(assetElement, "thumb_md5", asset.thumb_md5);
      XMLUtils::GetInt(   assetElement, "thumb_size", asset.thumb_size);
      XMLUtils::GetString(assetElement, "thumb_basename", asset.thumb_basename);
      XMLUtils::GetString(assetElement, "thumb_localpath", asset.thumb_localpath);

      std::string availability;
      XMLUtils::GetString(assetElement, "available_to", availability);
      asset.available_to.SetFromDBDateTime(availability);
      XMLUtils::GetString(assetElement, "available_from", availability);
      asset.available_from.SetFromDBDateTime(availability);

      group.assets.push_back(asset);
      assetElement = assetElement->NextSiblingElement("asset");
    }
    playList.groups.push_back(group);

    groupElement = groupElement->NextSiblingElement("group");
  }

  return true;
}

bool SaveLocalPlaylist(std::string home, const NWPlaylist &playList)
{
  CXBMCTinyXML xmlDoc;

  TiXmlElement rootElement("playlist");
  TiXmlNode *rootNode = xmlDoc.InsertEndChild(rootElement);
  if (rootNode)
  {
    XMLUtils::SetInt(   rootNode, "id", playList.id);
    XMLUtils::SetString(rootNode, "name", playList.name);
    XMLUtils::SetString(rootNode, "type", playList.type);
    XMLUtils::SetString(rootNode, "video_format", playList.video_format);
    XMLUtils::SetString(rootNode, "updated_date", playList.updated_date);
    std::string play_order;
    for (auto order: playList.play_order)
      play_order += std::to_string(order) + ",";
    // remove the trailing ','
    play_order.pop_back();
    XMLUtils::SetString(rootNode, "play_order", play_order);

    for (auto group: playList.groups)
    {
      TiXmlElement groupElement("group");
      TiXmlNode *groupNode = rootNode->InsertEndChild(groupElement);
      if (groupNode)
      {
        XMLUtils::SetInt(   groupNode, "id", group.id);
        XMLUtils::SetString(groupNode, "name", group.name);
        XMLUtils::SetInt(   groupNode, "next_asset_index", group.next_asset_index);
        for (auto asset: group.assets)
        {
          TiXmlElement assetElement("asset");
          TiXmlNode *assetNode = groupNode->InsertEndChild(assetElement);
          if (assetNode)
          {
            XMLUtils::SetInt(   assetNode, "id", asset.id);
            XMLUtils::SetInt(   assetNode, "group_id", asset.group_id);
            XMLUtils::SetString(assetNode, "name", asset.name);
            XMLUtils::SetString(assetNode, "type", asset.type);

            XMLUtils::SetString(assetNode, "video_url", asset.video_url);
            XMLUtils::SetString(assetNode, "video_md5", asset.video_md5);
            XMLUtils::SetInt(   assetNode, "video_size", asset.video_size);
            XMLUtils::SetString(assetNode, "video_basename", asset.video_basename);
            XMLUtils::SetString(assetNode, "video_localpath", asset.video_localpath);

            XMLUtils::SetString(assetNode, "thumb_url", asset.thumb_url);
            XMLUtils::SetString(assetNode, "thumb_md5", asset.thumb_md5);
            XMLUtils::SetInt(   assetNode, "thumb_size", asset.thumb_size);
            XMLUtils::SetString(assetNode, "thumb_basename", asset.thumb_basename);
            XMLUtils::SetString(assetNode, "thumb_localpath", asset.thumb_localpath);

            XMLUtils::SetString(assetNode, "available_to", asset.available_to.GetAsDBDateTime());
            XMLUtils::SetString(assetNode, "available_from", asset.available_from.GetAsDBDateTime());
          }
        }
      }
    }

    return xmlDoc.SaveFile(home + kNWClient_PlaylistFileName);
  }
  
  return false;
}
