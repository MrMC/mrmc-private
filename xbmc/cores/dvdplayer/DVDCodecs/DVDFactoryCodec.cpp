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

#include "system.h"
#include "utils/log.h"

#include "DVDFactoryCodec.h"
#include "Video/DVDVideoCodec.h"
#include "Audio/DVDAudioCodec.h"
#include "Overlay/DVDOverlayCodec.h"
#include "cores/dvdplayer/DVDCodecs/DVDCodecs.h"

#if defined(TARGET_DARWIN_OSX)
  #include "Video/DVDVideoCodecVDA.h"
  #include "Video/DVDVideoCodecVideoToolBox.h"
#endif
#if defined(TARGET_DARWIN_IOS)
  #include "utils/SystemInfo.h"
  #include "Video/DVDVideoCodecVideoToolBox.h"
  #include "Video/DVDVideoCodecAVFoundation.h"
#endif
#include "Video/DVDVideoCodecFFmpeg.h"
#include "Video/DVDVideoCodecLibMpeg2.h"
#if defined(TARGET_ANDROID)
  #include "Video/DVDVideoCodecAndroidMediaCodec.h"
  #include "platform/android/activity/AndroidFeatures.h"
#endif
#include "Audio/DVDAudioCodecFFmpeg.h"
#include "Audio/DVDAudioCodecPassthrough.h"
#if defined(TARGET_DARWIN)
  #include "Audio/DVDAudioCodecAudioConverter.h"
#endif
#include "Overlay/DVDOverlayCodecSSA.h"
#include "Overlay/DVDOverlayCodecText.h"
#include "Overlay/DVDOverlayCodecTX3G.h"
#include "Overlay/DVDOverlayCodecFFmpeg.h"


#include "DVDStreamInfo.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/VideoSettings.h"
#include "utils/StringUtils.h"

CDVDVideoCodec* CDVDFactoryCodec::OpenCodec(CDVDVideoCodec* pCodec, CDVDStreamInfo &hints, CDVDCodecOptions &options )
{
  try
  {
    CLog::Log(LOGDEBUG, "FactoryCodec - Video: %s - Opening", pCodec->GetName());
    if( pCodec->Open( hints, options ) )
    {
      CLog::Log(LOGDEBUG, "FactoryCodec - Video: %s - Opened", pCodec->GetName());
      return pCodec;
    }

    CLog::Log(LOGDEBUG, "FactoryCodec - Video: %s - Failed", pCodec->GetName());
    pCodec->Dispose();
    delete pCodec;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "FactoryCodec - Video: Failed with exception");
  }
  return nullptr;
}

CDVDAudioCodec* CDVDFactoryCodec::OpenCodec(CDVDAudioCodec* pCodec, CDVDStreamInfo &hints, CDVDCodecOptions &options )
{
  try
  {
    CLog::Log(LOGDEBUG, "FactoryCodec - Audio: %s - Opening", pCodec->GetName());
    if( pCodec->Open( hints, options ) )
    {
      CLog::Log(LOGDEBUG, "FactoryCodec - Audio: %s - Opened", pCodec->GetName());
      return pCodec;
    }

    CLog::Log(LOGDEBUG, "FactoryCodec - Audio: %s - Failed", pCodec->GetName());
    pCodec->Dispose();
    delete pCodec;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "FactoryCodec - Audio: Failed with exception");
  }
  return nullptr;
}

CDVDOverlayCodec* CDVDFactoryCodec::OpenCodec(CDVDOverlayCodec* pCodec, CDVDStreamInfo &hints, CDVDCodecOptions &options )
{
  try
  {
    CLog::Log(LOGDEBUG, "FactoryCodec - Overlay: %s - Opening", pCodec->GetName());
    if( pCodec->Open( hints, options ) )
    {
      CLog::Log(LOGDEBUG, "FactoryCodec - Overlay: %s - Opened", pCodec->GetName());
      return pCodec;
    }

    CLog::Log(LOGDEBUG, "FactoryCodec - Overlay: %s - Failed", pCodec->GetName());
    pCodec->Dispose();
    delete pCodec;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "FactoryCodec - Audio: Failed with exception");
  }
  return nullptr;
}


CDVDVideoCodec* CDVDFactoryCodec::CreateVideoCodec(CDVDStreamInfo &hint, const CRenderInfo &info)
{
  CDVDVideoCodec* pCodec = nullptr;
  CDVDCodecOptions options;

  if (info.formats.empty())
    options.m_formats.push_back(RENDER_FMT_YUV420P);
  else
    options.m_formats = info.formats;

  options.m_opaque_pointer = info.opaque_pointer;


  if ((hint.codec == AV_CODEC_ID_MPEG2VIDEO || hint.codec == AV_CODEC_ID_MPEG1VIDEO) && (hint.stills || hint.filename == "dvd"))
  {
     // If dvd is an mpeg2 and hint.stills
     if ( (pCodec = OpenCodec(new CDVDVideoCodecLibMpeg2(), hint, options)) ) return pCodec;
  }

#if defined(TARGET_DARWIN_OSX)
  if (!hint.software && CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEVDA))
  {
    if (hint.codec == AV_CODEC_ID_H264 && !hint.ptsinvalid)
    {
      if ( (pCodec = OpenCodec(new CDVDVideoCodecVDA(), hint, options)) ) return pCodec;
    }
  }
#endif

