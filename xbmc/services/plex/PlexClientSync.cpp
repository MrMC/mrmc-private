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

#include "PlexClientSync.h"

#include "PlexClient.h"
#include "PlexUtils.h"

#include "contrib/easywsclient/easywsclient.hpp"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/JSONVariantParser.h"
#include "video/VideoInfoTag.h"

typedef enum MediaImportChangesetType
{
  MediaImportChangesetTypeNone = 0,
  MediaImportChangesetTypeAdded,
  MediaImportChangesetTypeChanged,
  MediaImportChangesetTypeRemoved
} MediaImportChangesetType;

CPlexClientSync::CPlexClientSync(CPlexClient *client, const std::string &name, const std::string &address, const std::string &deviceId, const std::string &accessToken)
  : CThread(StringUtils::Format("PlexClientSync[%s]", name.c_str()).c_str())
  , m_client(client)
  , m_address(address)
  , m_name(name)
  , m_sseSocket(nullptr)
  , m_stop(true)
{
  m_client = client;
  CURL curl(address);
  curl.SetFileName(":/eventsource/notifications?X-Plex-Token=" + accessToken);

  m_address = curl.Get();
}

CPlexClientSync::~CPlexClientSync()
{
  Stop();
}

void CPlexClientSync::Start()
{
  if (!m_stop)
    return;

  m_stop = false;
  CThread::Create();
}

void CPlexClientSync::Stop()
{
  if (m_stop)
    return;

  m_stop = true;
  CThread::StopThread();
  SAFE_DELETE(m_sseSocket);
}

void CPlexClientSync::Process()
{
  CLog::Log(LOGDEBUG, "CPlexClientSync: created %s", m_address.c_str());

  CURL curl(m_address);
  curl.SetProtocolOptions(curl.GetProtocolOptions() + "&format=json");
  m_sseSocket = new XFILE::CCurlFile();
  m_sseSocket->SetBufferSize(32768);
  if (!m_sseSocket->OpenForServerSideEvent(curl, (const void*)this, ServerSideEventCallback))
  {
    CLog::Log(LOGERROR, "CPlexClientSync: failed eventsource open to %s", m_name.c_str());
    m_stop = true;
  }
  else
    CLog::Log(LOGDEBUG, "CPlexClientSync: eventsource open to %s -> %s", m_name.c_str(), curl.Get().c_str());


  const int serverSideEventTimeoutMs = 250;
  while (!m_stop)
  {
    if (!m_sseSocket->ServerSideEventWait(serverSideEventTimeoutMs))
      break;
  }

  SAFE_DELETE(m_sseSocket);
  m_stop = true;
}

void CPlexClientSync::ProcessServerSideEvent(const std::string &sse)
{
  static const std::string TimelineEntry = "TimelineEntry";
  static const std::string StatusNotification = "StatusNotification";
  static const std::string ProgressNotification = "ProgressNotification";
  static const std::string PlaySessionStateNotification = "PlaySessionStateNotification";
  // sse token separator == 0x0a -> "\n"
  std::vector<std::string> sse_parts = StringUtils::Split(sse, '\n');
  for (auto it = sse_parts.begin(); it != sse_parts.end(); ++it)
  {
    std::string sse_part = *it;
    if (sse_part.empty())
      continue;

    // event tokens are boring as the info is contained in the data token.
    if (StringUtils::StartsWithNoCase(sse_part, "event:"))
      continue;

    CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessServerSideEvent msg %s", sse_part.c_str());

    if (StringUtils::StartsWithNoCase(sse_part, "data:"))
    {
      // data tokens are json with "data:" prefix. strip the prefix.
      size_t pos = sse_part.find(":");
      if (pos != std::string::npos)
      {
        // parse the json into a CVariant object.
        CVariant variant;
        if (CJSONVariantParser::Parse(sse_part.substr(pos + 1), variant))
        {
          if (variant.isMember(TimelineEntry))
          {
            // "metadataState":"loading"
            // "metadataState":"processing"
            // "metadataState":"created"
            // "metadataState":"created","mediaState":"analyzing"
            // "metadataState":"created","mediaState":"analyzing"
            // "metadataState":"created","mediaState":"thumbnailing"
            CVariant timelineEntry = variant[TimelineEntry];
            std::string metadataState = timelineEntry["metadataState"].asString();
            CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessServerSideEvent TimelineEntry:metadataState = %s", metadataState.c_str());
          }
          else if (variant.isMember(StatusNotification))
          {
            // messages we do not care about
            // "notificationName":"LIBRARY_UPDATE"
            CVariant status = variant[StatusNotification];
          }
          else if (variant.isMember(ProgressNotification))
          {
            // more messages we do not care about
            CVariant progress = variant[ProgressNotification];
          }
          else if (variant.isMember(PlaySessionStateNotification))
          {
            CVariant playSessionState = variant[PlaySessionStateNotification];
            std::string state = playSessionState["state"].asString();
            CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessServerSideEvent PlaySessionStateNotification:state = %s", state.c_str());
          }
          else
          {
            CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessServerSideEvent unknown %s", sse_part.c_str());
          }
        }
      }
    }
  }

}

size_t CPlexClientSync::ServerSideEventCallback(char *buffer, size_t size, size_t nitems, void *userp)
{
  if(userp == NULL) return 0;

  CPlexClientSync *ctx = (CPlexClientSync*)userp;
  size_t amount = size * nitems;

  std::string sse;
  sse.assign(buffer, amount);
  ctx->ProcessServerSideEvent(sse);
  //CLog::Log(LOGDEBUG, "CPlexClientSync:ServerSideEventsCallback msg %s", sse.c_str());
  return size * nitems;
}
