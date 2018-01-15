/*
 *      Copyright (C) 2018 Team MrMC
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

#import "ProgressThumbNailer.h"

#import <UIKit/UIKit.h>
#import <CoreFoundation/CoreFoundation.h>

#include "threads/SystemClock.h"
#include "DVDFileInfo.h"
#include "FileItem.h"
#include "settings/AdvancedSettings.h"
#include "pictures/Picture.h"
#include "video/VideoInfoTag.h"
#include "filesystem/StackDirectory.h"
#include "utils/log.h"
#include "utils/URIUtils.h"

#include "DVDStreamInfo.h"
#include "DVDInputStreams/DVDInputStream.h"
#ifdef HAVE_LIBBLURAY
#include "DVDInputStreams/DVDInputStreamBluray.h"
#endif
#include "DVDInputStreams/DVDFactoryInputStream.h"
#include "DVDDemuxers/DVDDemux.h"
#include "DVDDemuxers/DVDDemuxUtils.h"
#include "DVDDemuxers/DVDFactoryDemuxer.h"
#include "DVDDemuxers/DVDDemuxFFmpeg.h"
#include "DVDCodecs/DVDCodecs.h"
#include "DVDCodecs/DVDFactoryCodec.h"
#include "DVDCodecs/Video/DVDVideoCodec.h"
#include "DVDCodecs/Video/DVDVideoCodecFFmpeg.h"
#include "DVDDemuxers/DVDDemuxVobsub.h"

#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "filesystem/File.h"
#include "cores/FFmpeg.h"
#include "TextureCache.h"
#include "Util.h"
#include "utils/LangCodeExpander.h"

CProgressThumbNailer::CProgressThumbNailer(const CFileItem& item)
{
  m_path = item.GetPath();
  if (item.IsVideoDb() && item.HasVideoInfoTag())
    m_path = item.GetVideoInfoTag()->m_strFileNameAndPath;

  if (item.IsStack())
    m_path = XFILE::CStackDirectory::GetFirstStackedFile(item.GetPath());
}

CProgressThumbNailer::~CProgressThumbNailer()
{
  SAFE_DELETE(m_videoCodec);
  SAFE_DELETE(m_demuxer);
  SAFE_DELETE(m_inputStream);
}

bool CProgressThumbNailer::Initialize()
{
  m_redactPath = CURL::GetRedacted(m_path);

  CFileItem item(m_path, false);
  m_inputStream = CDVDFactoryInputStream::CreateInputStream(NULL, item);
  if (!m_inputStream)
  {
    CLog::Log(LOGERROR, "CProgressThumbNailer::ExtractThumb: Error creating stream for %s", m_redactPath.c_str());
    return false;
  }

  if (m_inputStream->IsStreamType(DVDSTREAM_TYPE_DVD)
   || m_inputStream->IsStreamType(DVDSTREAM_TYPE_BLURAY))
  {
    CLog::Log(LOGDEBUG, "CProgressThumbNailer::ExtractThumb: disc streams not supported for thumb extraction, file: %s", m_redactPath.c_str());
    SAFE_DELETE(m_inputStream);
    return false;
  }

  if (m_inputStream->IsStreamType(DVDSTREAM_TYPE_PVRMANAGER))
  {
    SAFE_DELETE(m_inputStream);
    return false;
  }

  if (!m_inputStream->Open())
  {
    CLog::Log(LOGERROR, "InputStream: Error opening, %s", m_redactPath.c_str());
    SAFE_DELETE(m_inputStream);
    return false;
  }

  try
  {
    m_demuxer = CDVDFactoryDemuxer::CreateDemuxer(m_inputStream, true);
    if(!m_demuxer)
    {
      SAFE_DELETE(m_inputStream);
      CLog::Log(LOGERROR, "%s - Error creating demuxer", __FUNCTION__);
      return false;
    }
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown when opening demuxer", __FUNCTION__);
    SAFE_DELETE(m_demuxer);
    SAFE_DELETE(m_inputStream);
    return false;
  }

  for (int i = 0; i < m_demuxer->GetNrOfStreams(); i++)
  {
    CDemuxStream* pStream = m_demuxer->GetStream(i);
    if (pStream)
    {
      // ignore if it's a picture attachment (e.g. jpeg artwork)
      if(pStream->type == STREAM_VIDEO && !(pStream->flags & AV_DISPOSITION_ATTACHED_PIC))
        m_videoStream = i;
      else
        pStream->SetDiscard(AVDISCARD_ALL);
    }
  }

  if (m_videoStream != -1)
  {
    CDVDStreamInfo hints(*m_demuxer->GetStream(m_videoStream), true);
    hints.software = true;
    CDVDCodecOptions dvdOptions;
    m_videoCodec = CDVDFactoryCodec::OpenCodec(new CDVDVideoCodecFFmpeg(), hints, dvdOptions);
    if (m_videoCodec)
    {
      m_aspect = hints.aspect;
      m_forced_aspect = hints.forced_aspect;
      return true;
    }
  }

  SAFE_DELETE(m_videoCodec);
  SAFE_DELETE(m_demuxer);
  SAFE_DELETE(m_inputStream);
  return false;
}

CGImageRef CProgressThumbNailer::ExtractThumb(float percentage)
{
  unsigned int nTime = XbmcThreads::SystemClockMillis();

  CGImageRef cgImageRef = nullptr;
  int packetsTried = 0;
  if (m_videoCodec)
  {
    // timebase is ms
    int totalLen = m_demuxer->GetStreamLength();
    int nSeekTo = (percentage * totalLen) / 100;

    //CLog::Log(LOGDEBUG,"%s - seeking to pos %dms (total: %dms) in %s", __FUNCTION__, nSeekTo, totalLen, m_redactPath.c_str());
    if (m_demuxer->SeekTime(nSeekTo, true))
    {
      int iDecoderState = VC_ERROR;
      DVDVideoPicture picture = {0};

      // num streams * 160 frames, should get a valid frame, if not abort.
      int abort_index = m_demuxer->GetNrOfStreams() * 160;
      do
      {
        DemuxPacket* pPacket = m_demuxer->Read();
        packetsTried++;
        if (!pPacket)
          break;

        if (pPacket->iStreamId != m_videoStream)
        {
          CDVDDemuxUtils::FreeDemuxPacket(pPacket);
          continue;
        }

        iDecoderState = m_videoCodec->Decode(pPacket->pData, pPacket->iSize, pPacket->dts, pPacket->pts);
        CDVDDemuxUtils::FreeDemuxPacket(pPacket);

        if (iDecoderState & VC_ERROR)
          break;

        if (iDecoderState & VC_PICTURE)
        {
          memset(&picture, 0, sizeof(DVDVideoPicture));
          if (m_videoCodec->GetPicture(&picture))
          {
            if (!(picture.iFlags & DVP_FLAG_DROPPED))
              break;
          }
        }

      } while (abort_index--);

      if (iDecoderState & VC_PICTURE && !(picture.iFlags & DVP_FLAG_DROPPED))
      {
        unsigned int nWidth = g_advancedSettings.GetThumbSize();
        double aspect = (double)picture.iDisplayWidth / (double)picture.iDisplayHeight;
        if(m_forced_aspect && m_aspect != 0)
          aspect = m_aspect;
        unsigned int nHeight = (unsigned int)((double)g_advancedSettings.GetThumbSize() / aspect);

        //AVPicture scaledPicture;
        //avpicture_alloc(&scaledPicture, AV_PIX_FMT_RGB24, nWidth, nHeight);

        uint8_t *scaledData = (uint8_t*)av_malloc(nWidth * nHeight * 3);
        int scaledLineSize = nWidth * 3;

        struct SwsContext *context = sws_getContext(picture.iWidth, picture.iHeight,
              AV_PIX_FMT_YUV420P, nWidth, nHeight, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (context)
        {

          // CGImages have flipped in y-axis coordinated, we can do the flip as we convert/scale
          uint8_t * flipData = scaledData + (scaledLineSize * (nHeight -1));
          int flipLineSize = -scaledLineSize;
          //uint8_t * flipData = scaledPicture.data[0] + (scaledPicture.linesize[0] * (nHeight -1));
          //int flipLineSize = - scaledPicture.linesize[0];

          uint8_t *src[] = { picture.data[0], picture.data[1], picture.data[2], 0 };
          int     srcStride[] = { picture.iLineSize[0], picture.iLineSize[1], picture.iLineSize[2], 0 };
          uint8_t *dst[] = { flipData, 0, 0, 0 };
          int     dstStride[] = { flipLineSize, 0, 0, 0 };
          sws_scale(context, src, srcStride, 0, picture.iHeight, dst, dstStride);
          sws_freeContext(context);

          CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault;
          //CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, scaledPicture.data[0], scaledPicture.linesize[0]*nHeight, kCFAllocatorNull);
          CFDataRef data = CFDataCreate(kCFAllocatorDefault, scaledData, scaledLineSize * nHeight);
          CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
          CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
          cgImageRef = CGImageCreate(nWidth,
                             nHeight,
                             8,
                             24,
                             scaledLineSize,
                             colorSpace,
                             bitmapInfo,
                             provider,
                             NULL,
                             NO,
                             kCGRenderingIntentDefault);
          CGColorSpaceRelease(colorSpace);
          CGDataProviderRelease(provider);
          CFRelease(data);
        }
        av_free(scaledData);
        //avpicture_free(&scaledPicture);
      }
      else
      {
        CLog::Log(LOGDEBUG,"%s - decode failed in %s after %d packets.", __FUNCTION__, m_redactPath.c_str(), packetsTried);
      }
    }
  }

  unsigned int nTotalTime = XbmcThreads::SystemClockMillis() - nTime;
  CLog::Log(LOGDEBUG,"%s - measured %u ms to extract thumb in %d packets. ", __FUNCTION__, nTotalTime, packetsTried);
  return cgImageRef;
}
