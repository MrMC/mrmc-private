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

/* This is needed for our implementation, more is better
boblight_init();
boblight_destroy(void* vpboblight)
boblight_connect(m_lighteffect, IP, port, 5000000)
boblight_setoption(m_lighteffect,-1, data.c_str())
boblight_addpixel(m_lighteffect, -1, rgb);
boblight_addpixelxy(m_lighteffect, x, y, rgb);
boblight_sendrgb(m_lighteffect, 1, NULL);
boblight_geterror(m_lighteffect)
boblight_setpriority(m_lighteffect, 255);
boblight_setscanrange(m_lighteffect, m_width, m_height);
 */

#define GAMMASIZE (sizeof(m_gammacurve) / sizeof(m_gammacurve[0]))

CLightEffectLED::CLightEffectLED()
{
  // defult values below
  m_speed = 100.0f;
  m_autospeed = 0.0f;
  m_interpolation = false;
  m_use = true;
  m_value = 1.0f;
  m_valuerange[0] = 0.0f;
  m_valuerange[1] = 1.0f;
  m_saturation = 1.0f;
  m_satrange[0] = 0.0f;
  m_satrange[1] = 1.0f;
  m_threshold = 0;
  m_gamma = 1.0f;
  m_hscan[0] = -1.0f;
  m_hscan[1] = -1.0f;
  m_vscan[0] = -1.0f;
  m_vscan[1] = -1.0f;
  // end default values
  
  m_singlechange = 0.0;
  
  m_width = -1;
  m_height = -1;
  
  memset(m_rgb, 0, sizeof(m_rgb));
  m_rgbcount = 0;
  memset(m_prevrgb, 0, sizeof(m_prevrgb));
  memset(m_hscanscaled, 0, sizeof(m_hscanscaled));
  memset(m_vscanscaled, 0, sizeof(m_vscanscaled));
  
  for (size_t i = 0; i < GAMMASIZE; i++)
    m_gammacurve[i] = i;
}

