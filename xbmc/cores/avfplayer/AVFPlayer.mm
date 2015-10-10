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

#include "system.h"

#if defined(TARGET_DARWIN_IOS)
#include "cores/avfplayer/AVFPlayer.h"

#include "Application.h"
#include "FileItem.h"
#include "GUIInfoManager.h"
#include "video/VideoThumbLoader.h"
#include "Util.h"
#include "cores/AudioEngine/AEFactory.h"
//#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/VideoRenderers/RenderFlags.h"
#include "cores/VideoRenderers/RenderFormats.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "settings/AdvancedSettings.h"
#include "settings/VideoSettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/LangCodeExpander.h"
#include "utils/Variant.h"
#include "windowing/WindowingFactory.h"

// for external subtitles
#include "cores/dvdplayer/DVDClock.h"
#include "cores/dvdplayer/DVDPlayerSubtitle.h"
#include "cores/dvdplayer/DVDDemuxers/DVDDemuxVobsub.h"
#include "settings/VideoSettings.h"

#import <UIKit/UIKit.h>
#import <AVFoundation/AVPlayer.h>
#import <AVFoundation/AVAsset.h>
#import <AVFoundation/AVPlayerItem.h>
#import <AVFoundation/AVPlayerLayer.h>
#import <AVFoundation/AVAssetResourceLoader.h>

#import "platform/darwin/DarwinUtils.h"
#import "platform/darwin/NSLogDebugHelpers.h"
#if defined(TARGET_DARWIN_TVOS)
#import "platform/darwin/tvos/MainController.h"
#else
#import "platform/darwin/ios/XBMCController.h"
#endif

NSString * const MrMCScheme = @"mrmc";
static const NSString *ItemStatusContext;

#pragma mark - AVAssetResourceLoaderDelegate
@interface CFileResourceLoader : NSObject <AVAssetResourceLoaderDelegate>

@end

@implementation CFileResourceLoader
  XFILE::CFile   *m_cfile = nullptr;
  uint8_t        *buffer  = nullptr;

- (id)init
{
	self = [super init];
	return self;
}

- (void)dealloc
{
  if (m_cfile)
    SAFE_DELETE(m_cfile);
  if (buffer)
    SAFE_DELETE_ARRAY(buffer);

  [super dealloc];
}

- (void)fillInContentInformation:(AVAssetResourceLoadingRequest *)loadingRequest
{
  CLog::Log(LOGNOTICE, "resourceLoader contentRequest");
  AVAssetResourceLoadingContentInformationRequest *contentInformationRequest;
  contentInformationRequest = loadingRequest.contentInformationRequest;

  unsigned int flags = 0;//READ_TRUNCATED | READ_CHUNKED;
  //flags |= READ_AUDIO_VIDEO;
  //flags |= READ_NO_CACHE; // Make sure CFile honors our no-cache hint
  if (m_cfile)
    SAFE_DELETE(m_cfile);
  if (buffer)
   SAFE_DELETE_ARRAY(buffer);

  m_cfile = new XFILE::CFile();
  NSURL *resourceURL = [loadingRequest.request URL];
  if (m_cfile->Open([resourceURL.path UTF8String], flags))
  {
    //m_isSeekPossible = m_pFile->IoControl(XFILE::IOCTRL_SEEK_POSSIBLE, NULL) != 0;
    buffer = new uint8_t[2048*1024];
    //provide information about the content
    // https://developer.apple.com/library/ios/documentation/Miscellaneous/Reference/UTIRef/Articles/System-DeclaredUniformTypeIdentifiers.html
    NSString *mimeType = @"com.apple.quicktime-movie";
    contentInformationRequest.contentType = mimeType;
    contentInformationRequest.contentLength = m_cfile->GetLength();
    contentInformationRequest.byteRangeAccessSupported = YES;
  }
}

- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
  CLog::Log(LOGNOTICE, "resourceLoader shouldWaitForLoadingOfRequestedResource");
  BOOL canHandle = NO;

  NSURL *resourceURL = [loadingRequest.request URL];
  if ([resourceURL.scheme isEqualToString:MrMCScheme])
  {
    canHandle = YES;
    if (loadingRequest.contentInformationRequest != nil)
      [self fillInContentInformation:loadingRequest];

    if (loadingRequest.dataRequest != nil)
    {
      CLog::Log(LOGNOTICE, "resourceLoader dataRequest");

      AVAssetResourceLoadingDataRequest *dataRequest = loadingRequest.dataRequest;

      NSURLResponse* response = [[NSURLResponse alloc] initWithURL:resourceURL MIMEType:@"video/quicktime" expectedContentLength:[dataRequest requestedLength] textEncodingName:nil];
      [loadingRequest setResponse:response];
      [response release];

      CLog::Log(LOGNOTICE, "resourceLoader1 currentOffset(%lld), requestedOffset(%lld), requestedLength(%ld)",
        dataRequest.currentOffset, dataRequest.requestedOffset, dataRequest.requestedLength);

      NSUInteger remainingLength =
        [dataRequest requestedLength] - static_cast<NSUInteger>([dataRequest currentOffset] - [dataRequest requestedOffset]);

      m_cfile->Seek(dataRequest.currentOffset, SEEK_SET);
      do {
        NSUInteger receivedLength = dataRequest.requestedLength > 1024*1024 ? 1024 *1024 : dataRequest.requestedLength;
        receivedLength = m_cfile->Read(buffer, receivedLength);

        CLog::Log(LOGNOTICE, "resourceLoader2 currentOffset(%lld), requestedOffset(%lld), requestedLength(%ld)",
          dataRequest.currentOffset, dataRequest.requestedOffset, dataRequest.requestedLength);
        NSUInteger length = MIN(receivedLength, remainingLength);
        NSData* decodedData = [[NSData alloc] initWithBytes:buffer length:length];
        CLog::Log(LOGNOTICE, "resourceLoader [dataRequest respondWithData] length(%ld)", length);

        [dataRequest respondWithData:decodedData];
        [decodedData release];

        remainingLength -= length;
      } while (remainingLength);

      if ([dataRequest currentOffset] + [dataRequest requestedLength] >= [dataRequest requestedOffset])
        [loadingRequest finishLoading];
    }
  }

  return canHandle;
}

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  CLog::Log(LOGNOTICE, "resourceLoader didCancelLoadingRequest");
}

