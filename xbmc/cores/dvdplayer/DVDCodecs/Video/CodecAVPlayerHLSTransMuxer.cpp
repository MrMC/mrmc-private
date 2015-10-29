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

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "libavutil/opt.h"

#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include "cores/dvdplayer/DVDCodecs/Video/CodecAVPlayerHLSTransMuxer.h"
#include "cores/dvdplayer/DVDClock.h"
#include "filesystem/File.h"
#include "utils/BitstreamConverter.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

typedef struct seginfo_struct
{
  const char *prefix;
  const char *prefixpath;
  const char *m3u8filepath;
  const char *m3u8tmpfilepath;
  int         num_segments;
  int         target_duration;
  //
  int         seg_index;
  int         bgn_segment;
  int         end_segment;
  double      segment_time;
  double      prev_segment_time;
  double      ptsbased_segment_time;
  double      prev_ptsbased_segment_time;
  std::vector<double> actual_durations;
} seginfo_struct;

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
static int write_index_file(const seginfo_struct *options, const int end)
{
  std::string m3u8;
  
  m3u8 += "#EXTM3U\n";
  m3u8 += "#EXT-X-VERSION:3\n";

  // The EXT-X-TARGETDURATION tag specifies
  // the maximum media file duration, find it.
  float max_duration = options->target_duration;
  for (int i = options->bgn_segment; i <= options->end_segment; ++i)
  {
    if (options->actual_durations[i-1] > max_duration)
      max_duration = options->actual_durations[i-1];
  }
  m3u8 += StringUtils::Format(
    "#EXT-X-TARGETDURATION:%lu\n",
    (unsigned long)rint(max_duration));

  // if we are rotating segments over a fixed number of segments
  // update the sequence number with the next current segment.
  // if this is not present, readers assume the media sequence value is zero.
  if (options->num_segments)
    m3u8 += StringUtils::Format(
      "#EXT-X-MEDIA-SEQUENCE:%u\n", options->bgn_segment);

  // #EXTINF durations must be within 20 % of advertised duration
  for (int i = options->bgn_segment; i <= options->end_segment; ++i)
  {
    m3u8 += StringUtils::Format(
      "#EXTINF:%f,\n"
      "%s-%04d.ts\n",
      options->actual_durations[i-1], options->prefix, i);
  }

  // only add when all segments have been written,
  // readers will periodically re-read the m3u8 index if this is not present.
  if (end)
    m3u8 += "#EXT-X-ENDLIST\n";

  // write out to a temp file, then rename to real file
  FILE *m3u8_fp = fopen(options->m3u8tmpfilepath, "w");
  if (m3u8_fp)
  {
    fwrite(m3u8.c_str(), m3u8.size(), 1, m3u8_fp);
    fclose(m3u8_fp);
  }

  return rename(options->m3u8tmpfilepath, options->m3u8filepath);
}

