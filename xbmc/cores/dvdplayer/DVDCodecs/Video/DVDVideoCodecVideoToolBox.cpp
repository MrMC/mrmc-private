/*
 *      Copyright (C) 2010-2017 Team XBMC
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

#include "DVDVideoCodecVideoToolBox.h"

#include "cores/dvdplayer/DVDClock.h"
#include "platform/darwin/DictionaryUtils.h"
#include "platform/darwin/DarwinUtils.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "windowing/WindowingFactory.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"

extern "C" {
#include "libavformat/avformat.h"
}

#if !defined(TARGET_DARWIN_OSX)
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
static int CheckNP2( unsigned x )
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return ++x;
}
#endif

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
// helper function to create a CMSampleBufferRef from demuxer data
static CMSampleBufferRef
CreateSampleBufferFrom(CMFormatDescriptionRef fmt_desc,
  CMSampleTimingInfo &timingInfo, void *demux_buff, size_t demux_size)
{
  // need to retain the demux data until decoder is done with it.
  // the best way to do this is malloc/memcpy and use a kCFAllocatorMalloc.
  size_t demuxSize = demux_size;
  uint8_t *demuxData = (uint8_t*)malloc(demuxSize);
  memcpy(demuxData, demux_buff, demuxSize);

  CMBlockBufferRef videoBlock = nullptr;
  CMBlockBufferFlags flags = 0;
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
    kCFAllocatorDefault,  // CFAllocatorRef structureAllocator
    demuxData,            // void *memoryBlock
    demuxSize,            // size_t blockLength
    kCFAllocatorMalloc,   // CFAllocatorRef blockAllocator
    nullptr,              // const CMBlockBufferCustomBlockSource *customBlockSource
    0,                    // size_t offsetToData
    demux_size,           // size_t dataLength
    flags,                // CMBlockBufferFlags flags
    &videoBlock);         // CMBlockBufferRef

  CMSampleBufferRef sBufOut = nullptr;
  const size_t sampleSizeArray[] = {demuxSize};
  const CMSampleTimingInfo timingInfoArray[] = {timingInfo};

  if (status == noErr)
  {
    status = CMSampleBufferCreate(
      kCFAllocatorDefault,// CFAllocatorRef allocator
      videoBlock,         // CMBlockBufferRef dataBuffer
      true,               // Boolean dataReady
      nullptr,            // CMSampleBufferMakeDataReadyCallback makeDataReadyCallback
      nullptr,            // void *makeDataReadyRefcon
      fmt_desc,           // CMFormatDescriptionRef formatDescription
      1,                  // CMItemCount numSamples
      1,                  // CMItemCount numSampleTimingEntries
      timingInfoArray,    // const CMSampleTimingInfo *sampleTimingArray
      1,                  // CMItemCount numSampleSizeEntries
      sampleSizeArray,    // const size_t *sampleSizeArray
      &sBufOut);          // CMSampleBufferRef *sBufOut
  }
  CFRelease(videoBlock);

  return sBufOut;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CDVDVideoCodecVideoToolBox::CDVDVideoCodecVideoToolBox() : CDVDVideoCodec()
{
  m_fmt_desc    = nullptr;
  m_bitstream   = nullptr;
  m_vt_session  = nullptr;
  m_pFormatName = "vtb";

  m_queue_depth = 0;
  m_display_queue = nullptr;
  m_max_ref_frames = 4;
  pthread_mutex_init(&m_queue_mutex, NULL);

  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));
  m_DropPictures = false;
  m_codecControlFlags = 0;
  m_started = false;
  m_lastKeyframe = 0;
  m_sessionRestart = false;
  m_sessionRestartPTS = DVD_NOPTS_VALUE;
  m_enable_temporal_processing = false;
}

CDVDVideoCodecVideoToolBox::~CDVDVideoCodecVideoToolBox()
{
  Dispose();
  pthread_mutex_destroy(&m_queue_mutex);
}

bool CDVDVideoCodecVideoToolBox::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEVIDEOTOOLBOX) && !hints.software)
  {
    // some pvr livetv hole is bypassing normal hints.maybe_interlaced setup
    if (hints.codec == AV_CODEC_ID_H264)
    {
      CBitstreamConverter bs;
      // CBitstreamConverter might alter extradata, changing to 4 byte NALs
      // for avcC if 2 or 3 byte NALs are detected, save a copy to restore.
      uint8_t *saved_extradata[hints.extrasize];
      memcpy(saved_extradata, hints.extradata, hints.extrasize);

      if (!bs.Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, false))
        return false;

      CFDataRef avcCData = CFDataCreate(kCFAllocatorDefault,
        (const uint8_t*)bs.GetExtraData(), bs.GetExtraSize());
      bool interlaced = true;
      int max_ref_frames;
      uint8_t *spc = (uint8_t*)CFDataGetBytePtr(avcCData) + 6;
      uint32_t sps_size = BS_RB16(spc);
      if (sps_size)
        bs.parseh264_sps(spc+3, sps_size-1, &interlaced, &max_ref_frames);
      CFRelease(avcCData);

      // restore original extradata contents
      memcpy(hints.extradata, saved_extradata, hints.extrasize);

      // if maybe_interlaced is already set ( > 0 )
      // assume who set it is smarter than us :)
      // and ignore parseh264 results for interlaced
      if (hints.maybe_interlaced == -1 && interlaced)
        hints.maybe_interlaced = 1;
    }

    if (hints.maybe_interlaced > 0)
    {
      CLog::Log(LOGNOTICE, "%s - interlaced content.", __FUNCTION__);
      return false;
    }

    m_hints = hints;
    m_options = options;

    switch(m_hints.profile)
    {
      //case FF_PROFILE_H264_HIGH_10:
      case FF_PROFILE_H264_HIGH_10_INTRA:
      case FF_PROFILE_H264_HIGH_422:
      case FF_PROFILE_H264_HIGH_422_INTRA:
      case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
      case FF_PROFILE_H264_HIGH_444_INTRA:
      case FF_PROFILE_H264_CAVLC_444:
        CLog::Log(LOGNOTICE, "%s - unsupported h264 profile(%d)", __FUNCTION__, m_hints.profile);
        return false;
        break;
    }

    if (m_hints.width <= 0 || m_hints.height <= 0)
    {
      CLog::Log(LOGNOTICE, "%s - bailing with bogus hints, width(%d), height(%d)",
        __FUNCTION__, m_hints.width, m_hints.height);
      return false;
    }

    switch (m_hints.codec)
    {
      default:
      case AV_CODEC_ID_MPEG4:
      case AV_CODEC_ID_MPEG2VIDEO:
        return false;
      break;

      case AV_CODEC_ID_H264:
        if (m_hints.extrasize < 7 || m_hints.extradata == NULL)
        {
          CLog::Log(LOGNOTICE, "%s - avcC atom too data small or missing", __FUNCTION__);
          return false;
        }
        else
        {
          // use a bitstream converter for all flavors, that way
          // even avcC with 2-byte or silly 3-byte nals are covered.
          m_bitstream = new CBitstreamConverter;
          if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, false))
          {
            SAFE_DELETE(m_bitstream);
            return false;
          }

          if (m_bitstream->GetExtraSize() < 8)
          {
            SAFE_DELETE(m_bitstream);
            return false;
          }
          else
          {
            // check the avcC atom's sps for number of reference frames
            // ignore if interlaced, it's handled in hints check above (until we get it working :)
            bool interlaced = true;
            uint8_t *spc = m_bitstream->GetExtraData() + 6;
            uint32_t sps_size = BS_RB16(spc);
            if (sps_size)
              m_bitstream->parseh264_sps(spc+3, sps_size-1, &interlaced, &m_max_ref_frames);
          }
          if (m_max_ref_frames == 0)
            m_max_ref_frames = 2;

          if (m_hints.profile == FF_PROFILE_H264_MAIN && m_hints.level == 32 && m_max_ref_frames > 4)
          {
            // Main@L3.2, VTB cannot handle greater than 4 reference frames
            CLog::Log(LOGNOTICE, "%s - Main@L3.2 detected, VTB cannot decode.", __FUNCTION__);
            SAFE_DELETE(m_bitstream);
            return false;
          }
        }
        m_pFormatName = "vtb-h264";
      break;

      case AV_CODEC_ID_HEVC:
        if (!CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEVIDEOTOOLBOX_HEVC_HDR) &&
             (hints.profile == FF_PROFILE_HEVC_MAIN_10 ||
              hints.profile == FF_PROFILE_HEVC_REXT ||
              hints.codec_tag == MKTAG('d','v','h','1') ||
              hints.codec_tag == MKTAG('d','v','h','e') ||
              hints.codec_tag == MKTAG('D','O','V','I')))
        {
          // if bt2020+, kick it to AVFoundataion
          if (hints.colortransfer >= AVCOL_TRC_SMPTE2084)
            return false;
          // only AVFoundataion can handle DolbyVision
          if (hints.codec_tag == MKTAG('d','v','h','1') ||
              hints.codec_tag == MKTAG('d','v','h','e') ||
              hints.codec_tag == MKTAG('D','O','V','I'))
            return false;
        }
         if (m_hints.extrasize < 23 || m_hints.extradata == NULL)
        {
          CLog::Log(LOGNOTICE, "%s - hvcC atom too data small or missing", __FUNCTION__);
          return false;
        }
        else
        {
         // use a bitstream converter for all flavors
          m_bitstream = new CBitstreamConverter;
          if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, false))
          {
            SAFE_DELETE(m_bitstream);
            return false;
          }
          if (m_bitstream->GetExtraSize() < 23)
          {
            SAFE_DELETE(m_bitstream);
            return false;
          }
          // videotoolbox wants hvc1.
          // check for hev1, it might have in-stream
          // sps/pps/vps and we need to defer the open until
          // these are extracted from stream during decode.
          if (m_bitstream->GetExtraSize() == 23)
            m_hev1Format = true;
          m_pFormatName = "vtb-h265";
        }
      break;
    }

    CDarwinVideoUtils::FreeParameterSets(m_parameterSets);
    if (!m_hev1Format)
    {
      if (!CDarwinVideoUtils::CreateParameterSetArraysFromExtraData(
           m_parameterSets, m_hints.codec, m_bitstream->GetExtraData()))
      {
        Dispose();
        return false;
      }

      if (!CreateFormatDescriptorFromParameterSetArrays())
      {
        Dispose();
        return false;
      }

      if (!CreateVTSessionAndInitPictureFrame())
      {
        Dispose();
        return false;
      }
    }
    m_DropPictures = false;
    // default to 5 min, this helps us feed correct pts to the player.
    m_max_ref_frames = std::max(m_max_ref_frames + 1, 5);
    // some VUI bitstream restrictions lie (GoPro mp4)
    m_max_ref_frames += 4;
    // don't grow past 16
    m_max_ref_frames = m_max_ref_frames > 16 ? 16 : m_max_ref_frames;

    CLog::Log(LOGDEBUG,"VideoToolBox: opened width(%d), height(%d)", m_hints.width, m_hints.height);
    return true;
  }

  return false;
}

void CDVDVideoCodecVideoToolBox::Dispose()
{
  DestroyVTSession();
  if (m_fmt_desc)
    CFRelease(m_fmt_desc), m_fmt_desc = nullptr;
  SAFE_DELETE(m_bitstream);
  CDarwinVideoUtils::FreeParameterSets(m_parameterSets);

  if (m_videobuffer.iFlags & DVP_FLAG_ALLOCATED)
  {
    // release any previous retained cvbuffer reference
    if (m_videobuffer.cvBufferRef)
      CVBufferRelease(m_videobuffer.cvBufferRef);
    m_videobuffer.cvBufferRef = nullptr;
    m_videobuffer.iFlags = 0;
  }

  while (m_queue_depth)
    DisplayQueuePop();
}

void CDVDVideoCodecVideoToolBox::Reopen()
{
  m_started = false;
  Dispose();
  Open(m_hints, m_options);
}

void CDVDVideoCodecVideoToolBox::SetDropState(bool bDrop)
{
  // more a message to decoder to hurry up.
  // VideoToolBox has no such ability so ignore it.
  m_DropPictures = bDrop;
}

int CDVDVideoCodecVideoToolBox::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  if (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN)
  {
    if (m_queue_depth > 0)
      return VC_PICTURE;
    else
      return VC_BUFFER;
  }

  if (pData)
  {
    if (m_bitstream)
    {
      if (m_bitstream->Convert(pData, iSize))
      {
        iSize = m_bitstream->GetConvertSize();
        pData = m_bitstream->GetConvertBuffer();
      }
    }
    ValidateVTSessionParameterSetsForRestart(pData, iSize);
    if (!m_vt_session)
      return VC_BUFFER;

    if (CBitstreamParser::HasKeyframe(m_hints.codec, pData, iSize, false))
    {
      // VideoToolBox is picky about starting up with a keyframe
      // Check and skip until we hit one. m_lastKeyframe tracks how many frames back
      // was the last one. It is used during reset and reopen.
      //CLog::Log(LOGDEBUG, "%s - Keyframe found, m_lastKeyframe %d", __FUNCTION__, m_lastKeyframe);
      m_started = true;
      m_lastKeyframe = 0;
    }
    m_lastKeyframe++;

    if (!m_started && m_lastKeyframe < 64)
      return VC_BUFFER;

    CMSampleTimingInfo sampleTimingInfo = kCMTimingInfoInvalid;
    if (dts != DVD_NOPTS_VALUE)
      sampleTimingInfo.decodeTimeStamp = CMTimeMake(dts, DVD_TIME_BASE);
    if (pts != DVD_NOPTS_VALUE)
      sampleTimingInfo.presentationTimeStamp = CMTimeMake(pts, DVD_TIME_BASE);

    CMSampleBufferRef sampleBuff = CreateSampleBufferFrom(m_fmt_desc, sampleTimingInfo, pData, iSize);
    if (!sampleBuff)
    {
      CLog::Log(LOGNOTICE, "%s - CreateSampleBufferFrom failed", __FUNCTION__);
      return VC_ERROR;
    }

    VTDecodeFrameFlags decoderFlags = 0;
    if (m_enable_temporal_processing)
      decoderFlags |= kVTDecodeFrame_EnableTemporalProcessing;

    OSStatus status = VTDecompressionSessionDecodeFrame((VTDecompressionSessionRef)m_vt_session, sampleBuff, decoderFlags, nullptr, nullptr);
    if (status != noErr)
    {
      CFRelease(sampleBuff);
      // might not really be an error (could have been force inactive) so do not log it.
      if (status == kVTInvalidSessionErr)
      {
        m_sessionRestart = true;
        m_sessionRestartPTS = pts;
        if (m_display_queue)
        {
          m_sessionRestartPTS = m_display_queue->pts;
        }
        return VC_REOPEN;
      }

      if (status == kVTVideoDecoderMalfunctionErr)
      {
        CLog::Log(LOGNOTICE, "%s - VTDecompressionSessionDecodeFrame returned kVTVideoDecoderMalfunctionErr", __FUNCTION__);
        return VC_SWFALLBACK;
      }
      else
      {
        // VTDecompressionSessionDecodeFrame returned 8969 (codecBadDataErr)
        // VTDecompressionSessionDecodeFrame returned -12350
        // VTDecompressionSessionDecodeFrame returned -12902 (kVTParameterErr)
        // VTDecompressionSessionDecodeFrame returned -12903 (kVTInvalidSessionErr)
        // VTDecompressionSessionDecodeFrame returned -12909 (kVTVideoDecoderBadDataErr)
        // VTDecompressionSessionDecodeFrame returned -12911 (kVTVideoDecoderMalfunctionErr)
        CLog::Log(LOGNOTICE, "%s - VTDecompressionSessionDecodeFrame returned(%d)", __FUNCTION__, (int)status);
        return VC_ERROR;
      }
    }

/*
    // wait for decoding to finish
    status = VTDecompressionSessionWaitForAsynchronousFrames((VTDecompressionSessionRef)m_vt_session);
    if (status != kVTDecoderNoErr)
    {
      CLog::Log(LOGNOTICE, "%s - VTDecompressionSessionWaitForAsynchronousFrames returned(%d)",
        __FUNCTION__, (int)status);
      CFRelease(sampleBuff);
      return VC_ERROR;
    }
*/
    CFRelease(sampleBuff);

    // put a limit on convergence count to avoid
    // huge mem usage on streams without keyframes
    if (m_lastKeyframe > 300)
    {
      CLog::Log(LOGNOTICE, "%s - m_lastKeyframe (%i) clamped ", __FUNCTION__, m_lastKeyframe);
      m_lastKeyframe = 300;
    }
  }

  if (m_queue_depth < (2 * m_max_ref_frames))
    return VC_BUFFER;

  return VC_PICTURE;
}