@end

#pragma mark - AVPlayerLayerViewNew
@interface AVPlayerLayerViewNew : UIView

@property (nonatomic, retain, strong) AVPlayer *player;
@property (nonatomic, retain, strong) AVPlayerLayer *videoLayer;
@property (nonatomic, retain, strong) CFileResourceLoader *cfileloader;


- (id)initWithFrameAndUrl:(CGRect)frame withURL:(NSURL *)URL;

- (void)setHiddenAnimated:(BOOL)hide
                    delay:(NSTimeInterval)delay
                 duration:(NSTimeInterval)duration;
@end

@implementation AVPlayerLayerViewNew

- (id)initWithFrameAndUrl:(CGRect)frame withURL:(NSURL *)URL;
{
	self = [super initWithFrame:frame];
	if (self)
	{
    [self setNeedsLayout];
    [self layoutIfNeeded];

    self.hidden = YES;

    //NSDictionary *options = @{ AVURLAssetPreferPreciseDurationAndTimingKey : @YES };

    AVURLAsset *asset = [[AVURLAsset alloc] initWithURL:URL options:nil];
    self.cfileloader = [[[CFileResourceLoader alloc] init] autorelease];
    [asset.resourceLoader setDelegate:self.cfileloader queue:dispatch_get_main_queue()];

    AVPlayerItem *playerItem = [[AVPlayerItem alloc] initWithAsset:asset];

    self.player = [[AVPlayer alloc] initWithPlayerItem:playerItem];
    self.videoLayer = [AVPlayerLayer playerLayerWithPlayer:self.player];
		self.videoLayer.frame = self.frame;
		self.videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
		self.videoLayer.backgroundColor = [[UIColor blackColor] CGColor];

    [[self layer] addSublayer:self.videoLayer];

    [playerItem release];
    [asset release];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didPlayToEndTime:) name:AVPlayerItemDidPlayToEndTimeNotification object:nil];

#if AVPLAYERLAYER_DEBUG_MESSAGES
    [self.videoLayer addObserver:self forKeyPath:@"error" options:NSKeyValueObservingOptionNew context:nullptr];
    [self.videoLayer addObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection" options:NSKeyValueObservingOptionNew context:nullptr];
#endif
  }

	return self;
}

