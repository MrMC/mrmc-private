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

#include "boblight.h"

using namespace ANNOUNCEMENT;
using namespace KODI::MESSAGING;

CLightEffectServices::CLightEffectServices()
: CThread("LightEffectServices")
, m_active(false)
, m_width(32)
, m_height(32)
, m_staticON(false)
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
  // need to figure out how to receive the anouncement from tvOS that screensaver has kicked in
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
      // Needs replacing with our library call
      boblight_setpriority(m_lighteffect, 255);
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
  /*
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE = "services.lighteffects";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSIP = "services.lighteffectsip";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT = "services.lighteffectsport";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION = "services.lighteffectssaturation";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE = "services.lighteffectsvalue";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED = "services.lighteffectsspeed";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED = "services.lighteffectsautospeed";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSITERPOLATION = "services.lighteffectsinterpolation";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD = "services.lighteffectsthreshold";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON = "services.lighteffectsstaticon";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR = "services.lighteffectsstaticr";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG = "services.lighteffectsstaticg";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB = "services.lighteffectsstaticb";
   const std::string CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICSCREENSAVER = "services.lighteffectsstaticscreensaver";
  */
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
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSITERPOLATION ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD)
  {
    ApplyUserSettings();
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

bool CLightEffectServices::OnSettingUpdate(CSetting* &setting, const char *oldSettingId, const TiXmlNode *oldSettingNode)
{
  if (setting == NULL)
    return false;

  //const std::string &settingId = setting->GetId();

  return true;
}

void CLightEffectServices::Process()
{
  m_active = true;
  unsigned char *pixels;
  CRenderCapture *capture = g_renderManager.AllocRenderCapture();
  g_renderManager.Capture(capture, m_width, m_height, CAPTUREFLAG_CONTINUOUS);
  
  int row;
  int rgb[3];
  
  while(!m_bStop)
  {
    if (g_application.m_pPlayer->IsPlayingVideo())
    {
      // reset static bool for later
      m_staticON = false;
      
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
    else
    {
      // set static if its enabled
      if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON))
      {
        // only set static colour once, no point doing it over and over again
        if(!m_staticON)
        {
          m_staticON = true;
          SetStatic();
        }
      }
      // or kill the lights
      else
      {
        boblight_setpriority(m_lighteffect, 255);
      }
    }
  }
  g_renderManager.ReleaseRenderCapture(capture);
  boblight_setpriority(m_lighteffect, 255);
  m_active = false;
}

void CLightEffectServices::InitConnection()
{
  m_staticON = false;
  // Needs replacing with our library call
  m_lighteffect = boblight_init();
  
  // boblightd server IP address and port
  const char *IP = CSettings::GetInstance().GetString(CSettings::SETTING_SERVICES_LIGHTEFFECTSIP).c_str();
  int port = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT);
    
  // Needs replacing with our library call
  if (!boblight_connect(m_lighteffect, IP, port, 5000000))
  {
    m_staticON = true;
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(882), g_localizeStrings.Get(883), 3000, true);
    const CSetting *mysqlSetting = CSettings::GetInstance().GetSetting(CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE);
    ((CSettingBool*)mysqlSetting)->SetValue(false);
  }
  
}

void CLightEffectServices::ApplyUserSettings()
{
  
  std::string saturation = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION));
   std::string value = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE));
  std::string speed = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED));
  std::string autospeed = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(CSettings::SETTING_SERVICES_LIGHTEFFECTSAUTOSPEED));
  std::string interpolation = StringUtils::Format("%d", CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSITERPOLATION));
  std::string threshold = StringUtils::Format("%.1f", CSettings::GetInstance().GetNumber(CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD));
  
  SetOption("saturation",   saturation);
  SetOption("value",        value);
  SetOption("speed",        speed);
  SetOption("autospeed",    autospeed);
  SetOption("interpolation",interpolation);
  SetOption("threshold",    threshold);
}

void CLightEffectServices::SetOption(std::string option, std::string value)
{
  std::string data = StringUtils::Format("%s %s", option.c_str(), value.c_str());
       // Needs replacing with our library call
  if (!boblight_setoption(m_lighteffect,-1, data.c_str()))
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - error: %s for option '%s' and value '%s'",
              boblight_geterror(m_lighteffect),
              option.c_str(),
              value.c_str());
  else
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - option '%s' and value '%s' - Done!",
              option.c_str(),
              value.c_str());
    
}

void CLightEffectServices::SetStatic()
{
  int rgb[3];
  
  rgb[0] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR);
  rgb[1] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG);
  rgb[2] = CSettings::GetInstance().GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB);
  
  // Needs replacing with our library call
  boblight_addpixel(m_lighteffect, -1, rgb);
  boblight_setpriority(m_lighteffect, 128);
  boblight_sendrgb(m_lighteffect, 1, NULL);
}