void CDVDVideoCodecVideoToolBox::Reset(void)
{
  // flush decoder
  VTDecompressionSessionWaitForAsynchronousFrames((VTDecompressionSessionRef)m_vt_session);

  while (m_queue_depth)
    DisplayQueuePop();

  m_codecControlFlags = 0;
}

unsigned CDVDVideoCodecVideoToolBox::GetConvergeCount()
{
  return m_lastKeyframe;
}

unsigned CDVDVideoCodecVideoToolBox::GetAllowedReferences()
{
  return 5;
}

void CDVDVideoCodecVideoToolBox::SetCodecControl(int flags)
{
  m_codecControlFlags = flags;
}

bool CDVDVideoCodecVideoToolBox::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  // clone the video picture buffer settings.
  *pDvdVideoPicture = m_videobuffer;

  // get the top picture frame, we risk getting the wrong frame if the frame queue
  // depth is less than the number of encoded reference frames. If queue depth
  // is greater than the number of encoded reference frames, then the top frame
  // will never change and we can just grab a ref to the top frame.
  pthread_mutex_lock(&m_queue_mutex);
  pDvdVideoPicture->dts             = DVD_NOPTS_VALUE;
  pDvdVideoPicture->pts             = m_display_queue->pts;
  pDvdVideoPicture->iFlags          = DVP_FLAG_ALLOCATED;
  pDvdVideoPicture->iWidth          = (unsigned int)m_display_queue->width;
  pDvdVideoPicture->iHeight         = (unsigned int)m_display_queue->height;
  pDvdVideoPicture->iDisplayWidth   = (unsigned int)m_display_queue->width;
  pDvdVideoPicture->iDisplayHeight  = (unsigned int)m_display_queue->height;
  if (m_hints.aspect > 1.0 && !m_hints.forced_aspect)
  {
    pDvdVideoPicture->iDisplayWidth  = ((int)lrint(pDvdVideoPicture->iHeight * m_hints.aspect)) & ~3;
    if (pDvdVideoPicture->iDisplayWidth > pDvdVideoPicture->iWidth)
    {
      pDvdVideoPicture->iDisplayWidth  = pDvdVideoPicture->iWidth;
      pDvdVideoPicture->iDisplayHeight = ((int)lrint(pDvdVideoPicture->iWidth / m_hints.aspect)) & ~3;
    }
  }
  if (m_hints.colorrange == AVCOL_RANGE_JPEG)
    pDvdVideoPicture->color_range = 1;
  else
    pDvdVideoPicture->color_range = 0;
  pDvdVideoPicture->color_matrix    = m_hints.colorspace;
  pDvdVideoPicture->color_transfer  = m_hints.colortransfer;
  pDvdVideoPicture->cvBufferRef     = m_display_queue->pixel_buffer_ref;
  m_display_queue->pixel_buffer_ref = nullptr;

  // now we can pop the top frame
  frame_queue *top_frame = m_display_queue;
  m_display_queue = m_display_queue->nextframe;
  m_queue_depth--;
  free(top_frame);

  pthread_mutex_unlock(&m_queue_mutex);

  static double old_pts;
  if (g_advancedSettings.CanLogComponent(LOGVIDEO) && pDvdVideoPicture->pts < old_pts)
    CLog::Log(LOGDEBUG, "%s - VTBDecoderDecode dts(%f), pts(%f), old_pts(%f)", __FUNCTION__,
      pDvdVideoPicture->dts, pDvdVideoPicture->pts, old_pts);
  old_pts = pDvdVideoPicture->pts;

