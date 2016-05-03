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

#include "LightEffectClient.h"
#include "utils/StringUtils.h"
#include "boblight_client.h"

#include "tcpsocket.h"

CLightEffectClient::CLightEffectClient()
{
}

CLightEffectClient& CLightEffectClient::GetInstance()
{
  static CLightEffectClient sLightEffectClient;
  return sLightEffectClient;
}

bool CLightEffectClient::Connect(const char* ip, int port, int timeout)
{
//  CMessage message;
  CTcpData data;
  int64_t  now;
  int64_t  target;
  std::string   word;
  
  m_ip = ip;
  m_port = port;
  m_timeout = timeout;
  if (m_socket.Open(m_ip, m_port, m_timeout) != SUCCESS)
  {
    m_error = m_socket.GetError();
    return 0;
  }
  
  if (!WriteData("hello\n"))
    return 0;
  
  char test = ReadData();
  
  return true;
}

bool CLightEffectClient::WriteData(std::string data)
{
  
  
  CTcpData tcpData;
  tcpData.SetData(data);
  
  if (m_socket.Write(tcpData) != SUCCESS)
  {
    m_error = m_socket.GetError();
    return false;
  }
  
  return true;
}

char CLightEffectClient::ReadData()
{
  CTcpData tcpData;
  char cData;
  
  if (m_socket.Read(tcpData) != SUCCESS)
  {
    m_error = m_socket.GetError();
    return cData;
  }
  cData = *tcpData.GetData();
  return cData;
}

int CLightEffectClient::SetPrio(int prio)
{
  std::string data = StringUtils::Format("set priority %i\n", prio);
  
  return WriteData(data);
}