std::string CLightEffectLED::SetOption(const char* option, bool& send)
{
  std::string stroption = option;
  std::string strname;
  
  send = false;
    
  if (!CLightEffectClient::GetInstance().GetWord(stroption, strname))
    return "emtpy option"; //string with only whitespace
  
  if (strname == "interpolation")
  {
    // loose the blank space
    StringUtils::Replace(stroption, " ", "");
    m_interpolation = (stroption == "1");
    send = true;
    return "";
  }
  else
  {
    float value;
    std::stringstream stream;
    stream << stroption;
    stream >> value;
    if (stream.fail())
      return "invalid value " + stroption + " for option " + strname + " with type float";
    
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
    else if (strname == "autospeed")
    {
      m_autospeed = value;
      m_autospeed = fmax(m_autospeed, 0.0);
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
  return value < max ? (value > min ? value : min) : max;
}

int CLightEffectLED::Clamp(int value, int min, int max)
{
  return value < max ? (value > min ? value : min) : max;
}

void CLightEffectLED::AddPixel(int* rgb)
{
  if (rgb[0] >= m_threshold || rgb[1] >= m_threshold || rgb[2] >= m_threshold)
  {
    if (m_gamma == 1.0)
    {
      m_rgb[0] += Clamp(rgb[0], 0, 255);
      m_rgb[1] += Clamp(rgb[1], 0, 255);
      m_rgb[2] += Clamp(rgb[2], 0, 255);
    }
    else
    {
      m_rgb[0] += m_gammacurve[Clamp(rgb[0], 0, (int)GAMMASIZE - 1)];
      m_rgb[1] += m_gammacurve[Clamp(rgb[1], 0, (int)GAMMASIZE - 1)];
      m_rgb[2] += m_gammacurve[Clamp(rgb[2], 0, (int)GAMMASIZE - 1)];
    }
  }
  m_rgbcount++;
}

void CLightEffectLED::GetRGB(float* rgb)
{
  //if no pixels are set, the denominator is 0, so just return black
  if (m_rgbcount == 0)
  {
    for (int i = 0; i < 3; i++)
    {
      rgb[i] = 0.0f;
      m_rgb[i] = 0.0f;
    }
    
    return;
  }
  
  //convert from numerator/denominator to float
  for (int i = 0; i < 3; i++)
  {
    rgb[i] = Clamp(m_rgb[i] / (float)m_rgbcount / 255.0f, 0.0f, 1.0f);
    m_rgb[i] = 0.0f;
  }
  m_rgbcount = 0;
  
  //this tries to set the speed based on how fast the input is changing
  //it needs sync mode to work properly
  if (m_autospeed > 0.0)
  {
    float change = std::abs(rgb[0] - m_prevrgb[0]) + std::abs(rgb[1] - m_prevrgb[1]) + std::abs(rgb[2] - m_prevrgb[2]);
    change /= 3.0;
    
    //only apply singlechange if it's large enough, otherwise we risk sending it continously
    if (change > 0.001)
      m_singlechange = Clamp(change * m_autospeed / 10.0f, 0.0f, 1.0f);
    else
      m_singlechange = 0.0;
  }
  
  memcpy(m_prevrgb, rgb, sizeof(m_prevrgb));
  
  //we need some hsv adjustments
  if (m_value != 1.0 || m_valuerange[0] != 0.0 || m_valuerange[1] != 1.0 ||
      m_saturation != 1.0  || m_satrange[0] != 0.0 || m_satrange[1] != 1.0)
  {
    //rgb - hsv conversion, thanks wikipedia!
    float hsv[3];
    float max = std::max(std::max(rgb[0], rgb[1]), rgb[2]);
    float min = std::min(std::min(rgb[0], rgb[1]), rgb[2]);
    
    if (min == max) //grayscale
    {
      hsv[0] = -1.0f; //undefined
      hsv[1] = 0.0; //no saturation
      hsv[2] = min; //value
    }
    else
    {
      if (max == rgb[0]) //red zone
      {
        hsv[0] = (60.0f * ((rgb[1] - rgb[2]) / (max - min)) + 360.0f);
        while (hsv[0] >= 360.0f)
          hsv[0] -= 360.0f;
      }
      else if (max == rgb[1]) //green zone
      {
        hsv[0] = 60.0f * ((rgb[2] - rgb[0]) / (max - min)) + 120.0f;
      }
      else if (max == rgb[2]) //blue zone
      {
        hsv[0] = 60.0f * ((rgb[0] - rgb[1]) / (max - min)) + 240.0f;
      }
      
      hsv[1] = (max - min) / max; //saturation
      hsv[2] = max; //value
    }
    
    //saturation and value adjustment
    hsv[1] = Clamp(hsv[1] * m_saturation, m_satrange[0],   m_satrange[1]);
    hsv[2] = Clamp(hsv[2] * m_value,      m_valuerange[0], m_valuerange[1]);
    
    if (hsv[0] == -1.0f) //grayscale
    {
      for (int i = 0; i < 3; i++)
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
      
      if (hi == 0)
      { rgb[0] = v; rgb[1] = t; rgb[2] = p; }
      else if (hi == 1)
      { rgb[0] = q; rgb[1] = v; rgb[2] = p; }
      else if (hi == 2)
      { rgb[0] = p; rgb[1] = v; rgb[2] = t; }
      else if (hi == 3)
      { rgb[0] = p; rgb[1] = q; rgb[2] = v; }
      else if (hi == 4)
      { rgb[0] = t; rgb[1] = p; rgb[2] = v; }
      else if (hi == 5)
      { rgb[0] = v; rgb[1] = p; rgb[2] = q; }
    }
    
    for (int i = 0; i < 3; i++)
      rgb[i] = Clamp(rgb[i], 0.0f, 1.0f);
  }
}

//scale the light's scanrange to the dimensions set with boblight_setscanrange()
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

bool CLightEffectClient::Connect(const char* ip, int port, int timeout)
{
  std::string word;
  
  m_ip = ip;
  m_port = port;
  m_timeout = timeout;
  if (m_socket.Open(m_ip, m_port,5000000) != SUCCESS)
  {
    return 0;
  }
  
  if (!WriteData("hello\n"))
    return false;
  
  if (ReadData() != "hello\n")
    return false;
  
  if (!WriteData("get version\n"))
    return false;
  
  if (ReadData() != "version 5\n")
    return false;
  
  if (!WriteData("get lights\n"))
    return false;
  
  word = ReadData();
  if (!ParseLights(word))
  {
    return false;
  }
  
  return true;
}

bool CLightEffectClient::WriteData(std::string data)
{
  CTcpData writedata;
  writedata.SetData(data);
  if (m_socket.Write(writedata) != SUCCESS)
  {
    return false;
  }
  return true;
}

std::string CLightEffectClient::ReadData()
{
  CTcpData data;
  if (!m_socket.Read(data))
  {
    return NULL;
  }
  std::string retdata;
  retdata += data.GetData();
  return retdata;
}

bool CLightEffectClient::ParseLights(std::string& message)
{
  std::string word;
  int nrlights;
  
  //first word in the message is "lights", second word is the number of lights
  if (!ParseWord(message, "lights") || !GetWord(message, word) || !StrToInt(word, nrlights) || nrlights < 1)
    return false;
  
  for (int i = 0; i < nrlights; i++)
  {
    CLightEffectLED light;
    
    //first word sent is "light, second one is the name
    if (!ParseWord(message, "light") || !GetWord(message, light.m_name))
    {
      return false;
    }
    
    //third one is "scan"
    if (!ParseWord(message, "scan"))
      return false;
    
    //now we read the scanrange
    std::string scanarea;
    for (int i = 0; i < 4; i++)
    {
      if (!GetWord(message, word))
        return false;
      
      scanarea += word + " ";
    }
    
    ConvertFloatLocale(scanarea); //workaround for locale mismatch (, and .)
    
    if (sscanf(scanarea.c_str(), "%f %f %f %f", light.m_vscan, light.m_vscan + 1, light.m_hscan, light.m_hscan + 1) != 4)
      return false;
    
    m_lights.push_back(light);
  }    
  return true;
}

//removes one word from the string in the messages, and compares it to wordtocmp
bool CLightEffectClient::ParseWord(std::string& message, std::string wordtocmp)
{
  std::string readword;
  if (!GetWord(message, readword) || readword != wordtocmp)
    return false;
  
  return true;
}

bool CLightEffectClient::GetWord(std::string& data, std::string& word)
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

bool CLightEffectClient::StrToInt(const std::string& data, int& value)
{
  return sscanf(data.c_str(), "%i", &value) == 1;
}

//convert . or , to the current locale for correct conversion of ascii float
void CLightEffectClient::ConvertFloatLocale(std::string& strfloat)
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
  
  return WriteData(data);
}

void CLightEffectClient::SetScanRange(int width, int height)
{
  for (size_t i = 0; i < m_lights.size(); i++)
  {
    m_lights[i].SetScanRange(width, height);
  }
}

int CLightEffectClient::AddStaticPixels(int* rgb)
{

  for (size_t i = 0; i < m_lights.size(); i++)
    m_lights[i].AddPixel(rgb);

  return 1;
}
void CLightEffectClient::AddPixel(int* rgb, int x, int y)
{
  for (size_t i = 0; i < m_lights.size(); i++)
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
  
  for (size_t i = 0; i < m_lights.size(); i++)
  {
    float rgb[3];
    m_lights[i].GetRGB(rgb);
    data += StringUtils::Format("set light %s rgb %f %f %f\n",m_lights[i].m_name.c_str(),rgb[0],rgb[1],rgb[2]);
    if (m_lights[i].m_autospeed > 0.0 && m_lights[i].m_singlechange > 0.0)
      data += StringUtils::Format("set light %s singlechange %f\n",m_lights[i].m_name.c_str(),m_lights[i].m_singlechange);
  }
  
  //send a message that we want devices to sync to our input
  if (sync)
    data += "sync\n";
  
  if (!WriteData(data))
    return 0;
  
  return 1;
}
int CLightEffectClient::SetOption(const char* option)
{
  std::string error;
  std::string data;
  bool   send;
  
  for (size_t i = 0; i < m_lights.size(); i++)
  {
    error = m_lights[i].SetOption(option, send);
    if (!error.empty())
    {
      return 0;
    }
    if (send)
    {
      data += StringUtils::Format("set light %s %s\n",m_lights[i].m_name.c_str(),option);
    }
  }

  if (!WriteData(data))
    return 0;
  
  return 1;
  
}
