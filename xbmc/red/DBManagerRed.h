/*
 *  Copyright (C) 2014 Team RED
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
/*!
 \file DBManagerRed.h
\brief
*/
#pragma once
#include "dbwrappers/Database.h"


struct RedMediaAsset;
struct RedMediaGroup;
struct PlayerInfo;
struct ProgramInfo;

namespace dbiplus
{
  class field_value;
  typedef std::vector<field_value> sql_record;
}

class CDBManagerRed : public CDatabase
{
  friend class DatabaseUtils;
  friend class TestDatabaseUtilsHelper;

public:
  CDBManagerRed();
  virtual ~CDBManagerRed();

  virtual bool Open();
  virtual void Close();
  void         Clean();
  bool         AddAssetPlayback(const RedMediaAsset asset);
  bool         DeleteRecordsByUUID(std::vector<RedMediaAsset> assets);
  bool         GetAllPlayedAssets(std::vector<RedMediaAsset>& assets);
  bool         SaveMediagroup(RedMediaGroup mediagroup);
  bool         SaveProgram(ProgramInfo program);
  bool         GetProgram(ProgramInfo &program);
  bool         ClearMediagroups();
  bool         ClearProgram();
  bool         ClearZones();
  bool         ClearPlayer();
  bool         ClearProgramAssets();
  bool         ClearPlaylists();
  bool         SaveProgramAssets(std::vector<RedMediaAsset> assets);
  bool         SavePlayer(PlayerInfo player);
  bool         GetPlayer(PlayerInfo &player);
  bool         IsPlayerValid();
  bool         SetDownloadedAsset(const std::string AssetID, bool downloaded=true);
  bool         GetAllDownloadedAssets(std::vector<RedMediaAsset>& assets);
  std::string  GetPlayerID();

protected:
  virtual void CreateTables();
  virtual void UpdateTables(int version);
  virtual int  GetSchemaVersion() const;
  virtual const char*  GetBaseDBName() const;
  virtual void CreateAnalytics();

};
