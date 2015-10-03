#pragma once
/*
 *      Copyright (C) 2015 Team MrMC
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

#if defined(HAVE_VIDEOTOOLBOXDECODER)

#include <queue>

#include "DVDVideoCodec.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreMedia/CMTime.h>
#include <VideoToolbox/VideoToolbox.h>

struct frame_queue;
class CBitstreamConverter;

class CDVDVideoCodecVTB : public CDVDVideoCodec
{
public:
  CDVDVideoCodecVTB();
  virtual ~CDVDVideoCodecVTB();

  // Required overrides
  virtual bool  Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void  Dispose(void);
  virtual int   Decode(uint8_t *pData, int iSize, double dts, double pts);
  virtual void  Reset(void);
  virtual bool  GetPicture(DVDVideoPicture *pDvdVideoPicture);
  virtual bool  ClearPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual void  SetDropState(bool bDrop);
  virtual const char* GetName(void) { return (const char*)m_pFormatName; }

protected:
  void          DisplayQueuePop(void);
  void          CreateVTSession(int width, int height, CMVideoFormatDescriptionRef fmt_desc);
  void          DestroyVTSession(void);
  static void   VTDecoderCallback(
                  void              *decompressionOutputRefCon,
                  void              *sourceFrameRefCon,
                  OSStatus           status,
                  VTDecodeInfoFlags  infoFlags,
                  CVImageBufferRef   imageBuffer,
                  CMTime             presentationTimeStamp,
                  CMTime             presentationDuration);
  void          FrameRateTracking(double pts);

  CMFormatDescriptionRef  m_fmt_desc;
  const char             *m_pFormatName;
  bool                    m_DropPictures;
  DVDVideoPicture         m_videobuffer;

  double                  m_last_pts;
  double                  m_framerate;
  uint64_t                m_framecount;

  CBitstreamConverter    *m_bitstream;
  void                   *m_vt_session;     // opaque videotoolbox session
  double                  m_sort_time_offset;
  pthread_mutex_t         m_queue_mutex;    // mutex protecting queue manipulation
  frame_queue            *m_display_queue;  // display-order queue - next display frame is always at the queue head
  int32_t                 m_queue_depth;    // we will try to keep the queue depth at m_max_ref_frames
  int32_t                 m_max_ref_frames;
};

#endif
