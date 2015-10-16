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

#include "system.h"
#include "DBManagerRed.h"
#include "MediaManagerRed.h"
#include "PlayBackManagerRed.h"
#include "PlayerManagerRed.h"

#include "dbwrappers/dataset.h"
#include "settings/AdvancedSettings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

CDBManagerRed::CDBManagerRed()
{
}

CDBManagerRed::~CDBManagerRed()
{
}

bool CDBManagerRed::Open()
{
  CLog::Log(LOGDEBUG, "**RED** - CDBManagerRed::Open()");
  return CDatabase::Open();
}

void CDBManagerRed::Close()
{
  CLog::Log(LOGDEBUG, "**RED** - CDBManagerRed::Close()");
  CDatabase::Close();
}

void CDBManagerRed::CreateTables()
{
  try
  {
    CLog::Log(LOGDEBUG, "**RED** - create playCountAssets table");
    m_pDS->exec("CREATE TABLE playCountAssets ( assetID text, "
                " timePlayed text, mediagroupID text, uuid text primary key)");
    CLog::Log(LOGDEBUG, "**RED** - create player table");
    m_pDS->exec("CREATE TABLE player ( playerId text, "
                " name text, status text, ip text, update_interval text,"
                " router_ip text, gateway text, dns_server_1 text, dns_server_2 text,"
                " subnet_mask text, dhcp text, version_id text, version_name text,"
                " version_url text, player_group_id text, programId text, playlistId text,"
                " report_interval text, api_url text, download_duration text, startTime text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - create program table");
    m_pDS->exec("CREATE TABLE program ( Id text, "
                " date text, screen_w text, screen_h text, zones_count text,"
                " playlistId text, name text, lastUpdated text, mediaGroups_count text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - create zones table");
    m_pDS->exec("CREATE TABLE zones ( Id text, "
                " left text, top text, width text, height text,"
                " playlistId text, name text,last_updated text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - create mediagroups table");
    m_pDS->exec("CREATE TABLE mediagroups ( Id text, "
                " name text, playback text, startDate text, endDate text,"
                " assets_count text, playlist_id text)"
                );

    CLog::Log(LOGDEBUG, "**RED** - create playlists table");
    m_pDS->exec("CREATE TABLE playlists ( Id text, "
                " name text, last_updated text, media_group_count text,zone_id text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - create programassets table");
    m_pDS->exec("CREATE TABLE programassets ( assetId text, "
                " mediagroupID text, title text, type text, url text,"
                " md5 text, size text, thumbnail text, metadata_nb text,"
                " artist text, year text, genre text, secondary_genre text,"
                " bpm text, vocals text, intro text, outro text,"
                " tempo text, rating text, record_label text, isrc text,"
                " keywords text, composer text, name text, duration text,"
                " performancerightsgroup text, status text, localpath text, bDownloaded BOOLEAN)"
                );
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - %s unable to create RED tables:%i", __FUNCTION__, (int)GetLastError());
    RollbackTransaction();
  }
}

int CDBManagerRed::GetSchemaVersion() const
{
  return 8;
}

const char* CDBManagerRed::GetBaseDBName() const
{
  return "Red";
}

void CDBManagerRed::CreateAnalytics()
{
  CLog::Log(LOGDEBUG, "**RED** - %s - creating indices", __FUNCTION__);
//  m_pDS->exec("CREATE UNIQUE INDEX idx_red_assetID_timePlayed_uuid on playCountAssets(assetID,timePlayed, uuid);");
}

