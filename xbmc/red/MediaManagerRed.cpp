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

#include "MediaManagerRed.h"
#include "UtilitiesRed.h"

#include "Util.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/StringUtils.h"



CMediaManagerRed::CMediaManagerRed()
 : CThread("CMediaManagerRed")
 , m_Override(false)
 , m_NextDownloadTime(CDateTime::GetCurrentDateTime())
 , m_NextDownloadDuration(0, 6, 0, 0)
 , m_AssetUpdateCallBackFn(NULL)
 , m_AssetUpdateCallBackCtx(NULL)
{
}

CMediaManagerRed::~CMediaManagerRed()
{
  m_AssetUpdateCallBackFn = NULL;
  m_bStop = false;
  m_http.Cancel();
  StopThread();
}

void CMediaManagerRed::ClearAssets()
{
  if (!m_assets.empty())
  {
    CSingleLock lock(m_assets_lock);
    m_assets.clear();
  }
  if (!m_download.empty())
  {
    CSingleLock lock(m_download_lock);
    while (!m_download.empty())
      m_download.pop();
  }
}

void CMediaManagerRed::ClearDownloads()
{
  if (!m_download.empty())
  {
    CSingleLock lock(m_download_lock);
    while (!m_download.empty())
      m_download.pop();
  }
  m_http.Cancel();
}

size_t CMediaManagerRed::GetDownloadCount()
{
  CSingleLock lock(m_download_lock);
  return m_download.size();
}

bool CMediaManagerRed::GetLocalAsset(size_t index, RedMediaAsset &asset)
{
  bool rtn = false;
  CSingleLock lock(m_assets_lock);
  if (index < m_assets.size())
  {
    asset = m_assets[index];
    rtn = true;
  }

  return rtn;
}

bool CMediaManagerRed::GetLocalAsset(const std::string asset_id, RedMediaAsset &asset)
{
  bool rtn = false;
  for (size_t index = 0; index < m_assets.size(); index++)
  {
    if (asset_id == m_assets[index].id)
    {
      asset = m_assets[index];
      rtn = true;
      break;
    }
  }
  return rtn;
}

size_t CMediaManagerRed::GetLocalAssetCount()
{
  CSingleLock lock(m_assets_lock);
  return m_assets.size();
}

void CMediaManagerRed::QueueAssetsForDownload(std::vector<RedMediaAsset> &assets)
{
  if (!assets.empty())
  {
    for (size_t asset = 0; asset < assets.size(); asset++)
      QueueAssetForDownload(assets[asset]);
  }
}

void CMediaManagerRed::QueueAssetForDownload(RedMediaAsset &asset)
{
  // check if we already have this asset on disk
  if (XFILE::CFile::Exists(asset.localpath))
  {
    // verify it by md5 check
    if (StringUtils::EqualsNoCase(asset.md5, CUtil::GetFileMD5(asset.localpath)))
    {
      if (!Exists(asset))
      {
        // if we have the asset, write it into database as downloaded
        SetDownloadedAsset(asset.id);
        
        // we have a verified local existing asset, skip downloading it
        CSingleLock lock(m_assets_lock);
        m_assets.push_back(asset);
        if (m_AssetUpdateCallBackFn)
          (*m_AssetUpdateCallBackFn)(m_AssetUpdateCallBackCtx, asset, false);
      }
      return;
    }
  }

  SetDownloadedAsset(asset.id, false);
  
  CLog::Log(LOGERROR, "**RED** - CMediaManagerRed::QueueAssetForDownload "
            "%s", asset.localpath.c_str());

  // queue for download
  CSingleLock lock(m_download_lock);
  m_download.push(asset);
}

void CMediaManagerRed::OverrideDownloadWindow()
{
  m_Override = true;
}

void CMediaManagerRed::SetDownloadTime(const CDateTime &time, const CDateTimeSpan &duration)
{
  m_NextDownloadTime = time;
  m_NextDownloadDuration = duration;
}

void CMediaManagerRed::RegisterAssetUpdateCallBack(const void *ctx, AssetUpdateCallBackFn fn)
{
  m_AssetUpdateCallBackFn = fn;
  m_AssetUpdateCallBackCtx = ctx;
}

void CMediaManagerRed::Process()
{
  CLog::Log(LOGDEBUG, "**RED** - CMediaManagerRed::Process Started");

  while (!m_bStop)
  {
    Sleep(500);

    CDateTime cur = CDateTime::GetCurrentDateTime();
    // download can only occur in a datetime window.
    if (m_Override || cur >= m_NextDownloadTime)
    {
      CDateTime end = m_NextDownloadTime + m_NextDownloadDuration;
      if (!m_Override && cur >= end)
      {
        CLog::Log(LOGDEBUG, "**RED** - CMediaManagerRed::Process cur %s, end %s",
          cur.GetAsSaveString().c_str(), end.GetAsSaveString().c_str());
       
        // complicated but required. we get download time as a
        // hh:mm:ss field, duration is in hours. So we have to
        // be able to span over the beginning or end of a 24 hour day.
        // for example. bgn at 6pm, end at 6am. bgn would be 18:00:00,
        // duration would be 12.
        CDateTime cur = CDateTime::GetCurrentDateTime();
        CDateTime bgn = m_NextDownloadTime;
        m_NextDownloadTime.SetDateTime(cur.GetYear(), cur.GetMonth(), cur.GetDay(), bgn.GetHour(), bgn.GetMinute(), bgn.GetSecond());
        continue;
      }

      CSingleLock download_lock(m_download_lock);
      if (!m_download.empty())
      {
        // fetch an asset to download
        RedMediaAsset asset = m_download.front();
        m_download.pop();
        download_lock.Leave();

        // download it
        unsigned int size = strtol(asset.size.c_str(), NULL, 10);
        if (size && m_http.Download(asset.url, asset.localpath, &size))
        {
          // verify download by md5 check
          if (StringUtils::EqualsNoCase(asset.md5, CUtil::GetFileMD5(asset.localpath)))
          {
            // quick grab of thumbnail with no error checking.
            if (!XFILE::CFile::Exists(asset.thumbnail_localpath))
              m_http.Download(asset.thumbnail_url, asset.thumbnail_localpath, &size);

            if (!Exists(asset))
            {
              // we have a verified asset, keep it
              CSingleLock lock(m_assets_lock);
              m_assets.push_back(asset);
              if (m_AssetUpdateCallBackFn)
                (*m_AssetUpdateCallBackFn)(m_AssetUpdateCallBackCtx, asset, true);
            }
            
            if (m_download.empty())
              m_Override = false;
          }
          else
          {
            CLog::Log(LOGERROR, "**RED** - CMediaManagerRed::Process "
                      "md5 mismatch for %s", asset.localpath.c_str());
            // must be bad file, delete and requeue for download
            if (XFILE::CFile::Delete(asset.localpath))
            {
              // remove any thumbnail for this asset
              XFILE::CFile::Delete(asset.thumbnail_localpath);
              
              CSingleLock lock(m_download_lock);
              m_download.push(asset);
            }
         }
        }
        else
        {
          // download/save failed, just requeue
          CSingleLock lock(m_download_lock);
          m_download.push(asset);
        }
      }
    }
  }

  CLog::Log(LOGDEBUG, "**RED** - CMediaManagerRed::Process Stopped");
}

bool CMediaManagerRed::Exists(RedMediaAsset &asset)
{
  for (size_t index = 0; index < m_assets.size(); index++)
  {
    if (asset.id == m_assets[index].id)
      return true;
  }

  return false;
}
