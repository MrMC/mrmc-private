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

#include "FileItem.h"
#include "threads/Thread.h"

namespace XFILE 
{
  class CFile;
}

struct AVFormatContext;

class CDynamicHLSTransMuxer : public CThread
{
public:

  CDynamicHLSTransMuxer();
 ~CDynamicHLSTransMuxer();

  void            Open(const CFileItem &fileitem);
  void            Close();
protected:
  virtual void    Process();

private:
  static int      infile_interrupt_cb(void *ctx);
  static int      infile_read(void *ctx, uint8_t *buf, int size);
  static int64_t  infile_seek(void *ctx, int64_t  pos, int whence);

  bool            m_abort;
  CEvent          m_ready;
  int             m_speed;
  bool            m_paused;
  int             m_audio_index;
  int             m_audio_count;
  int             m_video_index;
  int             m_video_count;
  int             m_subtitle_index;
  int             m_subtitle_count;

  CFileItem       m_fileitem;
  XFILE::CFile   *m_infile;

  AVFormatContext *m_ifmt_ctx;
  AVFormatContext *m_ofmt_ctx;
};
