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
#include "cores/IPlayer.h"
#include "cores/IAudioCallback.h"
#include "dialogs/GUIDialogBusy.h"
#include "threads/Thread.h"

typedef struct player_info player_info_t;
typedef struct AVFChapterInfo AFVChapterInfo;
typedef struct AVFPlayerStreamInfo AVFPlayerStreamInfo;

class CAVFState;
class CDVDPlayerSubtitle;
class CDVDOverlayContainer;

class CAVFPlayer : public IPlayer, public CThread
{
public:

  CAVFPlayer(IPlayerCallback &callback);
  virtual ~CAVFPlayer();
  
  virtual void  RegisterAudioCallback(IAudioCallback* pCallback) {}
  virtual void  UnRegisterAudioCallback()                        {}
  virtual bool  OpenFile(const CFileItem &file, const CPlayerOptions &options);
  virtual bool  QueueNextFile(const CFileItem &file)             {return false;}
  virtual void  OnNothingToQueueNotify()                         {}
  virtual bool  CloseFile(bool reopen = false);
  virtual bool  IsPlaying() const;
  virtual void  Pause();
  virtual bool  IsPaused() const;
  virtual bool  HasVideo() const;
  virtual bool  HasAudio() const;
  virtual void  ToggleFrameDrop();
  virtual bool  CanSeek();
  virtual void  Seek(bool bPlus = true, bool bLargeStep = false, bool bChapterOverride = true);
  virtual bool  SeekScene(bool bPlus = true);
  virtual void  SeekPercentage(float fPercent = 0.0f);
  virtual float GetPercentage();
  virtual void  SetMute(bool bOnOff);
  virtual bool  ControlsVolume() {return true;}
  virtual void  SetVolume(float volume);
  virtual void  SetDynamicRangeCompression(long drc)              {}
  virtual void  GetAudioInfo(std::string &strAudioInfo);
  virtual void  GetVideoInfo(std::string &strVideoInfo);
  virtual void  GetGeneralInfo(std::string &strVideoInfo) {};
  virtual void  GetVideoAspectRatio(float &fAR);
  virtual bool  CanRecord()                                       {return false;};
  virtual bool  IsRecording()                                     {return false;};
  virtual bool  Record(bool bOnOff)                               {return false;};

  virtual void  SetAVDelay(float fValue = 0.0f);
  virtual float GetAVDelay();

  virtual void  SetSubTitleDelay(float fValue);
  virtual float GetSubTitleDelay();
  virtual int   GetSubtitleCount();
  virtual int   GetSubtitle();
  virtual void  GetSubtitleStreamInfo(int index, SPlayerSubtitleStreamInfo &info);
  virtual void  SetSubtitle(int iStream);
  virtual bool  GetSubtitleVisible();
  virtual void  SetSubtitleVisible(bool bVisible);
  virtual void  AddSubtitle(const std::string& strSubPath);

  virtual int   GetAudioStreamCount();
  virtual int   GetAudioStream();
  virtual void  SetAudioStream(int iStream);

  virtual TextCacheStruct_t* GetTeletextCache()                   {return NULL;};
  virtual void  LoadPage(int p, int sp, unsigned char* buffer)    {};

  virtual int   GetChapterCount();
  virtual int   GetChapter();
  virtual void  GetChapterName(std::string& strChapterName);
  virtual int   SeekChapter(int iChapter);

  virtual float GetActualFPS();
  virtual void  SeekTime(int64_t iTime = 0);
  virtual int64_t GetTime();
  virtual int64_t GetTotalTime();
  virtual void  GetAudioStreamInfo(int index, SPlayerAudioStreamInfo &info);
  virtual void  GetVideoStreamInfo(SPlayerVideoStreamInfo &info);
  virtual int   GetSourceBitrate();
  virtual int   GetSampleRate();
  virtual bool  GetStreamDetails(CStreamDetails &details);
  virtual void  ToFFRW(int iSpeed = 0);
  // Skip to next track/item inside the current media (if supported).
  virtual bool  SkipNext()                                        {return false;}

  //Returns true if not playback (paused or stopped beeing filled)
  virtual bool  IsCaching() const                                 {return false;};
  //Cache filled in Percent
  virtual int   GetCacheLevel() const                             {return -1;};

  virtual bool  IsInMenu() const                                  {return false;};
  virtual bool  HasMenu()                                         {return false;};

  virtual void  DoAudioWork()                                     {};
  virtual bool  OnAction(const CAction &action)                   {return false;};

  virtual bool  GetCurrentSubtitle(std::string& strSubtitle);
  //returns a state that is needed for resuming from a specific time
  virtual std::string GetPlayerState()                             {return "";};
  virtual bool  SetPlayerState(std::string state)                  {return false;};

  virtual std::string GetPlayingTitle()                            {return "";};

  virtual void  GetRenderFeatures(std::vector<int> &renderFeatures);
  virtual void  GetDeinterlaceMethods(std::vector<int> &deinterlaceMethods);
  virtual void  GetDeinterlaceModes(std::vector<int> &deinterlaceModes);
  virtual void  GetScalingMethods(std::vector<int> &scalingMethods);
  virtual void  GetAudioCapabilities(std::vector<int> &audioCaps);
  virtual void  GetSubtitleCapabilities(std::vector<int> &subCaps);

protected:
  virtual void  OnStartup();
  virtual void  OnExit();
  virtual void  Process();

private:
  int           GetVideoStreamCount();

  bool          CheckPlaying();
  bool          WaitForPlaying(int timeout_ms);
  void          ClearStreamInfos();

  void          FindSubtitleFiles();
  int           AddSubtitleFile(const std::string& filename, const std::string& subfilename = "");
  bool          OpenSubtitleStream(int index);

  int                     m_speed;
  bool                    m_paused;
  bool                    m_bAbortRequest;
  CEvent                  m_ready;
  CFileItem               m_item;
  CPlayerOptions          m_options;

  int64_t                 m_elapsed_ms;
  int64_t                 m_duration_ms;

  int                     m_audio_index;
  int                     m_audio_count;
  std::string             m_audio_info;
  int                     m_audio_delay;
  bool                    m_audio_passthrough_ac3;
  bool                    m_audio_passthrough_dts;
  bool                    m_audio_mute;
  float                   m_audio_volume;

  int                     m_video_index;
  int                     m_video_count;
  std::string             m_video_info;
  int                     m_video_width;
  int                     m_video_height;
  float                   m_video_fps_numerator;
  float                   m_video_fps_denominator;

  int                     m_subtitle_index;
  int                     m_subtitle_count;
  bool                    m_subtitle_show;
  int                     m_subtitle_delay;
  CDVDPlayerSubtitle     *m_dvdPlayerSubtitle;
  CDVDOverlayContainer   *m_dvdOverlayContainer;

  int                     m_chapter_count;

  int                     m_show_mainvideo;
  CRect                   m_dst_rect;
  int                     m_view_mode;
  float                   m_zoom;
  int                     m_contrast;
  int                     m_brightness;

  void                   *m_avf_avplayer;
  CCriticalSection        m_avf_csection;
  CAVFState              *m_avf_state;

  std::vector<AVFPlayerStreamInfo*> m_video_streams;
  std::vector<AVFPlayerStreamInfo*> m_audio_streams;
  std::vector<AVFPlayerStreamInfo*> m_subtitle_streams;
  std::vector<AVFChapterInfo*>      m_chapters;

};
