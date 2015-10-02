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

#import "config.h"

#if defined(TARGET_DARWIN_IOS) && !defined(TARGET_DARWIN_TVOS)
#import "cores/dvdplayer/DVDCodecs/Video/DVDVideoCodecSampleBufferLayer.h"

#import "cores/dvdplayer/DVDClock.h"
#import "cores/dvdplayer/DVDStreamInfo.h"
#import "cores/VideoRenderers/RenderManager.h"
#import "platform/darwin/AutoPool.h"
#import "platform/darwin/DarwinUtils.h"
#import "platform/darwin/ios/SampleBufferLayerView.h"
#import "platform/darwin/ios/XBMCController.h"
#import "utils/BitstreamConverter.h"
#import "utils/log.h"

// tracks pts in and output queue in display order
typedef struct pktTracker {
  double dts;
  double pts;
  size_t size;
} pktTracker;

static bool pktTrackerSortPredicate(const pktTracker* lhs, const pktTracker* rhs)
{
  if (lhs->pts != DVD_NOPTS_VALUE && rhs->pts != DVD_NOPTS_VALUE)
    return lhs->pts < rhs->pts;
  else
    return false;
}

// helper function to create a CMSampleBufferRef from demuxer data.
// the demuxer data is in accV format, length byte is already present.
static CMSampleBufferRef
CreateSampleBufferFrom(CMFormatDescriptionRef fmt_desc,
  CMSampleTimingInfo *timingInfo, void *demux_buff, size_t demux_size)
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
      timingInfo,         // const CMSampleTimingInfo *sampleTimingArray
      1,                  // CMItemCount numSampleSizeEntries
      sampleSizeArray,    // const size_t *sampleSizeArray
      &sBufOut);          // CMSampleBufferRef *sBufOut
  }
  CFRelease(videoBlock);

  return sBufOut;
}

enum AVMESSAGE
{
  ERROR = 0,
  NONE,
  RESET,
  START,
  PAUSE,
  PLAY,
};
class CAVFCodecMessage
{
public:
  CAVFCodecMessage()
  {
    pthread_mutex_init(&m_mutex, nullptr);
  }

 ~CAVFCodecMessage()
  {
    pthread_mutex_destroy(&m_mutex);
  }
  
  void enqueue(AVMESSAGE msg)
  {
    pthread_mutex_lock(&m_mutex);
    m_messages.push(msg);
    pthread_mutex_unlock(&m_mutex);
  }

  AVMESSAGE dequeue()
  {
    pthread_mutex_lock(&m_mutex);
    AVMESSAGE msg = m_messages.front();
    m_messages.pop();
    pthread_mutex_unlock(&m_mutex);
    return msg;
  }

  size_t size()
  {
    return m_messages.size();
  }
protected:
  pthread_mutex_t       m_mutex;
  std::queue<AVMESSAGE> m_messages;
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
// This codec renders direct to a UIView/CALayer via AVSampleBufferLayer.
// DVDPlayer/VideoRenderer runs in bypass mode as we totally bypass them.
CDVDVideoCodecSampleBufferLayer::CDVDVideoCodecSampleBufferLayer()
: CDVDVideoCodec()
, CThread("DVDVideoCodecSampleBufferLayer")
, m_decoder(nullptr)
, m_pFormatName("sbl-")
, m_speed(DVD_PLAYSPEED_NORMAL)
, m_bitstream(nullptr)
, m_withBlockRunning(false)
, m_messages(nullptr)
, m_framecount(0)
, m_framerate_ms(24000.0/1001.0)
{
  m_messages = new CAVFCodecMessage();
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));
  pthread_mutex_init(&m_trackerQueueMutex, nullptr);
  pthread_mutex_init(&m_sampleBuffersMutex, nullptr);
  // the best way to feed the video layer.
  m_providerQueue = dispatch_queue_create("com.mrmc.avsm_providercallback", DISPATCH_QUEUE_SERIAL);
  dispatch_set_target_queue( m_providerQueue, dispatch_get_global_queue( DISPATCH_QUEUE_PRIORITY_HIGH, 0 ) );
}

CDVDVideoCodecSampleBufferLayer::~CDVDVideoCodecSampleBufferLayer()
{
  Dispose();
}

