/*
 *      Copyright (C) 2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

// http://developer.android.com/reference/android/media/MediaCodec.html
//
// Android MediaCodec class can be used to access low-level media codec,
// i.e. encoder/decoder components. (android.media.MediaCodec). Requires SDK21+
//

#include "DVDVideoCodecAndroidMediaCodec.h"

#include <memory>

#include "Application.h"
#include "cores/dvdplayer/DVDClock.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "guilib/GraphicContext.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "utils/BitstreamConverter.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include "platform/android/activity/XBMCApp.h"
#include "platform/android/activity/AndroidFeatures.h"
#include <androidjni/ByteBuffer.h>
#include <androidjni/Build.h>
#include <androidjni/Display.h>
#include <androidjni/MediaCodecList.h>
#include <androidjni/MediaCodecInfo.h>
#include <androidjni/Surface.h>
#include <androidjni/SurfaceTexture.h>
#include <androidjni/View.h>
#include <androidjni/Window.h>

#include "platform/android/activity/JNIXBMCSurfaceTextureOnFrameAvailableListener.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define DEBUG_EXTRADATA 1

#define XMEDIAFORMAT_KEY_ROTATION "rotation-degrees"
#define XMEDIAFORMAT_KEY_SLICE "slice-height"
#define XMEDIAFORMAT_KEY_CROP_LEFT "crop-left"
#define XMEDIAFORMAT_KEY_CROP_RIGHT "crop-right"
#define XMEDIAFORMAT_KEY_CROP_TOP "crop-top"
#define XMEDIAFORMAT_KEY_CROP_BOTTOM "crop-bottom"

using namespace KODI::MESSAGING;

enum MEDIACODEC_STATES
{
  MEDIACODEC_STATE_UNINITIALIZED,
  MEDIACODEC_STATE_CONFIGURED,
  MEDIACODEC_STATE_FLUSHED,
  MEDIACODEC_STATE_RUNNING,
  MEDIACODEC_STATE_ENDOFSTREAM,
  MEDIACODEC_STATE_ERROR,
  MEDIACODEC_STATE_STOPPED
};

static bool CanSurfaceRenderBlackList(const std::string &name)
{
  // All devices 'should' be capiable of surface rendering
  // but that seems to be hit or miss as most odd name devices
  // cannot surface render.
  static const char *cannotsurfacerender_decoders[] = {
    NULL
  };
  for (const char **ptr = cannotsurfacerender_decoders; *ptr; ptr++)
  {
    if (!strnicmp(*ptr, name.c_str(), strlen(*ptr)))
      return true;
  }
  return false;
}

static bool IsBlacklisted(const std::string &name)
{
  static const char *blacklisted_decoders[] = {
    // No software decoders
    "OMX.google",
    // For Rockchip non-standard components
    "AVCDecoder",
    "AVCDecoder_FLASH",
    "FLVDecoder",
    "M2VDecoder",
    "M4vH263Decoder",
    "RVDecoder",
    "VC1Decoder",
    "VPXDecoder",
    // End of Rockchip
    NULL
  };
  for (const char **ptr = blacklisted_decoders; *ptr; ptr++)
  {
    if (!strnicmp(*ptr, name.c_str(), strlen(*ptr)))
      return true;
  }
  return false;
}

static bool IsSupportedColorFormat(int color_format)
{
  static const int supported_colorformats[] = {
    CJNIMediaCodecInfoCodecCapabilities::COLOR_FormatYUV420Planar,
    CJNIMediaCodecInfoCodecCapabilities::COLOR_TI_FormatYUV420PackedSemiPlanar,
    CJNIMediaCodecInfoCodecCapabilities::COLOR_FormatYUV420SemiPlanar,
    CJNIMediaCodecInfoCodecCapabilities::COLOR_QCOM_FormatYUV420SemiPlanar,
    CJNIMediaCodecInfoCodecCapabilities::OMX_QCOM_COLOR_FormatYVU420SemiPlanarInterlace,
    -1
  };
  for (const int *ptr = supported_colorformats; *ptr != -1; ptr++)
  {
    if (color_format == *ptr)
      return true;
  }
  return false;
}

/*****************************************************************************/
/*****************************************************************************/
class CDVDMediaCodecOnFrameAvailable : public CEvent, public CJNIXBMCSurfaceTextureOnFrameAvailableListener
{
public:
  CDVDMediaCodecOnFrameAvailable(std::shared_ptr<CJNISurfaceTexture> &surfaceTexture)
    : CJNIXBMCSurfaceTextureOnFrameAvailableListener()
    , m_surfaceTexture(surfaceTexture)
  {
    m_surfaceTexture->setOnFrameAvailableListener(*this);
  }

  virtual ~CDVDMediaCodecOnFrameAvailable()
  {
    // unhook the callback
    CJNIXBMCSurfaceTextureOnFrameAvailableListener nullListener(jni::jhobject(NULL));
    m_surfaceTexture->setOnFrameAvailableListener(nullListener);
  }

protected:
  void onFrameAvailable(CJNISurfaceTexture)
  {
    Set();
  }

private:
  std::shared_ptr<CJNISurfaceTexture> m_surfaceTexture;
};

/*****************************************************************************/
/*****************************************************************************/
CDVDMediaCodecInfo::CDVDMediaCodecInfo(
    ssize_t index
  , unsigned int texture
  , int64_t timestamp
  , double duration
  , AMediaCodec* codec
  , std::shared_ptr<CJNISurfaceTexture> &surfacetexture
  , std::shared_ptr<CDVDMediaCodecOnFrameAvailable> &frameready
  , std::shared_ptr<CJNIXBMCVideoView> &videoview
)
: m_refs(1)
, m_valid(true)
, m_isReleased(true)
, m_index(index)
, m_texture(texture)
, m_timestamp(timestamp)
, m_duration(duration)
, m_codec(codec)
, m_surfacetexture(surfacetexture)
, m_frameready(frameready)
, m_videoview(videoview)
{
  // paranoid checks
  assert(m_index >= 0);
  assert(m_codec != NULL);
}

CDVDMediaCodecInfo::~CDVDMediaCodecInfo()
{
  assert(m_refs == 0);
}

CDVDMediaCodecInfo* CDVDMediaCodecInfo::Retain()
{
  ++m_refs;
  m_isReleased = false;

  return this;
}

long CDVDMediaCodecInfo::Release()
{
  long count = --m_refs;
  if (count == 1)
    ReleaseOutputBuffer(0);
  if (count == 0)
    delete this;

  return count;
}

void CDVDMediaCodecInfo::Validate(bool state)
{
  CSingleLock lock(m_section);

  m_valid = state;
}

void CDVDMediaCodecInfo::ReleaseOutputBuffer(int64_t timestamp)
{
  CSingleLock lock(m_section);

  if (!m_valid || m_isReleased)
    return;

  // release OutputBuffer and render if indicated
  // then wait for rendered frame to become avaliable.

  if (timestamp)
    if (m_frameready)
      m_frameready->Reset();

  media_status_t mstat;
  if (timestamp > 1)
    mstat = AMediaCodec_releaseOutputBufferAtTime(m_codec, m_index, timestamp);
  else
    mstat = AMediaCodec_releaseOutputBuffer(m_codec, m_index, (timestamp > 0));
  m_isReleased = true;

  if (mstat != AMEDIA_OK)
  {
    CLog::Log(LOGERROR, "CDVDMediaCodecInfo::ReleaseOutputBuffer "
                        "error %d in render(%lld)", mstat, timestamp);
  }
}

