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

#include <string>
#include <vector>

#include "tcpsocket.h"


class CLightEffectClient
{
public:
  CLightEffectClient();
  static CLightEffectClient &GetInstance();
  bool         Connect(const char* ip, int port, int timeout);
  bool         WriteData(std::string data);
  char         ReadData();
  int          SetPrio(int prio);
  
private:
  CTcpClientSocket m_socket;
  std::string      m_ip;
  int              m_port;
  std::string      m_error;
  int              m_timeout;
};