bool CDVDVideoCodecSampleBufferLayer::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  //if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USESBL) && !hints.software)
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
        m_pFormatName = "sbl-h264";
      break;
      default:
        return false;
      break;
    }

    // create a CMVideoFormatDescription from avcC extradata.
    // skip over avcC header (six bytes)
    uint8_t *spc_ptr = m_bitstream->GetExtraData() + 6;
    // length of sequence parameter set data
    uint32_t sps_size = BS_RB16(spc_ptr); spc_ptr += 2;
    // pointer to sequence parameter set data
    uint8_t *sps_ptr = spc_ptr; spc_ptr += sps_size;
    // number of picture parameter sets
    //uint32_t pps_cnt = *spc_ptr++;
    spc_ptr++;
    // length of picture parameter set data
    uint32_t pps_size = BS_RB16(spc_ptr); spc_ptr += 2;
    // pointer to picture parameter set data
    uint8_t *pps_ptr = spc_ptr;

    // check if we are possibly interlaced and how many reference frames are present.
    // note that this is not a definative check for interlaced, there are other
    // flags that can signal interlaced but these are inside NALs.
    bool interlaced = true;
    int max_ref_frames = 0;
    if (sps_size)
      m_bitstream->parseh264_sps(sps_ptr+1, sps_size-1, &interlaced, &max_ref_frames);
    if (interlaced)
    {
      CLog::Log(LOGNOTICE, "%s - possible interlaced content.", __FUNCTION__);
      return false;
    }
    // default to 5 min, this helps us feed correct pts to the player.
    m_max_ref_frames = std::max(max_ref_frames + 1, 5);

    // bitstream converter avcC's always have 4 byte NALs.
    int nalUnitHeaderLength  = 4;
    size_t parameterSetCount = 2;
    const uint8_t* const parameterSetPointers[2] = {
      (const uint8_t*)sps_ptr, (const uint8_t*)pps_ptr };
    const size_t parameterSetSizes[2] = {
      sps_size, pps_size };
    CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
     parameterSetCount, parameterSetPointers, parameterSetSizes, nalUnitHeaderLength, &m_fmt_desc);

    // SampleBufferLayerView create MUST be done on main thread or
    // it will not get updates when a new video frame is decoded and presented.
    __block SampleBufferLayerView *mcview = nullptr;
    dispatch_sync(dispatch_get_main_queue(),^{
      CGRect bounds = CGRectMake(0, 0, width, height);
      mcview = [[SampleBufferLayerView alloc] initWithFrame:bounds];
      [g_xbmcController insertVideoView:mcview];
    });
    m_decoder = mcview;

    m_width = width;
    m_height = height;

    Create();

    m_messages->enqueue(START);
    return true;
  }

  return false;
}

void CDVDVideoCodecSampleBufferLayer::Dispose()
{
  StopThread();

  if (m_decoder)
  {
    StopSampleProvider();
    DrainQueues();

    dispatch_release(m_providerQueue);
    pthread_mutex_destroy(&m_trackerQueueMutex);
    pthread_mutex_destroy(&m_sampleBuffersMutex);

    dispatch_sync(dispatch_get_main_queue(),^{
      SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;
      [g_xbmcController removeVideoView:mcview];
      [mcview release];
    });
    m_decoder = nullptr;
  }

  if (m_bitstream)
    delete m_bitstream, m_bitstream = nullptr;
  
  delete m_messages, m_messages = nullptr;
}