ssize_t CDVDMediaCodecInfo::GetIndex() const
{
  CSingleLock lock(m_section);

  return m_index;
}

int CDVDMediaCodecInfo::GetTextureID() const
{
  // since m_texture never changes,
  // we do not need a m_section lock here.
  return m_texture;
}

void CDVDMediaCodecInfo::GetTransformMatrix(float *textureMatrix)
{
  CSingleLock lock(m_section);

  if (!m_valid)
    return;

  m_surfacetexture->getTransformMatrix(textureMatrix);
}

void CDVDMediaCodecInfo::UpdateTexImage()
{
  CSingleLock lock(m_section);

  if (!m_valid)
    return;

  // updateTexImage will check and spew any prior gl errors,
  // clear them before we call updateTexImage.
  glGetError();

  // this is key, after calling releaseOutputBuffer, we must
  // wait a little for MediaCodec to render to the surface.
  // Then we can updateTexImage without delay. If we do not
  // wait, then video playback gets jerky. To optomize this,
  // we hook the SurfaceTexture OnFrameAvailable callback
  // using CJNISurfaceTextureOnFrameAvailableListener and wait
  // on a CEvent to fire. 20ms seems to be a good max fallback.
  m_frameready->WaitMSec(20);

  m_surfacetexture->updateTexImage();
  if (xbmc_jnienv()->ExceptionCheck())
  {
    CLog::Log(LOGERROR, "CDVDMediaCodecInfo::UpdateTexImage updateTexImage:ExceptionCheck");
    xbmc_jnienv()->ExceptionDescribe();
    xbmc_jnienv()->ExceptionClear();
  }

  m_timestamp = m_surfacetexture->getTimestamp();
  if (xbmc_jnienv()->ExceptionCheck())
  {
    CLog::Log(LOGERROR, "CDVDMediaCodecInfo::UpdateTexImage getTimestamp:ExceptionCheck");
    xbmc_jnienv()->ExceptionDescribe();
    xbmc_jnienv()->ExceptionClear();
  }
}

void CDVDMediaCodecInfo::RenderUpdate(const CRect &SrcRect, const CRect &DestRect)
{
  CRect surfRect = m_videoview->getSurfaceRect();
  if (DestRect != surfRect)
  {
    CRect adjRect = CXBMCApp::MapRenderToDroid(DestRect);
    if (adjRect != surfRect)
    {
      m_videoview->setSurfaceRect(adjRect);
      CLog::Log(LOGDEBUG, "CMediaCodecVideoBuffer::RenderUpdate: Dest - %f+%f-%fx%f", DestRect.x1, DestRect.y1, DestRect.Width(), DestRect.Height());
      CLog::Log(LOGDEBUG, "CMediaCodecVideoBuffer::RenderUpdate: Adj  - %f+%f-%fx%f", adjRect.x1, adjRect.y1, adjRect.Width(), adjRect.Height());
    }
  }
}


/*****************************************************************************/
/*****************************************************************************/
CDVDVideoCodecAndroidMediaCodec::CDVDVideoCodecAndroidMediaCodec(bool surface_render, bool render_interlaced)
: m_formatname("mediacodec")
, m_opened(false)
, m_surface(NULL)
, m_textureId(0)
, m_bitstream(NULL)
, m_jnivideoview(nullptr)
, m_jnisurface(nullptr)
, m_render_sw(false)
, m_render_surface(surface_render)
, m_render_interlaced(render_interlaced)
, m_state(MEDIACODEC_STATE_UNINITIALIZED)
, m_lastpts(-1.0)
{
  memset(&m_videobuffer, 0x00, sizeof(DVDVideoPicture));
}

CDVDVideoCodecAndroidMediaCodec::~CDVDVideoCodecAndroidMediaCodec()
{
  Dispose();
}

