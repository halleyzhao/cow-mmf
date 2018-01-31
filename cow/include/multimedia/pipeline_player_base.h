/**
 * Copyright (C) 2017 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef pipeline_player_base_h
#define pipeline_player_base_h
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_meta.h"
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/codec.h"
#include "multimedia/pipeline.h"



namespace YUNOS_MM {
class SeekEventParam;
typedef MMSharedPtr<SeekEventParam> SeekEventParamSP;


class PipelinePlayerBase;
typedef MMSharedPtr<PipelinePlayerBase> PipelinePlayerBaseSP;

class PipelinePlayerBase : public Pipeline {
  public:

    // load the given media resource
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    virtual mm_status_t load(const char * uri,
                              const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t load(int fd, int64_t offset, int64_t length);
    virtual mm_status_t loadSubtitleUri(const char * uri);

    // set display name (for example X11 display name as hostname:protocol.port or Wayland nested display name)
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setDisplayName(const char* name);

    // set native display, (for example X11 display or Wayland display)
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setNativeDisplay(void * display);

    // set video rendering surface
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setVideoSurface(void * handle, bool isTexture = false);

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    virtual mm_status_t prepare();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventStartResult.
    //              others: error.
    virtual mm_status_t start();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventStopped.
    //              others: error.
    virtual mm_status_t stop();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventPaused.
    //              others: error.
    virtual mm_status_t pause();

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResetComplete.
    //              others: error.
    virtual mm_status_t resume();

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventSeekComplete.
    //              others: error.
    virtual mm_status_t seek(SeekEventParamSP param);
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResetComplete.
    //              others: error.
    virtual mm_status_t reset();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventFlushComplete.
    //              others: error.
    virtual mm_status_t flush();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error.
    virtual mm_status_t getState(ComponentStateType& state);
    // get current playback position
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t getCurrentPosition(int64_t& positionMs) const;
    // get media content duration
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t getDuration(int64_t& msec);
    // select track index of given media type
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t selectTrack(Component::MediaType mediaType, int index);
    // get selected track index of give media type
    // returns: -1 no track selected (no such mediaType).
    virtual int getSelectedTrack(Component::MediaType mediaType);


    virtual MMParamSP getTrackInfo();

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    // unblock the wait of current command execution
    virtual mm_status_t  unblock();
    virtual mm_status_t setAudioStreamType(int type);
    virtual mm_status_t getAudioStreamType(int *type);
    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;
    virtual mm_status_t setVolume(const float left, const float right);
    virtual mm_status_t getVolume(float& left, float& right) const;
    virtual mm_status_t setMute(bool mute);
    virtual mm_status_t getMute(bool * mute) const;
    virtual ~PipelinePlayerBase();

    virtual mm_status_t pushData(MediaBufferSP & buffer);
    virtual mm_status_t enableExternalSubtitleSupport(bool enable);

  protected:
     //prepareInternal should implemented by each derived class
     virtual mm_status_t prepareInternal() = 0;

  // FIXME: private
  protected:
    void resetMemberVariables();
    //ComponentsInfos mComponents;
    std::string mDisplayName;
    void* mNativeDisplay;
    void* mSurface;
    bool mSelfCreatedDisplay;
    bool mIsSurfaceTexture;
    StreamInfo mStreamInfo[Component::kMediaTypeCount];
    int32_t mSelectedTrack[Component::kMediaTypeCount];

    ComponentStateType mState;
    ComponentStateType mState2; // help for seek/flush/reset status check
    ComponentStateType mState3; // save the state before seek
    int32_t mDemuxIndex;
    int32_t mSinkClockIndex; // the sink component to get current playback position
    int32_t mAudioCodecIndex;
    int32_t mVideoCodecIndex;
    int32_t mSubtitleSourceIndex;
    int32_t mSubtitleSinkIndex;
    int32_t mVideoSinkIndex;
    int32_t mAudioSinkIndex;
    int32_t mScaledPlayRate;//Playrate based SCALED_PLAY_RATE which defined in mm_types.h
    int32_t mDashFirstSourceIndex;
    int32_t mDashSecondSourceIndex;

    // meta information
    int64_t mDurationMs;//Note: in ms
    int32_t mWidth, mHeight;
    int32_t mRotation;
    SeekEventParamSP mSeekEventParam;   // assumed that cowplayer uses one mSeekEventParam only. it is the fact
    uint32_t mSeekSequence;
    MediaMetaSP mMediaMeta; //saved param, so upper call methods in arbitrary order
    int32_t mAudioStreamType;
    std::string mAudioConnectionId;
    mutable int64_t mSeekPositionMs;

    bool mHasVideo;
    bool mHasAudio;
    bool mHasSubTitle;
    UriType mUriType;
    bool mSeekPreviewDone; //set to true when doing seek in paused state
    bool mVideoResolutionReportDone;
    bool mExternalSubtitleEnabled;

    Condition mSeekPreviewDoneCondition;
    uint32_t mBufferUpdateEventFilterCount;
    std::string mSubtitleUri;
    std::string mVideoMime;
    std::string mAudioMime;
    ClockSP mClock;
    std::string mDownloadPath;

    PipelinePlayerBase();
    mm_status_t updateTrackInfo();

    UriType getUriType(const char* uri);
    virtual mm_status_t loadPreparation(UriType type);
    PlaySourceComponent* getSourceComponent();
    DashSourceComponent* getDashSourceComponent();
    DashSourceComponent* getDashSecondSourceComponent();
    PlaySinkComponent* getSinkComponent(Component::MediaType type) const;
    mm_status_t flushInternal(bool skipDemuxer=false);

    class StateAutoSet;

    MM_DISALLOW_COPY(PipelinePlayerBase)

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER2(onComponentMessage)
    DECLARE_MSG_HANDLER2(onCowMessage)

}; // PipelinePlayerBase


class SeekEventParam {
  public:
    #define  InvalidSeekTime (int64_t(-2))

    static SeekEventParamSP create() {
        //return SeekEventParamSP(new SeekEventParam);
        SeekEventParamSP seekParam = SeekEventParamSP(new SeekEventParam());
        seekParam->mTargetSeekTime = InvalidSeekTime;
        seekParam->mPendingSeekTime = InvalidSeekTime;
        return seekParam;
    }

    void updateSeekTime(int64_t msec) {
        MMAutoLock locker(mLock);
        mPendingSeekTime = msec;
    }

    bool hasPendingSeek() {
        MMAutoLock locker(mLock);
        return mPendingSeekTime != InvalidSeekTime;
    }
    int64_t getSeekTime() {
        MMAutoLock locker(mLock);
        int64_t msec = mPendingSeekTime;
        mTargetSeekTime = mPendingSeekTime;
        mPendingSeekTime = InvalidSeekTime;
        return msec;
    }

    // helper function to report a decent current position during seek, no impact to seek operation
    void clearTargetSeekTime() {
        MMAutoLock locker(mLock);
        mTargetSeekTime = InvalidSeekTime;
    }
    bool getTargetSeekTime(int64_t& targetSeekTime) {
        MMAutoLock locker(mLock);
        if (mPendingSeekTime != InvalidSeekTime)
            mTargetSeekTime= mPendingSeekTime;

        if (mTargetSeekTime == InvalidSeekTime)
            return false;

        targetSeekTime = mTargetSeekTime;
        return true;
    }

  private:
    Lock mLock;
    int64_t mPendingSeekTime;
    int64_t mTargetSeekTime; // helper variable to report decent current playback position; no impact to seek operation
};


} // YUNOS_MM

#endif // pipeline_player_base_h

