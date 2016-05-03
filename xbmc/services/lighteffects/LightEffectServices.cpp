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

#include "LightEffectServices.h"

#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/Variant.h"

#include "LightEffectClient.h"

#include "boblight.h"

CLightEffectServices::CLightEffectServices()
: CThread("LightEffectServices")
, m_active(false)
, m_width(32)
, m_height(32)
{
}

CLightEffectServices::~CLightEffectServices()
{
  if (m_active)
    Stop();
}


CLightEffectServices& CLightEffectServices::GetInstance()
{
  static CLightEffectServices sLightEffectServices;
  return sLightEffectServices;
}

bool CLightEffectServices::Start()
{
  CSingleLock lock(m_critical);

  CThread::Create();
  return false;
}

void CLightEffectServices::Stop()
{
  CSingleLock lock(m_critical);

  StopThread();
}

bool CLightEffectServices::IsActive()
{
  return m_active;
}

bool CLightEffectServices::OnSettingChanging(const CSetting *setting)
{
  if (setting == NULL)
    return false;

  //const std::string &settingId = setting->GetId();

  return true;
}

void CLightEffectServices::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  //const std::string &settingId = setting->GetId();
}

bool CLightEffectServices::OnSettingUpdate(CSetting* &setting, const char *oldSettingId, const TiXmlNode *oldSettingNode)
{
  if (setting == NULL)
    return false;

  //const std::string &settingId = setting->GetId();

  return true;
}

void CLightEffectServices::Process()
{
  CRenderCapture *capture = g_renderManager.AllocRenderCapture();
  g_renderManager.Capture(capture, m_width, m_height, CAPTUREFLAG_CONTINUOUS);
  m_active = true;
  unsigned char *pixels;

  // below goes to settings
  // boblightd server IP address and port
  const char *IP = "192.168.1.4";
  int port = 19333;
  m_lighteffect = boblight_init();
  
  // Needs replacing with our library call
  boblight_connect(m_lighteffect, IP, port, 5000000);
  
  int row;
  int rgb[3];
  
  // below goes to settings, default values TBC
  // Needs replacing with our library call
  boblight_setoption(m_lighteffect,-1, "saturation    2.1");
  boblight_setoption(m_lighteffect,-1, "value    1.2");
  boblight_setoption(m_lighteffect,-1, "speed    70.0");
  boblight_setoption(m_lighteffect,-1, "autospeed    0.0");
  boblight_setoption(m_lighteffect,-1, "interpolation    0");
  boblight_setoption(m_lighteffect,-1, "threshold    10.0");
  
  while(!m_bStop)
  {
    capture->GetEvent().WaitMSec(1000);
    if (capture->GetUserState() == CAPTURESTATE_DONE)
    {
      //read out the pixels
      pixels = capture->GetPixels();
      // Needs replacing with our library call
      boblight_setscanrange(m_lighteffect, m_width, m_height);
      
      for (int y = 0; y < m_height;  y++)
      {
        row = m_width * y * 4;
        for (int x = 0; x < m_width; x++)
        {
          rgb[0] = pixels[row + x * 4 + 2];
          rgb[1] = pixels[row + x * 4 + 1];
          rgb[2] = pixels[row + x * 4];
          
          // Needs replacing with our library call
          boblight_addpixelxy(m_lighteffect, x, y, rgb);
        }
      }
      // Needs replacing with our library call
      boblight_setpriority(m_lighteffect, 128);
      boblight_sendrgb(m_lighteffect, 1, NULL);
    }
  }

  g_renderManager.ReleaseRenderCapture(capture);
  m_active = false;
}
