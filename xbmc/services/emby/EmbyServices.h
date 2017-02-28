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

#include <atomic>
#include <memory>

#include "EmbyClient.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "settings/lib/ISettingCallback.h"
#include "interfaces/IAnnouncer.h"
#include "utils/JobManager.h"

namespace SOCKETS
{
  class CUDPSocket;
  class CSocketListener;
}

enum class EmbyServicePlayerState
{
  paused = 1,
  playing = 2,
  stopped = 3,
};

class CEmbyClient;
typedef std::shared_ptr<CEmbyClient> CEmbyClientPtr;

class CEmbyServices
: public CThread
, public CJobQueue
, public ISettingCallback
, public ANNOUNCEMENT::IAnnouncer
{
  friend class CEmbyServiceJob;

public:
  static CEmbyServices &GetInstance();

  void Start();
  void Stop();
  bool IsActive();
  bool IsEnabled();
  bool HasClients() const;
  void GetClients(std::vector<CEmbyClientPtr> &clients) const;
  CEmbyClientPtr FindClient(const std::string &path);
  bool ClientIsLocal(std::string path);

  // ISettingCallback
  virtual void OnSettingAction(const CSetting *setting) override;
  virtual void OnSettingChanged(const CSetting *setting) override;

  // IAnnouncer callbacks
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data) override;

protected:
  void              UpdateLibraries(bool forced);

private:
  // private construction, and no assignements; use the provided singleton methods
  CEmbyServices();
  CEmbyServices(const CEmbyServices&);
  virtual ~CEmbyServices();

  void              SetUserSettings();
  void              GetUserSettings();
  bool              EmbySignedIn();

  // IRunnable entry point for thread
  virtual void      Process() override;

  bool              AuthenticateByName(std::string user, std::string pass);
  bool              GetEmbyServers(bool includeHttps);
  bool              PostSignInPinCode();
  bool              GetSignInByPinReply();
  void              FindEmbyServersByBroadcast();

  CEmbyClientPtr    GetClient(std::string uuid);
  bool              AddClient(CEmbyClientPtr foundClient);
  bool              RemoveClient(CEmbyClientPtr lostClient);
  bool              UpdateClient(CEmbyClientPtr updateClient);

  std::atomic<bool> m_active;
  CCriticalSection  m_critical;
  CEvent            m_processSleep;

  std::string       m_authToken;
  std::string       m_signInByPinId;
  std::string       m_signInByPinCode;
  bool              m_broadcast;
  SOCKETS::CSocketListener *m_broadcastListener;
  std::string       m_myHomeUser;
  XFILE::CCurlFile  m_emby;

  EmbyServicePlayerState m_playState;
  CCriticalSection  m_criticalClients;
  std::atomic<bool> m_hasClients;
  std::vector<CEmbyClientPtr> m_clients;
};
