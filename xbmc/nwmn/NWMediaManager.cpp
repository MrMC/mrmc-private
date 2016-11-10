/*
 *  Copyright (C) 2016 RootCoder, LLC.
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
 *  along with this app; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "NWMediaManager.h"

#include "Util.h"
#include "filesystem/File.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

CNWMediaManager::CNWMediaManager()
 : CThread("CNWMediaManager")
 , m_hasNetwork(false)
 , m_AssetUpdateCallBackFn(NULL)
 , m_AssetUpdateCallBackCtx(NULL)
{
}

CNWMediaManager::~CNWMediaManager()
{
  m_AssetUpdateCallBackFn = NULL;
  m_bStop = true;
  m_http.Cancel();
  StopThread();
}

void CNWMediaManager::ClearAssets()
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
      m_download.erase(m_download.begin());
  }
}

void CNWMediaManager::ClearDownloads()
{
  if (!m_download.empty())
  {
    CSingleLock lock(m_download_lock);
    while (!m_download.empty())
      m_download.erase(m_download.begin());
  }

  // do the cancel reset inside this lock
  CSingleLock download_lock(m_download_lock);
  m_http.Cancel();
}

size_t CNWMediaManager::GetDownloadCount()
{
  CSingleLock lock(m_download_lock);
  return m_download.size();
}

bool CNWMediaManager::GetLocalAsset(size_t index, struct NWAsset &asset)
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

bool CNWMediaManager::GetLocalAsset(int asset_id, NWAsset &asset)
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

size_t CNWMediaManager::GetLocalAssetCount()
{
  CSingleLock lock(m_assets_lock);
  return m_assets.size();
}

void CNWMediaManager::QueueAssetsForDownload(std::vector<NWAsset> &assets)
{
  if (!assets.empty())
  {
    for (size_t asset = 0; asset < assets.size(); asset++)
      QueueAssetForDownload(assets[asset]);
  }
}

void CNWMediaManager::UpdateNetworkStatus(bool hasNetwork)
{
  m_hasNetwork = hasNetwork;
}

void CNWMediaManager::QueueAssetForDownload(NWAsset &asset)
{
  // check if this asset is already in the download list
  auto it = std::find_if(m_download.begin(), m_download.end(),
    [asset](const NWAsset &existingAsset) { return existingAsset.id == asset.id; });
  if (it != m_download.end())
    return;

  // queue for download
  CSingleLock lock(m_download_lock);
  CLog::Log(LOGDEBUG, "**NW** - CNWMediaManager::QueueAssetForDownload "
    "%s", asset.video_localpath.c_str());
  m_download.push_back(asset);
}

bool CNWMediaManager::CheckAssetIsPresentLocal(NWAsset &asset)
{
  // check if we already have this asset on disk
  if (XFILE::CFile::Exists(asset.video_localpath))
  {
    // verify it by md5 check
    if (StringUtils::EqualsNoCase(asset.video_md5, CUtil::GetFileMD5(asset.video_localpath)))
    {
      if (!Exists(asset))
      {
        // we have a verified local existing asset, skip downloading it
        CSingleLock lock(m_assets_lock);
        m_assets.push_back(asset);
        if (m_AssetUpdateCallBackFn)
          (*m_AssetUpdateCallBackFn)(m_AssetUpdateCallBackCtx, asset, AssetDownloadState::IsPresent);
      }
      return true;
    }
  }

  return false;
}

void CNWMediaManager::RegisterAssetUpdateCallBack(const void *ctx, AssetUpdateCallBackFn fn)
{
  m_AssetUpdateCallBackFn = fn;
  m_AssetUpdateCallBackCtx = ctx;
}

void CNWMediaManager::Process()
{
  SetPriority(THREAD_PRIORITY_BELOW_NORMAL);
  CLog::Log(LOGDEBUG, "**NW** - CNWMediaManager::Process Started");

  m_http.SetBufferSize(32768 * 10);
  m_http.SetTimeout(5);

  while (!m_bStop)
  {
    Sleep(500);

    CSingleLock download_lock(m_download_lock);
    if (!m_download.empty())
    {
      // we might hae been canceled, so reset.
      m_http.Reset();

      // fetch an asset to download
      NWAsset asset = m_download.front();
      m_download.erase(m_download.begin());
      download_lock.Leave();

      // check if we already have this asset on disk
      if (CheckAssetIsPresentLocal(asset))
        continue;

      if (!m_hasNetwork)
        continue;

      // download it
      if (m_AssetUpdateCallBackFn)
        (*m_AssetUpdateCallBackFn)(m_AssetUpdateCallBackCtx, asset, AssetDownloadState::willDownload);

      unsigned int size = asset.video_size;
      if (size && m_http.Download(asset.video_url, asset.video_localpath, &size))
      {
        // verify download by md5 check
        if (StringUtils::EqualsNoCase(asset.video_md5, CUtil::GetFileMD5(asset.video_localpath)))
        {
          // quick grab of thumbnail with no error checking.
          if (!XFILE::CFile::Exists(asset.thumb_localpath))
          {
            size = asset.thumb_size;
            m_http.Download(asset.thumb_url, asset.thumb_localpath, &size);
          }

          if (!Exists(asset))
          {
            // we have a verified asset, keep it
            CSingleLock lock(m_assets_lock);
            m_assets.push_back(asset);
            if (m_AssetUpdateCallBackFn)
              (*m_AssetUpdateCallBackFn)(m_AssetUpdateCallBackCtx, asset, AssetDownloadState::wasDownloaded);
          }
        }
        else
        {
          CLog::Log(LOGDEBUG, "**NW** - CNWMediaManager::Process "
                    "md5 mismatch for %s", asset.video_localpath.c_str());
          // must be bad file, delete and requeue for download
          if (XFILE::CFile::Delete(asset.video_localpath))
          {
            // remove any thumbnail for this asset
            XFILE::CFile::Delete(asset.thumb_localpath);
            download_lock.Enter();
            m_download.push_back(asset);
          }
       }
      }
      else
      {
        CLog::Log(LOGDEBUG, "**NW** - CNWMediaManager::Process download/save failed, just requeue");
        // download/save failed, just requeue
        download_lock.Enter();
        m_download.push_back(asset);
      }
    }
  }

  CLog::Log(LOGDEBUG, "**NW** - CNWMediaManager::Process Stopped");
}

bool CNWMediaManager::Exists(NWAsset &asset)
{
  for (size_t index = 0; index < m_assets.size(); index++)
  {
    if (asset.id == m_assets[index].id)
      return true;
  }

  return false;
}
