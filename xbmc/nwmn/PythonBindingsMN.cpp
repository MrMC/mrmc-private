/*
 *  Copyright (C) 2015 Dream-Team
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "PythonBindingsMN.h"

#include "messaging/ApplicationMessenger.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "PlayerManagerMN.h"

CPythonBindingsMN::CPythonBindingsMN(void)
{
}

CPythonBindingsMN::~CPythonBindingsMN(void)
{}

void CPythonBindingsMN::Refresh()
{
  // Notify that we have changed settings
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info,
                                        "MemberNet",
                                        "Player details updated",
                                        TOAST_DISPLAY_TIME, false);
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->FullUpdate();
}

void CPythonBindingsMN::PlayPause()
{
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->PlayPause();
}

void CPythonBindingsMN::PausePlaying()
{
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->PausePlaying();
}

void CPythonBindingsMN::StopPlaying()
{
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->StopPlaying();
}

void CPythonBindingsMN::PlayNext()
{
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->PlayNext();
}

void CPythonBindingsMN::PlayPrevious()
{
  CPlayerManagerMN* MNPlayerManager = CPlayerManagerMN::GetPlayerManager();
  if (MNPlayerManager)
    MNPlayerManager->PlayPrevious();
}