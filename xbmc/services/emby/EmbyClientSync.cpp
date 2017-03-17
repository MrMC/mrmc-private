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

#include "EmbyUtils.h"
#include "utils/JSONVariantParser.h"
#include "contrib/easywsclient/easywsclient.hpp"

CEmbyClientSync::CEmbyServerSync(const CEmbyServer& server, const std::string& name, const std::string& address, const std::string& deviceId, const std::string& accessToken)
  : CThread(StringUtils::Format("EmbyServerSync[%s]", name.c_str()).c_str())
  , m_server(server)
  , m_name(name)
  , m_address(address)
  , m_websocket(nullptr)
  , m_stop(true)
{
  CURL url(address);
  url.SetProtocol("ws");
  url.SetOption("api_key", accessToken);
  url.SetOption("deviceId", deviceId);

  m_address = url.Get();
}

CEmbyClientSync::~CEmbyServerSync()
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

void CEmbyClientSync::AddImport(const CMediaImport& import)
{
  const auto importIt = std::find_if(m_imports.cbegin(), m_imports.cend(),
    [&import](const CMediaImport& other)
    {
      return import.GetPath().compare(other.GetPath()) == 0 &&
        import.GetSource().GetIdentifier() == other.GetSource().GetIdentifier() &&
        import.GetMediaTypes() == other.GetMediaTypes();
    });
  if (importIt == m_imports.cend())
    m_imports.push_back(import);
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
          CLog::Log(LOGERROR, "CEmbyMediaImporter: invalid websocket notification from %s", m_name.c_str());
          return;
        }

        const std::string msgType = msgObject[NotificationMessageType].asString();
        CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());

        const auto msgData = msgObject[NotificationData];
        if (!msgData.isObject())
        {
          CLog::Log(LOGDEBUG, "CEmbyMediaImporter: ignoring websocket notification of type \"%s\" from %s", msgType.c_str(), m_name.c_str());
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

          std::unordered_map<CMediaImport, ChangesetItems> changedItemsMap;
          for (const auto& changedLibraryItem : changedLibraryItems)
          {
            CLog::Log(LOGDEBUG, "CEmbyMediaImporter: processing changed item with id \"%s\"...", changedLibraryItem.itemId.c_str());

            CFileItemPtr item;
            if (changedLibraryItem.changesetType == MediaImportChangesetTypeAdded || changedLibraryItem.changesetType == MediaImportChangesetTypeChanged)
            {
              item = GetItemDetails(changedLibraryItem.itemId);
              if (item == nullptr)
              {
                CLog::Log(LOGERROR, "CEmbyMediaImporter: failed to get details for changed item with id \"%s\"", changedLibraryItem.itemId.c_str());
                continue;
              }
            }
            else
            {
              // TODO: removed item
            }

            if (item == nullptr)
            {
              CLog::Log(LOGERROR, "CEmbyMediaImporter: failed to process changed item with id \"%s\"", changedLibraryItem.itemId.c_str());
              continue;
            }

            CMediaImport import;
            if (!FindImportForItem(item, import))
            {
              CLog::Log(LOGWARNING, "CEmbyMediaImporter: received changed item with id \"%s\" from unknown media import", changedLibraryItem.itemId.c_str());
              continue;
            }

            changedItemsMap[import].push_back(std::make_pair(changedLibraryItem.changesetType, item));
          }

          for (const auto& changedItems : changedItemsMap)
            CServiceBroker::GetMediaImportManager().ChangeImportedItems(changedItems.first, changedItems.second);
        }
        else if (msgType == NotificationMessageTypeUserDataChanged)
        {
          const auto userDataList = msgData[NotificationUserDataChangedUserDataList];
          if (!msgData.isArray())
          {
            CLog::Log(LOGERROR, "CEmbyMediaImporter: missing \"%s\" in websocket notification of type \"%s\" from %s", NotificationUserDataChangedUserDataList, msgType.c_str(), m_name.c_str());
            return;
          }

          for (auto userData = userDataList.begin_array(); userData != userDataList.end_array(); ++userData)
          {
            if (!userData->isObject() || !userData->isMember(NotificationUserDataChangedUserDataItemId))
              continue;

            const std::string itemId = (*userData)[NotificationUserDataChangedUserDataItemId].asString();

            CFileItemPtr item = GetItemDetails(itemId);
            if (item == nullptr)
            {
              CLog::Log(LOGERROR, "CEmbyMediaImporter: failed to get details for item with id \"%s\"", itemId.c_str());
              continue;
            }

            CMediaImport import;
            if (!FindImportForItem(item, import))
            {
              CLog::Log(LOGWARNING, "CEmbyMediaImporter: received changed item with id \"%s\" from unknown media import", itemId.c_str());
              continue;
            }

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

CFileItemPtr CEmbyClientSync::GetItemDetails(const std::string& itemId) const
{
  // get the URL to retrieve all details of the item from the Emby server
  const auto getItemUrl = m_server.BuildUserItemUrl(itemId);

  std::string result;
  // retrieve all details of the item
  if (!m_server.ApiGet(getItemUrl, result) || result.empty())
  {
    CLog::Log(LOGERROR, "CEmbyMediaImporter: failed to retrieve details for item with id \"%s\" from %s", itemId.c_str(), CURL::GetRedacted(getItemUrl).c_str());
    return nullptr;
  }

  auto resultObject = CJSONVariantParser::Parse(result);
  if (!resultObject.isObject())
  {
    CLog::Log(LOGERROR, "CEmbyMediaImporter: invalid response for item with id \"%s\" from %s", itemId.c_str(), CURL::GetRedacted(getItemUrl).c_str());
    return nullptr;
  }

  return ToFileItem(resultObject, m_server);
}

bool CEmbyClientSync::FindImportForItem(const CFileItemPtr item, CMediaImport& import) const
{
  // try to find a matching media import
  const auto importIt = std::find_if(m_imports.cbegin(), m_imports.cend(),
    [item](const CMediaImport& import)
  {
    const auto mediaTypes = import.GetMediaTypes();
    // TODO: also check the path
    // TODO: this only works for videos
    return std::find(mediaTypes.cbegin(), mediaTypes.cend(), item->GetVideoInfoTag()->m_type) != mediaTypes.cend();
  });

  if (importIt == m_imports.cend())
    return false;

  import = *importIt;
  return true;
}