void CDBManagerRed::UpdateTables(int version)
{
  if (version < 4)
  {
    CLog::Log(LOGDEBUG, "**RED** - UPDATE rename asset table");
    m_pDS->exec("ALTER TABLE asset RENAME TO playCountAssets");
    
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create player table");
    m_pDS->exec("CREATE TABLE player ( playerId text, "
                " name text, status text, ip text, update_interval text,"
                " router_ip text, gateway text, dns_server_1 text, dns_server_2 text,"
                " subnet_mask text, dhcp text, version_id text, version_name text,"
                " version_url text, player_group_id text, programId text, playlistId text,"
                " report_interval text, api_url text, download_duration text, startTime text)"
                );
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create program table");
    m_pDS->exec("CREATE TABLE program ( Id text, "
                " date text, screen_w text, screen_h text, zones_count text,"
                " playlistId text, name text, lastUpdated text, mediaGroups_count text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create zones table");
    m_pDS->exec("CREATE TABLE zones ( Id text, "
                " left text, top text, width text, height text,"
                " playlistId text, name text,last_updated text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create mediagroups table");
    m_pDS->exec("CREATE TABLE mediagroups ( Id text, "
                " name text, playback text, startDate text, endDate text,"
                " assets_count text, playlist_id text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create playlists table");
    m_pDS->exec("CREATE TABLE playlists ( Id text, "
                " name text, last_updated text, media_group_count text,zone_id text)"
                );
    
    CLog::Log(LOGDEBUG, "**RED** - UPDATE create programassets table");
    m_pDS->exec("CREATE TABLE programassets ( assetId text, "
                " mediagroupID text, title text, type text, url text,"
                " md5 text, size text, thumbnail text, metadata_nb text,"
                " artist text, year text, genre text, secondary_genre text,"
                " bpm text, vocals text, intro text, outro text,"
                " tempo text, rating text, record_label text, isrc text,"
                " keywords text, composer text, name text, duration text,"
                " performancerightsgroup text, status text, localpath text )"
                );
  }
  if (version < 6)
  {
    CLog::Log(LOGDEBUG, "**RED** - UPDATE add 'bDownloaded' to  programassets table");
    m_pDS->exec("ALTER TABLE programassets ADD bDownloaded BOOLEAN;");
  }
  if (version < 7)
  {
    CLog::Log(LOGDEBUG, "**RED** - UPDATE add 'mediagroupID' to  playCountAssets table");
    m_pDS->exec("ALTER TABLE playCountAssets ADD mediagroupID text;");
  }
}

bool CDBManagerRed::AddAssetPlayback(const RedMediaAsset asset)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    CDateTime date=CDateTime::GetCurrentDateTime();
    std::string strDate = date.GetAsDBDateTime();
    std::string uuid = StringUtils::CreateUUID();
    
    std::string strSQL=PrepareSQL("INSERT INTO playCountAssets (assetID,timePlayed,uuid,mediagroupID) values ('%s', '%s', '%s', %s)",
                      asset.id.c_str(),
                      strDate.c_str(),
                      uuid.c_str(),
                      asset.mediagroup_id.c_str());
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to AddAssetPlayback(%s)", asset.id.c_str());
  }
  return true;
}

bool CDBManagerRed::DeleteRecordsByUUID(std::vector<RedMediaAsset> assets)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
  
    // remove items with following uuid
    // delete from asset where uuid in ('256FF6D4-C3E3-015B-3645-0D8BBBB060A9', 'A69FE916-FBED-9143-879D-B1EBBB42A869')
    std::string strSQL = PrepareSQL("delete from playCountAssets where uuid in (");
    
    for (std::vector<RedMediaAsset>::iterator assetit = assets.begin(); assetit != assets.end(); ++assetit)
    {
      strSQL += PrepareSQL("'%s',",assetit->uuid.c_str());
    }
    strSQL = StringUtils::TrimRight(strSQL, ","); // remove last ',' from above
    strSQL += ")";
    
    // use shorter debug line, we know it works
    CLog::Log(LOGERROR, "**RED** - DeleteRecordsByUUID ()");
//    CLog::Log(LOGERROR, "**RED** - DeleteRecordsByUUID [%s]", strSQL.c_str());
    m_pDS->exec(strSQL.c_str());
    m_pDS->close(); // cleanup recordset data
  
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - %s failed", __FUNCTION__ );
  }
  return false;
}

bool CDBManagerRed::GetAllPlayedAssets(std::vector<RedMediaAsset>& assets)
{
  try
  {
    assets.clear();
    
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("select * from playCountAssets ");
    if (!m_pDS->query(strSQL.c_str())) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    while (!m_pDS->eof())
    {
      RedMediaAsset asset;
      const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
      asset.id            = record->at(0).get_asString();
      asset.timePlayed    = record->at(1).get_asString();
      asset.uuid          = record->at(2).get_asString();
      asset.mediagroup_id = record->at(3).get_asString();
      assets.push_back(asset);
      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - %s failed", __FUNCTION__);
  }
  
  return false;
}

bool CDBManagerRed::SaveMediagroup(RedMediaGroup mediagroup)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("INSERT INTO mediagroups (Id,name,playback,startDate,endDate,assets_count, playlist_id) values ('%s', '%s', '%s','%s', '%s', '%i', '%s')",
                                 mediagroup.id.c_str(),
                                 mediagroup.name.c_str(),
                                 mediagroup.playbackType.c_str(),
                                 mediagroup.startDate.GetAsDBDateTime().c_str(),
                                 mediagroup.endDate.GetAsDBDateTime().c_str(),
                                 mediagroup.assets.size(),
                                 mediagroup.playlistId.c_str()
                                 );
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
    
    SaveProgramAssets(mediagroup.assets);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to SaveMediagroup(%s)", mediagroup.id.c_str());
  }
  return true;
}

