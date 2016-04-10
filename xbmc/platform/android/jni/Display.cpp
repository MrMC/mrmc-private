/*
 *      Copyright (C) 2014 Team Kodi
 *      http://xbmc.org
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "Display.h"

#include "jutils/jutils-details.hpp"

using namespace jni;

int CJNIDisplayMode::getPhysicalHeight()
{
  return call_method<jint>(m_object,
    "getPhysicalHeight", "()I");
}

int CJNIDisplayMode::getPhysicalWidth()
{
  return call_method<jint>(m_object,
    "getPhysicalWidth", "()I");
}

float CJNIDisplayMode::getRefreshRate()
{
  return call_method<jfloat>(m_object,
    "getRefreshRate", "()F");
}

/*************/

CJNIDisplay::CJNIDisplay()
 : CJNIBase("android.view.Display")
{
}

float CJNIDisplay::getRefreshRate()
{
  return call_method<jfloat>(m_object,
    "getRefreshRate", "()F");
}

std::vector<float> CJNIDisplay::getSupportedRefreshRates()
{
  if (GetSDKVersion() >= 21)
    return jcast<std::vector<float>>(
      call_method<jhfloatArray>(m_object, "getSupportedRefreshRates", "()[F"));
  else
    return std::vector<float>();
}

CJNIDisplayMode CJNIDisplay::getMode()
{
  if (GetSDKVersion() >= 23)
    return call_method<jhobject>(m_object,
                                 "getMode", "()Landroid/view/Display$Mode;");
  else
    return jhobject();
}

int CJNIDisplay::getWidth()
{
  return call_method<jint>(m_object,
    "getWidth", "()I");
}

int CJNIDisplay::getHeight()
{
  return call_method<jint>(m_object,
    "getHeight", "()I");
}

std::vector<CJNIDisplayMode> CJNIDisplay::getSupportedModes()
{
  if (GetSDKVersion() >= 23)
    return jcast<CJNIDisplayModes>(call_method<jhobjectArray>(m_object,
                                                              "getSupportedModes", "()[Landroid/view/Display$Mode;"));
  else
    return CJNIDisplayModes();
}

