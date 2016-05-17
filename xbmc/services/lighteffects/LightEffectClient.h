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
#include "LightSocket.h"

class CLightEffectLED
{
public:
  CLightEffectLED();
  
  int         Clamp(int value, int min, int max);
  float       Clamp(float value, float min, float max);
  
  std::string SetOption(const char *option, bool &send);
  std::string GetOption(const char *option, std::string &output);
  
  void        SetScanRange(int width, int height);
  void        AddPixel(int *rgb);
  void        GetRGB(float *rgb);

  std::string m_name;

  bool        m_use;
  float       m_speed;
  float       m_autospeed;
  float       m_singlechange;
  bool        m_interpolation;
  
  float       m_value;
  float       m_valuerange[2];
  float       m_saturation;
  float       m_satrange[2];
  int         m_threshold;
  
  int         m_rgbcount;
  float       m_rgb[3];
  float       m_prevrgb[3];
  
  int         m_width;
  int         m_height;
  float       m_hscan[2];
  float       m_vscan[2];
  int         m_hscanscaled[2];
  int         m_vscanscaled[2];

  float       m_gamma;
  float       m_gammacurve[256];
};

class CLightEffectClient
{
public:
  CLightEffectClient();
  static CLightEffectClient &GetInstance();
  bool         Connect(const char *ip, int port, int timeout);
  std::string  ReadData();
  int          SetPriority(int prio);
  void         SetScanRange(int width, int height);
  int          AddStaticPixels(int *rgb);
  void         AddPixel(int *rgb, int x, int y);
  int          SendRGB(bool sync);
  int          SetOption(const char *option);
  bool         ParseLights(std::string &message);
  bool         ParseWord(std::string &message, std::string wordtocmp);
  bool         GetWord(std::string &data, std::string &word);
  void         Locale(std::string &strfloat);
  
private:
  CLightSocket     m_socket;
  std::string      m_ip;
  int              m_port;
  std::string      m_error;
  int              m_timeout;
  std::vector <CLightEffectLED> m_lights;
};
