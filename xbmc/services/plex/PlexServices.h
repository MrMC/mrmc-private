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
  bool HasClients() const { return !m_clients.empty(); }
  void GetClients(std::vector<CPlexClient> &clients) const {clients = m_clients; }

  // ISettingCallback
  virtual void OnSettingAction(const CSetting *setting) override;
  virtual void OnSettingChanged(const CSetting *setting) override;

  // IAnnouncer callbacks
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data) override;

private:
  // private construction, and no assignements; use the provided singleton methods
  CPlexServices();
  CPlexServices(const CPlexServices&);
  virtual ~CPlexServices();

  void          SetUserSettings();
  void          GetUserSettings();

  // IRunnable entry point for thread
  virtual void  Process() override;

  bool          FetchPlexToken();
  bool          FetchMyPlexServers();
  bool          FetchSignInPin();
  bool          WaitForSignInByPin();

  void          SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket);
  CPlexClient*  GetClient(std::string uuid);
  bool          AddClient(CPlexClient server);
  bool          GetMyHomeUsers(std::string &homeusername);

  std::atomic<bool> m_active;
  CCriticalSection  m_critical;

  std::string       m_myPlexUser;
  std::string       m_myPlexPass;
  std::string       m_myPlexToken;
  bool              m_useGDMServer;
  std::string       m_signInByPinId;
  std::string       m_signInByPinCode;
  std::string       m_myHomeUser;
  std::vector<CPlexClient> m_clients;
};