- (void)dealloc
{
#if AVPLAYERLAYER_DEBUG_MESSAGES
  [self.videoLayer removeObserver:self forKeyPath:@"error"];
  [self.videoLayer removeObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection"];
#endif
  // humm, why do I need to do these releases ?
  [self.videoLayer removeFromSuperlayer];
  [self.videoLayer release];
  [self.player release];
  [self.cfileloader release];
  [super dealloc];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object
  change:(NSDictionary *)change context:(void *)context
{
  if (context == &ItemStatusContext)
  {
    CLog::Log(LOGNOTICE, "resourceLoader observeValueForKeyPath");
    return;
  }
  [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  return;
}

- (void)didPlayToEndTime:(NSNotification *)note
{
  NSLog(@"didPlayToEndTime");
}

- (void)setHiddenAnimated:(BOOL)hide
  delay:(NSTimeInterval)delay duration:(NSTimeInterval)duration
{
  [UIView animateWithDuration:duration delay:delay
      options:UIViewAnimationOptionAllowAnimatedContent animations:^{
      if (hide) {
        self.alpha = 0;
      } else {
        self.alpha = 0;
        self.hidden = NO; // We need this to see the animation 0 -> 1
        self.alpha = 1;
      }
    } completion:^(BOOL finished) {
      self.hidden = hide;
  }];
}
@end

#pragma mark - CAVFState
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
enum AVFSTATE
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

class CAVFState
{
public:
  CAVFState()
  {
    pthread_mutex_init(&m_mutex, nullptr);
  }

 ~CAVFState()
  {
    pthread_mutex_destroy(&m_mutex);
  }

  void set(AVFSTATE state)
  {
    pthread_mutex_lock(&m_mutex);

    m_stateQueue.push_back(state);
    pthread_mutex_unlock(&m_mutex);
  }

  AVFSTATE get()
  {
    static AVFSTATE playerstate = AVFSTATE::IDLE;

    pthread_mutex_lock(&m_mutex);
    size_t dequeue_size = m_stateQueue.size();
    if (dequeue_size > 0)
    {
      // serialized state is the front element.
      playerstate = m_stateQueue.front();
      // pop the front element if there are
      // more present.
      if (dequeue_size > 1)
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
  std::deque<AVFSTATE> m_stateQueue;
};

#pragma mark - CAVFPlayer
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
struct AVFChapterInfo
{
  std::string name;
  int64_t     seekto_ms;
};

struct AVFPlayerStreamInfo
{
  void Clear()
  {
    id                = 0;
    width             = 0;
    height            = 0;
    aspect_ratio_num  = 0;
    aspect_ratio_den  = 0;
    frame_rate_num    = 0;
    frame_rate_den    = 0;
    bit_rate          = 0;
    duration          = 0;
    channel           = 0;
    sample_rate       = 0;
    language          = "";
    type              = STREAM_NONE;
    source            = STREAM_SOURCE_NONE;
    name              = "";
    filename          = "";
    filename2         = "";
  }

  int           id;
  StreamType    type;
  StreamSource  source;
  int           width;
  int           height;
  int           aspect_ratio_num;
  int           aspect_ratio_den;
  int           frame_rate_num;
  int           frame_rate_den;
  int           bit_rate;
  int           duration;
  int           channel;
  int           sample_rate;
  int           format;
  std::string   language;
  std::string   name;
  std::string   filename;
  std::string   filename2;  // for vobsub subtitles, 2 files are necessary (idx/sub) 
};

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
CAVFPlayer::CAVFPlayer(IPlayerCallback &callback)
: IPlayer(callback)
, CThread                 ("CAVFPlayer" )
, m_speed                 (0            )
, m_paused                (false        )
, m_bAbortRequest         (false        )
, m_ready                 (true         )
, m_audio_index           (0            )
, m_audio_count           (0            )
, m_audio_delay           (0            )
, m_audio_passthrough_ac3 (false        )
, m_audio_passthrough_dts (false        )
, m_audio_mute            (false        )
, m_audio_volume          (0.0f         )
, m_video_index           (0            )
, m_video_count           (0            )
, m_video_width           (0            )
, m_video_height          (0            )
, m_video_fps_numerator   (0            )
, m_video_fps_denominator (0            )
, m_subtitle_index        (0            )
, m_subtitle_count        (0            )
, m_subtitle_show         (false        )
, m_subtitle_delay        (0            )
, m_chapter_count         (0            )
, m_show_mainvideo        (0            )
, m_view_mode             (0            )
, m_zoom                  (0            )
, m_contrast              (0            )
, m_brightness            (0            )
, m_avf_avplayer          (nullptr      )
{
  m_avf_state = new CAVFState();
  // for external subtitles
  m_dvdOverlayContainer = new CDVDOverlayContainer;
  m_dvdPlayerSubtitle   = new CDVDPlayerSubtitle(m_dvdOverlayContainer);

  // Suspend AE temporarily so exclusive or hog-mode sinks
  // don't block external player's access to audio device
  if (!CAEFactory::Suspend())
  {
    CLog::Log(LOGNOTICE,"%s: Failed to suspend AudioEngine before launching external player", __FUNCTION__);
  }
}

CAVFPlayer::~CAVFPlayer()
{
  CloseFile();

  delete m_avf_state;
  delete m_dvdPlayerSubtitle;
  delete m_dvdOverlayContainer;

  // Resume AE processing of XBMC native audio
  if (!CAEFactory::Resume())
  {
    CLog::Log(LOGFATAL, "%s: Failed to restart AudioEngine after return from external player",__FUNCTION__);
  }
}

bool CAVFPlayer::OpenFile(const CFileItem &file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "CAVFPlayer: Opening: %s", file.GetPath().c_str());
    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if (IsRunning())
      CloseFile();

    m_bAbortRequest = false;

    m_item = file;
    m_options = options;

    m_elapsed_ms  =  0;
    m_duration_ms =  0;

    m_audio_info  = "none";
    m_audio_delay = 0;
    m_audio_mute  = CAEFactory::IsMuted();
    //m_audio_volume = VolumePercentToScale(CAEFactory::GetVolume());
    m_audio_passthrough_ac3 = CSettings::GetInstance().GetBool("audiooutput.ac3passthrough");
    m_audio_passthrough_dts = CSettings::GetInstance().GetBool("audiooutput.dtspassthrough");

    m_video_info  = "none";
    m_video_width    =  0;
    m_video_height   =  0;
    m_video_fps_numerator = 25;
    m_video_fps_denominator = 1;

    m_subtitle_delay =  0;

    m_chapter_count  =  0;

    m_show_mainvideo = -1;
    m_dst_rect.SetRect(0, 0, 0, 0);
    m_zoom           = -1;
    m_contrast       = -1;
    m_brightness     = -1;

    ClearStreamInfos();

    // setup to spin the busy dialog until we are playing
    m_ready.Reset();

    g_renderManager.PreInit();

    // create the playing thread
    Create();

    // wait for the ready event
    CGUIDialogBusy::WaitOnEvent(m_ready, g_advancedSettings.m_videoBusyDialogDelay_ms, false);

    // Playback might have been stopped due to some error.
    if (m_bStop || m_bAbortRequest)
      return false;

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

bool CAVFPlayer::CloseFile(bool reopen)
{
  // do this once, please
  if (m_bAbortRequest)
    return true;

  m_bAbortRequest = true;
  m_avf_state->set(AVFSTATE::STOP);

  CLog::Log(LOGDEBUG, "CAVFPlayer::CloseFile");

  CLog::Log(LOGDEBUG, "CAVFPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  CLog::Log(LOGDEBUG, "CAVFPlayer: finished waiting");

  g_renderManager.UnInit();

  return true;
}

bool CAVFPlayer::IsPlaying() const
{
  return !m_bStop;
}

void CAVFPlayer::Pause()
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::Pause");
  CSingleLock lock(m_avf_csection);

  // toggle pause/resume off m_paused
  m_paused = !m_paused;
  if (m_paused)
    m_avf_state->set(AVFSTATE::PAUSE);
  else
    m_avf_state->set(AVFSTATE::PLAY);
}

bool CAVFPlayer::IsPaused() const
{
  return m_paused;
}

bool CAVFPlayer::HasVideo() const
{
  //return m_video_count > 0;
  return true;
}

bool CAVFPlayer::HasAudio() const
{
  return m_audio_count > 0;
}

void CAVFPlayer::ToggleFrameDrop()
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::ToggleFrameDrop");
}

bool CAVFPlayer::CanSeek()
{
  return GetTotalTime() > 0;
}

void CAVFPlayer::Seek(bool bPlus, bool bLargeStep, bool bChapterOverride)
{
  CSingleLock lock(m_avf_csection);

  // try chapter seeking first, chapter_index is ones based.
  int chapter_index = GetChapter();
  if (bLargeStep && bChapterOverride && chapter_index > 0)
  {
    if (!bPlus)
    {
      // seek to previous chapter
      SeekChapter(chapter_index - 1);
      return;
    }
    else if (chapter_index < GetChapterCount())
    {
      // seek to next chapter
      SeekChapter(chapter_index + 1);
      return;
    }
  }

  int64_t seek_ms;
  if (g_advancedSettings.m_videoUseTimeSeeking)
  {
    if (bLargeStep && (GetTotalTime() > (2000 * g_advancedSettings.m_videoTimeSeekForwardBig)))
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForwardBig : g_advancedSettings.m_videoTimeSeekBackwardBig;
    else
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForward    : g_advancedSettings.m_videoTimeSeekBackward;
    // convert to milliseconds
    seek_ms *= 1000;
    seek_ms += m_elapsed_ms;
  }
  else
  {
    float percent;
    if (bLargeStep)
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForwardBig : g_advancedSettings.m_videoPercentSeekBackwardBig;
    else
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForward    : g_advancedSettings.m_videoPercentSeekBackward;
    percent /= 100.0f;
    percent += (float)m_elapsed_ms/(float)m_duration_ms;
    // convert to milliseconds
    seek_ms = m_duration_ms * percent;
  }

  if (seek_ms <= 1000)
    seek_ms = 1000;

  if (seek_ms > m_duration_ms)
    seek_ms = m_duration_ms;

  // do seek here
  g_infoManager.SetDisplayAfterSeek(100000);
  SeekTime(seek_ms);
  m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - m_elapsed_ms));
  g_infoManager.SetDisplayAfterSeek();
}

bool CAVFPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::SeekScene");
  return false;
}

void CAVFPlayer::SeekPercentage(float fPercent)
{
  CSingleLock lock(m_avf_csection);
  if (m_duration_ms)
  {
    int64_t seek_ms = fPercent * m_duration_ms / 100.0;
    if (seek_ms <= 1000)
      seek_ms = 1000;

    // do seek here
    g_infoManager.SetDisplayAfterSeek(100000);
    SeekTime(seek_ms);
    m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - m_elapsed_ms));
    g_infoManager.SetDisplayAfterSeek();
  }
}

