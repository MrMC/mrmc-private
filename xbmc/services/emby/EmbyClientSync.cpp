/*
 *      Copyright (C) 2017 Team MrMC
 *      https://github.com/MrMC
 *      based from EmbyMediaImporter.cpp
 *      Copyright (C) 2016 Team XBMC
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

#include "EmbyClientSync.h"
#include "EmbyClient.h"

#include "EmbyUtils.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/JSONVariantParser.h"
#include "contrib/easywsclient/easywsclient.hpp"
#include "video/VideoInfoTag.h"

#include <unordered_map>


typedef enum MediaImportChangesetType
{
  MediaImportChangesetTypeNone = 0,
  MediaImportChangesetTypeAdded,
  MediaImportChangesetTypeChanged,
  MediaImportChangesetTypeRemoved
} MediaImportChangesetType;

CEmbyClientSync::CEmbyClientSync(CEmbyClient *client, const std::string &name, const std::string &address, const std::string &deviceId, const std::string &accessToken)
  : CThread(StringUtils::Format("EmbyClientSync[%s]", name.c_str()).c_str())
  , m_client(client)
  , m_address(address)
  , m_name(name)
  , m_websocket(nullptr)
  , m_stop(true)
{
  m_client = client;
  CURL url(address);
  url.SetProtocol("ws");
  url.SetOption("api_key", accessToken);
  url.SetOption("deviceId", deviceId);

  m_address = url.Get();
}

CEmbyClientSync::~CEmbyClientSync()
{
  Stop();
}

void CEmbyClientSync::Start()
{
  if (!m_stop)
    return;

  m_stop = false;
  CThread::Create();
}

void CEmbyClientSync::Stop()
{
  if (m_stop)
    return;

  m_stop = true;
  CThread::StopThread();
}

void CEmbyClientSync::Process()
{
  static const int WebSocketTimeoutMs = 100;

  static const std::string NotificationMessageType = "MessageType";
  static const std::string NotificationData = "Data";
  static const std::string NotificationMessageTypeLibraryChanged = "LibraryChanged";
  static const std::string NotificationMessageTypeUserDataChanged = "UserDataChanged";
  static const std::string NotificationMessageTypePlaybackStart = "PlaybackStart";
  static const std::string NotificationMessageTypePlaybackStopped = "PlaybackStopped";
  static const std::string NotificationLibraryChangedItemsAdded = "ItemsAdded";
  static const std::string NotificationLibraryChangedItemsUpdated = "ItemsUpdated";
  static const std::string NotificationLibraryChangedItemsRemoved = "ItemsRemoved";
  static const std::string NotificationUserDataChangedUserDataList = "UserDataList";
  static const std::string NotificationUserDataChangedUserDataItemId = "ItemId";

  struct ChangedLibraryItem
  {
    std::string itemId;
    MediaImportChangesetType changesetType;
  };

  m_websocket.reset(easywsclient::WebSocket::from_url(m_address /* TODO: , origin */));

  while (!m_stop && m_websocket->getReadyState() != easywsclient::WebSocket::CLOSED)
  {
    m_websocket->poll(WebSocketTimeoutMs);
    m_websocket->dispatch(
      [this](const std::string& msg)
      {
        const auto msgObject = CJSONVariantParser::Parse(msg);
        if (!msgObject.isObject() || !msgObject.isMember(NotificationMessageType) || !msgObject.isMember(NotificationData))
        {
          CLog::Log(LOGERROR, "CEmbyClientSync: invalid websocket notification from %s", m_name.c_str());
          return;
        }

        const std::string msgType = msgObject[NotificationMessageType].asString();
        CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());

        const auto msgData = msgObject[NotificationData];
        if (!msgData.isObject())
        {
          CLog::Log(LOGDEBUG, "CEmbyClientSync: ignoring websocket notification of type \"%s\" from %s", msgType.c_str(), m_name.c_str());
          return;
        }

        if (msgType == NotificationMessageTypeLibraryChanged)
        {
          const auto itemsAdded = msgData[NotificationLibraryChangedItemsAdded];
          const auto itemsUpdated = msgData[NotificationLibraryChangedItemsUpdated];
          const auto itemsRemoved = msgData[NotificationLibraryChangedItemsRemoved];

          std::vector<ChangedLibraryItem> changedLibraryItems;

          if (itemsAdded.isArray())
          {
            for (auto item = itemsAdded.begin_array(); item != itemsAdded.end_array(); ++item)
            {
              if (item->isString() && !item->empty())
                changedLibraryItems.emplace_back(ChangedLibraryItem { item->asString(), MediaImportChangesetTypeAdded });
            }
          }

          if (itemsUpdated.isArray())
          {
            for (auto item = itemsUpdated.begin_array(); item != itemsUpdated.end_array(); ++item)
            {
              if (item->isString() && !item->empty())
                changedLibraryItems.emplace_back(ChangedLibraryItem{ item->asString(), MediaImportChangesetTypeChanged });
            }
          }

          if (itemsRemoved.isArray())
          {
            for (auto item = itemsRemoved.begin_array(); item != itemsRemoved.end_array(); ++item)
            {
              if (item->isString() && !item->empty())
                changedLibraryItems.emplace_back(ChangedLibraryItem{ item->asString(), MediaImportChangesetTypeRemoved });
            }
          }

          for (const auto& changedLibraryItem : changedLibraryItems)
          {
            CLog::Log(LOGDEBUG, "CEmbyClientSync: processing changed item with id \"%s\"...", changedLibraryItem.itemId.c_str());

            CFileItemPtr item;
            if (changedLibraryItem.changesetType == MediaImportChangesetTypeAdded || changedLibraryItem.changesetType == MediaImportChangesetTypeChanged)
            {
              item = m_client->FindViewItemByServiceId(changedLibraryItem.itemId);
              if (item == nullptr)
              {
                CLog::Log(LOGERROR, "CEmbyClientSync: failed to get details for changed item with id \"%s\"", changedLibraryItem.itemId.c_str());
                continue;
              }
            }
            else
            {
              // TODO: removed item
            }

            if (item == nullptr)
            {
              CLog::Log(LOGERROR, "CEmbyClientSync: failed to process changed item with id \"%s\"", changedLibraryItem.itemId.c_str());
              continue;
            }
/*
            CMediaImport import;
            if (!FindImportForItem(item, import))
            {
              CLog::Log(LOGWARNING, "CEmbyClientSync: received changed item with id \"%s\" from unknown media import", changedLibraryItem.itemId.c_str());
              continue;
            }
*/
          }
        }
        else if (msgType == NotificationMessageTypeUserDataChanged)
        {
          const auto userDataList = msgData[NotificationUserDataChangedUserDataList];
          if (!msgData.isArray())
          {
            CLog::Log(LOGERROR, "CEmbyClientSync: missing \"%s\" in websocket notification of type \"%s\" from %s", NotificationUserDataChangedUserDataList.c_str(), msgType.c_str(), m_name.c_str());
            return;
          }

          for (auto userData = userDataList.begin_array(); userData != userDataList.end_array(); ++userData)
          {
            if (!userData->isObject() || !userData->isMember(NotificationUserDataChangedUserDataItemId))
              continue;

            const std::string itemId = (*userData)[NotificationUserDataChangedUserDataItemId].asString();

            CFileItemPtr item = m_client->FindViewItemByServiceId(itemId);
            if (item == nullptr)
            {
              CLog::Log(LOGERROR, "CEmbyClientSync: failed to get details for item with id \"%s\"", itemId.c_str());
              continue;
            }
/*
            CMediaImport import;
            if (!FindImportForItem(item, import))
            {
              CLog::Log(LOGWARNING, "CEmbyClientSync: received changed item with id \"%s\" from unknown media import", itemId.c_str());
              continue;
            }
*/
            // TODO
          }
        }
        else if (msgType == NotificationMessageTypePlaybackStart)
        {
          // TODO
        }
        else if (msgType == NotificationMessageTypePlaybackStopped)
        {
          // TODO
        }
      });
  }

  m_websocket->close();
  m_websocket.reset();
  m_stop = true;
}
