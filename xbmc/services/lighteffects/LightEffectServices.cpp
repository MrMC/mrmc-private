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

#include "Application.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "guilib/LocalizeStrings.h"
#include "interfaces/AnnouncementManager.h"
#include "messaging/ApplicationMessenger.h"

#include "LightEffectClient.h"

using namespace ANNOUNCEMENT;
using namespace KODI::MESSAGING;

CLightEffectServices::CLightEffectServices()
: CThread("LightEffectServices")
, m_active(false)
, m_width(32)
, m_height(32)
, m_staticON(false)
, m_lightsON(true)
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

void CLightEffectServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  // this is not used on tvOS, needs testing on droid
  if (flag == GUI && !strcmp(sender, "xbmc") && !strcmp(message, "OnScreensaverDeactivated"))
  {
    if (!CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICSCREENSAVER))
    {
      m_staticON = false;
      SetStatic();
    }
  }
  else if (flag == GUI && !strcmp(sender, "xbmc") && !strcmp(message, "OnScreensaverActivated"))
  {
    if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICSCREENSAVER))
    {
      m_staticON = true;
      m_lighteffect->SetPriority(255);
    }
  }
}

bool CLightEffectServices::Start()
{
  CSingleLock lock(m_critical);
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE) && !m_active)
  {
    InitConnection();
    ApplyUserSettings();
    CThread::Create();
  }
  return false;
}

void CLightEffectServices::Stop()
{
  CSingleLock lock(m_critical);
  if (m_active)
    StopThread();
}

bool CLightEffectServices::IsActive()
{
  return m_active;
}

void CLightEffectServices::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE)
  {
    // start or stop the service
    if (static_cast<const CSettingBool*>(setting)->GetValue())
      Start();
    else
      Stop();
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSIP ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT)
  {
    //restart
    Stop();
    Start();
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION    ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED         ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE         ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED     ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD)
  {
    SetOption(settingId);
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON)
  {
    m_staticON = false;
    if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON)
      m_staticON = !static_cast<const CSettingBool*>(setting)->GetValue();
      
  }
  CSettings::GetInstance().Save();
}

void CLightEffectServices::Process()
{
  int row;
  int rgb[3];
  m_active = true;
  unsigned char *pixels;
  CRenderCapture *capture = g_renderManager.AllocRenderCapture();
  g_renderManager.Capture(capture, m_width, m_height, CAPTUREFLAG_CONTINUOUS);
  
  while(!m_bStop)
  {
    if (g_application.m_pPlayer->IsPlayingVideo())
    {
      // reset static bool for later
      m_staticON = false;
      m_lightsON = true;
      m_lighteffect->SetPriority(128);
      
      capture->GetEvent().WaitMSec(1000);
      if (capture->GetUserState() == CAPTURESTATE_DONE)
      {
        //read out the pixels
        pixels = capture->GetPixels();
        m_lighteffect->SetScanRange(m_width, m_height);
        
        for (int y = 0; y < m_height;  y++)
        {
          row = m_width * y * 4;
          for (int x = 0; x < m_width; x++)
          {
            rgb[0] = pixels[row + x * 4 + 2];
            rgb[1] = pixels[row + x * 4 + 1];
            rgb[2] = pixels[row + x * 4];
            
            m_lighteffect->AddPixel(rgb, x, y);
          }
        }
        m_lighteffect->SendRGB(true);
      }
    }
    else
    {
      // set static if its enabled
      if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON))
      {
        // only set static colour once, no point doing it over and over again
        if(!m_staticON)
        {
          m_staticON = true;
          m_lightsON = true;
          SetStatic();
        }
      }
      // or kill the lights
      else
      {
        if (m_lightsON)
        {
          m_lighteffect->SetPriority(255);
          m_lightsON = false;
        }
      }
    }
  }
  g_renderManager.ReleaseRenderCapture(capture);
  m_lighteffect->SetPriority(255);
  m_active = false;
}

void CLightEffectServices::InitConnection()
{
  m_staticON = false;
  m_lighteffect = new CLightEffectClient();
  
  // boblightd server IP address and port
  const char *IP = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_LIGHTEFFECTSIP).c_str();
  int port = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT);
    
  if (!m_lighteffect->Connect(IP, port, 5000000))
  {
    m_staticON = true;
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(882), g_localizeStrings.Get(883), 3000, true);
    const CSetting *mysqlSetting = CSettings::GetInstance().GetSetting(CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE);
    ((CSettingBool*)mysqlSetting)->SetValue(false);
  }
}

void CLightEffectServices::ApplyUserSettings()
{
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD);
}

void CLightEffectServices::SetOption(std::string setting)
{
  std::string value;
  std::string option;
  if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION)
  {
    option = "interpolation";
    value  = StringUtils::Format("%d", CSettings::GetInstance().GetBool(setting));
  }
  else
  {
    value  = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(setting));
    if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION)
      option = "saturation";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE)
      option = "value";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED)
      option = "speed";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED)
      option = "autospeed";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD)
      option = "threshold";
  }
    
  std::string data = StringUtils::Format("%s %s", option.c_str(), value.c_str());
  if (!m_lighteffect->SetOption(data.c_str()))
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - error: for option '%s' and value '%s'",
              option.c_str(),
              value.c_str());
  else
  {
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - option '%s' and value '%s' - Done!",
              option.c_str(),
              value.c_str());
    // this will refresh static colours once options are changed
    m_staticON = false;
  }
}

void CLightEffectServices::SetStatic()
{
  int rgb[3];
  rgb[0] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR);
  rgb[1] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG);
  rgb[2] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB);
  
  m_lighteffect->AddStaticPixels(rgb);
  m_lighteffect->SetPriority(128);
  m_lighteffect->SendRGB(true);
}