static void update_index_file(seginfo_struct *options, int end)
{
  // if num_segments is not zero, we are rotating segments over a fixed number of segments
  // so we have to increment the bgn/end segment values, then remove the dropped segment file.
  // if num_segments is zero, then just ncrement the bgn/end segment values.
  int remove_file = 0;
  if (options->num_segments && (options->end_segment - options->bgn_segment) >= options->num_segments - 1)
  {
    remove_file = 1;
    options->bgn_segment++;
  }

  options->end_segment++;
  write_index_file(options, end);

  if (remove_file)
  {
    std::string remove_filename = StringUtils::Format("%s-%04u.ts", options->prefixpath, options->bgn_segment - 1);
    remove(remove_filename.c_str());
  }
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
CCodecAVPlayerHLSTransMuxer::CCodecAVPlayerHLSTransMuxer()
: CThread       ("CCodecAVPlayerHLSTransMuxer")
, m_seginfo     (nullptr                      )
, m_ofmt_ctx    (nullptr                      )
, m_parser_ctx  (nullptr                      )
{
}

CCodecAVPlayerHLSTransMuxer::~CCodecAVPlayerHLSTransMuxer()
{
  Close();
}

bool CCodecAVPlayerHLSTransMuxer::Open(const CDVDStreamInfo &hints)
{
  CLog::Log(LOGNOTICE, "CCodecAVPlayerHLSTransMuxer::Open:");

  // setup some defaults
  m_seginfo = new seginfo_struct;
  m_seginfo->prefix = "test";
  m_seginfo->prefixpath  = "/Users/davilla/Movies/tmp";
  m_seginfo->m3u8filepath= "/Users/davilla/Movies/tmp/test.m3u8";
  m_seginfo->m3u8tmpfilepath = "/Users/davilla/Movies/tmp/.test.m3u8";
  m_seginfo->seg_index = 1;
  m_seginfo->num_segments  = 0;
  m_seginfo->target_duration = 10;
  m_seginfo->bgn_segment = 1;
  m_seginfo->end_segment = 0;
  m_seginfo->segment_time = 0;
  m_seginfo->prev_segment_time = 0;
  m_seginfo->ptsbased_segment_time = 0;
  m_seginfo->prev_ptsbased_segment_time = 0;

  m_parser_ctx = av_parser_init(AV_CODEC_ID_H264);
  m_parser_ctx->flags |= PARSER_FLAG_COMPLETE_FRAMES;
  ;;m_parser_ctx->flags |= PARSER_FLAG_ONCE;
  // setup the ffmpeg output routines
  avformat_alloc_output_context2(&m_ofmt_ctx, NULL, "mpegts", NULL);
  if (!m_ofmt_ctx) {
    CLog::Log(LOGERROR, "Could not allocated output context");
    return false;
  }
  if (m_ofmt_ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
    CLog::Log(LOGERROR, "bullshit");
  m_ofmt_ctx->duration = 0;
  m_ofmt_ctx->start_time = 0;

  // create a new video stream
  AVStream *output_stream = avformat_new_stream(m_ofmt_ctx, NULL);
  output_stream->id = 0;
  output_stream->time_base.num = 1;
  output_stream->time_base.den = 90000;
  // means all stream should contains stream data only,
  // and other data is provided by setting AVCodecContext.extradata.
  // for H264 it is SPS and PPS data
  // Place global headers in extradata instead of every keyframe
  if (m_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    output_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
  // hlsenc.c in ffmpeg has this. more voodoo.
  if (m_ofmt_ctx->oformat->priv_class && m_ofmt_ctx->priv_data)
    av_opt_set(m_ofmt_ctx->priv_data, "mpegts_flags", "resend_headers", 0);

  // setup the codec context for the new stream
  AVCodecContext *codec_ctx = output_stream->codec;
  codec_ctx->width = hints.width;
  codec_ctx->height = hints.height;
  codec_ctx->codec_id = AV_CODEC_ID_H264;
  codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  // extradata_data/extradata_size is already padded with FF_INPUT_BUFFER_PADDING_SIZE
  codec_ctx->extradata = (uint8_t*)av_mallocz(hints.extrasize);
  codec_ctx->extradata_size = hints.extrasize;
  memcpy(codec_ctx->extradata, hints.extradata, hints.extrasize);

  // setup codec timebase
  // this seems wrong and is opposite to others.
  // but reversing them gives the wrong timebase in the
  // resulting pts from av_stream_get_end_pts, wtf ?
  if (hints.rfpsrate > 0 && hints.rfpsscale != 0)
  {
    // check ffmpeg r_frame_rate 1st
    codec_ctx->time_base.num = hints.rfpsscale;
    codec_ctx->time_base.den = hints.rfpsrate;
  }
  else if (hints.fpsrate > 0 && hints.fpsscale != 0)
  {
    // then ffmpeg avg_frame_rate next
    codec_ctx->time_base.num = hints.fpsscale;
    codec_ctx->time_base.den = hints.fpsrate;
  }
  AVRational codec_timebase = {hints.rfpsscale, hints.rfpsrate};
  av_codec_set_pkt_timebase(codec_ctx, codec_timebase);

  AVRational video_ratio;
  if (hints.forced_aspect)
    video_ratio = av_d2q((double)hints.width/hints.height, SHRT_MAX);
  else
    video_ratio = av_d2q(hints.aspect, SHRT_MAX);
  codec_ctx->sample_aspect_ratio = video_ratio;
  output_stream->sample_aspect_ratio = codec_ctx->sample_aspect_ratio;
  output_stream->need_parsing = AVSTREAM_PARSE_FULL;

  AVCodec *codec = avcodec_find_decoder(CODEC_ID_H264);
  if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
  {
    CLog::Log(LOGERROR, "Could not open codec");
    return false;
  }

  std::string output_filename = StringUtils::Format("%s/%s-%04d.ts",
    m_seginfo->prefixpath, m_seginfo->prefix, m_seginfo->seg_index++);
  if (avio_open(&m_ofmt_ctx->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0)
  {
    CLog::Log(LOGERROR, "Could not open '%s'", output_filename.c_str());
    return false;
  }

  // only write the header for the 1st segment
  // doing this for all segments will fail mediastreamvalidator
  // as this is not a m3u8 index file with EXT-X-DISCONTINUITY set.
  // also, only write the trailer on the final segment.
  avformat_write_header(m_ofmt_ctx, NULL);

  return true;
}

void CCodecAVPlayerHLSTransMuxer::Close()
{
  if (m_seginfo)
  {
    CLog::Log(LOGNOTICE, "CCodecAVPlayerHLSTransMuxer::Close");
    SAFE_DELETE(m_seginfo);
  }
  if (m_ofmt_ctx)
  {
    for(size_t i = 0; i < m_ofmt_ctx->nb_streams; ++i)
    {
      av_freep(&m_ofmt_ctx->streams[i]->codec);
      av_freep(&m_ofmt_ctx->streams[i]);
    }
    av_freep(&m_ofmt_ctx);
  }
  if (m_parser_ctx)
    av_parser_close(m_parser_ctx), m_parser_ctx = nullptr;
}

bool CCodecAVPlayerHLSTransMuxer::Write(uint8_t* pData, int iSize, double dts, double pts, double fps)
{
  // these are our two (in/out) timebases
  AVRational i_timebase;
  i_timebase.num = 1;
  i_timebase.den = DVD_TIME_BASE;
  AVRational o_timebase = m_ofmt_ctx->streams[0]->time_base;

  if (pData)
  {
    AVPacket packet;
    av_init_packet(&packet);

    // create an avpacket with DVDPlayer timebase unit
    packet.dts = AV_NOPTS_VALUE;
    if (dts != DVD_NOPTS_VALUE)
      packet.dts = dts;

    packet.pts = AV_NOPTS_VALUE;
    if (pts != DVD_NOPTS_VALUE)
      packet.pts = pts;

    packet.size = iSize;
    packet.data = pData;
    packet.stream_index = 0;
    // Duration of this packet in AVStream->time_base units, 0 if unknown.
    packet.duration = (1.0 / fps) * DVD_TIME_BASE;
    if (CBitstreamParser::FindIdrSlice(pData, iSize))
      packet.flags |= AV_PKT_FLAG_KEY;
    packet.pos = -1;

    int outbuf_size = 0;
    uint8_t *outbuf = NULL;
    int len = av_parser_parse2(m_parser_ctx
      , m_ofmt_ctx->streams[0]->codec, &outbuf, &outbuf_size
      , packet.data, packet.size
      , packet.pts
      , packet.dts
      , packet.pos);
    // our parse is setup to parse complete frames, and we are passing
    // complete frames, so we don't care about outbufs. we only need
    // the codec context updated.

    // rescale packet timing to mpegts container
    av_packet_rescale_ts(&packet, i_timebase, o_timebase);

    // use video stream as segment time base and split at keyframes
    if (packet.pts != AV_NOPTS_VALUE && packet.flags & AV_PKT_FLAG_KEY)
    {
      m_seginfo->segment_time = packet.pts * av_q2d(o_timebase);
      CLog::Log(LOGDEBUG, "**** found AV_PKT_FLAG_KEY segment_time(%f)", m_seginfo->segment_time);
    }

    // check for segment target duration, a keyframe 0.5 sec before
    // target duration is ok.
    if (m_seginfo->segment_time - m_seginfo->prev_segment_time >= m_seginfo->target_duration - 0.5)
    {
      // flush any buffered data
      av_write_frame(m_ofmt_ctx, NULL);
      // flush/close current mpegts segment file
      avio_flush(m_ofmt_ctx->pb);
      avio_close(m_ofmt_ctx->pb);

      // segment_time/prev_segment_time is only an approximation
      // of the real segment duration. use the actual output video stream
      // to get the ending pts for this segment's duration.
      int64_t lastpts = av_stream_get_end_pts(m_ofmt_ctx->streams[0]);
      m_seginfo->ptsbased_segment_time = (double)lastpts * av_q2d(o_timebase);
      CLog::Log(LOGDEBUG, "**** update ptsbased_segment_time(%f)", m_seginfo->ptsbased_segment_time);
      double actual_duration = m_seginfo->ptsbased_segment_time - m_seginfo->prev_ptsbased_segment_time;
      m_seginfo->actual_durations.push_back(actual_duration);

      // update m3u8 index file
      update_index_file(m_seginfo, 0);

      // open a new mpegts file for the next segment.
      std::string output_filename = StringUtils::Format("%s/%s-%04d.ts", m_seginfo->prefixpath, m_seginfo->prefix, m_seginfo->seg_index++);
      if (avio_open(&m_ofmt_ctx->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0)
      {
        CLog::Log(LOGERROR, "Could not open '%s'", output_filename.c_str());
        return false;
      }
      m_seginfo->prev_segment_time = m_seginfo->segment_time;
      m_seginfo->prev_ptsbased_segment_time = m_seginfo->ptsbased_segment_time;
    }

    // write out video packets into the segment
    int ret = av_write_frame(m_ofmt_ctx, &packet);
    //int ret = av_interleaved_write_frame(m_ofmt_ctx, &packet);
    if (ret < 0)
      CLog::Log(LOGERROR, "Warning: Could not write frame of stream(%d)", ret);
    else if (ret > 0)
    {
      CLog::Log(LOGDEBUG, "End of stream requested");
      av_free_packet(&packet);
      return false;
    }
    av_free_packet(&packet);

    //int64_t lastpts = av_stream_get_end_pts(m_ofmt_ctx->streams[0]);
    //double ptsbased_segment_time = (double)lastpts * av_q2d(o_timebase);
    //CLog::Log(LOGDEBUG, "**** last ptsbased_segment_time(%f)", ptsbased_segment_time);
  }
  else
  {
    // done, write out last segment and update index file

    // av_write_trailer will do a avio_flush
    av_write_trailer(m_ofmt_ctx);
    avio_close(m_ofmt_ctx->pb);

    // calc the last segment duration.
    int64_t lastpts = av_stream_get_end_pts(m_ofmt_ctx->streams[0]);
    m_seginfo->ptsbased_segment_time = (double)lastpts * av_q2d(o_timebase);
    double actual_duration = m_seginfo->ptsbased_segment_time - m_seginfo->prev_ptsbased_segment_time;
    // make sure that the last segment duration is at least one second
    // this might be more voodoo since we are using float based durations.
    if (actual_duration < 1.0)
      actual_duration = 1.0;
    m_seginfo->actual_durations.push_back(actual_duration);

    // last update of m3u8 index file
    update_index_file(m_seginfo, 1);
  }

  return true;
}