bool CDVDVideoCodecAndroidMediaCodec::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  // mediacodec crashes with null size. Trap this...
  if (!hints.width || !hints.height)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Open - null size, cannot handle");
    return false;
  }
  else if (hints.stills || (hints.dvd && CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELMPEG2) > 0))
  {
    // Won't work reliably
    return false;
  }
  else if (hints.codec == AV_CODEC_ID_HEVC && hints.width == 1936 && hints.height == 1086)
  {
    // Won't work reliably under fireOS but shield will support it.
    // As both can sw decode it, put to ffmpeg.
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open - HEVC@1936x1086, punt");
    return false;
  }

  if (hints.orientation && m_render_surface && CJNIBase::GetSDKVersion() < 23)
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open - Surface does not support orientation before API 23");
    return false;
  }

  m_drop = false;
  m_codecControlFlags = 0;
  m_hints = hints;
  int profile(0);
  bool check_dv = false;

  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: CodecID: %d \n", m_hints.codec);
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: Codec Tag: %c%c%c%c \n", 
      (m_hints.codec_tag & 0x000000ff),
      (m_hints.codec_tag & 0x0000ff00) >> 8,
      (m_hints.codec_tag & 0x00ff0000) >> 16,
      (m_hints.codec_tag & 0xff000000) >> 24
      );
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: Profile / Level: %d / %d \n", m_hints.profile, m_hints.level);
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: PTS_invalid: %d \n", m_hints.ptsinvalid);
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: wxh: %dx%d \n", m_hints.width,  m_hints.height);
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open hints: colors space: %d; range: %d; transf: %d; prime: %d\n", 
      m_hints.colorspace, m_hints.colorrange, m_hints.colortransfer, m_hints.colorprimaries);
  }

  switch(m_hints.codec)
  {
    case AV_CODEC_ID_MPEG2VIDEO:
      m_mime = "video/mpeg2";
      m_formatname = "amc-mpeg2";
      break;
    case AV_CODEC_ID_MPEG4:
      m_mime = "video/mp4v-es";
      m_formatname = "amc-mpeg4";
      break;
    case AV_CODEC_ID_H263:
      m_mime = "video/3gpp";
      m_formatname = "amc-h263";
      break;
    case AV_CODEC_ID_VP6:
    case AV_CODEC_ID_VP6F:
      m_mime = "video/x-vnd.on2.vp6";
      m_formatname = "amc-vp6";
      break;
    case AV_CODEC_ID_VP8:
      m_mime = "video/x-vnd.on2.vp8";
      m_formatname = "amc-vp8";
      break;
    case AV_CODEC_ID_VP9:
      switch(hints.profile)
      {
        case FF_PROFILE_VP9_0:
          profile = CJNIMediaCodecInfoCodecProfileLevel::VP9Profile0;
          break;
        case FF_PROFILE_VP9_1:
          profile = CJNIMediaCodecInfoCodecProfileLevel::VP9Profile1;
          break;
        case FF_PROFILE_VP9_2:
          profile = CJNIMediaCodecInfoCodecProfileLevel::VP9Profile2;
          break;
        case FF_PROFILE_VP9_3:
          profile = CJNIMediaCodecInfoCodecProfileLevel::VP9Profile3;
          break;
        default:;
      }
      m_mime = "video/x-vnd.on2.vp9";
      m_formatname = "amc-vp9";
      break;
    case AV_CODEC_ID_AVS:
    case AV_CODEC_ID_CAVS:
    case AV_CODEC_ID_H264:
      switch(hints.profile)
      {
        case FF_PROFILE_H264_HIGH_10:
          profile = CJNIMediaCodecInfoCodecProfileLevel::AVCProfileHigh10;
          break;
        case FF_PROFILE_H264_HIGH_10_INTRA:
          // No known h/w decoder supporting Hi10P
          CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open - no known h/w decoder supports Hi10P");
          return false;
      }
      m_mime = "video/avc";
      m_formatname = "amc-h264";
      // check for h264-avcC and convert to h264-annex-b if needed
      if (m_hints.extradata)
      {
        m_bitstream = new CBitstreamConverter;
        if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, true))
        {
          SAFE_DELETE(m_bitstream);
          return false;
        }
      }
      else
        return false;
      break;
    case AV_CODEC_ID_HEVC:
      m_mime = "video/hevc";
      m_formatname = "amc-h265";
      // check for hevc-hvcC and convert to h265-annex-b
      if (m_hints.extradata)
      {
        m_bitstream = new CBitstreamConverter;
        if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, true))
        {
          SAFE_DELETE(m_bitstream);
        }
      }
      else
        return false;
      
      // check for DolbyVision, hints.profile will be wrong
      // and we have to look at codec_tag
      if (hints.codec_tag == MKTAG('d','v','h','1') ||
          hints.codec_tag == MKTAG('d','v','h','e') ||
          hints.codec_tag == MKTAG('D','O','V','I'))
      {
        check_dv = true;
        m_mime = "video/dolby-vision";
        m_formatname = "amc-dv";
        CLog::Log(LOGINFO, "CDVDVideoCodecAndroidMediaCodec::Open - Dolby Vision detected: extradata: %d", m_hints.extrasize);
      }

      break;
    case AV_CODEC_ID_WMV3:
      if (m_hints.extrasize == 4 || m_hints.extrasize == 5)
      {
        // Convert to SMPTE 421M-2006 Annex-L
        static uint8_t annexL_hdr1[] = {0x8e, 0x01, 0x00, 0xc5, 0x04, 0x00, 0x00, 0x00};
        static uint8_t annexL_hdr2[] = {0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        free(m_hints.extradata);
        m_hints.extrasize = 36;
        m_hints.extradata = malloc(m_hints.extrasize);

        unsigned int offset = 0;
        char buf[4];
        memcpy(m_hints.extradata, annexL_hdr1, sizeof(annexL_hdr1));
        offset += sizeof(annexL_hdr1);
        memcpy(&((char *)(m_hints.extradata))[offset], hints.extradata, 4);
        offset += 4;
        BS_WL32(buf, hints.height);
        memcpy(&((char *)(m_hints.extradata))[offset], buf, 4);
        offset += 4;
        BS_WL32(buf, hints.width);
        memcpy(&((char *)(m_hints.extradata))[offset], buf, 4);
        offset += 4;
        memcpy(&((char *)(m_hints.extradata))[offset], annexL_hdr2, sizeof(annexL_hdr2));
      }

      m_mime = "video/x-ms-wmv";
      m_formatname = "amc-wmv";
      break;
    case AV_CODEC_ID_VC1:
    {
      if (m_hints.extrasize < 16)
        return false;

      // Reduce extradata to first SEQ header
      unsigned int seq_offset = 0;
      for (; seq_offset <= m_hints.extrasize-4; ++seq_offset)
      {
        char *ptr = &((char*)m_hints.extradata)[seq_offset];
        if (ptr[0] == 0x00 && ptr[1] == 0x00 && ptr[2] == 0x01 && ptr[3] == 0x0f)
          break;
      }
      if (seq_offset > m_hints.extrasize-4)
        return false;

      if (seq_offset)
      {
        free(m_hints.extradata);
        m_hints.extrasize -= seq_offset;
        m_hints.extradata = malloc(m_hints.extrasize);
        memcpy(m_hints.extradata, &((char *)(hints.extradata))[seq_offset], m_hints.extrasize);
      }

      m_mime = "video/wvc1";
      m_formatname = "amc-vc1";
      break;
    }

    default:
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: Unknown hints.codec(%d)", hints.codec);
      return false;
      break;
  }

  if (hints.maybe_interlaced > 0)
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open - possible interlaced codec(%s%s), profile(%d), level(%d)",
      m_formatname.c_str(), m_render_surface ? "-sfc" : "-egl", hints.profile, hints.level);
    if (!m_render_interlaced)
      return false;
  }

#ifdef DEBUG_EXTRADATA
  CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec: Extradata size: %d", m_hints.extrasize);
  if (m_hints.extrasize)
  {
    std::string line;
    for (unsigned int y=0; y*8 < m_hints.extrasize; ++y)
    {
      line = "";
      for (unsigned int x=0; x<8 && y*8 + x < m_hints.extrasize; ++x)
      {
        line += StringUtils::Format("%02x ", ((char *)m_hints.extradata)[y*8+x]);
      }
      CLog::Log(LOGDEBUG, "%s", line.c_str());
    }
  }
