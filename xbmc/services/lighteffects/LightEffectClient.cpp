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
#include <sstream>
#include <cmath>

CLightEffectLED::CLightEffectLED()
{
  m_speed = 100.0f;
  m_threshold = 0;
  m_interpolation = false;

  m_value = 1.0f;
  m_valuerange[0] = 0.0f;
  m_valuerange[1] = 1.0f;
  m_saturation = 1.0f;
  m_saturationrange[0] = 0.0f;
  m_saturationrange[1] = 1.0f;

  m_rgb[0] = m_rgb[1] = m_rgb[2] = 0.0f;
  m_rgbcount = 0;

  m_width = -1;
  m_height = -1;
  m_hscan[0] = m_hscan[1] = -1.0f;
  m_vscan[0] = m_vscan[1] = -1.0f;
  m_hscanscaled[0] = m_hscanscaled[1] = 0;
  m_vscanscaled[0] = m_vscanscaled[1] = 0;
}

std::string CLightEffectLED::SetOption(const char* option, bool& send)
{
  send = false;

  std::string strname;
  std::string stroption = option;
  if (!CLightEffectClient::GetInstance().GetWord(stroption, strname))
    return "empty option";

  if (strname == "interpolation")
  {
    // eat the blank space
    StringUtils::Replace(stroption, " ", "");
    m_interpolation = (stroption == "1");
    send = true;
    return "";
  }
  else
  {
    float value;
    try {
      value = std::stod(stroption);
    }
    catch (...)
    {
      return "invalid value " + stroption + " for option " + strname + " with type float";
    }

    if (strname == "saturation")
    {
      m_saturation = value;
      m_saturation = fmax(m_saturation, 0.0);
    }
    else if (strname == "speed")
    {
      m_speed = value;
      m_speed = Clamp(m_speed, 0.0, 100.0);
      send = true;
    }
    else if (strname == "value")
    {
      m_value = value;
      m_value = fmax(m_value, 0.0);
    }
    else if (strname == "threshold")
    {
      m_threshold = value;
      m_threshold = Clamp(m_threshold, 0, 255);
    }
    return "";
  }
}

float CLightEffectLED::Clamp(float value, float min, float max)
{
  return std::min(std::max(value, min), max);
}

int CLightEffectLED::Clamp(int value, int min, int max)
{
  return std::min(std::max(value, min), max);
}

void CLightEffectLED::AddPixel(int* rgb)
{
  if (rgb[0] >= m_threshold || rgb[1] >= m_threshold || rgb[2] >= m_threshold)
  {
    m_rgb[0] += Clamp(rgb[0], 0, 255);
    m_rgb[1] += Clamp(rgb[1], 0, 255);
    m_rgb[2] += Clamp(rgb[2], 0, 255);
  }
  m_rgbcount++;
}

