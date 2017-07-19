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

#include "config.h"

#include "LiteUtils.h"

#include "CompileInfo.h"
#include "Util.h"
#include "dialogs/GUIDialogOK.h"
#include "guilib/GUIMessage.h"
#include "guilib/LocalizeStrings.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#if defined(TARGET_ANDROID)
#include "platform/android/activity/AndroidFeatures.h"
#endif

int CLiteUtils::nextReminderTrigger = 0;
bool CLiteUtils::NeedReminding()
{
  if (nextReminderTrigger <= 0)
  {
    nextReminderTrigger = CUtil::GetRandomNumber(3,8);
    return true;
  }
  else
    nextReminderTrigger--;
  return nextReminderTrigger <= 0;
}

int CLiteUtils::GetItemSizeLimit()
{
  // if we are lite, trim to 7 items + ".."
  return 7 + 1;
}

void CLiteUtils::ShowIsLiteDialog(int preTruncateSize)
{
  if (!NeedReminding())
    return;
  std::string line2 = StringUtils::Format(g_localizeStrings.Get(897).c_str(), preTruncateSize);
  std::string line3;
#if defined(TARGET_DARWIN)
  line3 = StringUtils::Format(g_localizeStrings.Get(898).c_str(), "Apple");
#elif defined(TARGET_ANDROID)
  line3 = StringUtils::Format(g_localizeStrings.Get(898).c_str(), CAndroidFeatures::IsAmazonDevice() ? "Amazon":"Google Play");
#endif
  if (!line3.empty())
    CGUIDialogOK::ShowAndGetInput(CCompileInfo::GetAppName(), 896, line2, line3);
}