//  CLog::Log(LOGDEBUG, "%s - VTBDecoderDecode dts(%f), pts(%f), cvBufferRef(%p)", __FUNCTION__,
//    pDvdVideoPicture->dts, pDvdVideoPicture->pts, pDvdVideoPicture->cvBufferRef);

  if (m_codecControlFlags & DVD_CODEC_CTRL_DROP)
    pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;

  // if vtb session restarts, we start decoding at last IDR frame
  // but dvdplay/renderer will show frames in fast forward style
  // until we hit sync point. Visually anoying so we force those
  // frames to get dropped and not shown.
  if (m_sessionRestart)
  {
    if (m_sessionRestartPTS == pDvdVideoPicture->pts)
      m_sessionRestart = false;
    else
      pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;
  }

  return true;
}

bool CDVDVideoCodecVideoToolBox::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  // release any previous retained image buffer ref that
  // has not been passed up to renderer (ie. dropped frames, etc).
  if (pDvdVideoPicture->cvBufferRef)
    CVBufferRelease(pDvdVideoPicture->cvBufferRef);

  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

void CDVDVideoCodecVideoToolBox::DisplayQueuePop(void)
{
  if (!m_display_queue || m_queue_depth == 0)
    return;

  // pop the top frame off the queue
  pthread_mutex_lock(&m_queue_mutex);
  frame_queue *top_frame = m_display_queue;
  m_display_queue = m_display_queue->nextframe;
  m_queue_depth--;
  pthread_mutex_unlock(&m_queue_mutex);

  // and release it
  if (top_frame->pixel_buffer_ref)
    CVBufferRelease(top_frame->pixel_buffer_ref);
  free(top_frame);
}

