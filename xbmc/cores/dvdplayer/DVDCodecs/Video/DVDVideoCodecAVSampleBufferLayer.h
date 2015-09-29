#pragma once
/*
 *      Copyright (C) 2015 Team MrMC
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
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "DVDVideoCodec.h"
#include "threads/Thread.h"

#include <CoreMedia/CoreMedia.h>

class CBitstreamConverter;

class CDVDVideoCodecAVSampleBufferLayer : public CDVDVideoCodec, CThread
{
public:
  CDVDVideoCodecAVSampleBufferLayer();
  virtual ~CDVDVideoCodecAVSampleBufferLayer();

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
  virtual void  Process();

  double        GetPlayerPtsSeconds();

  void                   *m_decoder;   // opaque decoder reference
  CMFormatDescriptionRef  m_fmt_desc;
  int32_t                 m_format;
  const char             *m_pFormatName;
  bool                    m_DropPictures;
  bool                    m_decode_async;
  DVDVideoPicture         m_videobuffer;
  CBitstreamConverter    *m_bitstream;

  int m_width;
  int m_height;
  double m_dts;
  double m_pts;
};
