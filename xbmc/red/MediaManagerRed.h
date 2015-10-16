#pragma once

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

#include <queue>
#include <string>
#include "RedMedia.h"
#include "XBDateTime.h"
#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "filesystem/CurlFile.h"

typedef void (*AssetUpdateCallBackFn)(const void *ctx, RedMediaAsset &asset, bool wasDownloaded);

class CMediaManagerRed : public CThread
{
public:
  CMediaManagerRed();
  virtual ~CMediaManagerRed();

  void          ClearAssets();
  void          ClearDownloads();
  size_t        GetDownloadCount();
  bool          GetLocalAsset(size_t index, RedMediaAsset &asset);
  bool          GetLocalAsset(const std::string asset_id, RedMediaAsset &asset);
  size_t        GetLocalAssetCount();
  void          QueueAssetsForDownload(std::vector<RedMediaAsset> &assets);

  void          QueueAssetForDownload(RedMediaAsset &asset);

  void          RegisterAssetUpdateCallBack(const void *ctx, AssetUpdateCallBackFn fn);
  
  void          OverrideDownloadWindow();
  void          SetDownloadTime(const CDateTime &time, const CDateTimeSpan &duration);

protected:
  virtual void  Process();
  bool          Exists(RedMediaAsset &asset);

  bool          m_Override;
  CDateTime     m_NextDownloadTime;
  CDateTimeSpan m_NextDownloadDuration;

  XFILE::CCurlFile      m_http;
  std::queue<RedMediaAsset> m_download;
  CCriticalSection      m_download_lock;

  std::vector<RedMediaAsset> m_assets;
  CCriticalSection      m_assets_lock;

  AssetUpdateCallBackFn m_AssetUpdateCallBackFn;
  const void           *m_AssetUpdateCallBackCtx;
};
