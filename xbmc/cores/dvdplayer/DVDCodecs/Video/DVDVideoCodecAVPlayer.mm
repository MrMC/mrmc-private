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

#import "config.h"

#if defined(TARGET_DARWIN_TVOS)
#import "cores/dvdplayer/DVDCodecs/Video/DVDVideoCodecAVPlayer.h"
#import "cores/dvdplayer/DVDCodecs/Video/CodecAVPlayerHLSTransMuxer.h"

#import "cores/dvdplayer/DVDClock.h"
#import "cores/dvdplayer/DVDStreamInfo.h"
#import "cores/VideoRenderers/RenderManager.h"
#import "platform/darwin/AutoPool.h"
#import "platform/darwin/DarwinUtils.h"
#import "platform/darwin/tvos/MainController.h"
#import "utils/BitstreamConverter.h"
#import "utils/log.h"

#include <dlfcn.h>

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

#pragma mark - CAVPState
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
enum AVPSTATE
{
  ERROR = 0,
  IDLE,
  SETUP,
  START,
  PAUSE,
  PLAY,
  PLAYING,
  STOP,
};

class CAVPSTATE
{
public:
  CAVPSTATE()
  {
    pthread_mutex_init(&m_mutex, nullptr);
  }

 ~CAVPSTATE()
  {
    pthread_mutex_destroy(&m_mutex);
  }

  void set(AVPSTATE state)
  {
    pthread_mutex_lock(&m_mutex);

    m_stateQueue.push_back(state);
    pthread_mutex_unlock(&m_mutex);
  }

  AVPSTATE get()
  {
    static AVPSTATE playerstate = AVPSTATE::IDLE;

    pthread_mutex_lock(&m_mutex);
    size_t dequeue_size = m_stateQueue.size();
    if (dequeue_size > 0)
    {
      // serialized state is the front element.
      playerstate = m_stateQueue.front();
      m_stateQueue.pop_front();
    }

    pthread_mutex_unlock(&m_mutex);
    return playerstate;
  }

  size_t size()
  {
    return m_stateQueue.size();
  }
protected:
  pthread_mutex_t      m_mutex;
  std::deque<AVPSTATE> m_stateQueue;
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
// This codec renders direct to a UIView/CALayer via AVSampleBufferLayer.
// DVDPlayer/VideoRenderer runs in bypass mode as we totally bypass them.
CDVDVideoCodecAVPlayer::CDVDVideoCodecAVPlayer()
: CDVDVideoCodec()
, CThread             ("DVDVideoCodecAVPlayer")
, m_decoder           (nullptr                )
, m_muxer             (nullptr                )
, m_pFormatName       ("avp-"                 )
, m_speed             (DVD_PLAYSPEED_NORMAL   )
, m_bitstream         (nullptr                )
, m_withBlockRunning  (false                  )
, m_framecount        (0                      )
, m_framerate_ms      (24000.0/1001.0         )
{
  //m_avp_state = new CAVPState();
  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));
  pthread_mutex_init(&m_trackerQueueMutex, nullptr);
}

CDVDVideoCodecAVPlayer::~CDVDVideoCodecAVPlayer()
{
  Dispose();
}

bool CDVDVideoCodecAVPlayer::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  //if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEAVP) && !hints.software)
  {
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
        // we want annex-b, not avcC. use a bitstream converter for all flavors,
        m_bitstream = new CBitstreamConverter;
        if (!m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, true))
          return false;

        m_pFormatName = "avp-h264";
      break;

      default:
        return false;
      break;
    }

    //m_max_ref_frames = std::max(max_ref_frames + 1, 5);
    m_max_ref_frames = 5;

    m_width = width;
    m_height = height;

    m_muxer = new CCodecAVPlayerHLSTransMuxer();

    CDVDStreamInfo muxerhints = hints;
    muxerhints.extradata = m_bitstream->GetExtraData();
    muxerhints.extrasize = m_bitstream->GetExtraSize();
    m_muxer->Open(muxerhints);
    // or CDVDStreamInfo will delete it from under us.
    muxerhints.extradata = nullptr;
    muxerhints.extrasize = 0;
    //Create();

    //m_messages->enqueue(START);
    return true;
  }

  return false;
}

