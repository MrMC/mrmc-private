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
}

#include "cores/avfplayer/DynamicHLSTransMuxer.h"
#include "filesystem/File.h"
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
  std::vector<double> actual_durations;
} seginfo_struct;

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
static int write_index_file(const seginfo_struct &options, const int bgn_segment, const int end_segment, const int end)
{
  std::string m3u8;
  
  m3u8 += "#EXTM3U\n";
  m3u8 += "#EXT-X-VERSION:3\n";

  // The EXT-X-TARGETDURATION tag specifies
  // the maximum media file duration, find it.
  float max_duration = options.target_duration;
  for (int i = bgn_segment; i <= end_segment; ++i)
  {
    if (options.actual_durations[i-1] > max_duration)
      max_duration = options.actual_durations[i-1];
  }
  m3u8 += StringUtils::Format(
    "#EXT-X-TARGETDURATION:%lu\n",
    (unsigned long)rint(max_duration));

  // if we are rotating segments over a fixed number of segments
  // update the sequence number with the next current segment.
  // if this is not present, readers assume the media sequence value is zero.
  if (options.num_segments)
    m3u8 += StringUtils::Format(
      "#EXT-X-MEDIA-SEQUENCE:%u\n", bgn_segment);

  // #EXTINF durations must be within 20 % of advertised duration
  for (int i = bgn_segment; i <= end_segment; ++i)
  {
    m3u8 += StringUtils::Format(
      "#EXTINF:%f,\n"
      "%s-%04d.ts\n",
      options.actual_durations[i-1], options.prefix, i);
  }

  // only add when all segments have been written,
  // readers will periodically re-read the m3u8 index if this is not present.
  if (end)
    m3u8 += "#EXT-X-ENDLIST\n";

  // write out to a temp file, then rename to real file
  FILE *m3u8_fp = fopen(options.m3u8tmpfilepath, "w");
  if (m3u8_fp)
  {
    fwrite(m3u8.c_str(), m3u8.size(), 1, m3u8_fp);
    fclose(m3u8_fp);
  }

  return rename(options.m3u8tmpfilepath, options.m3u8filepath);
}

static void update_index_file(const seginfo_struct &options, int &bgn_segment, int &end_segment, int end)
{
  // if num_segments is not zero, we are rotating segments over a fixed number of segments
  // so we have to increment the bgn/end segment values, then remove the dropped segment file.
  // if num_segments is zero, then just ncrement the bgn/end segment values.
  int remove_file = 0;
  if (options.num_segments && (end_segment - bgn_segment) >= options.num_segments - 1)
  {
    remove_file = 1;
    bgn_segment++;
  }

  write_index_file(options, bgn_segment, ++end_segment, end);

  if (remove_file)
  {
    std::string remove_filename = StringUtils::Format("%s-%04u.ts", options.prefixpath, bgn_segment - 1);
    remove(remove_filename.c_str());
  }
}

