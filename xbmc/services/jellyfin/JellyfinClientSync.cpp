/*
 *      Copyright (C) 2020 Team MrMC
 *      https://github.com/MrMC
 *      based from JellyfinMediaImporter.cpp
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

#include "JellyfinClientSync.h"

#include "JellyfinServices.h"
#include "JellyfinClient.h"
#include "JellyfinUtils.h"

#include "contrib/easywsclient/easywsclient.hpp"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/JSONVariantParser.h"
#include "video/VideoInfoTag.h"

CJellyfinClientSync::CJellyfinClientSync(const std::string &name, const std::string &address, const std::string &deviceId, const std::string &accessToken)
  : CThread(StringUtils::Format("JellyfinClientSync[%s]", name.c_str()).c_str())
  , m_address(address)
  , m_name(name)
  , m_websocket(nullptr)
  , m_stop(true)
{
  // ws://server.local:32400/:/websockets/notifications
  CURL curl(address);
  if (curl.GetProtocol() == "http")
    curl.SetProtocol("ws");
  else if (curl.GetProtocol() == "https")
    curl.SetProtocol("wss");
  curl.SetOption("api_key", accessToken);
  curl.SetOption("deviceId", deviceId);

  m_address = curl.Get();
}

CJellyfinClientSync::~CJellyfinClientSync()
{
  Stop();
}

void CJellyfinClientSync::Start()
{
  if (!m_stop)
    return;

  m_stop = false;
  CThread::Create();
}

void CJellyfinClientSync::Stop()
{
  if (m_stop)
    return;

  m_stop = true;
  CThread::StopThread();
}

void CJellyfinClientSync::Process()
{
  m_websocket = easywsclient::WebSocket::from_url(m_address /* TODO: , origin */);
  if (!m_websocket)
  {
    CLog::Log(LOGERROR, "CJellyfinClientSync: websocket connection failed from %s", m_name.c_str());
    m_stop = true;
  }
  else
    CLog::Log(LOGDEBUG, "CJellyfinClientSync: websocket connected to %s", m_name.c_str());

  while (!m_stop && m_websocket->getReadyState() != easywsclient::WebSocket::CLOSED)
  {
    m_websocket->poll(100);
    m_websocket->dispatch(
      [this](const std::string& msg)
      {
        CVariant msgObject;
        if (!CJSONVariantParser::Parse(msg, msgObject) ||
          !msgObject.isObject() ||
          !msgObject.isMember("MessageType")
          || !msgObject.isMember("Data"))
        {
          CLog::Log(LOGERROR, "CJellyfinClientSync: invalid websocket notification from %s", m_name.c_str());
          return;
        }

        const std::string msgType = msgObject["MessageType"].asString();
        // ignore SessionEnded (server spew)
        if (msgType == "SessionEnded")
          return;
        // ignore UserUpdated (server spew)
        if (msgType == "UserUpdated")
          return;
        // ignore PlaybackStart (server spew)
        if (msgType == "PlaybackStart")
          return;
        // ignore PlaybackStopped (server spew)
        if (msgType == "PlaybackStopped")
          return;
        // ignore ScheduledTaskEnded (server spew)
        if (msgType == "ScheduledTaskEnded")
          return;

        //CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());

        const auto msgData = msgObject["Data"];
        if (!msgData.isObject())
        {
          CLog::Log(LOGDEBUG, "CJellyfinClientSync: ignoring websocket notification of type \"%s\" from %s", msgType.c_str(), m_name.c_str());
          return;
        }

        if (msgType == "LibraryChanged")
        {
          CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(m_address);
          if (client && client->GetPresence())
          {
#if defined(JELLYFIN_DEBUG_VERBOSE)
            CLog::Log(LOGDEBUG, "CJellyfinClientSync: processing LibraryChanged");
            CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());
#endif
            const auto itemsAdded = msgData["ItemsAdded"];
            if (itemsAdded.isArray())
            {
              std::vector<std::string> ids;
              for (auto item = itemsAdded.begin_array(); item != itemsAdded.end_array(); ++item)
              {
                if (item->isString())
                  ids.push_back(item->asString());
              }
              if (!ids.empty())
                client->AddNewViewItems(ids);
            }

            const auto itemsUpdated = msgData["ItemsUpdated"];
            if (itemsUpdated.isArray())
            {
              std::vector<std::string> ids;
              for (auto item = itemsUpdated.begin_array(); item != itemsUpdated.end_array(); ++item)
              {
                if (item->isString())
                  ids.push_back(item->asString());
              }
              if (!ids.empty())
                client->UpdateViewItems(ids);
            }

            const auto itemsRemoved = msgData["ItemsRemoved"];
            if (itemsRemoved.isArray())
            {
              std::vector<std::string> ids;
              for (auto item = itemsRemoved.begin_array(); item != itemsRemoved.end_array(); ++item)
              {
                if (item->isString())
                  ids.push_back(item->asString());
              }
              if (!ids.empty())
                client->RemoveViewItems(ids);
            }
          }
        }
        else if (msgType == "UserDataChanged")
        {
          // we see these from us or some other jellyfin client causes changes (seems like during playback)
          CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(m_address);
          if (client && client->GetPresence())
          {
#if defined(JELLYFIN_DEBUG_VERBOSE)
            CLog::Log(LOGDEBUG, "CJellyfinClientSync: processing UserDataChanged");
            CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());
#endif
            const auto userDataList = msgData["UserDataList"];
            std::vector<std::string> ids;
            for (auto userData = userDataList.begin_array(); userData != userDataList.end_array(); ++userData)
            {
              if (userData->isObject() && userData->isMember("ItemId"))
                ids.push_back((*userData)["ItemId"].asString());
            }
            if (!ids.empty())
              client->UpdateViewItems(ids);
          }
        }
        else
        {
          CLog::Log(LOGDEBUG, "[%s] %s: %s", this->m_name.c_str(), msgType.c_str(), msg.c_str());
        }
      });
  }

  if (m_websocket)
  {
    m_websocket->close();
    SAFE_DELETE(m_websocket);
  }
  m_stop = true;
}