float CAVFPlayer::GetPercentage()
{
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

void CAVFPlayer::SetMute(bool bOnOff)
{
  m_audio_mute = bOnOff;
}

void CAVFPlayer::SetVolume(float volume)
{
  // volume is a float percent from 0.0 to 1.0
  //m_audio_volume = VolumePercentToScale(volume);
}

void CAVFPlayer::GetAudioInfo(std::string &strAudioInfo)
{
  CSingleLock lock(m_avf_csection);
  if (m_audio_streams.empty() || m_audio_index > (int)(m_audio_streams.size() - 1))
    return;

  //strAudioInfo = StringUtils::Format("Audio stream (%s) [Kb/s:%.2f]",
  //  AudioCodecName(m_audio_streams[m_audio_index]->format),
  //  (double)m_audio_streams[m_audio_index]->bit_rate / 1024.0);
}

void CAVFPlayer::GetVideoInfo(std::string &strVideoInfo)
{
  CSingleLock lock(m_avf_csection);
  if (m_video_streams.empty() || m_video_index > (int)(m_video_streams.size() - 1))
    return;

  //strVideoInfo = StringUtils::Format("Video stream (%s) [fr:%.3f Mb/s:%.2f]",
  //  VideoCodecName(m_video_streams[m_video_index]->format),
  //  GetActualFPS(),
  //  (double)m_video_streams[m_video_index]->bit_rate / (1024.0*1024.0));
}

int CAVFPlayer::GetAudioStreamCount()
{
  //CLog::Log(LOGDEBUG, "CAVFPlayer::GetAudioStreamCount");
  return m_audio_count;
}

int CAVFPlayer::GetAudioStream()
{
  //CLog::Log(LOGDEBUG, "CAVFPlayer::GetAudioStream");
  return m_audio_index;
}

void CAVFPlayer::SetAudioStream(int SetAudioStream)
{
  //CLog::Log(LOGDEBUG, "CAVFPlayer::SetAudioStream");
  CSingleLock lock(m_avf_csection);

  if (SetAudioStream > (int)m_audio_streams.size() || SetAudioStream < 0)
    return;

  m_audio_index = SetAudioStream;
}

void CAVFPlayer::SetAVDelay(float fValue)
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::SetAVDelay (%f)", fValue);
  m_audio_delay = fValue * 1000.0;
}

float CAVFPlayer::GetAVDelay()
{
  return (float)m_audio_delay / 1000.0;
}

void CAVFPlayer::SetSubTitleDelay(float fValue = 0.0f)
{
  if (GetSubtitleCount())
  {
    CSingleLock lock(m_avf_csection);
    m_subtitle_delay = fValue * 1000.0;
  }
}

float CAVFPlayer::GetSubTitleDelay()
{
  return (float)m_subtitle_delay / 1000.0;
}

int CAVFPlayer::GetSubtitleCount()
{
  return m_subtitle_count;
}

int CAVFPlayer::GetSubtitle()
{
  if (m_subtitle_show)
    return m_subtitle_index;
  else
    return -1;
}

void CAVFPlayer::GetSubtitleStreamInfo(int index, SPlayerSubtitleStreamInfo &info)
{
  CSingleLock lock(m_avf_csection);

  if (index > (int)m_subtitle_streams.size() -1 || index < 0)
    return;

  info.language = m_subtitle_streams[index]->language;
  info.name = m_subtitle_streams[m_subtitle_index]->name;
}
 
void CAVFPlayer::SetSubtitle(int iStream)
{
  CSingleLock lock(m_avf_csection);

  if (iStream > (int)m_subtitle_streams.size() || iStream < 0)
    return;

  m_subtitle_index = iStream;

  // smells like a bug, if no showing subs and we get called
  // to set the subtitle, we are expected to update internal state
  // but not show the subtitle.
  if (!m_subtitle_show)
    return;

  {
    m_dvdPlayerSubtitle->CloseStream(true);
    OpenSubtitleStream(m_subtitle_index);
  }
}

bool CAVFPlayer::GetSubtitleVisible()
{
  return m_subtitle_show;
}