#endif

  // CJNIMediaCodec::createDecoderByXXX doesn't handle errors nicely,
  // it crashes if the codec isn't found. This is fixed in latest AOSP,
  // but not in current 4.1 devices. So 1st search for a matching codec, then create it.
  m_codec = nullptr;
  m_colorFormat = -1;
  std::string lookup_mime = m_mime;
  if (check_dv)
    lookup_mime = "video/dolby-vision";

  while (true)
  {
    int num_codecs = CJNIMediaCodecList::getCodecCount();
    for (int i = 0; i < num_codecs; i++)
    {
      CJNIMediaCodecInfo codec_info = CJNIMediaCodecList::getCodecInfoAt(i);
      if (codec_info.isEncoder())
        continue;
      if (!m_codec)
      {
        m_codecname = codec_info.getName();
        if (IsBlacklisted(m_codecname))
          continue;
      }

      if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        CLog::Log(LOGDEBUG, "----  MediaCodec: %s -----------", codec_info.getName().c_str());

      std::vector<int> color_formats;
      CJNIMediaCodecInfoCodecCapabilities codec_caps = codec_info.getCapabilitiesForType(m_mime);
      if (xbmc_jnienv()->ExceptionCheck())
      {
        // Unsupported type?
        xbmc_jnienv()->ExceptionClear();
        continue;
      }

      color_formats = codec_caps.colorFormats();
      if (profile)
      {
        std::vector<CJNIMediaCodecInfoCodecProfileLevel> profileLevels = codec_caps.profileLevels();
        if (std::find_if(profileLevels.cbegin(), profileLevels.cend(),
          [&](const CJNIMediaCodecInfoCodecProfileLevel profileLevel){ return profileLevel.profile() == profile; }) == profileLevels.cend())
        {
          CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Open: profile not supported: %d", profile);
          continue;
        }
      }

      std::vector<std::string> types = codec_info.getSupportedTypes();
      // return the 1st one we find, that one is typically 'the best'
      for (size_t j = 0; j < types.size(); ++j)
      {
        if (g_advancedSettings.CanLogComponent(LOGVIDEO))
          CLog::Log(LOGDEBUG, "type: %s", types[j].c_str());

        if (!m_codec && types[j] == m_mime)
        {
          m_codec = AMediaCodec_createCodecByName(m_codecname.c_str());
          // clear any jni exceptions, jni gets upset if we do not.
          if (!m_codec)
          {
            CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Open ExceptionCheck");
            m_codec = nullptr;
            continue;
          }

          if (m_render_sw)
          {
            for (size_t k = 0; k < color_formats.size(); ++k)
            {
              CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Open "
                                  "m_codecname(%s), colorFormat(%d)", m_codecname.c_str(), color_formats[k]);
              if (IsSupportedColorFormat(color_formats[k]))
                m_colorFormat = color_formats[k]; // Save color format for initial output configuration
            }
          }
          if (!g_advancedSettings.CanLogComponent(LOGVIDEO))
            break;
        }
        if (!g_advancedSettings.CanLogComponent(LOGVIDEO))
          break;
      }
      if (m_codec && !g_advancedSettings.CanLogComponent(LOGVIDEO))
        break;
    }
    if (m_codec || !check_dv)
      break;

    check_dv = false;
    m_mime = "video/hevc";
    m_formatname = "amc-h265";
  }

  if (!m_codec)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec:: Failed to create Android MediaCodec");
    SAFE_DELETE(m_bitstream);
    return false;
  }

  // blacklist of devices that cannot surface render.
  m_render_sw = CanSurfaceRenderBlackList(m_codecname) || g_advancedSettings.m_mediacodecForceSoftwareRendring;
  if (m_render_sw)
  {
    if (m_colorFormat == -1)
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec:: No supported color format");
      m_codec = nullptr;
      SAFE_DELETE(m_bitstream);
      return false;
    }
    m_render_surface = false;
  }

  if (m_render_surface)
  {
    m_jnivideoview.reset(CJNIXBMCVideoView::createVideoView(this));
    if (!m_jnivideoview || !m_jnivideoview->waitForSurface(500))
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec: VideoView creation failed!!");
      if (m_jnivideoview)
      {
        m_jnivideoview->release();
        m_jnivideoview.reset();
      }
      AMediaCodec_delete(m_codec);
      m_codec = nullptr;
      SAFE_DELETE(m_bitstream);
      return false;
    }
  }

  // setup a YUV420P DVDVideoPicture buffer.
  // first make sure all properties are reset.
  memset(&m_videobuffer, 0x00, sizeof(DVDVideoPicture));

  m_videobuffer.dts = DVD_NOPTS_VALUE;
  m_videobuffer.pts = DVD_NOPTS_VALUE;
  m_videobuffer.color_range  = 0;
  m_videobuffer.color_matrix = 4;
  m_videobuffer.iFlags  = DVP_FLAG_ALLOCATED;
  if (!m_render_surface && !CAndroidFeatures::IsShieldTVDevice())
  {
    // for FireOS, MediaCodec returns the interlaced frames as is,
    // for Shield, MediaCodec will kick in an internal deinterlacer
    // and return progressive frames.
    if (hints.maybe_interlaced > 0)
    {
      m_videobuffer.iFlags |= DVP_FLAG_INTERLACED;
      // should be DVP_FLAG_TOP_FIELD_FIRST but android seems to be inverted ?
      //m_videobuffer.iFlags |= DVP_FLAG_TOP_FIELD_FIRST;
    }
  }
  m_videobuffer.iWidth  = m_hints.width;
  m_videobuffer.iHeight = m_hints.height;
  // these will get reset to crop values later
  m_videobuffer.iDisplayWidth  = m_hints.width;
  m_videobuffer.iDisplayHeight = m_hints.height;

  if (!ConfigureMediaCodec())
  {
    AMediaCodec_delete(m_codec);
    m_codec = nullptr;
    SAFE_DELETE(m_bitstream);
    return false;
  }

  CLog::Log(LOGINFO, "CDVDVideoCodecAndroidMediaCodec:: "
    "Open Android MediaCodec %s", m_codecname.c_str());

  m_opened = true;

  return m_opened;
}

void CDVDVideoCodecAndroidMediaCodec::Dispose()
{
  if (!m_opened)
    return;

  m_opened = false;

  // release any retained demux packets
  while (!m_demux.empty())
  {
    amc_demux &demux_pkt = m_demux.front();
    free(demux_pkt.pData);
    m_demux.pop();
  }

  // invalidate any inflight buffers
  FlushInternal();

  // clear m_videobuffer bits
  if (m_render_sw)
  {
    free(m_videobuffer.data[0]), m_videobuffer.data[0] = NULL;
    free(m_videobuffer.data[1]), m_videobuffer.data[1] = NULL;
    free(m_videobuffer.data[2]), m_videobuffer.data[2] = NULL;
  }
  m_videobuffer.iFlags = 0;
  // m_videobuffer.mediacodec is unioned with m_videobuffer.data[0]
  // so be very careful when and how you touch it.
  m_videobuffer.mediacodec = NULL;

  if (m_codec)
  {
    AMediaCodec_stop(m_codec);
    AMediaCodec_delete(m_codec);
    m_codec = nullptr;
    m_state = MEDIACODEC_STATE_STOPPED;
  }
  ReleaseSurfaceTexture();

  if (m_surface)
    ANativeWindow_release(m_surface);
  m_surface = nullptr;

  if (m_render_surface)
  {
    m_jnivideoview->release();
    m_jnivideoview.reset();
  }

  SAFE_DELETE(m_bitstream);
}