bool CDBManagerRed::SaveProgramAssets(std::vector<RedMediaAsset> assets)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    CDateTime date=CDateTime::GetCurrentDateTime();
    std::string strDate = date.GetAsDBDateTime();
    std::string uuid = StringUtils::CreateUUID();
    
    std::string strSQL;
    
    for (std::vector<RedMediaAsset>::iterator assetit = assets.begin(); assetit != assets.end(); ++assetit)
    {
      
      strSQL=PrepareSQL("INSERT INTO programassets (assetId,mediagroupID,title,type,url,md5,size,thumbnail, metadata_nb,artist, year, genre, secondary_genre,bpm , vocals , intro , outro ,tempo, rating, record_label, isrc,keywords , composer , name , duration , performancerightsgroup , status, localpath) values ('%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s','%s', '%s', '%s','%s','%s', '%s')",
                        assetit->id.c_str(),
                        assetit->mediagroup_id.c_str(),
                        "",
                        assetit->type.c_str(),
                        assetit->url.c_str(),
                        assetit->md5.c_str(),
                        assetit->size.c_str(),
                        assetit->thumbnail_localpath.c_str(),
                        "",
                        assetit->artist.c_str(),
                        assetit->year.c_str(),
                        assetit->genre.c_str(),
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        assetit->composer.c_str(),
                        assetit->name.c_str(),
                        "",
                        "",
                        "",
                        assetit->localpath.c_str()
                        );
      
      m_pDS->exec(strSQL.c_str());
      m_pDS->close();
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to SaveProgramAssets()");
  }
  return true;
}

bool CDBManagerRed::SavePlayer(PlayerInfo player)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("INSERT INTO player (playerId, name, status, ip, update_interval,router_ip, gateway, dns_server_1, dns_server_2,subnet_mask , dhcp , version_id , version_name ,version_url, player_group_id , programId , playlistId ,report_interval, api_url, download_duration,startTime) values ('%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s')",
                        player.strPlayerID.c_str(),
                        player.strPlayerName.c_str(),
                        player.strStatus.c_str(),
                        "",
                        player.strUpdateInterval.c_str(),
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        "",
                        player.strProgramID.c_str(),
                        player.strPlaylistID.c_str(),
                        player.strReportInterval.c_str(),
                        "",
                        player.strDownloadDuration.c_str(),
                        player.strDownloadStartTime.c_str()
                        );
      
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to SavePlayer()");
  }
  return true;
}

bool CDBManagerRed::SaveProgram(ProgramInfo program)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    ClearProgram();
    ClearZones();
    ClearPlaylists();
    ClearMediagroups();
    ClearProgramAssets();
    
    std::string strSQL;
    strSQL=PrepareSQL("INSERT INTO program (Id, date, screen_w, screen_h, zones_count,playlistId, name , lastUpdated , mediaGroups_count) values ('%s', '%s', '%s','%s', '%s', '%s','%s', '%s', '%s')",
                                 program.strProgramID.c_str(),
                                 program.strDate.c_str(),
                                 program.strScreenW.c_str(),
                                 program.strScreenH.c_str(),
                                 "",
                                 "",
                                 "",
                                 "",
                                 ""
                                 );
    
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
    
    
    for (std::vector<RedMediaZone>::iterator rzit = program.zones.begin(); rzit != program.zones.end(); ++rzit)
    {
      strSQL=PrepareSQL("INSERT INTO zones (Id, left, top, width, height,playlistId, name , last_updated) values ('%s', '%s','%s', '%s', '%s','%s', '%s', '%s')",
                                   rzit->strId.c_str(),
                                   rzit->strLeft.c_str(),
                                   rzit->strTop.c_str(),
                                   rzit->strWidth.c_str(),
                                   rzit->strHeight.c_str(),
                                   rzit->strPlaylistID.c_str(),
                                   rzit->strName.c_str(),
                                   rzit->strLastUpdated.c_str()
                                   );
      
      m_pDS->exec(strSQL.c_str());
      m_pDS->close();
      
      for (std::vector<RedMediaPlaylist>::iterator rpit = rzit->playlists.begin(); rpit != rzit->playlists.end(); ++rpit)
      {
                          
        strSQL=PrepareSQL("INSERT INTO playlists (Id, name, last_updated, media_group_count, zone_id) values ('%s', '%s','%s', '%i', '%s')",
                          rpit->strID.c_str(),
                          rpit->strName.c_str(),
                          rpit->strLastUpdated.c_str(),
                          rpit->intMediaGroupsCount,
                          rzit->strId.c_str()
                          );
        
        m_pDS->exec(strSQL.c_str());
        m_pDS->close();
                          
        for (std::vector<RedMediaGroup>::iterator mgit = rpit->MediaGroups.begin(); mgit != rpit->MediaGroups
             .end(); ++mgit)
        {
          SaveMediagroup(*mgit);
        }
      }
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to SaveProgram()");
  }
  return true;
}

