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

#include <string>
#include "FileItem.h"

class CPlexClient
{
public:
  CPlexClient();
  ~CPlexClient();
  static CPlexClient &GetInstance();
  void GetLocalMovies(CFileItemList &items);
  void GetLocalTvshows(CFileItemList &items);
  void GetLocalSeasons(CFileItemList &items, const std::string directory);
  void SetWatched(std::string id);
  void SetUnWatched(std::string id);
  void SetOffset(CFileItem item, int offsetSeconds);


private:
  // private construction, and no assignements; use the provided singleton methods

};