void CAVFPlayer::SetSubtitleVisible(bool bVisible)
{
  m_subtitle_show = (bVisible && m_subtitle_count);
  CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleOn = bVisible;

  if (m_subtitle_show  && m_subtitle_count)
  {
    if (CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleStream < m_subtitle_count)
      m_subtitle_index = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleStream;
    // on startup, if asked to show subs and SetSubtitle has not
    // been called, we are expected to switch/show the 1st subtitle
    if (m_subtitle_index < 0)
      m_subtitle_index = 0;
    OpenSubtitleStream(m_subtitle_index);
  }
}

void CAVFPlayer::AddSubtitle(const std::string& strSubPath)
{
  CSingleLock lock(m_avf_csection);
  AddSubtitleFile(strSubPath);
  return;
}

void CAVFPlayer::GetVideoAspectRatio(float &fAR)
{
  fAR = g_renderManager.GetAspectRatio();
}

int CAVFPlayer::GetChapterCount()
{
  return m_chapter_count;
}

int CAVFPlayer::GetChapter()
{
  int chapter_index = -1;
  for (int i = 0; i < m_chapter_count; i++)
  {
    if (m_elapsed_ms >= m_chapters[i]->seekto_ms)
      chapter_index = i;
  }
  return chapter_index + 1;
}

void CAVFPlayer::GetChapterName(std::string& strChapterName)
{
  if (m_chapter_count)
    strChapterName = m_chapters[GetChapter() - 1]->name;
}

int CAVFPlayer::SeekChapter(int chapter_index)
{
  CSingleLock lock(m_avf_csection);

  // chapter_index is a one based value.
  if (m_chapter_count > 1)
  {
    if (chapter_index < 1)
      chapter_index = 1;
    if (chapter_index > m_chapter_count)
      return 0;

    // time units are seconds,
    // so we add 1000ms to get into the chapter.
    int64_t seek_ms = m_chapters[chapter_index - 1]->seekto_ms + 1000;

    //  seek to 1 second and play is immediate.
    if (seek_ms <= 0)
      seek_ms = 1000;

    // seek to chapter here
    g_infoManager.SetDisplayAfterSeek(100000);
    SeekTime(seek_ms);
    m_callback.OnPlayBackSeekChapter(chapter_index);
    g_infoManager.SetDisplayAfterSeek();
  }
  else
  {
    // we do not have a chapter list so do a regular big jump.
    if (chapter_index > 0)
      Seek(true,  true);
    else
      Seek(false, true);
  }
  return 0;
}

float CAVFPlayer::GetActualFPS()
{
  //float video_fps = m_video_fps_numerator / m_video_fps_denominator;
  //CLog::Log(LOGDEBUG, "CAVFPlayer::GetActualFPS:m_video_fps(%f)", video_fps);
  return 23.976;
}

void CAVFPlayer::SeekTime(int64_t seek_ms)
{
  CSingleLock lock(m_avf_csection);

  // we cannot seek if paused
  if (m_paused)
    return;

  if (seek_ms <= 0)
    seek_ms = 100;

  // seek here
  AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;
  CMTime seekTime = CMTimeMake(seek_ms , 1000);
  [avf_avplayer.player seekToTime:seekTime];
}

int64_t CAVFPlayer::GetTime()
{
  return m_elapsed_ms;
}

int64_t CAVFPlayer::GetTotalTime()
{
  return m_duration_ms;
}

void CAVFPlayer::GetAudioStreamInfo(int index, SPlayerAudioStreamInfo &info)
{
  CSingleLock lock(m_avf_csection);
  if (index < 0 || m_audio_streams.empty() || index > (int)(m_audio_streams.size() - 1))
    return;

  info.bitrate = m_audio_streams[index]->bit_rate;

  info.language = m_audio_streams[index]->language;

  info.channels = m_audio_streams[index]->channel;

  //info.audioCodecName = AudioCodecName(m_audio_streams[index]->format);

  if (info.audioCodecName.size())
    info.name = info.audioCodecName + " ";

  switch(info.channels)
  {
  case 1:
    info.name += "Mono";
    break;
  case 2: 
    info.name += "Stereo";
    break;
  case 6: 
    info.name += "5.1";
    break;
  case 7:
    info.name += "6.1";
    break;
  case 8:
    info.name += "7.1";
    break;
  default:
    char temp[32];
    sprintf(temp, "%d-chs", info.channels);
    info.name += temp;
  }
}

void CAVFPlayer::GetVideoStreamInfo(SPlayerVideoStreamInfo &info)
{
  CSingleLock lock(m_avf_csection);
  if (m_video_streams.empty() || m_video_index > (int)(m_video_streams.size() - 1))
    return;

  info.bitrate = m_video_streams[m_video_index]->bit_rate;
  //info.videoCodecName = VideoCodecName(m_video_streams[m_video_index]->format);
  info.videoAspectRatio = g_renderManager.GetAspectRatio();
  CRect viewRect;
  g_renderManager.GetVideoRect(info.SrcRect, info.DestRect, viewRect);
}

int CAVFPlayer::GetSourceBitrate()
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::GetSourceBitrate");
  return 0;
}

int CAVFPlayer::GetSampleRate()
{
  CSingleLock lock(m_avf_csection);
  if (m_audio_streams.empty() || m_audio_index > (int)(m_audio_streams.size() - 1))
    return 0;
  
  return m_audio_streams[m_audio_index]->sample_rate;
}

bool CAVFPlayer::GetStreamDetails(CStreamDetails &details)
{
  //CLog::Log(LOGDEBUG, "CAVFPlayer::GetStreamDetails");
  return false;
}

