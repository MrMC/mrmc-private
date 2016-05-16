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

#ifndef TCP
#define TCP

#include <string>
#include <netinet/in.h>
#include <vector>

#define FAIL    0
#define SUCCESS 1
#define TIMEOUT 2

class CSocketData
{
  public:
    void SetData(uint8_t* data, int size, bool append = false);
    void SetData(std::string data, bool append = false);

    int   GetSize() { return m_data.size() - 1; }
    char* GetData() { return &m_data[0]; }
                                                                                          
    void Clear();

  private:
    std::vector<char> m_data;
    void CopyData(char* data, int size, bool append);
};

class CLightSocket //base class
{
  public:
    CLightSocket();
    ~CLightSocket();

    virtual int Open(std::string address, int port, int usectimeout = -1);
    void Close();
    bool IsOpen() { return m_sock != -1; }

    std::string GetAddress() { return m_address; }
    int         GetPort()    { return m_port; }
    int         GetSock()    { return m_sock; }

    void        SetTimeout(int usectimeout) { m_usectimeout = usectimeout; }
    
  protected:
    std::string m_address;

    int     m_sock;
    int     m_usectimeout;
    int     m_port;

    int SetNonBlock(bool nonblock = true);
    int SetSockOptions();
    int SetKeepalive();
    int WaitForSocket(bool write, std::string timeoutstr);
};

class CLightClientSocket : public CLightSocket
{
  public:
    int Open(std::string address, int port, int usectimeout = -1);
    int Read(CSocketData& data);
    int Write(CSocketData& data);
    int SetInfo(std::string address, int port, int sock);
};
#endif //TCP