void CLightEffectLED::GetRGB(float *rgb)
{
  if (m_rgbcount == 0)
  {
    for (int i = 0; i < 3; ++i)
    {
      rgb[i] = 0.0f;
      m_rgb[i] = 0.0f;
    }
    
    return;
  }

  // 0 to 255 rgb convert to 0.0 to 1.0 rgb
  for (int i = 0; i < 3; ++i)
  {
    rgb[i] = Clamp(m_rgb[i] / (float)m_rgbcount / 255.0f, 0.0f, 1.0f);
    m_rgb[i] = 0.0f;
  }
  m_rgbcount = 0;

  // rgb -> hvs convert, apply value/saturation, hvs -> rgb convert.
  if (m_value != 1.0 || m_valuerange[0] != 0.0 || m_valuerange[1] != 1.0 ||
      m_saturation != 1.0  || m_saturationrange[0] != 0.0 || m_saturationrange[1] != 1.0)
  {
    float hsv[3];
    float max = std::max(std::max(rgb[0], rgb[1]), rgb[2]);
    float min = std::min(std::min(rgb[0], rgb[1]), rgb[2]);
    
    if (min == max)
    {
      hsv[0] = -1.0f;
      hsv[1] = 0.0;
      hsv[2] = min;
    }
    else
    {
      float span = max - min;
      if (max == rgb[0])
      {
        hsv[0] = 60.0f * ((rgb[1] - rgb[2]) / span) + 360.0f;
        while (hsv[0] >= 360.0f)
          hsv[0] -= 360.0f;
      }
      else if (max == rgb[1])
      {
        hsv[0] = 60.0f * ((rgb[2] - rgb[0]) / span) + 120.0f;
      }
      else if (max == rgb[2])
      {
        hsv[0] = 60.0f * ((rgb[0] - rgb[1]) / span) + 240.0f;
      }

      hsv[1] = span / max;
      hsv[2] = max;
    }

    hsv[1] = Clamp(hsv[1] * m_saturation, m_saturationrange[0],   m_saturationrange[1]);
    hsv[2] = Clamp(hsv[2] * m_value,      m_valuerange[0], m_valuerange[1]);

    if (hsv[0] == -1.0f)
    {
      for (int i = 0; i < 3; ++i)
        rgb[i] = hsv[2];
    }
    else
    {
      int hi = (int)(hsv[0] / 60.0f) % 6;
      float f = (hsv[0] / 60.0f) - (float)(int)(hsv[0] / 60.0f);

      float s = hsv[1];
      float v = hsv[2];
      float p = v * (1.0f - s);
      float q = v * (1.0f - f * s);
      float t = v * (1.0f - (1.0f - f) * s);

      switch(hi)
      {
        case 0:
          rgb[0] = v; rgb[1] = t; rgb[2] = p;
          break;
        case 1:
          rgb[0] = q; rgb[1] = v; rgb[2] = p;
          break;
        case 2:
          rgb[0] = p; rgb[1] = v; rgb[2] = t;
          break;
        case 3:
          rgb[0] = p; rgb[1] = q; rgb[2] = v;
          break;
        case 4:
          rgb[0] = t; rgb[1] = p; rgb[2] = v;
          break;
        case 5:
          rgb[0] = v; rgb[1] = p; rgb[2] = q;
          break;
      }
    }

    for (int i = 0; i < 3; ++i)
      rgb[i] = Clamp(rgb[i], 0.0f, 1.0f);
  }
}

void CLightEffectLED::SetScanRange(int width, int height)
{
  m_width = width;
  m_height = height;

  m_hscanscaled[0] = lround(m_hscan[0] / 100.0 * ((float)width  - 1));
  m_hscanscaled[1] = lround(m_hscan[1] / 100.0 * ((float)width  - 1));
  m_vscanscaled[0] = lround(m_vscan[0] / 100.0 * ((float)height - 1));
  m_vscanscaled[1] = lround(m_vscan[1] / 100.0 * ((float)height - 1));
}

CLightEffectClient::CLightEffectClient()
{
}

CLightEffectClient& CLightEffectClient::GetInstance()
{
  static CLightEffectClient sLightEffectClient;
  return sLightEffectClient;
}

bool CLightEffectClient::Connect(const char *ip, int port, int timeout)
{
  m_ip = ip;
  m_port = port;
  m_timeout = timeout;
  if (m_socket.Open(m_ip, m_port, 5000000) != CLightSocket::SUCCESS)
    return false;

  const char hello[] = "hello\n";
  if (m_socket.Write(hello, strlen(hello)) != CLightSocket::SUCCESS)
    return false;

  if (ReadData() != hello)
    return false;

  const char get_version[] = "get version\n";
  if (m_socket.Write(get_version, strlen(get_version)) != CLightSocket::SUCCESS)
    return false;

  if (ReadData() != "version 5\n")
    return false;

  const char get_lights[] = "get lights\n";
  if (m_socket.Write(get_lights, strlen(get_lights)) != CLightSocket::SUCCESS)
    return false;

  std::string word = ReadData();
  if (!ParseLights(word))
    return false;

  return true;
}

std::string CLightEffectClient::ReadData()
{
  std::string data;
  if (m_socket.Read(data) == CLightSocket::SUCCESS)
    return data;

  return "error";
}

