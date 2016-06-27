#pragma once
/*
 *      Copyright (C) 2016 Team MrMC
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

#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "settings/lib/ISettingCallback.h"
#include "interfaces/IAnnouncer.h"
#include "PlexClient.h"

namespace SOCKETS
{
  class CUDPSocket;
}
class CPlexClient;

class CPlexServices
: public CThread
, public ISettingCallback
, public ANNOUNCEMENT::IAnnouncer
{
public:
  static CPlexServices &GetInstance();

  void Start();
  void Stop();
  bool IsActive();
  void GetServers(std::vector<CPlexClient> &servers) const {servers = m_clients; }
  // ISetting callbacks
  virtual void OnSettingChanged(const CSetting *setting) override;

  // IAnnouncer callbacks
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)override;

private:
  // private construction, and no assignements; use the provided singleton methods
  CPlexServices();
  CPlexServices(const CPlexServices&);
  virtual ~CPlexServices();

  // IRunnable entry point for thread
  virtual void  Process() override;

  bool          InitConnection();
  void          ApplyUserSettings();

  bool          FetchPlexToken();
  bool          FetchMyPlexServers();
  void          SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket);
  CPlexClient*  GetClient(std::string uuid);
  bool          AddClient(CPlexClient server);

  std::atomic<bool> m_active;
  CCriticalSection  m_critical;

  std::string       m_client_uuid;

  bool              m_myPlexEnabled;
  std::string       m_myPlexUser;
  std::string       m_myPlexPass;
  std::string       m_myPlexToken;
  bool              m_autoGDM;
  std::string       m_localHost;
  std::string       m_localPort;
  std::vector<CPlexClient> m_clients;
};