int CDVDVideoCodecAndroidMediaCodec::Decode(uint8_t *pData, int iSize, double dts, double pts)
{
  if (!m_opened)
    return VC_ERROR;

  // Are we stopped? If so, wait and loop
  if (m_state == MEDIACODEC_STATE_STOPPED)
  {
    if (pData)
    {
      amc_demux demux_pkt;
      demux_pkt.dts = dts;
      demux_pkt.pts = pts;
      demux_pkt.iSize = iSize;
      demux_pkt.pData = (uint8_t*)malloc(iSize);
      memcpy(demux_pkt.pData, pData, iSize);
      m_demux.push(demux_pkt);
    }

    Sleep(20);
    return 0;
  }

  if (m_hints.ptsinvalid)
    pts = DVD_NOPTS_VALUE;

  int rtn = 0;

  // must check for an output picture 1st,
  // otherwise, mediacodec can stall on some devices.
  int retgp = GetOutputPicture();

  if (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN)
  {
    if (retgp > 0)
      return VC_PICTURE;
    else
      return VC_BUFFER;
  }

  if (retgp > 0)
  {
    rtn |= VC_PICTURE;
  }
  else if (retgp == -1)  // EOS
  {
    AMediaCodec_flush(m_codec);
    m_state = MEDIACODEC_STATE_FLUSHED;
    rtn |= VC_BUFFER;
  }
  else
  {
    if (m_demux.size() < 16)  // No need to overbuffer; If we are over, we have a problem
      rtn |= VC_BUFFER;
    else
      CLog::Log(LOGWARNING, "CDVDVideoCodecAndroidMediaCodec: demux packets over-buffering");
  }


  if (pData)
  {
    // queue demux pkt in case we cannot get an input buffer
    amc_demux demux_pkt;
    demux_pkt.dts = dts;
    demux_pkt.pts = pts;
    demux_pkt.iSize = iSize;
    demux_pkt.pData = (uint8_t*)malloc(iSize);
    memcpy(demux_pkt.pData, pData, iSize);
    m_demux.push(demux_pkt);
  }

  if (!m_demux.empty() && m_state != MEDIACODEC_STATE_ENDOFSTREAM)
  {
    // try to fetch an input buffer
    int64_t timeout_us = 5000;
    ssize_t index = AMediaCodec_dequeueInputBuffer(m_codec, timeout_us);
    if (index >= 0)
    {
      if (m_state == MEDIACODEC_STATE_FLUSHED)
        m_state = MEDIACODEC_STATE_RUNNING;
      if (!(m_state == MEDIACODEC_STATE_FLUSHED || m_state == MEDIACODEC_STATE_RUNNING))
        CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Decode Dequeue: Wrong state (%d)", m_state);

      // we have an input buffer, fill it.
      size_t out_size;
      uint8_t* dst_ptr = AMediaCodec_getInputBuffer(m_codec, index, &out_size);

      // fetch the front demux packet
      amc_demux &demux_pkt = m_demux.front();
      if (demux_pkt.iSize > out_size)
      {
        CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Decode, iSize(%d) > size(%d)", demux_pkt.iSize, out_size);
        demux_pkt.iSize = out_size;
      }
      if (dst_ptr)
      {
        if (m_bitstream)
        {
          m_bitstream->Convert(demux_pkt.pData, demux_pkt.iSize);
          iSize = m_bitstream->GetConvertSize();
          pData = m_bitstream->GetConvertBuffer();
        }
        else
        {
          iSize = demux_pkt.iSize;
          pData = demux_pkt.pData;
        }

        // Codec specifics
        switch(m_hints.codec)
        {
          case AV_CODEC_ID_VC1:
          {
            if (iSize >= 4 &&
                pData[0] == 0x00 &&
                pData[1] == 0x00 &&
                pData[2] == 0x01 &&
               (pData[3] == 0x0d || pData[3] == 0x0f))
              memcpy(dst_ptr, pData, iSize);
            else
            {
              dst_ptr[0] = 0x00;
              dst_ptr[1] = 0x00;
              dst_ptr[2] = 0x01;
              dst_ptr[3] = 0x0d;
              memcpy(dst_ptr+4, pData, iSize);
              iSize += 4;
            }
            break;
          }

          default:
            memcpy(dst_ptr, pData, iSize);
            break;
        }
      }
      free(demux_pkt.pData);
      m_demux.pop();

      // Translate from dvdplayer dts/pts to MediaCodec pts,
      // pts WILL get re-ordered by MediaCodec if needed.
      // Do not try to pass pts as a unioned double/int64_t,
      // some android devices will diddle with presentationTimeUs
      // and you will get NaN back and DVDPlayerVideo will barf.
      int64_t presentationTimeUs = AV_NOPTS_VALUE;
      if (demux_pkt.pts != DVD_NOPTS_VALUE)
        presentationTimeUs = demux_pkt.pts;
      else if (demux_pkt.dts != DVD_NOPTS_VALUE)
        presentationTimeUs = demux_pkt.dts;

      if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: "
                            "pts(%lld), demux_pkt.iSize(%d)", presentationTimeUs, demux_pkt.iSize);

      int flags = 0;
      int offset = 0;
      media_status_t mstat = AMediaCodec_queueInputBuffer(m_codec, index, offset, iSize, presentationTimeUs, flags);
      if (mstat != AMEDIA_OK)
        CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Decode error(%d)", mstat);
    }
  }


  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::Decode, "
                        "rtn(%d), m_demux.size(%ld)", rtn, m_demux.size());

  return rtn;
}

void CDVDVideoCodecAndroidMediaCodec::Reset()
{
  if (!m_opened)
    return;

  // dump any pending demux packets
  while (!m_demux.empty())
  {
    amc_demux &demux_pkt = m_demux.front();
    free(demux_pkt.pData);
    m_demux.pop();
  }

  if (m_codec)
  {
    // flush all outputbuffers inflight, they will
    // become invalid on m_codec->flush and generate
    // a spew of java exceptions if used
    FlushInternal();

    // now we can flush the actual MediaCodec object
    if (m_state == MEDIACODEC_STATE_RUNNING)
    {
      AMediaCodec_flush(m_codec);
      m_state = MEDIACODEC_STATE_FLUSHED;
    }
    else
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::Reset Wrong state (%d)", m_state);

    // Invalidate our local DVDVideoPicture bits
    m_videobuffer.pts = DVD_NOPTS_VALUE;
    if (!m_render_sw)
      m_videobuffer.mediacodec = NULL;
  }
  m_drop = false;
  m_codecControlFlags = 0;
  m_lastpts = -1;
}

bool CDVDVideoCodecAndroidMediaCodec::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (!m_opened)
    return false;

  *pDvdVideoPicture = m_videobuffer;
  if (m_codecControlFlags & DVD_CODEC_CTRL_DROP)
    pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;

  // Invalidate our local DVDVideoPicture bits
  m_videobuffer.pts = DVD_NOPTS_VALUE;
  if (!m_render_sw)
    m_videobuffer.mediacodec = NULL;

  m_lastpts = pDvdVideoPicture->pts;
  return true;
}

bool CDVDVideoCodecAndroidMediaCodec::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (pDvdVideoPicture->format == RENDER_FMT_MEDIACODEC || pDvdVideoPicture->format == RENDER_FMT_MEDIACODECSURFACE)
    SAFE_RELEASE(pDvdVideoPicture->mediacodec);
  memset(pDvdVideoPicture, 0x00, sizeof(DVDVideoPicture));

  return true;
}

void CDVDVideoCodecAndroidMediaCodec::SetDropState(bool bDrop)
{
  // more a message to decoder to hurry up.
  // MediaCodec has no such ability so ignore it.
  m_drop = bDrop;
}

void CDVDVideoCodecAndroidMediaCodec::SetCodecControl(int flags)
{
  if (m_codecControlFlags != flags)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s %x->%x", "CDVDVideoCodecAndroidMediaCodec", __func__, m_codecControlFlags, flags);
    m_codecControlFlags = flags;
  }
}

int CDVDVideoCodecAndroidMediaCodec::GetDataSize(void)
{
  // just ignore internal buffering contribution.
  return 0;
}

double CDVDVideoCodecAndroidMediaCodec::GetTimeSize(void)
{
  // just ignore internal buffering contribution.
  return 0.0;
}

unsigned CDVDVideoCodecAndroidMediaCodec::GetAllowedReferences()
{
  return 2;
}