int CDVDVideoCodecSampleBufferLayer::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  if (pData)
  {
    m_bitstream->Convert(pData, iSize);
    int frameSize = m_bitstream->GetConvertSize();
    uint8_t *frame = m_bitstream->GetConvertBuffer();

    UpdateFrameRateTracking(pts);

    CMSampleTimingInfo sampleTimingInfo = kCMTimingInfoInvalid;
    if (m_framerate_ms > 0.0)
      sampleTimingInfo.duration = CMTimeMake(1.0/m_framerate_ms * DVD_TIME_BASE, DVD_TIME_BASE);
    if (dts != DVD_NOPTS_VALUE)
      sampleTimingInfo.decodeTimeStamp = CMTimeMake(dts, DVD_TIME_BASE);
    if (pts != DVD_NOPTS_VALUE)
      sampleTimingInfo.presentationTimeStamp = CMTimeMake(pts, DVD_TIME_BASE);

    CMSampleBufferRef sampleBuffer = CreateSampleBufferFrom(m_fmt_desc, &sampleTimingInfo, frame, frameSize);

    pthread_mutex_lock(&m_sampleBuffersMutex);
    m_sampleBuffers.push(sampleBuffer);
    pthread_mutex_unlock(&m_sampleBuffersMutex);

    m_dts = dts;
    m_pts = pts;
    pktTracker *tracker = new pktTracker;
    tracker->dts = dts;
    tracker->pts = pts;
    // want size as passed by player.
    tracker->size = iSize;
    pthread_mutex_lock(&m_trackerQueueMutex);
    m_trackerQueue.push_back(tracker);
    m_trackerQueue.sort(pktTrackerSortPredicate);
    pthread_mutex_unlock(&m_trackerQueueMutex);

    Sleep(10);
  }

  if (m_trackerQueue.size() < (2* m_max_ref_frames))
    return VC_BUFFER;

  return VC_PICTURE;
}

void CDVDVideoCodecSampleBufferLayer::Reset(void)
{
  m_messages->enqueue(RESET);
  m_messages->enqueue(START);
}

bool CDVDVideoCodecSampleBufferLayer::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (m_framerate_ms > 0.0)
    pDvdVideoPicture->iDuration     = 1.0 / m_framerate_ms * DVD_TIME_BASE;
  pDvdVideoPicture->format          = RENDER_FMT_BYPASS;
  pDvdVideoPicture->iFlags          = DVP_FLAG_ALLOCATED;
  pDvdVideoPicture->color_range     = 0;
  pDvdVideoPicture->color_matrix    = 4;
  pDvdVideoPicture->iWidth          = m_width;
  pDvdVideoPicture->iHeight         = m_height;
  pDvdVideoPicture->iDisplayWidth   = pDvdVideoPicture->iWidth;
  pDvdVideoPicture->iDisplayHeight  = pDvdVideoPicture->iHeight;

  if (m_trackerQueue.size() > 0)
  {
    pthread_mutex_lock(&m_trackerQueueMutex);
    pktTracker *tracker = m_trackerQueue.front();
    m_trackerQueue.pop_front();
    pthread_mutex_unlock(&m_trackerQueueMutex);
    pDvdVideoPicture->dts = tracker->dts;
    pDvdVideoPicture->pts = tracker->pts;
    delete tracker;
  }
  else
  {
    // we 'should' never get here as we are tracking demux passed to us
    // and for each pkt, we ether pass a fake frame or drop it.
    pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
    pDvdVideoPicture->pts = m_pts;
    if (m_speed == DVD_PLAYSPEED_NORMAL)
    {
      pDvdVideoPicture->pts = GetPlayerPtsSeconds() * (double)DVD_TIME_BASE;
      // video pts cannot be late or dvdplayer goes nuts,
      // so run it one frame ahead
      pDvdVideoPicture->pts += 1 * pDvdVideoPicture->iDuration;
    }
  }

  return true;
}

void CDVDVideoCodecSampleBufferLayer::SetDropState(bool bDrop)
{
  // this gets called before 'Decode',
  // it tells us to drop the next picture frame.
  // so pretend to drop the next picture frame.
  if (bDrop)
  {
    if (m_trackerQueue.size() >= m_max_ref_frames)
    {
      pthread_mutex_lock(&m_trackerQueueMutex);
      pktTracker *tracker = m_trackerQueue.front();
      m_trackerQueue.pop_front();
      pthread_mutex_unlock(&m_trackerQueueMutex);
      delete tracker;
    }
  }
}

void CDVDVideoCodecSampleBufferLayer::SetSpeed(int iSpeed)
{
  if (iSpeed == m_speed)
    return;

  switch(iSpeed)
  {
    case DVD_PLAYSPEED_PAUSE:
      m_messages->enqueue(PAUSE);
      break;
    default:
    case DVD_PLAYSPEED_NORMAL:
      m_messages->enqueue(PLAY);
      break;
  }
  m_speed = iSpeed;
}