bool CDBManagerRed::GetProgram(ProgramInfo &program)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL;
    
    strSQL=PrepareSQL("select * from program");
    
    if (!m_pDS->query(strSQL.c_str())) return false;
    int iRowsFound;
    iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
    
    program.strProgramID = record->at(1).get_asString();
    program.strDate = record->at(2).get_asString();
    program.strScreenW = record->at(3).get_asString();
    program.strScreenH = record->at(4).get_asString();
    
    m_pDS->close();
    
    strSQL=PrepareSQL("select * from zones");
    
    if (!m_pDS->query(strSQL.c_str())) return false;
    iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    
    
    while (!m_pDS->eof())
    {
      RedMediaZone zone;
      const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
      zone.strId = record->at(0).get_asString();
      zone.strLeft = record->at(1).get_asString();
      zone.strTop = record->at(2).get_asString();
      zone.strWidth = record->at(3).get_asString();
      zone.strHeight = record->at(4).get_asString();
      zone.strPlaylistID = record->at(5).get_asString();
      zone.strName = record->at(6).get_asString();
      zone.strLastUpdated = record->at(7).get_asString();
      
      program.zones.push_back(zone);
      m_pDS->next();
    }
    
    for (std::vector<RedMediaZone>::iterator rzit = program.zones.begin(); rzit != program.zones.end(); ++rzit)
    {
      strSQL=PrepareSQL("select * from playlists where zone_id=%s",rzit->strId.c_str() );
      if (!m_pDS->query(strSQL.c_str())) return false;
      iRowsFound = m_pDS->num_rows();
      if (iRowsFound == 0)
      {
        m_pDS->close();
        return false;
      }
      
      while (!m_pDS->eof())
      {
        RedMediaPlaylist playlist;
        const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
        playlist.strID = record->at(0).get_asString();
        playlist.strName = record->at(1).get_asString();
        playlist.strLastUpdated = record->at(2).get_asString();
        playlist.strZoneID = record->at(4).get_asString();
        
        rzit->playlists.push_back(playlist);
        m_pDS->next();
      }
      
      for (std::vector<RedMediaPlaylist>::iterator rpit = rzit->playlists.begin(); rpit != rzit->playlists.end(); ++rpit)
      {
        strSQL=PrepareSQL("select * from mediagroups where playlist_id=%s",rpit->strID.c_str() );
        if (!m_pDS->query(strSQL.c_str())) return false;
        iRowsFound = m_pDS->num_rows();
        if (iRowsFound == 0)
        {
          m_pDS->close();
          return false;
        }
        
        while (!m_pDS->eof())
        {
          const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
          RedMediaGroup mediagroup;
          mediagroup.id = record->at(0).get_asString();
          mediagroup.name = record->at(1).get_asString();
          mediagroup.playbackType = record->at(2).get_asString();
          mediagroup.startDate.SetFromDBDateTime(record->at(3).get_asString());
          mediagroup.endDate.SetFromDBDateTime(record->at(4).get_asString());
          mediagroup.assetIndex = record->at(6).get_asString();
          
          rpit->MediaGroups.push_back(mediagroup);
          m_pDS->next();
        }
        
        for (std::vector<RedMediaGroup>::iterator mgit = rpit->MediaGroups.begin(); mgit != rpit->MediaGroups
             .end(); ++mgit)
        {
          strSQL=PrepareSQL("select * from programassets where mediagroupID=%s",mgit->id.c_str() );
          if (!m_pDS->query(strSQL.c_str())) return false;
          iRowsFound = m_pDS->num_rows();
          if (iRowsFound == 0)
          {
            m_pDS->close();
            return false;
          }
          
          while (!m_pDS->eof())
          {
            RedMediaAsset asset;
            const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
            asset.id = record->at(0).get_asString();
            asset.mediagroup_id = record->at(1).get_asString();
            asset.type = record->at(3).get_asString();
            asset.url = record->at(4).get_asString();
            asset.md5 = record->at(5).get_asString();
            asset.size = record->at(6).get_asString();
            asset.thumbnail_localpath = record->at(7).get_asString();
            asset.artist = record->at(9).get_asString();
            asset.year = record->at(10).get_asString();
            asset.genre = record->at(11).get_asString();
            asset.composer = record->at(22).get_asString();
            asset.name = record->at(23).get_asString();
            asset.localpath = record->at(27).get_asString();
            mgit->assets.push_back(asset);
            m_pDS->next();
          }
        }
      }
    }
    
    
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to GetProgram()");
  }
  return true;
}