void CDVDVideoCodecAndroidMediaCodec::FlushInternal()
{
  // invalidate any existing inflight buffers and create
  // new ones to match the number of output buffers

  if (m_render_sw)
    return;

  for (size_t i = 0; i < m_inflight.size(); ++i)
  {
    m_inflight[i]->Validate(false);
    m_inflight[i]->Release();
  }
  m_inflight.clear();
}

bool CDVDVideoCodecAndroidMediaCodec::ConfigureMediaCodec(void)
{
  // setup a MediaFormat to match the video content,
  // used by codec during configure
  AMediaFormat* mediaformat = AMediaFormat_new();
  AMediaFormat_setString(mediaformat, AMEDIAFORMAT_KEY_MIME, m_mime.c_str());
  AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, m_hints.width);
  AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, m_hints.height);
  AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 0);

  if (CJNIBase::GetSDKVersion() >= 23 && m_render_surface)
  {
    // Handle rotation
    AMediaFormat_setInt32(mediaformat, XMEDIAFORMAT_KEY_ROTATION, m_hints.orientation);
    AMediaFormat_setInt32(mediaformat, "feature-tunneled-playback", 0);
  }

  // handle codec extradata
  if (m_hints.extrasize)
  {
    size_t size = m_hints.extrasize;
    void  *src_ptr = m_hints.extradata;
    if (m_bitstream)
    {
      size = m_bitstream->GetExtraSize();
      src_ptr = m_bitstream->GetExtraData();
    }
    CLog::Log(LOGDEBUG, "%s::%s csd-0: %d", "CDVDVideoCodecAndroidMediaCodec", __func__, size);
    // Allocate a byte buffer via allocateDirect in java instead of NewDirectByteBuffer,
    // since the latter doesn't allocate storage of its own, and we don't know how long
    // the codec uses the buffer.
    switch(m_hints.codec)
    {
      case AV_CODEC_ID_AVS:
      case AV_CODEC_ID_CAVS:
      case AV_CODEC_ID_H264:
        {
          // according to https://developer.android.com/reference/android/media/MediaCodec.html
          // Codec-specific Data, H.264/AVC requires sps -> csd-0 and pps -> csd-1. There is a
          // vauge reference to 'Alternately, you can concatenate all codec-specific data and
          // submit it as a single codec-config buffer.' but MediaTek hw can have issues with
          // passing both sps/pps in csd-0.
          int sps_size, pps_size;
          uint8_t *sps = nullptr, *pps = nullptr;
          m_bitstream->ExtractH264_SPS_PPS((uint8_t*)src_ptr, size, &sps, &sps_size, &pps, &pps_size);
          if (sps)
          {
            //CLog::MemDump((char*)sps, sps_size);
            uint8_t *dts_ptr = (uint8_t*)malloc(sps_size + 4);
            BS_WB32(dts_ptr, 0x00000001);
            memcpy(dts_ptr + 4, sps, sps_size);
            AMediaFormat_setBuffer(mediaformat, "csd-0", dts_ptr, sps_size+4);
            free(dts_ptr);
          }
          if (pps)
          {
            //CLog::MemDump((char*)pps, pps_size);
            uint8_t *dts_ptr = (uint8_t*)malloc(pps_size + 4);
            BS_WB32(dts_ptr, 0x00000001);
            memcpy(dts_ptr + 4, pps, pps_size);
            AMediaFormat_setBuffer(mediaformat, "csd-1", dts_ptr, pps_size+4);
            free(dts_ptr);
          }
        }
        break;
      default:
        AMediaFormat_setBuffer(mediaformat, "csd-0", src_ptr, size);
        break;
    }
  }

  if (m_render_surface)
  {
    m_jnivideosurface = m_jnivideoview->getSurface();
    if (!m_jnivideosurface)
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec: VideoView getSurface failed!!");
      m_jnivideoview->release();
      m_jnivideoview.reset();
      return false;
    }
    m_surface = ANativeWindow_fromSurface(xbmc_jnienv(), m_jnivideosurface.get_raw());

    m_formatname += "(S)";
  }
  else if (!m_render_sw)
    InitSurfaceTexture();

  // configure and start the codec.
  // use the MediaFormat that we have setup.
  // use a null MediaCrypto, our content is not encrypted.
  // use a null Surface, we will extract the video picture data manually.
  uint32_t flags = 0;
  media_status_t mstat;
  if (m_render_sw)
    mstat = AMediaCodec_configure(m_codec, mediaformat, nullptr, nullptr, flags);
  else
    mstat = AMediaCodec_configure(m_codec, mediaformat, m_surface, nullptr, flags);

  // always, check/clear jni exceptions.
  if (mstat != AMEDIA_OK)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec configure error: %d", mstat);
    return false;
  }
  m_state = MEDIACODEC_STATE_CONFIGURED;


  mstat = AMediaCodec_start(m_codec);
  // always, check/clear jni exceptions.
  if (mstat != AMEDIA_OK)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec start error: %d", mstat);
    return false;
  }
  m_state = MEDIACODEC_STATE_FLUSHED;

  // There is no guarantee we'll get an INFO_OUTPUT_FORMAT_CHANGED (up to Android 4.3)
  // Configure the output with defaults
  ConfigureOutputFormat(mediaformat);

  return true;
}