static AVStream *add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream)
{
  AVStream *output_stream = avformat_new_stream(output_format_context, 0);
  if (!output_stream)
  {
      CLog::Log(LOGERROR, "add_output_stream: Could not allocate stream");
      return nullptr;
  }

  AVCodecContext *dec_ctx = input_stream->codec;
  AVCodecContext *enc_ctx = output_stream->codec;
  switch (dec_ctx->codec_type)
  {
    default:
    case AVMEDIA_TYPE_VIDEO:
      avcodec_copy_context(enc_ctx, dec_ctx);
      break;

    case AVMEDIA_TYPE_AUDIO:
      avcodec_copy_context(enc_ctx, dec_ctx);
      break;
  }
  // means all stream should contains stream data only,
  // and other data is provided by setting AVCodecContext.extradata.
  // For AAC it is the ADTS header, for H264 it is SPS and PPS data
  if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

  return output_stream;
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
CDynamicHLSTransMuxer::CDynamicHLSTransMuxer()
: CThread                 ("CDynamicHLSTransMuxer")
, m_abort                 ( false        )
, m_ready                 ( true         )
, m_speed                 ( 0            )
, m_paused                ( false        )
, m_audio_index           (-1            )
, m_audio_count           ( 0            )
, m_video_index           (-1            )
, m_video_count           ( 0            )
, m_subtitle_index        (-1            )
, m_subtitle_count        ( 0            )
{
}

CDynamicHLSTransMuxer::~CDynamicHLSTransMuxer()
{
  Close();
}

void CDynamicHLSTransMuxer::Open(const CFileItem &fileitem)
{
  CLog::Log(LOGNOTICE, "CDynamicHLSTransMuxer::Open: %s", fileitem.GetPath().c_str());
  m_fileitem = fileitem;
  m_infile = new XFILE::CFile();
  m_infile->Open(m_fileitem.GetPath());
  Create();
}

void CDynamicHLSTransMuxer::Close()
{
  if (m_infile)
  {
    CLog::Log(LOGNOTICE, "CDynamicHLSTransMuxer::Close");
    m_infile->Close();
    SAFE_DELETE(m_infile);
  }
}

// avio input callbacks (interrupt, read, seek)
int CDynamicHLSTransMuxer::infile_interrupt_cb(void* ctx)
{
  CDynamicHLSTransMuxer *transmuxer = static_cast<CDynamicHLSTransMuxer*>(ctx);
  if (transmuxer && transmuxer->m_abort)
    return 1;
  return 0;
}
int CDynamicHLSTransMuxer::infile_read(void *ctx, uint8_t* buf, int size)
{
  if (infile_interrupt_cb(ctx))
    return AVERROR_EXIT;

  CDynamicHLSTransMuxer *transmuxer = static_cast<CDynamicHLSTransMuxer*>(ctx);
  return (int)transmuxer->m_infile->Read(buf, size);
}
int64_t CDynamicHLSTransMuxer::infile_seek(void *ctx, int64_t pos, int whence)
{
  if (infile_interrupt_cb(ctx))
    return AVERROR_EXIT;

  CDynamicHLSTransMuxer *transmuxer = static_cast<CDynamicHLSTransMuxer*>(ctx);
  return transmuxer->m_infile->Seek(pos, whence);
}

void CDynamicHLSTransMuxer::Process()
{
  // these are relative to input streams
  int               in_vindex = -1;
  int               in_aindex = -1;
  // ffmpeg vars
  AVIOContext      *in_ioctx  = nullptr;
  AVStream         *vstream   = nullptr;

  AVBitStreamFilterContext *bsf_toannexb = nullptr;
  const AVIOInterruptCB interrupt_callback = {infile_interrupt_cb, this};

  // setup some defaults
  seginfo_struct seginfo;
  seginfo.prefix = "test";
  seginfo.prefixpath  = "/Users/davilla/Movies/tmp";
  seginfo.m3u8filepath= "/Users/davilla/Movies/tmp/test.m3u8";
  seginfo.m3u8tmpfilepath = "/Users/davilla/Movies/tmp/.test.m3u8";
  seginfo.num_segments  = 0;
  seginfo.target_duration = 10;

  // setup the ffmpeg input routines
  #define AVIO_INPUT_BUFFER_SIZE (512*1024) // default avio input read size
  uint8_t* buffer = (uint8_t*)av_malloc(AVIO_INPUT_BUFFER_SIZE);
  in_ioctx = avio_alloc_context(buffer, AVIO_INPUT_BUFFER_SIZE,
    0, this, infile_read, NULL, infile_seek);
  //
  m_ifmt_ctx = avformat_alloc_context();
  m_ifmt_ctx->pb = in_ioctx;
  m_ifmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
  m_ifmt_ctx->interrupt_callback = interrupt_callback;
  //
  int ret = avformat_open_input(&m_ifmt_ctx, m_fileitem.GetPath().c_str(), NULL, NULL);
  if (ret != 0) {
    CLog::Log(LOGERROR, "Could not open input file, make sure it is a mpegts file: %d", ret);
    exit(1);
  }
  if (avformat_find_stream_info(m_ifmt_ctx, NULL) < 0) {
    CLog::Log(LOGERROR, "Could not read stream information");
    exit(1);
  }

  // setup the ffmpeg output routines
  avformat_alloc_output_context2(&m_ofmt_ctx, NULL, "mpegts", NULL);
  if (!m_ofmt_ctx) {
    CLog::Log(LOGERROR, "Could not allocated output context");
    exit(1);
  }
  // find the streams we want
  for (size_t i = 0; i < m_ifmt_ctx->nb_streams; i++)
  {
    // default stream to AVDISCARD_ALL, changed if we want the stream.
    m_ifmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    switch (m_ifmt_ctx->streams[i]->codec->codec_type)
    {
      case AVMEDIA_TYPE_VIDEO:
        if (in_vindex < 0)
        {
          in_vindex = (int)i;
          m_ifmt_ctx->streams[i]->discard = AVDISCARD_NONE;
          AVCodecContext *vcodec = m_ifmt_ctx->streams[i]->codec;
          AVCodec *decoder = avcodec_find_decoder(vcodec->codec_id);
          if (!decoder)
          {
            CLog::Log(LOGERROR,
              "Could not find video decoder %x, key frames will not be honored", vcodec->codec_id);
            if (avcodec_open2(vcodec, decoder, NULL) < 0)
              CLog::Log(LOGERROR, "Could not open video decoder, key frames will not be honored");
          }

          vstream = add_output_stream(m_ofmt_ctx, m_ifmt_ctx->streams[i]);
          if (vstream->codec->extradata[0] == 0x01)
          {
            bsf_toannexb = av_bitstream_filter_init("h264_mp4toannexb");
            // voodoo :)
            int dummy_int;
            uint8_t *dummy_p;
            av_bitstream_filter_filter(bsf_toannexb, vstream->codec, NULL, &dummy_p, &dummy_int, NULL, 0, 0);
          }
          m_video_index = m_ofmt_ctx->nb_streams - 1;
          m_video_count++;
        }
        break;

      case AVMEDIA_TYPE_AUDIO:
        if (in_aindex < 0)
        {
          in_aindex = (int)i;
          m_ifmt_ctx->streams[i]->discard = AVDISCARD_NONE;
          AVCodecContext *acodec = m_ifmt_ctx->streams[i]->codec;
          AVCodec *decoder = avcodec_find_decoder(acodec->codec_id);
          if (!decoder)
          {
            CLog::Log(LOGERROR,
              "Could not find audio decoder %x", acodec->codec_id);
          }
          else
          {
            if (avcodec_open2(acodec, decoder, NULL) < 0)
              CLog::Log(LOGERROR, "Could not open audio decoder");
          }

          add_output_stream(m_ofmt_ctx, m_ifmt_ctx->streams[i]);
          m_audio_index = m_ofmt_ctx->nb_streams - 1;
          m_audio_count++;
        }
        break;

      default:
        break;
    }
  }

  // do not print warnings when PTS and DTS are identical.
  //in_fmtctx->flags |= AVFMT_FLAG_IGNDTS;

  // hlsenc.c in ffmpeg has this. more voodoo.
  if (m_ofmt_ctx->oformat->priv_class && m_ofmt_ctx->priv_data)
    av_opt_set(m_ofmt_ctx->priv_data, "mpegts_flags", "resend_headers", 0);

  int output_index = 1;
  std::string output_filename = StringUtils::Format("%s/%s-%04d.ts", seginfo.prefixpath, seginfo.prefix, output_index++);
  if (avio_open(&m_ofmt_ctx->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0)
  {
    CLog::Log(LOGERROR, "Could not open '%s'", output_filename.c_str());
    exit(1);
  }

  // only write the header for the 1st segment
  // doing this for all segments will fail mediastreamvalidator
  // as this is not a m3u8 index file with EXT-X-DISCONTINUITY set.
  // also, only write the trailer on the final segment.
  avformat_write_header(m_ofmt_ctx, NULL);

  int decode_done;
  int bgn_segment = 1;
  int end_segment = 0;
  double segment_time = 0;
  double prev_segment_time = 0;
  double ptsbased_segment_time = 0;
  double prev_ptsbased_segment_time = 0;
  do
  {
    AVPacket packet;
    av_init_packet(&packet);

    decode_done = av_read_frame(m_ifmt_ctx, &packet);
    if (decode_done < 0)
      break;

    if (av_dup_packet(&packet) < 0)
    {
      CLog::Log(LOGERROR, "Could not duplicate packet");
      av_free_packet(&packet);
      break;
    }

    // rescale packet timing to mpegts container
    // todo: the ot_fmtctx index assumes any streams dropped
    // will be after video/audio. need to fix this later.
    AVStream *i_stream = m_ifmt_ctx->streams[packet.stream_index];
    AVStream *o_stream = m_ofmt_ctx->streams[packet.stream_index];
    av_packet_rescale_ts(&packet, i_stream->time_base, o_stream->time_base);
    packet.pos = -1;

    // use video stream as segment time base and split at keyframes
    if (packet.stream_index == in_vindex && (packet.flags & AV_PKT_FLAG_KEY))
      segment_time = packet.pts * av_q2d(vstream->time_base);

    // check for segment target duration, a keyframe 0.5 sec before
    // target duration is ok.
    if (segment_time - prev_segment_time >= seginfo.target_duration - 0.5)
    {
      // flush/close current mpegts segment file
      avio_flush(m_ofmt_ctx->pb);
      avio_close(m_ofmt_ctx->pb);

      // segment_time/prev_segment_time is only an approximation
      // of the real segment duration. use the actual output video stream
      // to get the ending pts for this segment's duration.
      int64_t lastpts = av_stream_get_end_pts(vstream);
      ptsbased_segment_time = (double)lastpts * av_q2d(vstream->time_base);
      double actual_duration = ptsbased_segment_time - prev_ptsbased_segment_time;
      seginfo.actual_durations.push_back(actual_duration);

      // update m3u8 index file
      update_index_file(seginfo, bgn_segment, end_segment, 0);

      // open a new mpegts file for the next segment.
      output_filename = StringUtils::Format("%s/%s-%04d.ts", seginfo.prefixpath, seginfo.prefix, output_index++);
      if (avio_open(&m_ofmt_ctx->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0)
      {
        CLog::Log(LOGERROR, "Could not open '%s'", output_filename.c_str());
        break;
      }
      prev_segment_time = segment_time;
      prev_ptsbased_segment_time = ptsbased_segment_time;
    }

    // convert video (h264) to Annex-B format if needed.
    if (packet.stream_index == in_vindex && bsf_toannexb)
    {
      AVPacket new_pkt = packet;
      ret = av_bitstream_filter_filter(bsf_toannexb, vstream->codec, NULL,
        &new_pkt.data, &new_pkt.size, packet.data, packet.size, packet.flags & AV_PKT_FLAG_KEY);
      if (ret > 0)
      {
        packet.side_data = NULL;
        packet.side_data_elems = 0;
        av_free_packet(&packet);
        new_pkt.buf = av_buffer_create(new_pkt.data, new_pkt.size, av_buffer_default_free, NULL, 0);
      }
      else if (ret == 0)
      {
        CLog::Log(LOGDEBUG, "write_frame: ret == 0, pkt.data(%p), new_pkt.data(%p)", packet.data, new_pkt.data);
      }
      else if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s failed for stream %d, codec %s",
          bsf_toannexb->filter->name, packet.stream_index, vstream->codec->codec ? vstream->codec->codec->name : "copy");
      }
      packet = new_pkt;
    }

    // write out video/audio packets into the segment
    ret = av_interleaved_write_frame(m_ofmt_ctx, &packet);
    if (ret < 0)
      CLog::Log(LOGERROR, "Warning: Could not write frame of stream(%d)", ret);
    else if (ret > 0)
    {
      CLog::Log(LOGDEBUG, "End of stream requested");
      av_free_packet(&packet);
      break;
    }
    av_free_packet(&packet);

  } while (!decode_done);

  // done, write out last segment and update index file

  // av_write_trailer will do a avio_flush
  av_write_trailer(m_ofmt_ctx);
  avio_close(m_ofmt_ctx->pb);

  // calc the last segment duration.
  int64_t lastpts = av_stream_get_end_pts(vstream);
  ptsbased_segment_time = (double)lastpts * av_q2d(vstream->time_base);
  double actual_duration = ptsbased_segment_time - prev_ptsbased_segment_time;
  // make sure that the last segment duration is at least one second
  // this might be more voodoo since we are using float based durations.
  if (actual_duration < 1.0)
    actual_duration = 1.0;
  seginfo.actual_durations.push_back(actual_duration);

  // last update of m3u8 index file
  update_index_file(seginfo, bgn_segment, end_segment, 1);

  // close down ffmpeg output operations
  if (vstream)
    avcodec_close(vstream->codec);
  if (bsf_toannexb)
    av_bitstream_filter_close(bsf_toannexb);
  for(size_t i = 0; i < m_ofmt_ctx->nb_streams; ++i)
  {
    av_freep(&m_ofmt_ctx->streams[i]->codec);
    av_freep(&m_ofmt_ctx->streams[i]);
  }
  av_freep(&m_ofmt_ctx);
  // close down ffmpeg input operations
  avformat_close_input(&m_ifmt_ctx);
  if (in_ioctx)
  {
    av_freep(&in_ioctx->buffer);
    av_freep(&in_ioctx);
  }
}