bool CDVDVideoCodecVideoToolBox::CreateFormatDescriptorFromParameterSetArrays()
{
  m_fmt_desc = CDarwinVideoUtils::CreateFormatDescriptorFromParameterSetArrays(
    m_parameterSets, m_hints.codec);
  if (m_fmt_desc == nullptr)
    return false;

  const Boolean useCleanAperture = true;
  const Boolean usePixelAspectRatio = false;
  auto videoSize = CMVideoFormatDescriptionGetPresentationDimensions(m_fmt_desc, usePixelAspectRatio, useCleanAperture);

  m_hints.width = videoSize.width;
  m_hints.height = videoSize.height;
  return true;
}

void
CDVDVideoCodecVideoToolBox::ValidateVTSessionParameterSetsForRestart(uint8_t *pData, int iSize)
{
  // temp bypass of hevc (only if we are hvc1)
  // for hev1, sps/pps/vps are in stream, not it extradata.
  // so we have defered codec open until we can pull them out
  // and package for code open.
  if (!m_hev1Format && m_hints.codec == AV_CODEC_ID_HEVC)
    return;

  VideoParameterSets parameterSets;
  if (!CDarwinVideoUtils::ParsePacketForVideoParameterSets(parameterSets, m_hints.codec, pData, iSize))
      return;

  bool wasReset = false;
  // quick test of parameter set count
  if (parameterSets.sps_count != m_parameterSets.sps_count ||
      parameterSets.pps_count != m_parameterSets.pps_count ||
      parameterSets.vps_count != m_parameterSets.vps_count)
  {
    ResetVTSession(parameterSets);
    wasReset = true;
  }
  else
  {
    for (size_t i = 0; i < parameterSets.sps_count; i++)
    {
      if (parameterSets.sps_sizes[i] != m_parameterSets.sps_sizes[i])
      {
        //if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        {
          CLog::Log(LOGDEBUG, "%s - sps changed size(%zu), orignal size(%zu)", __FUNCTION__, parameterSets.sps_sizes[i], m_parameterSets.sps_sizes[i]);
        }
        ResetVTSession(parameterSets);
        wasReset = true;
        break;
      }
      if (memcmp(parameterSets.sps_array[i], m_parameterSets.sps_array[i], parameterSets.sps_sizes[i]) != 0)
      {
        // some parameter size changed, recreate the vtsession
        //if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        {
          CLog::Log(LOGDEBUG, "%s - sps of size(%zu) contents differ", __FUNCTION__, parameterSets.sps_sizes[i]);
          CLog::MemDump((char*)parameterSets.sps_array[i], parameterSets.sps_sizes[i]);
          CLog::MemDump((char*)m_parameterSets.sps_array[i], m_parameterSets.sps_sizes[i]);
        }
        ResetVTSession(parameterSets);
        wasReset = true;
        break;
      }
    }

  if (!wasReset)
    CDarwinVideoUtils::FreeParameterSets(parameterSets);
  }
}

