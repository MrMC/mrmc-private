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

#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "settings/lib/ISettingCallback.h"
#include "interfaces/IAnnouncer.h"

class CSetting;
class TiXmlNode;
class CLightEffectServices;
class CVariant;

class CLightEffectServices
: public CThread
, public ISettingCallback
, public ANNOUNCEMENT::IAnnouncer
{
public:
  static CLightEffectServices &GetInstance();

  bool Start();
  void Stop();
  
  bool IsActive();

  // ISettingCallbacks
  virtual void OnSettingChanged(const CSetting *setting) override;
  
  virtual void Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)override;

private:
  // private construction, and no assignements; use the provided singleton methods
  CLightEffectServices();
  CLightEffectServices(const CLightEffectServices&);
  void SetOption(std::string option, std::string value);
  void SetStatic();
  void InitConnection();
  void ApplyUserSettings();
  virtual ~CLightEffectServices();

  // IRunnable entry point for thread
  virtual void  Process() override;

  std::atomic<bool> m_active;
  int               m_width;
  int               m_height;
  CCriticalSection  m_critical;
  void*             m_lighteffect;
  bool              m_staticON;
  
};