bool CLightEffectClient::ParseLights(std::string &message)
{
  std::string word;
  if (!ParseWord(message, "lights") || !GetWord(message, word))
    return false;
  
  int nrlights = std::atol(word.c_str());
  if (nrlights < 1)
    return false;

  for (int i = 0; i < nrlights; ++i)
  {
    CLightEffectLED light;
    if (!ParseWord(message, "light") || !GetWord(message, light.m_name))
      return false;

    if (!ParseWord(message, "scan"))
      return false;

    std::string scanarea;
    for (int i = 0; i < 4; i++)
    {
      if (!GetWord(message, word))
        return false;
      
      scanarea += word + " ";
    }

    Locale(scanarea);

    if (sscanf(scanarea.c_str(), "%f %f %f %f", light.m_vscan, light.m_vscan + 1, light.m_hscan, light.m_hscan + 1) != 4)
      return false;
    
    m_lights.push_back(light);
  }    
  return true;
}

bool CLightEffectClient::ParseWord(std::string &message, std::string wordtocmp)
{
  std::string readword;
  if (!GetWord(message, readword) || readword != wordtocmp)
    return false;

  return true;
}

bool CLightEffectClient::GetWord(std::string &data, std::string &word)
{
  std::stringstream datastream(data);
  std::string end;
  
  datastream >> word;
  if (datastream.fail())
  {
    data.clear();
    return false;
  }
  
  size_t pos = data.find(word) + word.length();
  if (pos >= data.length())
  {
    data.clear();
    return true;
  }
  
  data = data.substr(pos);
  
  datastream.clear();
  datastream.str(data);
  
  datastream >> end;
  if (datastream.fail())
    data.clear();
  
  return true;
}

void CLightEffectClient::Locale(std::string &strfloat)
{
  static struct lconv* locale = localeconv();

  size_t pos = strfloat.find_first_of(",.");

  while (pos != std::string::npos)
  {
    strfloat.replace(pos, 1, 1, *locale->decimal_point);
    pos++;
    
    if (pos >= strfloat.size())
      break;
    
    pos = strfloat.find_first_of(",.", pos);
  }
}

int CLightEffectClient::SetPriority(int prio)
{
  std::string data = StringUtils::Format("set priority %i\n", prio);
  return m_socket.Write(data.c_str(), data.length());
}

void CLightEffectClient::SetScanRange(int width, int height)
{
  for (size_t i = 0; i < m_lights.size(); ++i)
    m_lights[i].SetScanRange(width, height);
}

void CLightEffectClient::SetAllLights(int *rgb)
{
  for (size_t i = 0; i < m_lights.size(); ++i)
    m_lights[i].AddPixel(rgb);
}

void CLightEffectClient::AddPixel(int *rgb, int x, int y)
{
  for (size_t i = 0; i < m_lights.size(); ++i)
  {
    if (x >= m_lights[i].m_hscanscaled[0] && x <= m_lights[i].m_hscanscaled[1] &&
        y >= m_lights[i].m_vscanscaled[0] && y <= m_lights[i].m_vscanscaled[1])
    {
      m_lights[i].AddPixel(rgb);
    }
  }
}

int CLightEffectClient::SendRGB(bool sync)
{
  std::string data;

  for (size_t i = 0; i < m_lights.size(); ++i)
  {
    // convert from 0 to 255 range rgb -> 0.0 to 1.0 range rgb
    float rgb[3];
    m_lights[i].GetRGB(rgb);
    data += StringUtils::Format("set light %s rgb %f %f %f\n",m_lights[i].m_name.c_str(),rgb[0],rgb[1],rgb[2]);
  }

  if (sync)
    data += "sync\n";

  if (m_socket.Write(data.c_str(), data.length()) != CLightSocket::SUCCESS)
    return 0;

  return 1;
}

int CLightEffectClient::SetOption(const char *option)
{
  std::string data;
  for (size_t i = 0; i < m_lights.size(); ++i)
  {
    bool send;
    std::string error = m_lights[i].SetOption(option, send);
    if (!error.empty())
      return 0;

    if (send)
      data += StringUtils::Format("set light %s %s\n",m_lights[i].m_name.c_str(),option);
  }

  if (m_socket.Write(data.c_str(), data.length()) != CLightSocket::SUCCESS)
    return 0;

  return 1;
}
