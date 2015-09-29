/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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

#import "config.h"

#if defined(TARGET_DARWIN_IOS)
#import "DVDVideoCodecAVSampleBufferLayer.h"

#import "DVDClock.h"
#import "DVDStreamInfo.h"
#import "cores/VideoRenderers/RenderManager.h"
#import "platform/darwin/AutoPool.h"
#import "platform/darwin/DarwinUtils.h"
#import "platform/darwin/ios/MCSampleBufferLayerView.h"
#import "platform/darwin/ios/XBMCController.h"
#import "utils/BitstreamConverter.h"
#import "utils/log.h"

// helper function to create a CMSampleBufferRef from demuxer data
static CMSampleBufferRef
CreateSampleBufferFrom(CMFormatDescriptionRef fmt_desc,
  CMSampleTimingInfo *timingInfo, void *demux_buff, size_t demux_size)
{
  OSStatus status;
  CMBlockBufferRef videoBlock = nullptr;
  CMBlockBufferFlags flags = 0;
  status = CMBlockBufferCreateWithMemoryBlock(
    nullptr,          // CFAllocatorRef structureAllocator
    demux_buff,       // void *memoryBlock
    demux_size,       // size_t blockLengt
    kCFAllocatorNull, // CFAllocatorRef blockAllocator
    nullptr,          // const CMBlockBufferCustomBlockSource *customBlockSource
    0,                // size_t offsetToData
    demux_size,       // size_t dataLength
    flags,            // CMBlockBufferFlags flags
    &videoBlock);     // CMBlockBufferRef

  CMSampleBufferRef sBufOut = nullptr;
  const size_t sampleSizeArray[] = {demux_size};
  if (status == noErr)
  {
    status = CMSampleBufferCreate(
      kCFAllocatorDefault, // CFAllocatorRef allocator
      videoBlock,     // CMBlockBufferRef dataBuffer
      true,           // Boolean dataReady
      nullptr,        // CMSampleBufferMakeDataReadyCallback makeDataReadyCallback
      nullptr,        // void *makeDataReadyRefcon
      fmt_desc,       // CMFormatDescriptionRef formatDescription
      1,              // CMItemCount numSamples
      1,              // CMItemCount numSampleTimingEntries
      timingInfo,        // const CMSampleTimingInfo *sampleTimingArray
      1,              // CMItemCount numSampleSizeEntries
      sampleSizeArray,// const size_t *sampleSizeArray
      &sBufOut);      // CMSampleBufferRef *sBufOut
  }
  CFRelease(videoBlock);
  
  /*
   CLog::Log(LOGDEBUG, "%s - CreateSampleBufferFrom size %ld demux_buff [0x%08x] sBufOut [0x%08x]",
   __FUNCTION__, demux_size, (unsigned int)demux_buff, (unsigned int)sBufOut);
   */
  
  return sBufOut;
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
CDVDVideoCodecAVSampleBufferLayer::CDVDVideoCodecAVSampleBufferLayer()
: CDVDVideoCodec(), CThread("CAVSampleBufferLayerCodec")
{
  m_decoder = NULL;
  m_pFormatName = "avsb-";

  m_bitstream = NULL;
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));
  m_DropPictures = false;
}

CDVDVideoCodecAVSampleBufferLayer::~CDVDVideoCodecAVSampleBufferLayer()
{
  Dispose();
}

