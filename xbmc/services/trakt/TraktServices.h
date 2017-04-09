#pragma once
/*
 *      Copyright (C) 2017 Team MrMC
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

#include "filesystem/CurlFile.h"
#include "services/ServicesManager.h"
#include "settings/lib/ISettingCallback.h"
#include "interfaces/IAnnouncer.h"
#include "utils/JobManager.h"

#define TRAKT_DEBUG_VERBOSE

class CTraktServices
: public CJobQueue
, public ISettingCallback
, public ANNOUNCEMENT::IAnnouncer
{
  friend class CTraktServiceJob;

public:
  CTraktServices();
  CTraktServices(const CTraktServices&);
  virtual ~CTraktServices();
  static CTraktServices &GetInstance();

  bool              IsEnabled();

  // ISettingCallback
  virtual void      OnSettingAction(const CSetting *setting) override;
  virtual void      OnSettingChanged(const CSetting *setting) override;

  // IAnnouncer callbacks
  virtual void      Announce(ANNOUNCEMENT::AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data) override;
  
  void              SetItemWatched(CFileItem &item);
  void              SetItemUnWatched(CFileItem &item);
  void              SaveFileState(CFileItem &item, double currentTime, double totalTime);

protected:
  static void       ReportProgress(CFileItem &item, int percentage);
  static void       SetPlayState(MediaServicesPlayerState state);

private:
  // private construction, and no assignements; use the provided singleton methods


  void              SetUserSettings();
  void              GetUserSettings();
  bool              MyTraktSignedIn();

  bool              GetSignInPinCode();
  bool              GetSignInByPinReply();
  static CVariant   ParseIds(const std::map<std::string, std::string> &Ids, const std::string &type);
  static CVariant   GetTraktCVariant(const std::string &url);
  static void       ServerChat(const std::string &url, const CVariant &data);
  static void       SetItemWatchedJob(CFileItem &item, bool watched);


  std::atomic<bool> m_active;
  CCriticalSection  m_critical;
  CEvent            m_processSleep;

  std::string       m_authToken;
  std::string       m_refreshAuthToken;
  std::string       m_deviceCode;
  XFILE::CCurlFile  m_Trakttv;

};