int CDVDVideoCodecSampleBufferLayer::GetDataSize(void)
{
  pthread_mutex_lock(&m_trackerQueueMutex);

  int datasize = 0;
  std::list<pktTracker*>::iterator it;
  for(it = m_trackerQueue.begin(); it != m_trackerQueue.end(); ++it)
    datasize += (*it)->size;

  pthread_mutex_unlock(&m_trackerQueueMutex);

  CLog::Log(LOGDEBUG, "CDVDVideoCodecSampleBufferLayer::GetDataSize(%d)", datasize);

  return datasize;
}

double CDVDVideoCodecSampleBufferLayer::GetTimeSize(void)
{
  double timesize = 0.0;

  pthread_mutex_lock(&m_trackerQueueMutex);
  if (m_framerate_ms > 0.0)
    timesize = m_trackerQueue.size() * (m_framerate_ms / 1000.0);
  pthread_mutex_unlock(&m_trackerQueueMutex);
  CLog::Log(LOGDEBUG, "CDVDVideoCodecSampleBufferLayer::GetTimeSize(%f)", timesize);

  // lie to DVDPlayer, it is hardcoded to a max of 8 seconds,
  // if you buffer more than 8 seconds, it goes nuts.
  if (timesize < 0.0)
    timesize = 0.0;
  else if (timesize > 7.0)
    timesize = 7.0;

  return timesize;
}

