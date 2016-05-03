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

class CSetting;
class TiXmlNode;
class CLightEffectServices;

class CLightEffectServices
: public CThread
, public ISettingCallback
{
public:
  static CLightEffectServices &GetInstance();

  bool Start();
  void Stop();
  
  bool IsActive();

  // ISettingCallbacks
  virtual bool OnSettingChanging(const CSetting *setting) override;
  virtual void OnSettingChanged(const CSetting *setting) override;
  virtual bool OnSettingUpdate(CSetting* &setting, const char *oldSettingId, const TiXmlNode *oldSettingNode) override;

private:
  // private construction, and no assignements; use the provided singleton methods
  CLightEffectServices();
  CLightEffectServices(const CLightEffectServices&);
  virtual ~CLightEffectServices();

  // IRunnable entry point for thread
  virtual void  Process() override;

  std::atomic<bool> m_active;
  unsigned int      m_width;
  unsigned int      m_height;
  CCriticalSection  m_critical;
};