#if defined(TARGET_DARWIN)
  if (!hint.software)
  {
    switch(hint.codec)
    {
      case AV_CODEC_ID_H264:
      case AV_CODEC_ID_HEVC:
      case AV_CODEC_ID_MPEG4:
        if (hint.codec == AV_CODEC_ID_H264 && hint.ptsinvalid)
          break;
        if (hint.codec == AV_CODEC_ID_HEVC && hint.ptsinvalid)
          break;
        if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEVIDEOTOOLBOX))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecVideoToolBox(), hint, options)) ) return pCodec;
#if defined(TARGET_DARWIN_IOS)
        if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEAVF))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAVFoundation(), hint, options)) ) return pCodec;
#endif
        break;
      default:
        break;
    }
  }
#endif

#if defined(TARGET_ANDROID)
  if (!hint.software && CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEMEDIACODECSURFACE))
  {
    bool render_interlaced = CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEMEDIACODECSURFACE_INTERLACED);
    switch(hint.codec)
    {
      case AV_CODEC_ID_MPEG4:
      case AV_CODEC_ID_MSMPEG4V2:
      case AV_CODEC_ID_MSMPEG4V3:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELMPEG4))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(true, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_MPEG1VIDEO:
      case AV_CODEC_ID_MPEG2VIDEO:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELMPEG2))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(true, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_H264:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELH264))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(true, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_HEVC:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELHEVC))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(true, render_interlaced), hint, options)) ) return pCodec;
        break;
      default:
        CLog::Log(LOGINFO, "MediaCodec (Surface) Video Decoder...");
        if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(true, render_interlaced), hint, options)) ) return pCodec;
    }
  }
  if (!hint.software && CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEMEDIACODEC))
  {
    bool render_interlaced = CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEMEDIACODEC_INTERLACED);
    switch(hint.codec)
    {
      case AV_CODEC_ID_MPEG4:
      case AV_CODEC_ID_MSMPEG4V2:
      case AV_CODEC_ID_MSMPEG4V3:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELMPEG4))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(false, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_MPEG1VIDEO:
      case AV_CODEC_ID_MPEG2VIDEO:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELMPEG2))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(false, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_H264:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELH264))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(false, render_interlaced), hint, options)) ) return pCodec;
        break;
      case AV_CODEC_ID_HEVC:
        if (hint.width > CSettings::GetInstance().GetInt(CSettings::SETTING_VIDEOPLAYER_ACCELHEVC))
          if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(false, render_interlaced), hint, options)) ) return pCodec;
        break;
      default:
        CLog::Log(LOGINFO, "MediaCodec Video Decoder...");
        if ( (pCodec = OpenCodec(new CDVDVideoCodecAndroidMediaCodec(false, render_interlaced), hint, options)) ) return pCodec;
    }
  }
#endif

  std::string value = StringUtils::Format("%d", info.max_buffer_size);
  options.m_keys.push_back(CDVDCodecOption("surfaces", value));
  pCodec = OpenCodec(new CDVDVideoCodecFFmpeg(), hint, options);
  if (pCodec)
    return pCodec;

  return nullptr;
}

CDVDAudioCodec* CDVDFactoryCodec::CreateAudioCodec(CDVDStreamInfo &hint, bool allowpassthrough)
{
  CDVDAudioCodec* pCodec = NULL;
  CDVDCodecOptions options;

  // we don't use passthrough if "sync playback to display" is enabled
  if (allowpassthrough)
  {
    pCodec = OpenCodec(new CDVDAudioCodecPassthrough(), hint, options);
    if( pCodec ) return pCodec;
  }

// users report 9db reduction on AppleTV4/4K, disable until resoved
#if false && defined(TARGET_DARWIN)
  if (hint.codec == AV_CODEC_ID_AC3 || hint.codec == AV_CODEC_ID_EAC3)
  {
    if (!hint.realtime && hint.filename != "dvd")
    {
      pCodec = OpenCodec(new CDVDAudioCodecAudioConverter(), hint, options);
      if( pCodec )
        return pCodec;
    }
  }
#endif

  pCodec = OpenCodec(new CDVDAudioCodecFFmpeg(), hint, options);
  if (pCodec)
    return pCodec;

  return nullptr;
}

CDVDOverlayCodec* CDVDFactoryCodec::CreateOverlayCodec( CDVDStreamInfo &hint )
{
  CDVDOverlayCodec* pCodec = NULL;
  CDVDCodecOptions options;

  switch (hint.codec)
  {
    case AV_CODEC_ID_TEXT:
    case AV_CODEC_ID_SUBRIP:
      pCodec = OpenCodec(new CDVDOverlayCodecText(), hint, options);
      if (pCodec)
        return pCodec;
      break;

    case AV_CODEC_ID_SSA:
    case AV_CODEC_ID_ASS:
      pCodec = OpenCodec(new CDVDOverlayCodecSSA(), hint, options);
      if (pCodec)
        return pCodec;

      pCodec = OpenCodec(new CDVDOverlayCodecText(), hint, options);
      if (pCodec)
        return pCodec;
      break;

    case AV_CODEC_ID_MOV_TEXT:
      pCodec = OpenCodec(new CDVDOverlayCodecTX3G(), hint, options);
      if (pCodec)
        return pCodec;
      break;

    default:
      pCodec = OpenCodec(new CDVDOverlayCodecFFmpeg(), hint, options);
      if (pCodec)
        return pCodec;
      break;
  }

  return nullptr;
}
