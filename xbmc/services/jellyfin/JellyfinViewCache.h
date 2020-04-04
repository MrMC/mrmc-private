#pragma once
/*
 *      Copyright (C) 2020 Team MrMC
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

#include <string>
#include <vector>

#include "utils/Variant.h"
#include "threads/CriticalSection.h"

typedef struct JellyfinViewInfo
{
  std::string id;
  std::string name;
  std::string prefix;
  std::string mediaType;
} JellyfinViewInfo;

typedef struct JellyfinViewContent
{
  std::string id;
  std::string name;
  std::string etag;
  std::string prefix;
  std::string serverId;
  std::string mediaType;
  std::string iconId;
  CVariant items;
} JellyfinViewContent;

class CJellyfinViewCache
{
public:
  CJellyfinViewCache();
 ~CJellyfinViewCache();

  void  Init(const JellyfinViewContent &content);
  const std::string GetId() const;
  const std::string GetName() const;
  void  SetItems(CVariant &variant);
  CVariant &GetItems();
  bool  ItemsValid();
  bool  AppendItem(const CVariant &variant);
  bool  UpdateItem(const CVariant &variant);
  bool  RemoveItem(const std::string &itemId);

  bool  SetWatched(const std::string id, int playcount, double resumetime);
  bool  SetUnWatched(const std::string id);

  const JellyfinViewInfo GetInfo() const;

private:
  JellyfinViewContent m_cache;
  CCriticalSection m_cacheLock;
};