bool
CDVDVideoCodecVideoToolBox::ResetVTSession(VideoParameterSets &parameterSets)
{
  CDarwinVideoUtils::FreeParameterSets(m_parameterSets);
  if (m_fmt_desc)
    CFRelease(m_fmt_desc), m_fmt_desc = nullptr;

  m_parameterSets = parameterSets;
  if (!CreateFormatDescriptorFromParameterSetArrays())
    return false;

  DestroyVTSession();
  if (!CreateVTSessionAndInitPictureFrame())
    return false;

  CLog::Log(LOGDEBUG, "%s - width(%d), height(%d)", __FUNCTION__, m_hints.width, m_hints.height);
  return true;
}

bool
CDVDVideoCodecVideoToolBox::CreateVTSessionAndInitPictureFrame()
{
  int width = m_hints.width;
  int height = m_hints.height;
#if defined(TARGET_DARWIN_TVOS)
  if (!CDarwinUtils::IsAppleTV4KOrAbove())
  {
    // decoding, scaling and rendering 4k h264 runs into
    // some bandwidth limit. detect and scale down to reduce
    // the bandwidth requirements.
    int width_clamp = 1920;
    int new_width = CheckNP2(width);
    if (width != new_width)
    {
      // force picture width to power of two and scale up height
      // we do this because no GL_UNPACK_ROW_LENGTH in OpenGLES
      // and the CVPixelBufferPixel gets created using some
      // strange alignment when width is non-standard.
      double w_scaler = (double)new_width / width;
      width = new_width;
      height = height * w_scaler;
    }
    // scale output pictures down to 1080p size for display
    if (width > width_clamp)
    {
      double w_scaler = (float)width_clamp / width;
      width = width_clamp;
      height = height * w_scaler;
    }
  }
#elif defined(TARGET_DARWIN_IOS)
  double scale = 0.0;

  // decoding, scaling and rendering above 1920 x 800 runs into
  // some bandwidth limit. detect and scale down to reduce
  // the bandwidth requirements.
  int width_clamp = 1280;
  if ((width * height) > (1920 * 800))
    width_clamp = 960;

  // for retina devices it should be safe [tm] to
  // loosen the clamp a bit to 1280 pixels width
  if (CDarwinUtils::DeviceHasRetina(scale))
    width_clamp = 1280;

  int new_width = CheckNP2(width);
  if (width != new_width)
  {
    // force picture width to power of two and scale up height
    // we do this because no GL_UNPACK_ROW_LENGTH in OpenGLES
    // and the CVPixelBufferPixel gets created using some
    // strange alignment when width is non-standard.
    double w_scaler = (double)new_width / width;
    width = new_width;
    height = height * w_scaler;
  }
#if !defined(__LP64__)
  // scale output pictures down to 720p size for display
  if (width > width_clamp)
  {
    double w_scaler = (float)width_clamp / width;
    width = width_clamp;
    height = height * w_scaler;
  }
#endif
#endif
  CFMutableDictionaryRef destinationPixelBufferAttributes = CFDictionaryCreateMutable(
    nullptr, // CFAllocatorRef allocator
    0,    // CFIndex capacity
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks);

#if defined(TARGET_DARWIN_OSX)
  CFDictionarySetSInt32(destinationPixelBufferAttributes,
    kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_422YpCbCr8);
#else
  if (m_hints.colorrange == AVCOL_RANGE_JPEG)
    CFDictionarySetSInt32(destinationPixelBufferAttributes,
      kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);
  else
    CFDictionarySetSInt32(destinationPixelBufferAttributes,
      kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
#endif

  CFDictionarySetSInt32(destinationPixelBufferAttributes,
    kCVPixelBufferWidthKey, width);
  CFDictionarySetSInt32(destinationPixelBufferAttributes,
    kCVPixelBufferHeightKey, height);
  CFDictionarySetValue(destinationPixelBufferAttributes,
    kCVPixelBufferIOSurfacePropertiesKey, kCFBooleanTrue);
#if defined(TARGET_DARWIN_OSX)
  CFDictionarySetValue(destinationPixelBufferAttributes,
    kCVPixelBufferOpenGLTextureCacheCompatibilityKey, kCFBooleanTrue);
#endif
#if defined(TARGET_DARWIN_IOS)
  CFDictionarySetValue(destinationPixelBufferAttributes,
    kCVPixelBufferOpenGLESCompatibilityKey, kCFBooleanTrue);
  CFDictionarySetValue(destinationPixelBufferAttributes,
    kCVPixelBufferOpenGLESTextureCacheCompatibilityKey, kCFBooleanTrue);
#endif
  CFMutableDictionaryRef io_surface_properties = CFDictionaryCreateMutable(kCFAllocatorDefault,
    0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(destinationPixelBufferAttributes, kCVPixelBufferIOSurfacePropertiesKey, io_surface_properties);

  VTDecompressionOutputCallbackRecord outputCallback;
  outputCallback.decompressionOutputCallback = VTDecoderCallback;
  outputCallback.decompressionOutputRefCon = this;

  VTDecompressionSessionRef vt_session = nullptr;
  OSStatus status = VTDecompressionSessionCreate(
    nullptr, // CFAllocatorRef allocator
    m_fmt_desc,
    nullptr, // CFTypeRef sessionOptions
    destinationPixelBufferAttributes,
    &outputCallback,
    &vt_session);
  if (status != noErr)
  {
    m_vt_session = nullptr;
    CLog::Log(LOGERROR, "%s - failed with status = (%d)", __FUNCTION__, (int)status);
    // -12906, kVTCouldNotFindVideoDecoderErr
  }
  else
  {
    m_vt_session = (void*)vt_session;
  }

  CFRelease(io_surface_properties);
  CFRelease(destinationPixelBufferAttributes);

  // setup a DVDVideoPicture buffer.
  // first make sure all properties are reset.
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));

  m_videobuffer.dts = DVD_NOPTS_VALUE;
  m_videobuffer.pts = DVD_NOPTS_VALUE;
  m_videobuffer.format = RENDER_FMT_CVBREF;
  m_videobuffer.color_range  = 0;
  m_videobuffer.color_matrix = 4;
  m_videobuffer.iFlags  = DVP_FLAG_ALLOCATED;
  m_videobuffer.iWidth  = m_hints.width;
  m_videobuffer.iHeight = m_hints.height;
  m_videobuffer.iDisplayWidth  = m_hints.width;
  m_videobuffer.iDisplayHeight = m_hints.height;

  return m_vt_session != nullptr;
}