int CDVDVideoCodecAndroidMediaCodec::GetOutputPicture(void)
{
  int rtn = 0;

  int64_t timeout_us = 5000;
  AMediaCodecBufferInfo bufferInfo;
  ssize_t index = AMediaCodec_dequeueOutputBuffer(m_codec, &bufferInfo, timeout_us);
  if (index >= 0)
  {
    int64_t pts = bufferInfo.presentationTimeUs;
    m_videobuffer.dts = DVD_NOPTS_VALUE;
    m_videobuffer.pts = DVD_NOPTS_VALUE;
    if (pts != AV_NOPTS_VALUE)
      m_videobuffer.pts = pts;
    m_videobuffer.iDuration = 0.0;
    if (m_lastpts > -1.0 && pts - m_lastpts < DVD_TIME_BASE /* Seek ? */)
      m_videobuffer.iDuration = (pts - m_lastpts) / DVD_TIME_BASE;

    uint32_t flags = bufferInfo.flags;
    if (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: BUFFER_FLAG_END_OF_STREAM");
      AMediaCodec_releaseOutputBuffer(m_codec, index, false);
      return -1;
    }
    if (m_codecControlFlags & DVD_CODEC_CTRL_DROP)
    {
      if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::GetOutputPicture dropped "
                            "index(%d), pts(%f)", index, m_videobuffer.pts);
      AMediaCodec_releaseOutputBuffer(m_codec, index, false);
      return 1;
    }

    if (!m_render_sw)
    {
      size_t i = 0;
      for (; i < m_inflight.size(); ++i)
      {
        if (m_inflight[i]->GetIndex() == index)
          break;
      }
      if (i == m_inflight.size())
        m_inflight.push_back(
          new CDVDMediaCodecInfo(index, m_textureId, pts, m_videobuffer.iDuration, m_codec, m_surfaceTexture, m_frameAvailable, m_jnivideoview)
        );
      else
      {
        if (m_inflight[i]->IsValid())
          CLog::Log(LOGWARNING, "%s - Reusing a still valid buffer: %d", __FUNCTION__, index);
        m_inflight[i]->m_timestamp = pts;
        m_inflight[i]->m_duration = m_videobuffer.iDuration;
      }
      m_videobuffer.mediacodec = m_inflight[i]->Retain();
      m_videobuffer.mediacodec->Validate(true);
    }
    else
    {
      size_t out_size;
      uint8_t* buffer = AMediaCodec_getOutputBuffer(m_codec, index, &out_size);
      if (buffer && out_size)
      {
        int loop_end = 0;
        if (m_videobuffer.format == RENDER_FMT_NV12)
          loop_end = 2;
        else if (m_videobuffer.format == RENDER_FMT_YUV420P)
          loop_end = 3;

        for (int i = 0; i < loop_end; ++i)
        {
          uint8_t *src   = buffer + m_src_offset[i];
          int src_stride = m_src_stride[i];
          uint8_t *dst   = m_videobuffer.data[i];
          int dst_stride = m_videobuffer.iLineSize[i];

          int height = m_videobuffer.iHeight;
          if (i > 0)
            height = (m_videobuffer.iHeight + 1) / 2;

          if (src_stride == dst_stride)
            memcpy(dst, src, dst_stride * height);
          else
            for (int j = 0; j < height; j++, src += src_stride, dst += dst_stride)
              memcpy(dst, src, dst_stride);
        }
      }
      media_status_t mstat = AMediaCodec_releaseOutputBuffer(m_codec, index, false);
      if (mstat != AMEDIA_OK)
        CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::GetOutputPicture error: releaseOutputBuffer(%d)", mstat);
    }

    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec::GetOutputPicture "
                          "index(%d), pts(%f)", index, m_videobuffer.pts);

    rtn = 1;
  }
  else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
  {
    AMediaFormat* mediaformat = AMediaCodec_getOutputFormat(m_codec);
    if (!mediaformat)
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::GetOutputPicture(INFO_OUTPUT_FORMAT_CHANGED) ExceptionCheck: getOutputBuffers");
    else
      ConfigureOutputFormat(mediaformat);
  }
  else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
  {
    // ignore.
  }
  else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
  {
    // normal dequeueOutputBuffer timeout, ignore it.
  }
  else
  {
    // we should never get here
    CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec::GetOutputPicture unknown index(%d)", index);
    rtn = -2;
  }

  return rtn;
}

void CDVDVideoCodecAndroidMediaCodec::ConfigureOutputFormat(AMediaFormat* mediaformat)
{
  int width       = 0;
  int height      = 0;
  int stride      = 0;
  int slice_height= 0;
  int color_format= 0;
  int crop_left   = 0;
  int crop_top    = 0;
  int crop_right  = 0;
  int crop_bottom = 0;

  int tmpVal;
  if (AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, &tmpVal))
    width = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, &tmpVal))
    height = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_STRIDE, &tmpVal))
    stride = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, XMEDIAFORMAT_KEY_SLICE, &tmpVal))
    slice_height = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_COLOR_FORMAT, &tmpVal))
    color_format = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, XMEDIAFORMAT_KEY_CROP_LEFT, &tmpVal))
    crop_left = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, XMEDIAFORMAT_KEY_CROP_RIGHT, &tmpVal))
    crop_right = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, XMEDIAFORMAT_KEY_CROP_TOP, &tmpVal))
    crop_top = tmpVal;
  if (AMediaFormat_getInt32(mediaformat, XMEDIAFORMAT_KEY_CROP_BOTTOM, &tmpVal))
    crop_bottom = tmpVal;

  if (!crop_right)
    crop_right = width-1;
  if (!crop_bottom)
    crop_bottom = height-1;

  CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: "
    "width(%d), height(%d), stride(%d), slice-height(%d), color-format(%d)",
    width, height, stride, slice_height, color_format);
  CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: "
    "crop-left(%d), crop-top(%d), crop-right(%d), crop-bottom(%d)",
    crop_left, crop_top, crop_right, crop_bottom);

  if (!m_render_sw)
  {
    if (m_render_surface)
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: Multi-Surface Rendering");
      m_videobuffer.format = RENDER_FMT_MEDIACODECSURFACE;
    }
    else
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: Direct Surface Rendering");
      m_videobuffer.format = RENDER_FMT_MEDIACODEC;
    }
  }
  else
  {
    // Android device quirks and fixes

    // Samsung Quirk: ignore width/height/stride/slice: http://code.google.com/p/android/issues/detail?id=37768#c3
    if (strstr(m_codecname.c_str(), "OMX.SEC.avc.dec") != NULL || strstr(m_codecname.c_str(), "OMX.SEC.avcdec") != NULL)
    {
      width = stride = m_hints.width;
      height = slice_height = m_hints.height;
    }
    // FireTV stick gen3 quirk: Returned size > frame size
    if (StringUtils::StartsWith(CJNIBuild::MODEL, "AFTMM"))
    {
      width = stride = m_hints.width;
      height = slice_height = m_hints.height;
    }


    // No color-format? Initialize with the one we detected as valid earlier
    if (color_format == 0)
      color_format = m_colorFormat;
    if (stride <= width)
      stride = width;
    if (slice_height <= height)
    {
      slice_height = height;
      if (color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_FormatYUV420Planar)
      {
        // NVidia Tegra 3 on Nexus 7 does not set slice_heights
        if (strstr(m_codecname.c_str(), "OMX.Nvidia.") != NULL)
        {
          slice_height = (((height) + 15) & ~15);
          CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: NVidia Tegra 3 quirk, slice_height(%d)", slice_height);
        }
      }
    }
    if (color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_TI_FormatYUV420PackedSemiPlanar)
    {
      slice_height -= crop_top / 2;
      // set crop top/left here, since the offset parameter already includes this.
      // if we would ignore the offset parameter in the BufferInfo, we could just keep
      // the original slice height and apply the top/left cropping instead.
      crop_top = 0;
      crop_left = 0;
    }

    // default picture format to none
    for (int i = 0; i < 4; ++i)
      m_src_offset[i] = m_src_stride[i] = 0;
    // delete any existing buffers
    for (int i = 0; i < 4; i++)
      free(m_videobuffer.data[i]);

    // setup picture format and data offset vectors
    if (color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_FormatYUV420Planar)
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: COLOR_FormatYUV420Planar");

      // Y plane
      m_src_stride[0] = stride;
      m_src_offset[0] = crop_top * stride;
      m_src_offset[0]+= crop_left;

      // U plane
      m_src_stride[1] = (stride + 1) / 2;
      //  skip over the Y plane
      m_src_offset[1] = slice_height * stride;
      //  crop_top/crop_left divided by two
      //  because one byte of the U/V planes
      //  corresponds to two pixels horizontally/vertically
      m_src_offset[1]+= crop_top  / 2 * m_src_stride[1];
      m_src_offset[1]+= crop_left / 2;

      // V plane
      m_src_stride[2] = (stride + 1) / 2;
      //  skip over the Y plane
      m_src_offset[2] = slice_height * stride;
      //  skip over the U plane
      m_src_offset[2]+= ((slice_height + 1) / 2) * ((stride + 1) / 2);
      //  crop_top/crop_left divided by two
      //  because one byte of the U/V planes
      //  corresponds to two pixels horizontally/vertically
      m_src_offset[2]+= crop_top  / 2 * m_src_stride[2];
      m_src_offset[2]+= crop_left / 2;

      m_videobuffer.iLineSize[0] =  width;         // Y
      m_videobuffer.iLineSize[1] = (width + 1) /2; // U
      m_videobuffer.iLineSize[2] = (width + 1) /2; // V
      m_videobuffer.iLineSize[3] = 0;

      unsigned int iPixels = width * height;
      unsigned int iChromaPixels = iPixels/4;
      m_videobuffer.data[0] = (uint8_t*)malloc(16 + iPixels);
      m_videobuffer.data[1] = (uint8_t*)malloc(16 + iChromaPixels);
      m_videobuffer.data[2] = (uint8_t*)malloc(16 + iChromaPixels);
      m_videobuffer.data[3] = NULL;
      m_videobuffer.format  = RENDER_FMT_YUV420P;
    }
    else if (color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_FormatYUV420SemiPlanar
          || color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_QCOM_FormatYUV420SemiPlanar
          || color_format == CJNIMediaCodecInfoCodecCapabilities::COLOR_TI_FormatYUV420PackedSemiPlanar
          || color_format == CJNIMediaCodecInfoCodecCapabilities::OMX_QCOM_COLOR_FormatYVU420SemiPlanarInterlace)

    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecAndroidMediaCodec:: COLOR_FormatYUV420SemiPlanar");

      // Y plane
      m_src_stride[0] = stride;
      m_src_offset[0] = crop_top * stride;
      m_src_offset[0]+= crop_left;

      // UV plane
      m_src_stride[1] = stride;
      //  skip over the Y plane
      m_src_offset[1] = slice_height * stride;
      m_src_offset[1]+= crop_top * stride;
      m_src_offset[1]+= crop_left;

      m_videobuffer.iLineSize[0] = width;  // Y
      m_videobuffer.iLineSize[1] = width;  // UV
      m_videobuffer.iLineSize[2] = 0;
      m_videobuffer.iLineSize[3] = 0;

      unsigned int iPixels = width * height;
      unsigned int iChromaPixels = iPixels;
      m_videobuffer.data[0] = (uint8_t*)malloc(16 + iPixels);
      m_videobuffer.data[1] = (uint8_t*)malloc(16 + iChromaPixels);
      m_videobuffer.data[2] = NULL;
      m_videobuffer.data[3] = NULL;
      m_videobuffer.format  = RENDER_FMT_NV12;
    }
    else
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecAndroidMediaCodec:: Fixme unknown color_format(%d)", color_format);
      return;
    }
  }

  // Refering to the mediacodec API documentation the size of a frame is computed by:
  // 1.) Retrieve mediaFormat wich / height
  // 2.) Check if crop rect is given and overwrite values in 1.)
  if (crop_right)
    width = crop_right  + 1 - crop_left;
  if (crop_bottom)
    height = crop_bottom + 1 - crop_top;
  m_videobuffer.iWidth  = width;
  m_videobuffer.iHeight = height;
  m_videobuffer.iDisplayWidth  = width;
  m_videobuffer.iDisplayHeight = height;

  if (m_hints.aspect > 1.0 && !m_hints.forced_aspect)
  {
    m_videobuffer.iDisplayWidth  = ((int)lrint(m_videobuffer.iHeight * m_hints.aspect)) & ~3;
    if (m_videobuffer.iDisplayWidth > m_videobuffer.iWidth)
    {
      m_videobuffer.iDisplayWidth  = m_videobuffer.iWidth;
      m_videobuffer.iDisplayHeight = ((int)lrint(m_videobuffer.iWidth / m_hints.aspect)) & ~3;
    }
  }
}