void CAVFPlayer::ToFFRW(int iSpeed)
{
  CLog::Log(LOGDEBUG, "CAVFPlayer::ToFFRW: iSpeed(%d), m_speed(%d)", iSpeed, m_speed);
  CSingleLock lock(m_avf_csection);

  if (m_speed != iSpeed)
  {
    // recover power of two value
    int ipower = 0;
    int ispeed = abs(iSpeed);
    while (ispeed >>= 1) ipower++;

    switch(ipower)
    {
      // regular playback
      case  0:
        break;
      default:
        // N x fast forward/rewind (I-frames)
        // speed playback  1, 2, 4, 8 forward
        // speed playback -1,-2,-4,-8 forward
        break;
    }

    m_speed = iSpeed;
  }
}

bool CAVFPlayer::GetCurrentSubtitle(std::string& strSubtitle)
{
  strSubtitle = "";

  if (m_subtitle_count)
  {
    {
      double pts = DVD_MSEC_TO_TIME(m_elapsed_ms) - DVD_MSEC_TO_TIME(m_subtitle_delay);
      m_dvdOverlayContainer->CleanUp(pts);
      //m_dvdPlayerSubtitle->GetCurrentSubtitle(strSubtitle, pts);
    }
  }

  return !strSubtitle.empty();
}

void CAVFPlayer::OnStartup()
{
  //m_CurrentVideo.Clear();
  //m_CurrentAudio.Clear();
  //m_CurrentSubtitle.Clear();

  //CThread::SetName("AVFPlayer");
}

void CAVFPlayer::OnExit()
{
  //CLog::Log(LOGNOTICE, "CAVFPlayer::OnExit()");

  m_bStop = true;
  // if we did not manually stop playing, advance to the next item in xbmc's playlist
  if (m_options.identify == false)
  {
    if (m_bAbortRequest)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }
  // set event to inform openfile something went wrong
  // in case openfile is still waiting for this event
  m_ready.Set();
}