bool CDVDVideoCodecAVSampleBufferLayer::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  //if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEAVSBL) && !hints.software)
  {
    CCocoaAutoPool pool;

    int width  = hints.width;
    int height = hints.height;

    switch(hints.profile)
    {
      case FF_PROFILE_H264_HIGH_10:
      case FF_PROFILE_H264_HIGH_10_INTRA:
      case FF_PROFILE_H264_HIGH_422:
      case FF_PROFILE_H264_HIGH_422_INTRA:
      case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
      case FF_PROFILE_H264_HIGH_444_INTRA:
      case FF_PROFILE_H264_CAVLC_444:
        CLog::Log(LOGNOTICE, "%s - unsupported h264 profile(%d)", __FUNCTION__, hints.profile);
        return false;
        break;
    }

    if (width <= 0 || height <= 0)
    {
      CLog::Log(LOGNOTICE, "%s - bailing with bogus hints, width(%d), height(%d)",
        __FUNCTION__, width, height);
      return false;
    }

    switch (hints.codec)
    {
      case AV_CODEC_ID_H264:
        // we want avcC, not annex-b. use a bitstream converter for all flavors,
        // that way even avcC with silly 3-byte nals are covered.
        m_bitstream = new CBitstreamConverter;
        if (!m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, false))
          return false;

        m_format = 'avc1';
        m_pFormatName = "avsb-h264";
      break;
      default:
        return false;
      break;
    }

    // create a CMVideoFormatDescription from avcC extradata.
    // skip over avcC header (six bytes)
    uint8_t *spc = m_bitstream->GetExtraData() + 6;
    // length of sequence parameter set data
    uint32_t sps_size = BS_RB16(spc); spc += 2;
    // pointer to sequence parameter set data
    uint8_t *sps_ptr = spc; spc += sps_size;
    // number of picture parameter sets
    //uint32_t pps_cnt = *spc++;
    spc++;
    // length of picture parameter set data
    uint32_t pps_size = BS_RB16(spc); spc += 2;
    // pointer to picture parameter set data
    uint8_t *pps_ptr = spc;

    // bitstream converter avcC's always have 4 byte NALs.
    int NALUnitHeaderLength  = 4;
    size_t parameterSetCount = 2;
    const uint8_t* const parameterSetPointers[2] = {
      (const uint8_t*)sps_ptr,
      (const uint8_t*)pps_ptr
    };
    const size_t parameterSetSizes[2] = {
      sps_size,
      pps_size
    };
    CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
     parameterSetCount, parameterSetPointers, parameterSetSizes, NALUnitHeaderLength, &m_fmt_desc);

		CGRect bounds = CGRectMake(0, 0, width, height);
    // MCPlayerView create MUST be done on main thread or
    // it will not get updates when a new video frame is decoded and presented.
    __block MCSampleBufferLayerView *mcview = nullptr;
    dispatch_sync(dispatch_get_main_queue(),^{
        mcview = [[MCSampleBufferLayerView alloc] initWithFrame:bounds];
    });
    [g_xbmcController insertVideoView:mcview];

    m_width = width;
    m_height = height;
    m_decoder = mcview;

    m_DropPictures = false;

    Create();

    return true;
  }

  return false;
}

void CDVDVideoCodecAVSampleBufferLayer::Dispose()
{
  StopThread();

  if (m_decoder)
  {
    CCocoaAutoPool pool;
    MCSampleBufferLayerView *mcview = (MCSampleBufferLayerView*)m_decoder;
    [g_xbmcController removeVideoView:mcview];
    dispatch_sync(dispatch_get_main_queue(),^{
      [mcview release];
    });
    m_decoder = nullptr;
  }

  if (m_bitstream)
    delete m_bitstream, m_bitstream = NULL;
}

void CDVDVideoCodecAVSampleBufferLayer::SetDropState(bool bDrop)
{
  m_DropPictures = bDrop;
}

int CDVDVideoCodecAVSampleBufferLayer::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  if (!pData)
    return VC_BUFFER;

  if (pData)
  {
    CCocoaAutoPool pool;

    m_bitstream->Convert(pData, iSize);
    int frameSize = m_bitstream->GetConvertSize();
    uint8_t *frame = m_bitstream->GetConvertBuffer();

    CMSampleTimingInfo sampleTimingInfo = kCMTimingInfoInvalid;
    if (dts != DVD_NOPTS_VALUE)
      sampleTimingInfo.decodeTimeStamp = CMTimeMake(dts, DVD_TIME_BASE);
    if (pts != DVD_NOPTS_VALUE)
      sampleTimingInfo.presentationTimeStamp = CMTimeMake(pts, DVD_TIME_BASE);
    //CLog::Log(LOGNOTICE, "%s - decodeTimeStamp, pts(%f), time_s(%f)",
    //   __FUNCTION__, pts, CMTimeGetSeconds(sampleTimingInfo.presentationTimeStamp));

    CMSampleBufferRef sampleBuffer = CreateSampleBufferFrom(m_fmt_desc, &sampleTimingInfo, frame, frameSize);

    // Enqueue sample buffers which will be displayed at their above set presentationTimeStamp
    MCSampleBufferLayerView *mcview = (MCSampleBufferLayerView*)m_decoder;
    if (mcview)
    {
      if (mcview.videoLayer.readyForMoreMediaData)
      {
        // MUST be done on main thread or videoLayer will not get updates
        // it will not get updates when a new video frame is decoded and presented.
        dispatch_sync(dispatch_get_main_queue(),^{
            [mcview.videoLayer enqueueSampleBuffer:sampleBuffer];
        });

        if ([mcview.videoLayer status] == AVQueuedSampleBufferRenderingStatusFailed)
        {
          CLog::Log(LOGNOTICE, "%s - AFVDecoderDecode failed, status(%ld)",
            __FUNCTION__, (long)[mcview.videoLayer error].code);
        }
      }
      else
      {
        CLog::Log(LOGNOTICE, "%s - not readyForMoreMediaData, status(%d)",
         __FUNCTION__, (int)[mcview.videoLayer status]);
      }
      CFRelease(sampleBuffer);
    }

    m_dts = dts;
    m_pts = pts;
    //usleep(5 * 1000.0);
  }

  return VC_PICTURE | VC_BUFFER;
}