bool CDBManagerRed::GetPlayer(PlayerInfo &player)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("select * from player");
    
    if (!m_pDS->query(strSQL.c_str())) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
    player.strPlayerID = record->at(0).get_asString();
    player.strPlayerName = record->at(1).get_asString();
    player.strStatus = record->at(2).get_asString();
    player.strUpdateInterval = record->at(4).get_asString();
    player.strProgramID = record->at(15).get_asString();
    player.strPlaylistID = record->at(16).get_asString();
    player.strReportInterval = record->at(17).get_asString();
    player.strDownloadDuration = record->at(19).get_asString();
    player.strDownloadStartTime= record->at(20).get_asString();
    
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to GetPlayer()");
  }
  return true;
}

bool CDBManagerRed::ClearMediagroups()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    CDateTime date=CDateTime::GetCurrentDateTime();
    std::string strDate = date.GetAsDBDateTime();
    std::string uuid = StringUtils::CreateUUID();
    
    
    std::string strSQL=PrepareSQL("DELETE FROM  mediagroups");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearMediagroups");
    return false;
  }
  return true;
}

bool CDBManagerRed::ClearProgramAssets()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
  
    std::string strSQL=PrepareSQL("DELETE FROM programassets");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearProgramAssets()");
    return false;
  }
  return true;
}

bool CDBManagerRed::ClearPlayer()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("DELETE FROM  player");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearPlayer()");
    return false;
  }
  return true;
}

bool CDBManagerRed::ClearZones()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("DELETE FROM  zones");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearZones()");
    return false;
  }
  return true;
}

bool CDBManagerRed::ClearProgram()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("DELETE FROM program");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearProgram()");
    return false;
  }
  return true;
}

bool CDBManagerRed::ClearPlaylists()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("DELETE FROM playlists");
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to ClearPlaylists()");
    return false;
  }
  return true;
}

bool CDBManagerRed::IsPlayerValid()
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("select * from player");
    
    if (!m_pDS->query(strSQL.c_str())) return false;
    int iRowsFound = m_pDS->num_rows();
    m_pDS->close();
    
    return iRowsFound > 0;
    
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to IsPlayerValid()");
  }
  return false;
}


std::string CDBManagerRed::GetPlayerID()
{
  std::string playerId = "-1";
  try
  {
    if (NULL == m_pDB.get()) return playerId;
    if (NULL == m_pDS.get()) return playerId;
    
    std::string strSQL=PrepareSQL("select * from player");
    
    if (!m_pDS->query(strSQL.c_str())) return playerId;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
        m_pDS->close();
        return playerId;
    }
    const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
    playerId = record->at(0).get_asString();
    m_pDS->close();
    }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to add GetPlayerID()");
  }
  return playerId;
}

bool CDBManagerRed::SetDownloadedAsset(const std::string AssetID, bool downloaded)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("UPDATE programassets SET bDownloaded=%i WHERE assetId='%s'", downloaded, AssetID.c_str());
    m_pDS->exec(strSQL.c_str());
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - reddatabase:unable to SetDownloadedAsset()");
    return false;
  }
  return true;
}

bool CDBManagerRed::GetAllDownloadedAssets(std::vector<RedMediaAsset>& assets)
{
  try
  {
    assets.clear();
    
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    
    std::string strSQL=PrepareSQL("select * from programassets where bDownloaded=1 ");
    if (!m_pDS->query(strSQL.c_str())) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    while (!m_pDS->eof())
    {
      RedMediaAsset asset;
      const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();
      asset.id                  = record->at(0).get_asString();
      asset.mediagroup_id       = record->at(1).get_asString();
      asset.type                = record->at(3).get_asString();
      asset.url                 = record->at(4).get_asString();
      asset.md5                 = record->at(5).get_asString();
      asset.size                = record->at(6).get_asString();
      asset.thumbnail_localpath = record->at(7).get_asString();
      asset.artist              = record->at(9).get_asString();
      asset.year                = record->at(10).get_asString();
      asset.genre               = record->at(11).get_asString();
      asset.composer            = record->at(22).get_asString();
      asset.name                = record->at(23).get_asString();
      asset.localpath           = record->at(27).get_asString();
      
      assets.push_back(asset);
      m_pDS->next();

    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "**RED** - %s failed", __FUNCTION__);
  }
  
  return false;
}