void CDVDVideoCodecAndroidMediaCodec::CallbackInitSurfaceTexture(void *userdata)
{
  CDVDVideoCodecAndroidMediaCodec *ctx = static_cast<CDVDVideoCodecAndroidMediaCodec*>(userdata);
  ctx->InitSurfaceTexture();
}

void CDVDVideoCodecAndroidMediaCodec::InitSurfaceTexture(void)
{
  // We MUST create the GLES texture on the main thread
  // to match where the valid GLES context is located.
  // It would be nice to move this out of here, we would need
  // to create/fetch/create from g_RenderMananger. But g_RenderMananger
  // does not know we are using MediaCodec until Configure and we
  // we need m_surfaceTexture valid before then. Chicken, meet Egg.
  if (g_application.IsCurrentThread())
  {
    // localize GLuint so we do not spew gles includes in our header
    GLuint texture_id;

    glGenTextures(1, &texture_id);
    glBindTexture(  GL_TEXTURE_EXTERNAL_OES, texture_id);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(  GL_TEXTURE_EXTERNAL_OES, 0);
    m_textureId = texture_id;
  }
  else
  {
    ThreadMessageCallback callbackData;
    callbackData.callback = &CallbackInitSurfaceTexture;
    callbackData.userptr  = (void*)this;

    // wait for it.
    CApplicationMessenger::GetInstance().SendMsg(TMSG_CALLBACK, -1, -1, static_cast<void*>(&callbackData));
  }

  m_surfaceTexture = std::shared_ptr<CJNISurfaceTexture>(new CJNISurfaceTexture(m_textureId));
  // hook the surfaceTexture OnFrameAvailable callback
  m_frameAvailable = std::shared_ptr<CDVDMediaCodecOnFrameAvailable>(new CDVDMediaCodecOnFrameAvailable(m_surfaceTexture));
  m_jnisurface = new CJNISurface(*m_surfaceTexture);
  m_surface = ANativeWindow_fromSurface(xbmc_jnienv(), m_jnisurface->get_raw());

  return;
}

void CDVDVideoCodecAndroidMediaCodec::ReleaseSurfaceTexture(void)
{
  if (m_render_sw || m_render_surface)
    return;

  // it is safe to delete here even though these items
  // were created in the main thread instance
  SAFE_DELETE(m_jnisurface);
  m_frameAvailable.reset();
  m_surfaceTexture.reset();

  if (m_textureId > 0)
  {
    GLuint texture_id = m_textureId;
    glDeleteTextures(1, &texture_id);
    m_textureId = 0;
  }
}


void CDVDVideoCodecAndroidMediaCodec::surfaceChanged(CJNISurfaceHolder holder, int format, int width, int height)
{
}

void CDVDVideoCodecAndroidMediaCodec::surfaceCreated(CJNISurfaceHolder holder)
{
  if (m_state == MEDIACODEC_STATE_STOPPED)
  {
    ConfigureMediaCodec();
  }
}

void CDVDVideoCodecAndroidMediaCodec::surfaceDestroyed(CJNISurfaceHolder holder)
{
  if (m_state != MEDIACODEC_STATE_STOPPED && m_state != MEDIACODEC_STATE_UNINITIALIZED)
  {
    m_state = MEDIACODEC_STATE_STOPPED;
    if(m_surface)
      ANativeWindow_release(m_surface);
    AMediaCodec_stop(m_codec);
  }
}