void CDVDVideoCodecAVSampleBufferLayer::Reset(void)
{
  CCocoaAutoPool pool;

	// Flush the previous enqueued sample buffers for display while scrubbing
  MCSampleBufferLayerView *mcview = (MCSampleBufferLayerView*)m_decoder;
  dispatch_sync(dispatch_get_main_queue(),^{
    [mcview.videoLayer flush];
  });
}

bool CDVDVideoCodecAVSampleBufferLayer::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  pDvdVideoPicture->dts             = m_dts;
  pDvdVideoPicture->pts             = m_pts;
  pDvdVideoPicture->iDuration       = 0.0;
  pDvdVideoPicture->format          = RENDER_FMT_BYPASS;
  pDvdVideoPicture->iFlags          = DVP_FLAG_ALLOCATED;
  pDvdVideoPicture->color_range     = 0;
  pDvdVideoPicture->color_matrix    = 4;
  pDvdVideoPicture->iWidth          = m_width;
  pDvdVideoPicture->iHeight         = m_height;
  pDvdVideoPicture->iDisplayWidth   = pDvdVideoPicture->iWidth;
  pDvdVideoPicture->iDisplayHeight  = pDvdVideoPicture->iHeight;

  return true;
}

bool CDVDVideoCodecAVSampleBufferLayer::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  // release any previous retained image buffer ref that
  // has not been passed up to renderer (ie. dropped frames, etc).
  if (pDvdVideoPicture->cvBufferRef)
    CVBufferRelease(pDvdVideoPicture->cvBufferRef);

  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

void CDVDVideoCodecAVSampleBufferLayer::Process()
{
  CLog::Log(LOGDEBUG, "CAVSampleBufferLayerCodec::Process Started");

  CRect oldSrcRect, oldDestRect, oldViewRect;
  // bump our priority to be level with the krAEken (ActiveAE)
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);
  while (!m_bStop)
  {
    MCSampleBufferLayerView *mcview = (MCSampleBufferLayerView*)m_decoder;
    if (mcview)
    {
      CMTime cmtime  = CMTimebaseGetTime(mcview.videoLayer.controlTimebase);
      Float64 curtime_s = CMTimeGetSeconds(cmtime);
      //CLog::Log(LOGNOTICE, "%s - mcview.videoLayer.controlTimebase1, time_s(%f)", __FUNCTION__, curtime_s);

      double player_s = GetPlayerPtsSeconds() + 0.1;
      if (fabs(curtime_s - player_s) > 0.125)
      {
        CMTimebaseSetTime(mcview.videoLayer.controlTimebase, CMTimeMake(player_s, 1));

        CMTime cmtime  = CMTimebaseGetTime(mcview.videoLayer.controlTimebase);
        Float64 newtime_s = CMTimeGetSeconds(cmtime);
        CLog::Log(LOGNOTICE, "%s - mcview.videoLayer.controlTimebase2, curtime_s(%f) newtime_s(%f)", __FUNCTION__, curtime_s, newtime_s);
      }

      // update where we show the video in the view/layer
      CRect SrcRect, DestRect, ViewRect;
      g_renderManager.GetVideoRect(SrcRect, DestRect, ViewRect);
      // on startup, DestRect will be emptry until renderer inits,
      // after that, we only need to update if something changes.
      if (!DestRect.IsEmpty() &&
        (SrcRect != oldSrcRect || DestRect != oldDestRect || ViewRect != oldViewRect))
      {
        // fixme: handle device rotate
        dispatch_async(dispatch_get_main_queue(),^{
          CGRect frame = CGRectMake(
            DestRect.x1, DestRect.y1, DestRect.Width(), DestRect.Height());
          // save the offset
          CGPoint offset = frame.origin;
          // transform to zero x/y origin
          frame = CGRectOffset(frame, -frame.origin.x, -frame.origin.y);
          mcview.frame = frame;
          mcview.center= CGPointMake(mcview.center.x + offset.x, mcview.center.y + offset.y);
          // video layer needs to get resized too.
          mcview.videoLayer.frame = frame;
          if (mcview.hidden == YES)
            [mcview setHiddenAnimated:NO delay:NSTimeInterval(0.1) duration:NSTimeInterval(2.0)];
        });
      }
    }

    Sleep(250);
  }

  SetPriority(THREAD_PRIORITY_NORMAL);
  CLog::Log(LOGDEBUG, "CAVSampleBufferLayerCodec::Process Stopped");
}

double CDVDVideoCodecAVSampleBufferLayer::GetPlayerPtsSeconds()
{
  double clock_pts = 0.0;
  CDVDClock *playerclock = CDVDClock::GetMasterClock();
  if (playerclock)
    clock_pts = playerclock->GetClock() / DVD_TIME_BASE;

  return clock_pts;
}

#endif
