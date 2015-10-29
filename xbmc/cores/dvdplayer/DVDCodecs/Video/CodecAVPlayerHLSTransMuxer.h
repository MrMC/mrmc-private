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
#include "cores/dvdplayer/DVDStreamInfo.h"

struct seginfo_struct;
struct AVFormatContext;
struct AVCodecParserContext;

class CCodecAVPlayerHLSTransMuxer : public CThread
{
public:

  CCodecAVPlayerHLSTransMuxer();
 ~CCodecAVPlayerHLSTransMuxer();

  bool            Open(const CDVDStreamInfo &hints);
  void            Close();
  bool            Write(uint8_t* pData, int iSize, double dts, double pts, double fps);

private:
  seginfo_struct  *m_seginfo;
  AVFormatContext *m_ofmt_ctx;
  AVCodecParserContext *m_parser_ctx;
};