void CDVDVideoCodecAVPlayer::Dispose()
{
  StopThread();

  if (m_decoder)
  {
    DrainQueues();
    pthread_mutex_destroy(&m_trackerQueueMutex);
  }
  SAFE_DELETE(m_muxer);
  SAFE_DELETE(m_bitstream);
  //SAFE_DELETE(m_avf_state);
}

int CDVDVideoCodecAVPlayer::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  if (pData)
  {
    m_bitstream->Convert(pData, iSize);
    int frameSize = m_bitstream->GetConvertSize();
    uint8_t *frame = m_bitstream->GetConvertBuffer();

    UpdateFrameRateTracking(pts);
    m_muxer->Write(frame, frameSize, dts, pts, m_framerate_ms);

    // handle fake picture tracker
    m_dts = dts;
    m_pts = pts;
    pktTracker *tracker = new pktTracker;
    tracker->dts = dts;
    tracker->pts = pts;
    // want demux size as passed by player.
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

void CDVDVideoCodecAVPlayer::Reset(void)
{
  //m_messages->enqueue(RESET);
  //m_messages->enqueue(START);
}

bool CDVDVideoCodecAVPlayer::GetPicture(DVDVideoPicture* pDvdVideoPicture)
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

void CDVDVideoCodecAVPlayer::SetDropState(bool bDrop)
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

void CDVDVideoCodecAVPlayer::SetSpeed(int iSpeed)
{
  if (iSpeed == m_speed)
    return;

  switch(iSpeed)
  {
    case DVD_PLAYSPEED_PAUSE:
      //m_messages->enqueue(PAUSE);
      break;
    default:
    case DVD_PLAYSPEED_NORMAL:
      //m_messages->enqueue(PLAY);
      break;
  }
  m_speed = iSpeed;
}

int CDVDVideoCodecAVPlayer::GetDataSize(void)
{
  pthread_mutex_lock(&m_trackerQueueMutex);

  int datasize = 0;
  std::list<pktTracker*>::iterator it;
  for(it = m_trackerQueue.begin(); it != m_trackerQueue.end(); ++it)
    datasize += (*it)->size;

  pthread_mutex_unlock(&m_trackerQueueMutex);

  CLog::Log(LOGDEBUG, "CDVDVideoCodecAVPlayer::GetDataSize(%d)", datasize);

  return datasize;
}

double CDVDVideoCodecAVPlayer::GetTimeSize(void)
{
  double timesize = 0.0;

  pthread_mutex_lock(&m_trackerQueueMutex);
  if (m_framerate_ms > 0.0)
    timesize = m_trackerQueue.size() * (m_framerate_ms / 1000.0);
  pthread_mutex_unlock(&m_trackerQueueMutex);
  CLog::Log(LOGDEBUG, "CDVDVideoCodecAVPlayer::GetTimeSize(%f)", timesize);

  // lie to DVDPlayer, it is hardcoded to a max of 8 seconds,
  // if you buffer more than 8 seconds, it goes nuts.
  if (timesize < 0.0)
    timesize = 0.0;
  else if (timesize > 7.0)
    timesize = 7.0;

  return timesize;
}

void CDVDVideoCodecAVPlayer::Process()
{
/*
  CLog::Log(LOGDEBUG, "CDVDVideoCodecAVPlayer::Process Started");

  // bump our priority to be level with the krAEken (ActiveAE)
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);

    //CDynamicHLSTransMuxer *segmenter = new CDynamicHLSTransMuxer();
    //segmenter->Open(m_item);

    //for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    //{
    //  NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
    //}

    m_avf_state->set(AVFSTATE::SETUP);

    int speed = m_speed;
    bool stopPlaying = false;
    CRect oldSrcRect, oldDestRect, oldViewRect;
    while (!stopPlaying)
    {
      AVFSTATE player_state = m_avf_state->get();
      switch(player_state)
      {
        default:
        case AVFSTATE::IDLE:
        break;

        case AVFSTATE::SETUP:
        {

          // AVPlayerLayerViewNew create MUST be done on main thread or
          // it will not get updates when a new video frame is decoded and presented.
          dispatch_sync(dispatch_get_main_queue(),^{
            // <scheme>://<net_loc>/<path>;<params>?<query>#<fragment>
            std::string path = "/" + m_item.GetPath();
            NSString *filePath = [NSString stringWithUTF8String: path.c_str()];
            NSURLComponents *components = [NSURLComponents new];
            components.scheme = MrMCScheme;
            components.host   = @"mrmc-tvos.com";
            components.path   = filePath;

            CGRect frame = CGRectMake(0, 0,
              g_xbmcController.view.frame.size.width,
              g_xbmcController.view.frame.size.height);
            m_avp_avplayer = [[AVPlayerLayerViewNew alloc] initWithFrameAndUrl:frame withURL:[components URL]];
          });

          // wait for playback to start with 20 second timeout
          if (WaitForReadyToPlay(20000))
            m_avp_state->set(AVFSTATE::PLAY);
          else
          {
            // playback never started, some error or timeout
            m_avp_state->set(AVFSTATE::STOP);
          }
        }
        break;

        case AVFSTATE::PLAY:
        {
          dispatch_sync(dispatch_get_main_queue(),^{
            [g_xbmcController insertVideoView:m_avp_avplayer];
            [[m_avp_avplayer player] play];
          });
          m_avp_state->set(AVFSTATE::PLAYING);
        }
        break;

        case AVFSTATE::PAUSE:
        {
          dispatch_sync(dispatch_get_main_queue(),^{
            [[m_avp_state player] pause];
          });
          m_avf_state->set(AVFSTATE::IDLE);
        }
        break;

        case AVFSTATE::PLAYING:
        {
          CMTime currentTime = m_avf_avplayer.player.currentTime;
          Float64 timeBase_s = CMTimeGetSeconds(currentTime);
          m_elapsed_ms = timeBase_s * 1000;

          if (m_duration_ms <= 0)
          {
            AVPlayerItem *thePlayerItem = m_avf_avplayer.player.currentItem;
            if (thePlayerItem.status == AVPlayerItemStatusReadyToPlay)
            {
              CMTime durationTime = thePlayerItem.duration;
              Float64 duration_s = CMTimeGetSeconds(durationTime);
              m_duration_ms = duration_s * 1000;
            }
          }

          if (m_elapsed_ms == m_duration_ms)
          {
            m_avf_state->set(AVFSTATE::STOP);
            continue;
          }

          if (speed != m_speed)
          {
            m_avf_avplayer.player.rate = m_speed;
            speed = m_speed;
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
                m_avf_avplayer.frame = frame;
                m_avf_avplayer.center= CGPointMake(m_avf_avplayer.center.x + offset.x, m_avf_avplayer.center.y + offset.y);
                // video layer needs to get resized too,
                // not sure why, it should track the view.
                m_avf_avplayer.videoLayer.frame = frame;
                // we start up hidden, kick off an animated fade in.
                if (m_avf_avplayer.hidden == YES)
                  [m_avf_avplayer setHiddenAnimated:NO delay:NSTimeInterval(0.1) duration:NSTimeInterval(2.0)];
              });
              oldSrcRect  = SrcRect;
              oldDestRect = DestRect;
              oldViewRect = ViewRect;
            }
          }
        }
        break;

        case AVFSTATE::STOP:
        {
          dispatch_sync(dispatch_get_main_queue(),^{
            [g_xbmcController removeVideoView:m_avf_avplayer];
            //[m_avf_avplayer.player.currentItem cancelPendingSeeks];
            //[m_avf_avplayer.player.currentItem.asset cancelLoading];
            [m_avf_avplayer.player pause];
            m_avf_avplayer = nullptr;
          });
          stopPlaying = true;
        }
        break;
      }

      if (!stopPlaying)
        usleep(250 * 1000);
    }
  }
  catch(char* error)
  {
    CLog::Log(LOGERROR, "%s", error);
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAVPlayer::Process Exception thrown");
  }
*/
}

void CDVDVideoCodecAVPlayer::DrainQueues()
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
}

double CDVDVideoCodecAVPlayer::GetPlayerPtsSeconds()
{
  double clock_pts = 0.0;
  CDVDClock *playerclock = CDVDClock::GetMasterClock();
  if (playerclock)
    clock_pts = playerclock->GetClock() / DVD_TIME_BASE;

  return clock_pts;
}

void CDVDVideoCodecAVPlayer::UpdateFrameRateTracking(double pts)
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
