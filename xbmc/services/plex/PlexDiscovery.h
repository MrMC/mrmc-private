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

#include <map>
#include <vector>

#include "threads/Thread.h"
#include "threads/CriticalSection.h"

namespace SOCKETS
{
  class CUDPSocket;
}
class PlexServer;

class CPlexDiscovery
: public CThread
{
public:
  CPlexDiscovery();
  virtual ~CPlexDiscovery();

  void Start();
  void Stop();
  bool IsActive();

private:
  // IRunnable entry point for thread
  virtual void  Process() override;

  void          SendDiscoverBroadcast(SOCKETS::CUDPSocket *socket);
  PlexServer*   GetServer(std::string uuid);
  bool          AddServer(PlexServer server);

  std::atomic<bool> m_active;
  CCriticalSection  m_critical;
  std::vector<PlexServer> m_vServers;
};
