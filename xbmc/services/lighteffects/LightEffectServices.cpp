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


CLightEffectServices::CLightEffectServices()
: CThread("LightEffectServices")
, m_active(false)
, m_width(0)
, m_height(0)
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

  //CThread::Create();
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
  g_renderManager.Capture(capture, m_width, m_height, 0);
  m_active = true;

  while(!m_bStop)
  {
    capture->GetEvent().Wait();
    if (capture->GetUserState() == CAPTURESTATE_DONE)
    {
      //do something with m_capture->GetPixels();
    }
  }

  g_renderManager.ReleaseRenderCapture(capture);
  m_active = false;
}
