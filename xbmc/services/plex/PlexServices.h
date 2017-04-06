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

#include "PlexClient.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "services/ServicesManager.h"
#include "settings/lib/ISettingCallback.h"
#include "interfaces/IAnnouncer.h"
#include "utils/JobManager.h"

namespace SOCKETS
{
  class CUDPSocket;
  class CSocketListener;
}

class CPlexClient;
typedef std::shared_ptr<CPlexClient> CPlexClientPtr;

class CPlexServices
: public CThread
, public CJobQueue
, public ISettingCallback
, public ANNOUNCEMENT::IAnnouncer
{
  friend class CPlexServiceJob;

public:
  static CPlexServices &GetInstance();

  void Start();
  void Stop();
  bool IsActive();
  bool IsEnabled();
  bool HasClients() const;
  void GetClients(std::vector<CPlexClientPtr> &clients) const;
  CPlexClientPtr FindClient(const std::string &path);
  bool ClientIsLocal(std::string path);
  MediaServicesPlayerState GetPlayState() { return m_playState; };

  // ISettingCallback
  virtual void OnSettingAction(const CSetting *setting) override;
  virtual void OnSettingChanged(const CSetting *setting) override;

  // IAnnouncer callbacks
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data) override;

protected:
  void              UpdateLibraries(bool forced);

private:
  // private construction, and no assignements; use the provided singleton methods
  CPlexServices();
  CPlexServices(const CPlexServices&);
  virtual ~CPlexServices();

  void              SetUserSettings();
  void              GetUserSettings();
  bool              MyPlexSignedIn();

  // IRunnable entry point for thread
  virtual void      Process() override;

  bool              GetPlexToken(std::string user, std::string pass);
  bool              GetMyPlexServers(bool includeHttps);
  bool              GetSignInPinCode();
  bool              GetSignInByPinReply();

  void              CheckForGDMServers();

  PlexServerInfo    ParsePlexDeviceNode(const TiXmlElement* DeviceNode);

  CPlexClientPtr    GetClient(std::string uuid);
  bool              AddClient(CPlexClientPtr foundClient);
  bool              RemoveClient(CPlexClientPtr lostClient);
  bool              GetMyHomeUsers(std::string &homeusername);

  std::atomic<bool> m_active;
  CCriticalSection  m_critical;
  CEvent            m_processSleep;

  std::string       m_authToken;
  bool              m_useGDMServer;
  SOCKETS::CSocketListener *m_gdmListener;
  std::string       m_signInByPinId;
  std::string       m_signInByPinCode;
  std::string       m_myHomeUser;
  int               m_updateMins;
  XFILE::CCurlFile  m_plextv;

  MediaServicesPlayerState m_playState;
  CCriticalSection  m_criticalClients;
  std::atomic<bool> m_hasClients;
  std::vector<CPlexClientPtr> m_clients;
};