void CDVDVideoCodecSampleBufferLayer::Process()
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecSampleBufferLayer::Process Started");

  // bump our priority to be level with the krAEken (ActiveAE)
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);

  AVMESSAGE message = NONE;
  CRect oldSrcRect, oldDestRect, oldViewRect;

  while (!m_bStop)
  {
    if (m_messages->size())
      message = m_messages->dequeue();
    switch(message)
    {
      default:
      case NONE:
      // retain the last state and do nothing.
      break;

      case START: // we are just starting up.
      {
        // player clock returns < zero if reset.
        double player_s = GetPlayerPtsSeconds();
        if (player_s > 0.0)
        {
          // startup with video timebase matching the player clock.
          SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;
          // video clock was stopped, set the starting time and crank it up.
          CMTimebaseSetTime(mcview.videoLayer.controlTimebase, CMTimeMake(player_s, 1));
          CMTimebaseSetRate(mcview.videoLayer.controlTimebase, 1.0);
          message = NONE;
          CLog::Log(LOGDEBUG, "%s - CDVDVideoCodecSampleBufferLayer::Start player_s(%f)", __FUNCTION__, player_s);
        }
      }
      break;

      case RESET:
      {
        // just reset here, someone else will start us up again if needed.
        dispatch_sync(dispatch_get_main_queue(),^{
          // Flush the previous enqueued sample buffers for display while scrubbing
          SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;
          // stop decoding by setting control timebase rate to zero.
          CMTimebaseSetRate(mcview.videoLayer.controlTimebase, 0.0);
          [mcview.videoLayer stopRequestingMediaData];
          m_withBlockRunning = false;
          [mcview.videoLayer flush];
          DrainQueues();
        });
        message = NONE;
        CLog::Log(LOGDEBUG, "%s - CDVDVideoCodecSampleBufferLayer::Reset", __FUNCTION__);
      }
      break;

      case PAUSE:
      {
        // to pause, we just set the video timebase rate to zero.
        // buffers in flight are retained but not shown until the rate is non-zero.
        SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;
        CMTimebaseSetRate(mcview.videoLayer.controlTimebase, 0.0);
        CLog::Log(LOGDEBUG, "%s - CDVDVideoCodecSampleBufferLayer::Pause", __FUNCTION__);
        message = NONE;
      }
      break;

      case PLAY:
      {
        SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;

        // check if the usingBlock is running, if not, start it up.
        if (!m_withBlockRunning && mcview.videoLayer.readyForMoreMediaData == YES)
        {
            StartSampleProviderWithBlock();
            m_withBlockRunning = true;
        }

        // sync video layer time base to dvdplayer's player clock.
        CMTime cmtime  = CMTimebaseGetTime(mcview.videoLayer.controlTimebase);
        Float64 timeBase_s = CMTimeGetSeconds(cmtime);

        // player clock returns < zero if reset. check it.
        double player_s = GetPlayerPtsSeconds();
        if (player_s > 0.0)
        {
          double error = fabs(timeBase_s - player_s);
          if (error > 0.150 && error < 0.250)
          {
            // if we are outside our error margin,
            // adjust the video timebase rate to bring us back in sync.
            if (timeBase_s > 0.0)
            {
              double rate = 1 * (player_s / timeBase_s);
              CMTimebaseSetRate(mcview.videoLayer.controlTimebase, rate);
              CLog::Log(LOGDEBUG, "adjusting playback "
                "rate(%f) timeBase_s(%f) player_s(%f), sampleBuffers(%lu), trackerQueue(%lu)",
                 rate, timeBase_s, player_s, m_sampleBuffers.size(), m_trackerQueue.size());
            }
            else
            {
              // video timebase value was zero, not sure why.
              // quess and set the rate to 1, if the rate is zero,
              // the videoLayer will not pull sample buffers to display
              // and we see nothing.
              CMTimebaseSetRate(mcview.videoLayer.controlTimebase, 1.0);
            }
          }
          else if (error > 0.250)
          {
            // large diff, try a big jump
            CMTimebaseSetTime(mcview.videoLayer.controlTimebase, CMTimeMake(player_s, 1));
            CMTimebaseSetRate(mcview.videoLayer.controlTimebase, 1.0);
          }
        }

        // if renderer is configured, we now know size and
        // where to display the video.
        if (g_renderManager.IsConfigured())
        {
          CRect SrcRect, DestRect, ViewRect;
          g_renderManager.GetVideoRect(SrcRect, DestRect, ViewRect);
          // update where we show the video in the view/layer
          // once renderer inits, we only need to update if something changes.
          if (SrcRect  != oldSrcRect  ||
              DestRect != oldDestRect ||
              ViewRect != oldViewRect)
          {
            // things that might touch iOS gui need to happen on main thread.
            dispatch_async(dispatch_get_main_queue(),^{
              CGRect frame = CGRectMake(
                DestRect.x1, DestRect.y1, DestRect.Width(), DestRect.Height());
              // save the offset
              CGPoint offset = frame.origin;
              // transform to zero x/y origin
              frame = CGRectOffset(frame, -frame.origin.x, -frame.origin.y);
              mcview.frame = frame;
              mcview.center= CGPointMake(mcview.center.x + offset.x, mcview.center.y + offset.y);
              // video layer needs to get resized too,
              // not sure why, it should track the view.
              mcview.videoLayer.frame = frame;
              // we startup hidden, kick off an animated fade in.
              if (mcview.hidden == YES)
                [mcview setHiddenAnimated:NO delay:NSTimeInterval(0.1) duration:NSTimeInterval(2.0)];
            });
            oldSrcRect  = SrcRect;
            oldDestRect = DestRect;
            oldViewRect = ViewRect;
          }
        }
      }
      break;
    }

    Sleep(100);
  }

  SetPriority(THREAD_PRIORITY_NORMAL);
  CLog::Log(LOGDEBUG, "CDVDVideoCodecSampleBufferLayer::Process Stopped");
}

void CDVDVideoCodecSampleBufferLayer::DrainQueues()
{
  pthread_mutex_lock(&m_trackerQueueMutex);
  while (!m_trackerQueue.empty())
  {
    // run backwards, so list does not have to reorder.
    pktTracker *tracker = m_trackerQueue.back();
    delete tracker;
    m_trackerQueue.pop_back();
  }
  pthread_mutex_unlock(&m_trackerQueueMutex);

  pthread_mutex_lock(&m_sampleBuffersMutex);
  while (!m_sampleBuffers.empty())
  {
    CMSampleBufferRef sampleBuffer = m_sampleBuffers.front();
    m_sampleBuffers.pop();
    CFRelease(sampleBuffer);
  }
  pthread_mutex_unlock(&m_sampleBuffersMutex);
}

