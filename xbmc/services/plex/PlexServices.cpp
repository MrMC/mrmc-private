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

#include "PlexServices.h"

#include "Application.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "cores/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "guilib/LocalizeStrings.h"

#include "PlexClient.h"

using namespace ANNOUNCEMENT;

CPlexServices::CPlexServices()
: CThread("PlexServices")
{
  // my local movie URL
  
//  CPlexClient::GetInstance().GetLocalMovies(url);
  
}

CPlexServices::~CPlexServices()
{
  if (IsRunning())
    Stop();
}

CPlexServices& CPlexServices::GetInstance()
{
  static CPlexServices sPlexServices;
  return sPlexServices;
}

void CPlexServices::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
}

void CPlexServices::Start()
{
  CSingleLock lock(m_critical);
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_SERVICES_PLEXENABLE) && !IsRunning())
  {
    if (IsRunning())
      StopThread();
    CThread::Create();
  }
}

void CPlexServices::Stop()
{
  CSingleLock lock(m_critical);
  if (IsRunning())
  {
    StopThread();
  }
}

bool CPlexServices::IsActive()
{
  return IsRunning();
}

void CPlexServices::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_PLEXENABLE)
  {
    // start or stop the service
    if (static_cast<const CSettingBool*>(setting)->GetValue())
      Start();
    else
      Stop();
  }

  CSettings::GetInstance().Save();
}

void CPlexServices::Process()
{
  if (InitConnection())
  {
    ApplyUserSettings();

    while (!m_bStop)
    {
      usleep(50 * 1000);
    }
  }
}

bool CPlexServices::InitConnection()
{
  return true;
}

void CPlexServices::ApplyUserSettings()
{
}