void CAVFPlayer::Process()
{
  CLog::Log(LOGNOTICE, "CAVFPlayer::Process");
  try
  {
/*
    std::string url = m_item.GetPath();
    if (url.left(strlen("smb://")).Equals("smb://"))
    {
      // the name string needs to persist
      static const char *smb_name = "smb";
      vfs_protocol.name = smb_name;
    }
    else if (url.Left(strlen("afp://")).Equals("afp://"))
    {
      // the name string needs to persist
      static const char *afp_name = "afp";
      vfs_protocol.name = afp_name;
    }
    else if (url.Left(strlen("nfs://")).Equals("nfs://"))
    {
      // the name string needs to persist
      static const char *nfs_name = "nfs";
      vfs_protocol.name = nfs_name;
    }
    else if (url.Left(strlen("rar://")).Equals("rar://"))
    {
      // the name string needs to persist
      static const char *rar_name = "rar";
      vfs_protocol.name = rar_name;
    }
    else if (url.Left(strlen("ftp://")).Equals("ftp://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-ftp";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("ftps://")).Equals("ftps://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-ftps";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("http://")).Equals("http://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-http";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("https://")).Equals("https://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-https";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("hdhomerun://")).Equals("hdhomerun://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-hdhomerun";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("sftp://")).Equals("sftp://"))
    {
      // the name string needs to persist
      static const char *http_name = "xb-sftp";
      vfs_protocol.name = http_name;
      url = "xb-" + url;
    }
    else if (url.Left(strlen("udp://")).Equals("udp://"))
    {
      std::string udp_params;
      // bump up the default udp params for ffmpeg.
      // ffmpeg will strip out 'dummy=10', we only add it
      // to make the logic below with prpending '&' work right.
      // to watch for udp errors, 'cat /proc/net/udp'
      if (url.find("?") == std::string::npos)
        udp_params.append("?dummy=10");
      if (url.find("pkt_size=") == std::string::npos)
        udp_params.append("&pkt_size=5264");
      if (url.find("buffer_size=") == std::string::npos)
        udp_params.append("&buffer_size=5390336");
      // newer ffmpeg uses fifo_size instead of buf_size
      if (url.find("buf_size=") == std::string::npos)
        udp_params.append("&buf_size=5390336");

      if (udp_params.size() > 0)
        url.append(udp_params);
    }
    CLog::Log(LOGDEBUG, "CAVFPlayer::Process: URL=%s", url.c_str());
*/

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
            components.host   = nullptr;
            components.path   = filePath;

            CGRect frame = CGRectMake(0, 0,
              g_xbmcController.view.frame.size.width,
              g_xbmcController.view.frame.size.height);
            AVPlayerLayerViewNew *avf_avplayer = [[AVPlayerLayerViewNew alloc] initWithFrameAndUrl:frame withURL:[components URL]];
            [components release];
            [g_xbmcController insertVideoView:avf_avplayer];
            m_avf_avplayer = avf_avplayer;
         });

          // wait for playback to start with 20 second timeout
          if (WaitForPlaying(20000))
          {
            m_speed = 1;
            m_callback.OnPlayBackSpeedChanged(m_speed);

            // drop CGUIDialogBusy dialog and release the hold in OpenFile.
            m_ready.Set();

            // we are playing but hidden and all stream fields are valid.
            // check for video in media content
            //if (GetVideoStreamCount() > 0)
            {
              SetAVDelay(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_AudioDelay);

              // turn on/off subs
              SetSubtitleVisible(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleOn);
              SetSubTitleDelay(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleDelay);

              // setup renderer for bypass. This tell renderer to get out of the way as
              // hw decoder will be doing the actual video rendering in a video plane
              // that is under the GUI layer.
              int width  = m_video_width;
              int height = m_video_height;
              double fFrameRate = GetActualFPS();
              unsigned int flags = 0;

              flags |= CONF_FLAGS_FULLSCREEN;
              std::string formatstr = "BYPASS";
              CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: %s",
                __FUNCTION__, width, height, fFrameRate, formatstr.c_str());

              if (!g_renderManager.Configure(width, height, width, height, fFrameRate, flags, RENDER_FMT_BYPASS, 0, 0))
                CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);

              if (!g_renderManager.IsStarted())
                CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
            }

            if (m_options.identify == false)
              m_callback.OnPlayBackStarted();

            m_avf_state->set(AVFSTATE::PLAY);
          }
          else
          {
            // playback never started, some error or timeout
            m_avf_state->set(AVFSTATE::STOP);
          }
        }
        break;

        case AVFSTATE::PLAY:
        {
          AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;
          dispatch_sync(dispatch_get_main_queue(),^{
            [[avf_avplayer player] play];
          });
          m_avf_state->set(AVFSTATE::PLAYING);
        }
        break;

        case AVFSTATE::PAUSE:
        {
          AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;
          dispatch_sync(dispatch_get_main_queue(),^{
            [[avf_avplayer player] pause];
          });
          m_avf_state->set(AVFSTATE::IDLE);
        }
        break;

        case AVFSTATE::PLAYING:
        {
          AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;

          CMTime currentTime = avf_avplayer.player.currentTime;
          Float64 timeBase_s = CMTimeGetSeconds(currentTime);
          m_elapsed_ms = timeBase_s * 1000;

          if (m_duration_ms <= 0)
          {
            AVPlayerItem *thePlayerItem = avf_avplayer.player.currentItem;
            if (thePlayerItem.status == AVPlayerItemStatusReadyToPlay)
            {
              /*
              NOTE:
              Because of the dynamic nature of HTTP Live Streaming Media, the best practice
              for obtaining the duration of an AVPlayerItem object has changed in iOS 4.3.
              Prior to iOS 4.3, you would obtain the duration of a player item by fetching
              the value of the duration property of its associated AVAsset object. However,
              note that for HTTP Live Streaming Media the duration of a player item during
              any particular playback session may differ from the duration of its asset. For
              this reason a new key-value observable duration property has been defined on
              AVPlayerItem.

              See the AV Foundation Release Notes for iOS 4.3 for more information.
              */
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
            avf_avplayer.player.rate = m_speed;
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
                avf_avplayer.frame = frame;
                avf_avplayer.center= CGPointMake(avf_avplayer.center.x + offset.x, avf_avplayer.center.y + offset.y);
                // video layer needs to get resized too,
                // not sure why, it should track the view.
                avf_avplayer.videoLayer.frame = frame;
                // we start up hidden, kick off an animated fade in.
                if (avf_avplayer.hidden == YES)
                  [avf_avplayer setHiddenAnimated:NO delay:NSTimeInterval(0.1) duration:NSTimeInterval(2.0)];
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
          AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;
          dispatch_sync(dispatch_get_main_queue(),^{
            [g_xbmcController removeVideoView:avf_avplayer];
            [avf_avplayer.player pause];
            [avf_avplayer release];
          });
          m_avf_avplayer = nullptr;
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
    CLog::Log(LOGERROR, "CAVFPlayer::Process Exception thrown");
  }

  m_ready.Set();

  ClearStreamInfos();
}

void CAVFPlayer::GetRenderFeatures(std::vector<int> &renderFeatures)
{
  renderFeatures.push_back(RENDERFEATURE_ZOOM);
  renderFeatures.push_back(RENDERFEATURE_CONTRAST);
  renderFeatures.push_back(RENDERFEATURE_BRIGHTNESS);
  renderFeatures.push_back(RENDERFEATURE_STRETCH);
}

void CAVFPlayer::GetDeinterlaceMethods(std::vector<int> &deinterlaceMethods)
{
  deinterlaceMethods.push_back(VS_INTERLACEMETHOD_DEINTERLACE);
}

void CAVFPlayer::GetDeinterlaceModes(std::vector<int> &deinterlaceModes)
{
  deinterlaceModes.push_back(VS_DEINTERLACEMODE_AUTO);
}

void CAVFPlayer::GetScalingMethods(std::vector<int> &scalingMethods)
{
}

void CAVFPlayer::GetAudioCapabilities(std::vector<int> &audioCaps)
{
  audioCaps.push_back(IPC_AUD_SELECT_STREAM);
  audioCaps.push_back(IPC_AUD_SELECT_OUTPUT);
#if defined(HAS_AVFPlayer_AUDIO_SETDELAY)
  audioCaps.push_back(IPC_AUD_OFFSET);
#endif
}

void CAVFPlayer::GetSubtitleCapabilities(std::vector<int> &subCaps)
{
  subCaps.push_back(IPC_SUBS_EXTERNAL);
  subCaps.push_back(IPC_SUBS_SELECT);
  subCaps.push_back(IPC_SUBS_OFFSET);
}


////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
int CAVFPlayer::GetVideoStreamCount()
{
  //CLog::Log(LOGDEBUG, "CAVFPlayer::GetVideoStreamCount(%d)", m_video_count);
  return m_video_count;
}

bool CAVFPlayer::CheckPlaying()
{
  return true;
}

bool CAVFPlayer::WaitForPlaying(int timeout_ms)
{
  while (!m_bAbortRequest && (timeout_ms > 0))
  {
    AVPlayerLayerViewNew *avf_avplayer = (AVPlayerLayerViewNew*)m_avf_avplayer;

    if (avf_avplayer.videoLayer.isReadyForDisplay == YES)
    {
      CLog::Log(LOGDEBUG, "CAVFPlayer::Process avf_avplayer.videoLayer.isReadyForDisplay");
      CGRect video_bounds = avf_avplayer.videoLayer.videoRect;
      m_video_width = video_bounds.size.width;
      m_video_height = video_bounds.size.height;
      return true;
    }

    usleep(20 * 1000);
    timeout_ms -= 20;
  }

  return false;
}

void CAVFPlayer::ClearStreamInfos()
{
  CSingleLock lock(m_avf_csection);

  if (!m_audio_streams.empty())
  {
    for (unsigned int i = 0; i < m_audio_streams.size(); i++)
      delete m_audio_streams[i];
    m_audio_streams.clear();
  }
  m_audio_count = 0;
  m_audio_index = -1;

  if (!m_video_streams.empty())
  {
    for (unsigned int i = 0; i < m_video_streams.size(); i++)
      delete m_video_streams[i];
    m_video_streams.clear();
  }
  m_video_count = 0;
  m_video_index = -1;

  if (!m_subtitle_streams.empty())
  {
    for (unsigned int i = 0; i < m_subtitle_streams.size(); i++)
      delete m_subtitle_streams[i];
    m_subtitle_streams.clear();
  }
  m_subtitle_count = 0;
  m_subtitle_index = -1;

  if (!m_chapters.empty())
  {
    for (unsigned int i = 0; i < m_chapters.size(); i++)
      delete m_chapters[i];
    m_chapters.clear();
  }
  m_chapter_count = 0;
}

void CAVFPlayer::FindSubtitleFiles()
{
  // find any available external subtitles
  std::vector<std::string> filenames;
  CUtil::ScanForExternalSubtitles(m_item.GetPath(), filenames);
/*
  // find any upnp subtitles
  std::string key("upnp:subtitle:1");
  for(unsigned s = 1; m_item.HasProperty(key); key.Format("upnp:subtitle:%u", ++s))
    filenames.push_back(m_item.GetProperty(key).asString());

  for(unsigned int i=0;i<filenames.size();i++)
  {
    // if vobsub subtitle:		
    if (URIUtils::HasExtension(filenames[i], ".idx"))
    {
      std::string strSubFile;
      if ( CUtil::FindVobSubPair( filenames, filenames[i], strSubFile ) )
        AddSubtitleFile(filenames[i], strSubFile);
    }
    else 
    {
      if ( !CUtil::IsVobSub(filenames, filenames[i] ) )
      {
        AddSubtitleFile(filenames[i]);
      }
    }   
  }
*/
}

int CAVFPlayer::AddSubtitleFile(const std::string &filename, const std::string &subfilename)
{
  std::string ext = URIUtils::GetExtension(filename);
  std::string vobsubfile = subfilename;

  if(ext == ".idx")
  {
    /* TODO: we do not handle idx/sub binary subs yet.
    if (vobsubfile.empty())
      vobsubfile = URIUtils::ReplaceExtension(filename, ".sub");

    CDVDDemuxVobsub v;
    if(!v.Open(filename, vobsubfile))
      return -1;
    m_SelectionStreams.Update(NULL, &v);
    int index = m_SelectionStreams.IndexOf(STREAM_SUBTITLE, m_SelectionStreams.Source(STREAM_SOURCE_DEMUX_SUB, filename), 0);
    m_SelectionStreams.Get(STREAM_SUBTITLE, index).flags = flags;
    m_SelectionStreams.Get(STREAM_SUBTITLE, index).filename2 = vobsubfile;
    return index;
    */
    return -1;
  }
  if(ext == ".sub")
  {
    // check for texual sub, if this is a idx/sub pair, ignore it.
    std::string strReplace(URIUtils::ReplaceExtension(filename,".idx"));
    if (XFILE::CFile::Exists(strReplace))
      return -1;
  }

  AVFPlayerStreamInfo *info = new AVFPlayerStreamInfo;
  info->Clear();

  info->id       = 0;
  info->type     = STREAM_SUBTITLE;
  info->source   = STREAM_SOURCE_TEXT;
  info->filename = filename;
  info->name     = URIUtils::GetFileName(filename);
  info->frame_rate_num = m_video_fps_numerator;
  info->frame_rate_den = m_video_fps_denominator;
  m_subtitle_streams.push_back(info);

  return (int)m_subtitle_streams.size();
}

bool CAVFPlayer::OpenSubtitleStream(int index)
{
  CLog::Log(LOGNOTICE, "Opening external subtitle stream: %i", index);

  CDemuxStream* pStream = NULL;
  std::string filename;
  CDVDStreamInfo hint;

  if (m_subtitle_streams[index]->source == STREAM_SOURCE_DEMUX_SUB)
  {
    /*
    int index = m_SelectionStreams.IndexOf(STREAM_SUBTITLE, source, iStream);
    if(index < 0)
      return false;
    SelectionStream st = m_SelectionStreams.Get(STREAM_SUBTITLE, index);

    if(!m_pSubtitleDemuxer || m_pSubtitleDemuxer->GetFileName() != st.filename)
    {
      CLog::Log(LOGNOTICE, "Opening Subtitle file: %s", st.filename.c_str());
      auto_ptr<CDVDDemuxVobsub> demux(new CDVDDemuxVobsub());
      if(!demux->Open(st.filename, st.filename2))
        return false;
      m_pSubtitleDemuxer = demux.release();
    }

    pStream = m_pSubtitleDemuxer->GetStream(iStream);
    if(!pStream || pStream->disabled)
      return false;
    pStream->SetDiscard(AVDISCARD_NONE);
    double pts = m_dvdPlayerVideo.GetCurrentPts();
    if(pts == DVD_NOPTS_VALUE)
      pts = m_CurrentVideo.dts;
    if(pts == DVD_NOPTS_VALUE)
      pts = 0;
    pts += m_offset_pts;
    m_pSubtitleDemuxer->SeekTime((int)(1000.0 * pts / (double)DVD_TIME_BASE));

    hint.Assign(*pStream, true);
    */
    return false;
  }
  else if (m_subtitle_streams[index]->source == STREAM_SOURCE_TEXT)
  {
    filename = m_subtitle_streams[index]->filename;

    hint.Clear();
    hint.fpsscale = m_subtitle_streams[index]->frame_rate_den;
    hint.fpsrate  = m_subtitle_streams[index]->frame_rate_num;
  }

  m_dvdPlayerSubtitle->CloseStream(true);
  if (!m_dvdPlayerSubtitle->OpenStream(hint, filename))
  {
    CLog::Log(LOGWARNING, "%s - Unsupported stream %d. Stream disabled.", __FUNCTION__, index);
    if(pStream)
    {
      pStream->disabled = true;
      pStream->SetDiscard(AVDISCARD_ALL);
    }
    return false;
  }

  return true;
}

#endif