void CDVDVideoCodecSampleBufferLayer::StartSampleProviderWithBlock()
{
  SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;

  // ok, for those that have never seen a usingBlock structure. these are
  // special, works like a mini-thread that fires when videoLayer
  // needs demux data. You need to pair this with StopSampleProvider
  // to stop the callbacks or very bad things might happen...
  [mcview.videoLayer requestMediaDataWhenReadyOnQueue:m_providerQueue usingBlock:^
  {
    while(mcview.videoLayer.readyForMoreMediaData)
    {
      if (m_sampleBuffers.size())
      {
        CMSampleBufferRef nextSampleBuffer = m_sampleBuffers.front();
        if (nextSampleBuffer)
        {
          [mcview.videoLayer enqueueSampleBuffer:nextSampleBuffer];
          [mcview setNeedsDisplay];

          CFRelease(nextSampleBuffer);
          pthread_mutex_lock(&m_sampleBuffersMutex);
          m_sampleBuffers.pop();
          pthread_mutex_unlock(&m_sampleBuffersMutex);

          if ([mcview.videoLayer status] == AVQueuedSampleBufferRenderingStatusFailed)
          {
            CLog::Log(LOGNOTICE, "%s - AFVDecoderDecode failed, status(%ld)",
              __FUNCTION__, (long)[mcview.videoLayer error].code);
          }
        }
      }
      else
      {
        //CLog::Log(LOGDEBUG, "%s: no more sample buffers to enqueue", __FUNCTION__);
        break;
      }
    }
    // yield a little here or we hammer the cpu
    usleep(5 * 1000);
  }];
}

void CDVDVideoCodecSampleBufferLayer::StopSampleProvider()
{
  dispatch_sync(dispatch_get_main_queue(),^{
    SampleBufferLayerView *mcview = (SampleBufferLayerView*)m_decoder;
    [mcview.videoLayer stopRequestingMediaData];
  });
}

double CDVDVideoCodecSampleBufferLayer::GetPlayerPtsSeconds()
{
  double clock_pts = 0.0;
  CDVDClock *playerclock = CDVDClock::GetMasterClock();
  if (playerclock)
    clock_pts = playerclock->GetClock() / DVD_TIME_BASE;

  return clock_pts;
}

void CDVDVideoCodecSampleBufferLayer::UpdateFrameRateTracking(double pts)
{
  static double last_pts = DVD_NOPTS_VALUE;

  m_framecount++;

  if (pts == DVD_NOPTS_VALUE)
  {
    last_pts = DVD_NOPTS_VALUE;
    return;
  }

  float duration = pts - last_pts;
  // if pts is re-ordered, the diff might be negative
  // flip it and try.
  if (duration < 0.0)
    duration = -duration;
  last_pts = pts;

  // clamp duration to sensible range,
  // 66 fsp to 20 fsp
  if (duration >= 15000.0 && duration <= 50000.0)
  {
    double framerate_ms;
    switch((int)(0.5 + duration))
    {
      // 59.940 (16683.333333)
      case 16000 ... 17000:
        framerate_ms = 60000.0 / 1001.0;
        break;

      // 50.000 (20000.000000)
      case 20000:
        framerate_ms = 50000.0 / 1000.0;
        break;

      // 49.950 (20020.000000)
      case 20020:
        framerate_ms = 50000.0 / 1001.0;
        break;

      // 29.970 (33366.666656)
      case 32000 ... 35000:
        framerate_ms = 30000.0 / 1001.0;
        break;

      // 25.000 (40000.000000)
      case 40000:
        framerate_ms = 25000.0 / 1000.0;
        break;

      // 24.975 (40040.000000)
      case 40040:
        framerate_ms = 25000.0 / 1001.0;
        break;

      /*
      // 24.000 (41666.666666)
      case 41667:
        framerate = 24000.0 / 1000.0;
        break;
      */

      // 23.976 (41708.33333)
      case 40200 ... 43200:
        // 23.976 seems to have the crappiest encodings :)
        framerate_ms = 24000.0 / 1001.0;
        break;

      default:
        framerate_ms = 0.0;
        //CLog::Log(LOGDEBUG, "%s: unknown duration(%f), cur_pts(%f)",
        //  __MODULE_NAME__, duration, cur_pts);
        break;
    }

    if (framerate_ms > 0.0 && (int)m_framerate_ms != (int)framerate_ms)
    {
      m_framerate_ms = framerate_ms;
      CLog::Log(LOGDEBUG, "%s: detected new framerate(%f) at frame(%llu)",
        __FUNCTION__, m_framerate_ms, m_framecount);
    }
  }
}

#endif