void
CDVDVideoCodecVideoToolBox::DestroyVTSession()
{
  if (m_vt_session)
  {
    // Prevent deadlocks in VTDecompressionSessionInvalidate by waiting for the frames to complete manually.
    // Seems to have appeared in iOS11
    VTDecompressionSessionWaitForAsynchronousFrames((VTDecompressionSessionRef)m_vt_session);
    VTDecompressionSessionInvalidate((VTDecompressionSessionRef)m_vt_session);
    CFRelease((VTDecompressionSessionRef)m_vt_session);
    m_vt_session = nullptr;
  }
}

typedef void (*VTDecompressionOutputCallback)(
		void * CM_NULLABLE decompressionOutputRefCon,
		void * CM_NULLABLE sourceFrameRefCon,
		OSStatus status,
		VTDecodeInfoFlags infoFlags,
		CM_NULLABLE CVImageBufferRef imageBuffer,
		CMTime presentationTimeStamp,
		CMTime presentationDuration );

void
CDVDVideoCodecVideoToolBox::VTDecoderCallback(
  void              *refcon,
  void              *frameInfo,
  OSStatus           status,
  VTDecodeInfoFlags  infoFlags,
  CVBufferRef        imageBuffer,
  CMTime             pts,
  CMTime             duration)
{
  // This is an sync callback due to VTDecompressionSessionWaitForAsynchronousFrames
  CDVDVideoCodecVideoToolBox *ctx = (CDVDVideoCodecVideoToolBox*)refcon;
  if (status != noErr)
  {
    //CLog::Log(LOGDEBUG, "%s - status error (%d)", __FUNCTION__, (int)status);
    return;
  }
  if (imageBuffer == nullptr)
  {
    //CLog::Log(LOGDEBUG, "%s - imageBuffer is nullptr", __FUNCTION__);
    return;
  }

  /*
  // https://bugzilla.gnome.org/show_bug.cgi?id=728435
  // ignore the dropped flag if buffer was received
  if (kVTDecodeInfo_FrameDropped & infoFlags)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s - frame dropped", __FUNCTION__);
    return;
  }
  */

  // allocate a new frame and populate it with some information.
  // this pointer to a frame_queue type keeps track of the newest decompressed frame
  // and is then inserted into a linked list of frame pointers depending on the display time (pts)
  // parsed out of the bitstream and stored in the frameInfo dictionary by the client
  frame_queue *newFrame = (frame_queue*)calloc(sizeof(frame_queue), 1);
  newFrame->nextframe = nullptr;
  if (CVPixelBufferIsPlanar(imageBuffer) )
  {
    newFrame->width  = CVPixelBufferGetWidthOfPlane(imageBuffer, 0);
    newFrame->height = CVPixelBufferGetHeightOfPlane(imageBuffer, 0);
  }
  else
  {
    newFrame->width  = CVPixelBufferGetWidth(imageBuffer);
    newFrame->height = CVPixelBufferGetHeight(imageBuffer);
  }
  newFrame->pixel_buffer_format = CVPixelBufferGetPixelFormatType(imageBuffer);;
  newFrame->pixel_buffer_ref = CVBufferRetain(imageBuffer);
  newFrame->pts = (double)pts.value;

  // since the frames we get may be in decode order rather than presentation order
  // our hypothetical callback places them in a queue of frames which will
  // hold them in display order for display on another thread
  pthread_mutex_lock(&ctx->m_queue_mutex);

  frame_queue base;
  base.nextframe = ctx->m_display_queue;
  frame_queue *ptr = &base;
  for(; ptr->nextframe; ptr = ptr->nextframe)
  {
    if (ptr->nextframe->pts == DVD_NOPTS_VALUE || newFrame->pts == DVD_NOPTS_VALUE)
      continue;
    if (ptr->nextframe->pts > newFrame->pts)
      break;
  }
  // insert after ptr
  newFrame->nextframe = ptr->nextframe;
  ptr->nextframe = newFrame;

  // update anchor if needed
  if (newFrame->nextframe == ctx->m_display_queue)
    ctx->m_display_queue = newFrame;

  ctx->m_queue_depth++;
  pthread_mutex_unlock(&ctx->m_queue_mutex);
}
