/*
 *      Copyright (C) 2020 Team MrMC
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

#include "JellyfinDirectory.h"

#include "Application.h"
#include "FileItem.h"
#include "URL.h"
#include "network/Network.h"
#include "network/Socket.h"
#include "filesystem/Directory.h"
#include "guilib/LocalizeStrings.h"
#include "services/jellyfin/JellyfinUtils.h"
#include "services/jellyfin/JellyfinViewCache.h"
#include "services/jellyfin/JellyfinServices.h"
#include "utils/log.h"
#include "utils/Base64URL.h"
#include "utils/StringUtils.h"
#include "utils/JSONVariantParser.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "video/VideoInfoTag.h"
#include "video/VideoDatabase.h"
#include "music/MusicDatabase.h"

using namespace XFILE;

CJellyfinDirectory::CJellyfinDirectory()
{ }

CJellyfinDirectory::~CJellyfinDirectory()
{ }

bool CJellyfinDirectory::GetDirectory(const CURL& url, CFileItemList &items)
{
#if defined(JELLYFIN_DEBUG_VERBOSE)
  CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory");
#endif
  {
    assert(url.IsProtocol("jellyfin"));
    std::string path = url.Get();
    if (path == "jellyfin://")
    {
      // we are broswing network for clients.
      return FindByBroadcast(items);
    }
    if (url.GetFileName() == "wan" ||
        url.GetFileName() == "local")
    {
      // user selected some jellyfin server found by
      // broadcast. now we need to stop the dir
      // recursion so return a dummy items.
      CFileItemPtr item(new CFileItem("", false));
      CURL curl1(url);
      curl1.SetFileName("dummy");
      item->SetPath(curl1.Get());
      item->SetLabel("dummy");
      item->SetLabelPreformated(true);
      //just set the default folder icon
      item->FillInDefaultIcon();
      item->m_bIsShareOrDrive = true;
      items.Add(item);
      return true;
    }
  }

  std::string strUrl = url.Get();
  std::string section = URIUtils::GetFileName(strUrl);
  items.SetPath(strUrl);
  std::string basePath = strUrl;
  URIUtils::RemoveSlashAtEnd(basePath);
  basePath = URIUtils::GetFileName(basePath);
  
  CVideoDatabase database;
  database.Open();
  bool hasMovies = database.HasContent(VIDEODB_CONTENT_MOVIES);
  bool hasShows = database.HasContent(VIDEODB_CONTENT_TVSHOWS);
  database.Close();

#if defined(JELLYFIN_DEBUG_VERBOSE)
  CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory strURL = %s", strUrl.c_str());
#endif
  if (StringUtils::StartsWithNoCase(strUrl, "jellyfin://movies/"))
  {
    if (section.empty())
    {
      //look through all jellyfin clients and pull content data for "movie" type
      std::vector<CJellyfinClientPtr> clients;
      CJellyfinServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        const std::vector<JellyfinViewInfo> contents = client->GetViewInfoForMovieContent();
        if (contents.size() > 1 || ((items.Size() > 0 || CServicesManager::GetInstance().HasPlexServices() ||
                                     clients.size() > 1 || hasMovies) && contents.size() == 1))
        {
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.name);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CJellyfinUtils::SetJellyfinItemProperties(*pItem, "movies", client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            curl.SetFileName(content.prefix);
            pItem->SetPath("jellyfin://movies/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            curl.SetFileName(CJellyfinUtils::ConstructFileName(curl, "Items/") + content.id + "/Images/Primary"); //"Items/"
            pItem->SetArt("thumb", curl.Get());
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
#if defined(JELLYFIN_DEBUG_VERBOSE)
            CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
#endif
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          curl.SetFileName(contents[0].prefix);
          //client->GetMovies(items, curl.Get()); ????
          CDirectory::GetDirectory("jellyfin://movies/" + basePath + "/" + Base64URL::Encode(curl.Get()), items);
          items.SetContent("movies");
          CJellyfinUtils::SetJellyfinItemProperties(items, "movies", client);
          for (int item = 0; item < items.Size(); ++item)
            CJellyfinUtils::SetJellyfinItemProperties(*items[item], "movies", client);
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedmovies")
          label = g_localizeStrings.Get(20386);
        else if (URIUtils::GetFileName(basePath) == "inprogressmovies")
          label = g_localizeStrings.Get(627);
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
      }
    }
    else
    {
      CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);

      std::string filter;
      if (path == "genres")
        filter = "Genres";
      else if (path == "years")
        filter = "Years";
      else if (path == "sets")
        filter = "Collections";
   //   else if (path == "countries")
   //     filter = "country";
      else if (path == "studios")
        filter = "Studios";
      else
        filter = path;

      if (path == "" || path == "titles" || path == "filter")
      {
        client->GetMovies(items, Base64URL::Decode(section), path == "filter");
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("movies");
      }
      else if (path == "recentlyaddedmovies")
      {
        CJellyfinUtils::GetJellyfinRecentlyAddedMovies(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(20386));
        items.SetContent("movies");
      }
      else if (path == "inprogressmovies")
      {
        CJellyfinUtils::GetJellyfinInProgressMovies(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(627));
        items.SetContent("movies");
      }
      else if (path == "set")
      {
        CJellyfinUtils::GetJellyfinSet(items, Base64URL::Decode(section));
        //We set the items name in GetJellyfinSets
        items.SetContent("movies");
      }
      else if (path == "filters")
      {
        client->GetMoviesFilters(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("filters");
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.ClearSortState();
      }
      else if(!filter.empty())
      {
        client->GetMoviesFilter(items, Base64URL::Decode(section), filter);
        StringUtils::ToCapitalize(filter);
        items.SetLabel(filter);
        items.SetContent("movies");
      }
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory' client(%s), found %d movies", client->GetServerName().c_str(), items.Size());
#endif
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "jellyfin://tvshows/"))
  {
    if (section.empty())
    {
      //look through all jellyfin servers and pull content data for "show" type
      std::vector<CJellyfinClientPtr> clients;
      CJellyfinServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        const std::vector<JellyfinViewInfo> contents = client->GetViewInfoForTVShowContent();
        if (contents.size() > 1 || ((items.Size() > 0 || CServicesManager::GetInstance().HasPlexServices() ||
                                     clients.size() > 1 || hasShows) && contents.size() == 1))
        {
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.name);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CJellyfinUtils::SetJellyfinItemProperties(*pItem, "tvshows", client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            curl.SetFileName(content.prefix);
            pItem->SetPath("jellyfin://tvshows/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            curl.SetFileName("Items/" + content.id + "/Images/Primary");
            pItem->SetArt("thumb", curl.Get());
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
#if defined(JELLYFIN_DEBUG_VERBOSE)
           CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
#endif
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          curl.SetFileName(contents[0].prefix);
          //client->GetTVShows(items, curl.Get()); ????
          CDirectory::GetDirectory("jellyfin://tvshows/" + basePath + "/" + Base64URL::Encode(curl.Get()), items);
          CJellyfinUtils::SetJellyfinItemProperties(items, "tvshows", client);
          for (int item = 0; item < items.Size(); ++item)
            CJellyfinUtils::SetJellyfinItemProperties(*items[item], "tvshows", client);
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedepisodes")
          label = g_localizeStrings.Get(20387);
        else if (URIUtils::GetFileName(basePath) == "inprogressshows")
          label = g_localizeStrings.Get(626);
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
      }
    }
    else
    {
      CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);
      
      std::string filter;
      if (path == "genres")
        filter = "Genres";
      else if (path == "years")
        filter = "Years";
     // else if (path == "sets")
     //   filter = "Collections";
      //   else if (path == "countries")
      //     filter = "country";
      else if (path == "studios")
        filter = "Studios";
      else
        filter = path;

      if (path == "" || path == "titles" || path == "filter")
      {
        client->GetTVShows(items, Base64URL::Decode(section), path == "filter");
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("tvshows");
      }
      else if (path == "shows")
      {
        CJellyfinUtils::GetJellyfinSeasons(items,Base64URL::Decode(section));
        if(items.Size() > 0 && items[0]->GetVideoInfoTag()->m_type == MediaTypeSeason)
          items.SetContent("seasons");
        else
          items.SetContent("episodes");
        
      }
      else if (path == "seasons")
      {
        CJellyfinUtils::GetJellyfinEpisodes(items,Base64URL::Decode(section));
        items.SetContent("episodes");
      }
      else if (path == "recentlyaddedepisodes")
      {
        CJellyfinUtils::GetJellyfinRecentlyAddedEpisodes(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(20387));
        items.SetContent("episodes");
      }
      else if (path == "inprogressshows")
      {
        CJellyfinUtils::GetJellyfinInProgressShows(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(626));
        items.SetContent("episodes");
      }
      else if (path == "filters")
      {
        client->GetTVShowFilters(items, Base64URL::Decode(section));
        items.SetLabel("Filters");
        items.SetContent("filters");
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.ClearSortState();
      }
      else if(!filter.empty())
      {
        client->GetTVShowsFilter(items, Base64URL::Decode(section), filter);
        StringUtils::ToCapitalize(filter);
        items.SetLabel(filter);
        items.SetContent("tvshows");
      }
#if defined(JELLYFIN_DEBUG_VERBOSE)
      CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory' client(%s), found %d shows", client->GetServerName().c_str(), items.Size());
#endif
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "jellyfin://music/"))
  {
    if (section.empty())
    {
      //look through all jellyfin servers and pull content data for "show" type
      std::vector<CJellyfinClientPtr> clients;
      CJellyfinServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        const std::vector<JellyfinViewInfo> contents = client->GetViewInfoForMusicContent();
        if (contents.size() > 1 || ((items.Size() > 0 || CServicesManager::GetInstance().HasPlexServices() || clients.size() > 1) && contents.size() == 1))
        {
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.name);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CJellyfinUtils::SetJellyfinItemProperties(*pItem, "music", client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            curl.SetFileName(content.prefix);
            pItem->SetPath("jellyfin://music/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            curl.SetFileName("Items/" + content.id + "/Images/Primary");
            pItem->SetArt("thumb", curl.Get());
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
#if defined(JELLYFIN_DEBUG_VERBOSE)
            CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
#endif
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          curl.SetFileName(contents[0].prefix);
          client->GetMusicArtists(items, curl.Get());
          items.SetContent("artists");
          items.SetPath("jellyfin://music/albums/");
          CJellyfinUtils::SetJellyfinItemProperties(items, "music", client);
          for (int item = 0; item < items.Size(); ++item)
            CJellyfinUtils::SetJellyfinItemProperties(*items[item], "music", client);
#if defined(JELLYFIN_DEBUG_VERBOSE)
          CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory '/all' client(%s), shows(%d)", client->GetServerName().c_str(), items.Size());
#endif
        }
      }
    }
    else
    {
      CJellyfinClientPtr client = CJellyfinServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);
      
      std::string filter = "all";
      if (path == "albums")
        filter = "albums";
      
      if (path == "" || path == "root" || path == "artists")
      {
        client->GetMusicArtists(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(36917));
        items.SetContent("artists");
      }
      else if (path == "albums")
      {
        CJellyfinUtils::GetJellyfinAlbum(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(36919));
        items.SetContent("albums");
      }
      else if (path == "artistalbums")
      {
        CJellyfinUtils::GetJellyfinArtistAlbum(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(36919));
        items.SetContent("albums");
      }
      else if (path == "songs")
      {
        CJellyfinUtils::GetJellyfinSongs(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(36921));
        items.SetContent("songs");
      }
      else if (path == "albumsongs")
      {
        CJellyfinUtils::GetJellyfinAlbumSongs(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(36921));
        items.SetContent("songs");
      }
      else if (path == "recentlyaddedalbums")
      {
        CJellyfinUtils::GetJellyfinRecentlyAddedAlbums(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(359));
        items.SetContent("albums");
      }
    }
    return true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "CJellyfinDirectory::GetDirectory got nothing from %s", CURL::GetRedacted(strUrl).c_str());
  }

  return false;
}

DIR_CACHE_TYPE CJellyfinDirectory::GetCacheType(const CURL& url) const
{
  return DIR_CACHE_NEVER;
}

bool CJellyfinDirectory::FindByBroadcast(CFileItemList &items)
{
  bool rtn = false;
  static const int NS_JELLYFIN_BROADCAST_PORT(7359);
  static const std::string NS_JELLYFIN_BROADCAST_ADDRESS("255.255.255.255");
  static const std::string NS_JELLYFIN_BROADCAST_SEARCH_MSG("who is JellyfinServer?");

  SOCKETS::CSocketListener *m_broadcastListener = nullptr;

  if (!m_broadcastListener)
  {
    SOCKETS::CUDPSocket *socket = SOCKETS::CSocketFactory::CreateUDPSocket();
    if (socket)
    {
      CNetworkInterface *iface = g_application.getNetwork().GetFirstConnectedInterface();
      if (iface && iface->IsConnected())
      {
        SOCKETS::CAddress my_addr;
        my_addr.SetAddress(iface->GetCurrentIPAddress().c_str());
        if (!socket->Bind(my_addr, NS_JELLYFIN_BROADCAST_PORT, 0))
        {
          CLog::Log(LOGERROR, "CJellyfinServices:CheckJellyfinServers Could not listen on port %d", NS_JELLYFIN_BROADCAST_PORT);
          SAFE_DELETE(m_broadcastListener);
          return rtn;
        }

        if (socket)
        {
          socket->SetBroadCast(true);
          // create and add our socket to the 'select' listener
          m_broadcastListener = new SOCKETS::CSocketListener();
          m_broadcastListener->AddSocket(socket);
        }
      }
      else
      {
        SAFE_DELETE(socket);
      }
    }
    else
    {
      CLog::Log(LOGERROR, "CJellyfinServices:CheckJellyfinServers Could not create socket for GDM");
      return rtn;
    }
  }

  SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_broadcastListener->GetFirstSocket();
  if (socket)
  {
    SOCKETS::CAddress discoverAddress;
    discoverAddress.SetAddress(NS_JELLYFIN_BROADCAST_ADDRESS.c_str(), NS_JELLYFIN_BROADCAST_PORT);
    std::string discoverMessage = NS_JELLYFIN_BROADCAST_SEARCH_MSG;
    int packetSize = socket->SendTo(discoverAddress, discoverMessage.length(), discoverMessage.c_str());
    if (packetSize < 0)
      CLog::Log(LOGERROR, "CJellyfinServices::CheckJellyfinServers:CheckforGDMServers discover send failed");
  }

  static const int DiscoveryTimeoutMs = 10000;
  // listen for broadcast reply until we timeout
  if (socket && m_broadcastListener->Listen(DiscoveryTimeoutMs))
  {
    char buffer[8192] = {0};
    SOCKETS::CAddress sender;
    int packetSize = socket->Read(sender, 8192, buffer);
    if (packetSize > -1)
    {
      if (packetSize > 0)
      {
        CVariant data;
        std::string jsonpacket = buffer;
        if (!CJSONVariantParser::Parse(jsonpacket, data))
          return rtn;
        static const std::string ServerPropertyAddress = "Address";
        if (data.isObject() && data.isMember(ServerPropertyAddress))
        {
          JellyfinServerInfo jellyfinServerInfo = CJellyfinServices::GetInstance().GetJellyfinLocalServerInfo(data[ServerPropertyAddress].asString());
          if (!jellyfinServerInfo.ServerId.empty())
          {
            CLog::Log(LOGNOTICE, "CJellyfinServices::CheckJellyfinServers Server found %s", jellyfinServerInfo.ServerName.c_str());
            CFileItemPtr local(new CFileItem("", true));
            CURL curl1(jellyfinServerInfo.LocalAddress);
            curl1.SetProtocol("jellyfin");
            // set a magic key
            curl1.SetFileName("local");
            local->SetPath(curl1.Get());
            local->SetLabel(jellyfinServerInfo.ServerName + " (local)");
            local->SetLabelPreformated(true);
            //just set the default folder icon
            local->FillInDefaultIcon();
            local->m_bIsShareOrDrive = true;

            items.Add(local);

            // jellyfin does use WanAddress and it might be missing
            if (!jellyfinServerInfo.WanAddress.empty())
            {
              CFileItemPtr remote(new CFileItem("", true));
              CURL curl2(jellyfinServerInfo.WanAddress);
              curl2.SetProtocol("jellyfin");
              // set a magic key
              curl1.SetFileName("wan");
              remote->SetPath(curl2.Get());
              remote->SetLabel(jellyfinServerInfo.ServerName + " (wan)");
              remote->SetLabelPreformated(true);
              //just set the default folder icon
              remote->FillInDefaultIcon();
              remote->m_bIsShareOrDrive = true;
              items.Add(remote);
            }
            rtn = true;
          }
        }
      }
    }
  }

  if (m_broadcastListener)
  {
    // before deleting listener, fetch and delete any sockets it uses.
    SOCKETS::CUDPSocket *socket = (SOCKETS::CUDPSocket*)m_broadcastListener->GetFirstSocket();
    // we should not have to do the close,
    // delete 'should' do it.
    socket->Close();
    SAFE_DELETE(socket);
    SAFE_DELETE(m_broadcastListener);
  }

  return rtn;
}
