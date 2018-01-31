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
#include <math.h>
#include <unistd.h>
#include "multimedia/pipeline_player_base.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/elapsedtimer.h"
#include "multimedia/mm_audio.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("COW-PLPB")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

BEGIN_MSG_LOOP(PipelinePlayerBase)
    MSG_ITEM2(PL_MSG_componentMessage, onComponentMessage)
    MSG_ITEM2(PL_MSG_cowMessage, onCowMessage)
END_MSG_LOOP()


static const int64_t PREVIEW_SEEK_DONE_TIME = 10 * 1000 * 1000;
#define ETIMEOUT 110

class PipelinePlayerBase::StateAutoSet
{
  public:
    explicit StateAutoSet(ComponentStateType &set, ComponentStateType to)
                            : mSet(set)
    {
        mSet = to;
    }
    ~StateAutoSet()
    {
        mSet = kComponentStateInvalid;
    }
  private:
    ComponentStateType &mSet;
    MM_DISALLOW_COPY(StateAutoSet);
};

void PipelinePlayerBase::resetMemberVariables()
{
    mSeekSequence   = 0;
    mSeekPositionMs = -1;
    mSeekPreviewDone = true;
    mVideoResolutionReportDone = false;
}

PipelinePlayerBase::PipelinePlayerBase()
    : mNativeDisplay(NULL)
    , mSurface(NULL)
    , mSelfCreatedDisplay(false)
    , mIsSurfaceTexture(false)
    , mState(kComponentStateNull)
    , mState2(kComponentStateInvalid)
    , mState3(kComponentStateInvalid)
    , mDemuxIndex(-1)
    , mSinkClockIndex(-1)
    , mAudioCodecIndex(-1)
    , mVideoCodecIndex(-1)
    , mSubtitleSourceIndex(-1)
    , mSubtitleSinkIndex(-1)
    , mVideoSinkIndex(-1)
    , mAudioSinkIndex(-1)
    , mScaledPlayRate(SCALED_PLAY_RATE)
    , mDashFirstSourceIndex(-1)
    , mDashSecondSourceIndex(-1)
    , mDurationMs(-1)
    , mWidth(-1)
    , mHeight(-1)
    , mRotation(0)
    , mAudioStreamType(3)
    , mHasVideo(false)
    , mHasAudio(false)
    , mHasSubTitle(false)
    , mUriType(kUriDefault)
    , mExternalSubtitleEnabled(false)
    , mSeekPreviewDoneCondition(mLock)
    , mBufferUpdateEventFilterCount(0)
{
    FUNC_TRACK();
    int i=0;

    resetMemberVariables();
    //mComponents.reserve(5);
    for (i=0; i<Component::kMediaTypeCount; i++) {
        mSelectedTrack[i] = -1;
    }

    mMediaMeta = MediaMeta::create();
    if (!mMediaMeta) {
        ERROR("create MediaMeta failed, no mem\n");
    }
}

PipelinePlayerBase::~PipelinePlayerBase()
{
    FUNC_TRACK();
}
mm_status_t PipelinePlayerBase::loadPreparation( UriType type )
{
    FUNC_TRACK();

    if (!mComponents.empty()) {
        DEBUG("clear for loop playing");// release components resource here to avoid crash when realsing in reset method
        reset();
    }

    setState(mState, kComponentStateNull);
    ComponentSP comp;
    if (type == kUriRtpIF) {
        comp = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_RTP_DEMUXER);
        if (!comp) {
            ERROR("fail to create component:%s\n", MEDIA_MIMETYPE_MEDIA_RTP_DEMUXER);
            return MM_ERROR_NO_COMPONENT;
        }
    }
    else if (type == kUriMPD) {
        // TODO create IMPD and input::DASHManager
        //      extract video and audio representatons, create video/audio DashStream and SegExtractor
        comp = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_DASH_DEMUXER);
        if (!comp) {
            ERROR("fail to create component:%s\n", MEDIA_MIMETYPE_MEDIA_DASH_DEMUXER);
            return MM_ERROR_NO_COMPONENT;
        }
    }
    else if (type == kUriExternal) {
        comp = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_APP_SOURCE);
        if (!comp) {
            ERROR("fail to create component:%s\n", MEDIA_MIMETYPE_MEDIA_APP_SOURCE);
            return MM_ERROR_NO_COMPONENT;
        }
    }
    else {
        comp = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_DEMUXER);
        if (!comp) {
            ERROR("fail to create component:%s\n", MEDIA_MIMETYPE_MEDIA_DEMUXER);
            return MM_ERROR_NO_COMPONENT;
        }
    }

    mComponents.push_back(ComponentInfo(comp, ComponentInfo::kComponentTypeSource));
    mDemuxIndex = mComponents.size() - 1;
    if (type == kUriMPD)
        mDashFirstSourceIndex = mDemuxIndex;
    mUriType = type;

    return MM_ERROR_SUCCESS;
}

PlaySourceComponent* PipelinePlayerBase::getSourceComponent()
{
    FUNC_TRACK();
    PlaySourceComponent* source = NULL;
    if (mDemuxIndex < 0 || (size_t)mDemuxIndex >= mComponents.size()) {
        MMLOGD("no source component\n");
        return NULL;
    }

    ComponentSP comp = mComponents[mDemuxIndex].component;
    ASSERT_RET(comp, source);

    source = DYNAMIC_CAST<PlaySourceComponent*>(comp.get());

    return source;
}

DashSourceComponent* PipelinePlayerBase::getDashSourceComponent()
{
    FUNC_TRACK();
    DashSourceComponent* source = NULL;
    ASSERT_RET(mDemuxIndex >=0 && (uint32_t)mDemuxIndex < mComponents.size(), source);
    ComponentSP comp = mComponents[mDemuxIndex].component;
    ASSERT_RET(comp, source);

    source = DYNAMIC_CAST<DashSourceComponent*>(comp.get());

    return source;
}

DashSourceComponent* PipelinePlayerBase::getDashSecondSourceComponent()
{
    FUNC_TRACK();
    DashSourceComponent* source = NULL;
    if (mDashSecondSourceIndex < 0 || mDashSecondSourceIndex >= (int32_t)mComponents.size())
        return NULL;
    ComponentSP comp = mComponents[mDashSecondSourceIndex].component;
    ASSERT_RET(comp, source);

    source = DYNAMIC_CAST<DashSourceComponent*>(comp.get());

    return source;
}

PlaySinkComponent* PipelinePlayerBase::getSinkComponent(Component::MediaType type) const
{
    FUNC_TRACK();
    PlaySinkComponent* sink = NULL;
    ComponentSP comp;
    if (type == Component::kMediaTypeVideo) {
        ASSERT_RET(mVideoSinkIndex >=0 && (uint32_t)mVideoSinkIndex < mComponents.size(), sink);
        comp = mComponents[mVideoSinkIndex].component;
    } else if (type == Component::kMediaTypeAudio) {
         ASSERT_RET(mSinkClockIndex >=0 && (uint32_t)mSinkClockIndex < mComponents.size(), sink);
         comp = mComponents[mSinkClockIndex].component;
    }
    ASSERT_RET(comp, sink);
    sink = DYNAMIC_CAST<PlaySinkComponent*>(comp.get());

    return sink;
}

Pipeline::UriType PipelinePlayerBase::getUriType(const char* uri) {
    const char *p = strstr(uri, "rtp://");
    if (p && (p==uri))
       return kUriRtpIF;

    p = strstr(uri, "user://");
    if (p && (p==uri))
       return kUriExternal;

    size_t size = strlen(uri);
    if (size < 4)
       return kUriDefault;

    p = uri + (size - 4);
    if (!strcmp(p, ".mpd"))
        return kUriMPD;

    return kUriDefault;
}

 mm_status_t PipelinePlayerBase::load(const char * uri,
                           const std::map<std::string, std::string> * headers)
{
     FUNC_TRACK();
     mm_status_t status = MM_ERROR_UNKNOWN;
     UriType type = kUriDefault;

     if (!uri)
         return MM_ERROR_INVALID_URI;

     type = getUriType(uri);

     status = loadPreparation(type);
    if (status != MM_ERROR_SUCCESS)
        return status;

    if (type != kUriExternal) {
        PlaySourceComponent* source = getSourceComponent();
        ASSERT_RET(source, MM_ERROR_NO_COMPONENT);
        if (!mDownloadPath.empty()) {
            MediaMetaSP meta = MediaMeta::create();
            meta->setString(MEDIA_ATTR_FILE_DOWNLOAD_PATH, mDownloadPath.c_str());
            source->setParameter(meta);
        }
        status = source->setUri(uri, headers);
        INFO("status = %d\n", status);
        ASSERT(status == MM_ERROR_SUCCESS);
    }

    return status;
}

mm_status_t PipelinePlayerBase::load(int fd, int64_t offset, int64_t length)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;

    if (!fd)
        return MM_ERROR_INVALID_URI;

    status = loadPreparation(kUriDefault);
    if (status != MM_ERROR_SUCCESS)
        return status;

    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);
    status = source->setUri(fd, offset, length);
    ASSERT(status == MM_ERROR_SUCCESS);

    return status;
}

 mm_status_t PipelinePlayerBase::loadSubtitleUri(const char * uri)
{
    FUNC_TRACK();
    if (!uri)
        return MM_ERROR_INVALID_URI;

    mSubtitleUri = std::string(uri);
    if (mSubtitleSourceIndex != -1) {
        PlaySourceComponent *subtitleSource = DYNAMIC_CAST<PlaySourceComponent*>(mComponents[mSubtitleSourceIndex].component.get());
        if(subtitleSource){
            subtitleSource->setUri(uri);
        }
    }

    return MM_ERROR_SUCCESS;
}

MMParamSP PipelinePlayerBase::getTrackInfo()
{
    FUNC_TRACK();
    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, nilParam);
    return source->getTrackInfo();
}

mm_status_t PipelinePlayerBase::updateTrackInfo()
{
    FUNC_TRACK();
    MMParamSP contentParam = getTrackInfo();
    if (!contentParam) {
        MMLOGD("no track info\n");
        return MM_ERROR_SUCCESS;
    }

    mm_status_t status = MM_ERROR_UNKNOWN;
    int32_t streamCount = 0;
    int i;
    status = contentParam->readInt32(&streamCount);
    ASSERT_RET(status == MM_ERROR_SUCCESS, status);
    INFO("streamCount=%d\n", streamCount);

    for (i=0; i<streamCount; i++) {
        MMAutoLock locker(mLock);
        int32_t trackType;
        int32_t trackCount;
        status = contentParam->readInt32(&trackType);
        status = contentParam->readInt32(&trackCount);
        int j = 0;
        for (j=0; j<trackCount; j++) {
            int32_t trackId;
            int32_t width = 0, height = 0;
            int32_t codecId;
            const char* codecName = NULL;
            const char* mime = NULL;
            const char* title = NULL;
            const char* language = NULL;
            status = contentParam->readInt32(&trackId);
            INFO("trackType:%d, trackCount=%d, trackId:%d\n", trackType, trackCount, trackId);
            status = contentParam->readInt32(&codecId);
            codecName = contentParam->readCString();
            mime = contentParam->readCString();
            title = contentParam->readCString();
            language = contentParam->readCString();

            if (trackType == Component::kMediaTypeVideo) {
                width = contentParam->readInt32();
                height = contentParam->readInt32();
            }
            INFO("\t\t trackId=%d, size: %dx%d, codecId=%d, codecName=%s, mime = %s, title=%s, language=%s\n",
                trackId, width, height, codecId, codecName, mime, title, language);
            TrackInfo track;
            track.id = trackId;
            track.codecId = (CowCodecID)codecId;
            track.mime = std::string(mime);
            track.codecName = std::string(codecName);
            track.title = std::string(title);
            track.language = std::string(language);
            // FIXME, copy the strings twice
            mStreamInfo[trackType].push_back(track);
        }
        mSelectedTrack[trackType] = 0;
    }

    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);
    bool isSeekable = source->isSeekable();
    INFO("isSeekable %d\n", isSeekable);
    mMediaMeta->setInt32(MEDIA_ATTR_STREAM_IS_SEEKABLE, isSeekable);

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setDisplayName(const char* name)
{
    return MM_ERROR_UNSUPPORTED;
}

mm_status_t PipelinePlayerBase::setNativeDisplay(void * display)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mNativeDisplay = display;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setVideoSurface(void * handle, bool isTexture)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    // the handle which is set before prepare() will be used
    mSurface = handle;
    mIsSurfaceTexture = isTexture;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::prepare()
{
    FUNC_TRACK();

    // CHECK_PIPELINE_STATE(kComponentStatePrepared, Component::kEventPrepareResult);
    {
        MMAutoLock locker(mLock);
        if (mState >= kComponentStatePreparing) {
            WARNING("pipelien is aready in preparing state: %d", mState);
            notify(Component::kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
            return MM_ERROR_SUCCESS;
        }
    }

    if (!mMediaMeta) {
        mMediaMeta = MediaMeta::create();
    }

    mm_status_t status = MM_ERROR_SUCCESS;
    status = prepareInternal();
    if (status != MM_ERROR_SUCCESS) {
        notify(Component::kEventPrepareResult, status, 0, nilParam);
    }

    SET_PIPELINE_STATE(status, prepare, kComponentStatePreparing, kComponentStatePrepared, Component::kEventPrepareResult);
    return status;
}

mm_status_t PipelinePlayerBase::start()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventStartResult);

    SET_PIPELINE_STATE(status, start, kComponentStatePlay, kComponentStatePlaying, Component::kEventStartResult);

    return status;
}

mm_status_t PipelinePlayerBase::stop()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    CHECK_PIPELINE_STATE(kComponentStateStopped, Component::kEventStopped);
    CHECK_PIPELINE_STATE(kComponentStateNull, Component::kEventStopped);

    {
        MMAutoLock locker(mLock);
        mSeekEventParam.reset();
    }

    SET_PIPELINE_STATE(status, stop, kComponentStateStop, kComponentStateStopped, Component::kEventStopped);
    resetMemberVariables();

    return status;
}

mm_status_t PipelinePlayerBase::pause()
{
    FUNC_TRACK();

    // CHECK_PIPELINE_STATE(kComponentStatePaused, Component::kEventPaused);

    {
        MMAutoLock locker(mLock);
        if (mState != kComponentStatePlay &&
            mState != kComponentStatePlaying) {
            WARNING("pipelien is in state: %s", sInternalStateStr[mState]);
            notify(Component::kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
            return MM_ERROR_SUCCESS;
        }
    }

    mm_status_t status = MM_ERROR_SUCCESS;
    SET_PIPELINE_STATE(status, pause, kComponentStatePausing, kComponentStatePaused, Component::kEventPaused);

    return status;
}

mm_status_t PipelinePlayerBase::resume()
{
    FUNC_TRACK();
    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventResumed);

    mm_status_t status = MM_ERROR_SUCCESS;
    SET_PIPELINE_STATE(status, resume, kComponentStatePlay, kComponentStatePlaying, Component::kEventResumed);

    return status;
}

mm_status_t PipelinePlayerBase::seek(SeekEventParamSP param)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    {
        //update seekEvent first
        MMAutoLock locker(mLock);
        mSeekEventParam = param;

    }

    INFO("pipelineplayerbase state mState=%s\n", sInternalStateStr[mState]);
    if (mState != kComponentStatePlaying && mState != kComponentStatePaused && mState != kComponentStatePrepared) {
        ERROR("invalid state for seek");
        return MM_ERROR_INVALID_STATE;
    }

    bool isPlaying = (mState == kComponentStatePlaying);
    bool isPaused = (mState == kComponentStatePaused);

    if (!param) {
        ERROR("the parameter of seek invalid");
        return MM_ERROR_INVALID_PARAM;
    }
    if (!param->hasPendingSeek()) {
        INFO("the seek time has already been used by previous processing, just return\n");
        return MM_ERROR_SUCCESS; // the seek time has alraedy been used by previous processing
    }

    DEBUG("do_real_seek");
    //Seeking operation during variable playing cause to normal playing
    if (mScaledPlayRate != SCALED_PLAY_RATE) {
        MediaMetaSP meta = MediaMeta::create();
        meta->setInt32(MEDIA_ATTR_PALY_RATE, SCALED_PLAY_RATE);
        setParameter(meta);
    }

    StateAutoSet autoSet(mState3, mState);
    mSeekSequence++;

    // FIXME: app may receive mesage of state change, should decoder does pause internally?
    if (isPlaying) {
        status = pause();
        if (status != MM_ERROR_SUCCESS) {
            ERROR("pause fail when seek");
            return status;
        }
    }


    status = flushInternal(true);
    //ASSERT_RET(status == MM_ERROR_SUCCESS, status);

    PlaySourceComponent* source = getSourceComponent();
    if (!source) {
        ERROR("no source component when seek");
        return MM_ERROR_INVALID_PARAM;
    }

    setState(mComponents[mDemuxIndex].state2, kComponentStateInvalid);
    setState(mState2, kComponentStateInvalid);

    int64_t msec = param->getSeekTime();
    DEBUG("seek to %" PRId64, msec);
    status = source->seek(msec, mSeekSequence);

    do {
        if (mUriType == kUriMPD) {
            PlaySourceComponent* secondSource = getDashSecondSourceComponent();
            if (!secondSource) {
                INFO("no second source");
                break;
            }
            status = secondSource->seek(msec, mSeekSequence);
        }
    } while (0);

    if (mSubtitleSourceIndex != -1) {
        PlaySourceComponent *subtitleSource = DYNAMIC_CAST<PlaySourceComponent*>(mComponents[mSubtitleSourceIndex].component.get());
        if(subtitleSource){
            subtitleSource->seek(msec, mSeekSequence);
        }
    }

    // FIXME: after component does flush internally during seek, call seek for each component and remove the flush above
    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mState2, kComponentSeekComplete, true/*pipeline state*/, true/*state2*/);
    } else if (status == MM_ERROR_SUCCESS) {
        setState(mState2, kComponentSeekComplete);
        // notify(Component::kEventSeekComplete, MM_ERROR_SUCCESS, 0, nilParam);
    } else {
        ERROR("%s seek failed with status <%d>\n", source->name(), status);
        if (isPlaying) {
            status = start();
        }
        return status;
    }

    //do seekPreview when video stream exists
    if (isPaused && mHasVideo) {
        //check video sink
        PlaySinkComponent *sink = getSinkComponent(Component::kMediaTypeVideo);
        if (!sink) {
            ERROR("no sink component when seek");
            return MM_ERROR_INVALID_PARAM;
        }
        MediaMetaSP meta = MediaMeta::create();
        meta->setInt32("render-mode", 1/*RM_SEEKPREVIEW*/);
        sink->setParameter(meta);

        //check audio sink, skip to start audio sink
        if (mHasAudio) {
            setState(mComponents[mAudioSinkIndex].state, kComponentStatePlaying);
            DEBUG("fake audio playing\n");
        }

        mSeekPreviewDone = false; //remeber to pause when seekPreview done
        status = start();

        {
            MMAutoLock locker(mLock);
            while (!mSeekPreviewDone) {
                DEBUG("wait seek preview done message\n");
                int ret = mSeekPreviewDoneCondition.timedWait(PREVIEW_SEEK_DONE_TIME);
                if (ret) {
                    DEBUG("wait for seek preview done message %d(%s)\n", ret, strerror(ret));
                    return (ret == ETIMEOUT) ? MM_ERROR_TIMED_OUT : MM_ERROR_UNKNOWN;
                } else {
                    DEBUG("get seek preview done message\n");
                }
            }
        }

        PlaySinkComponent *audioSink = getSinkComponent(Component::kMediaTypeAudio);
        if (audioSink) {
            audioSink->seek(msec, mSeekSequence);
        }
        DEBUG("pause after seek\n");
        status = pause();
    } else {
        if (isPlaying) {
            status = start();
        }
    }

    return status;
}

mm_status_t PipelinePlayerBase::unblock()
{
    {
        MMAutoLock locker(mLock);
        mEscapeWait = true;
    }
    mCondition.broadcast();
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::reset()
{
    FUNC_TRACK();
    uint32_t i=0;
    mm_status_t status = MM_ERROR_SUCCESS;

    CHECK_PIPELINE_STATE(kComponentStateNull, Component::kEventResetComplete);

    {
        MMAutoLock locker(mLock);
        mEscapeWait = false;
        mSeekEventParam.reset();
    }

    if (mComponents.size()== 0)
        return status;

    setState(mState2, kComponentStateInvalid);

    for (i=0; i<mComponents.size(); i++) {
        setState(mComponents[i].state2, kComponentStateInvalid);
        mm_status_t ret = mComponents[i].component->reset();
        if (ret == MM_ERROR_SUCCESS) {
            INFO("%s reset done", mComponents[i].component->name());
            setState(mComponents[i].state2, kComponentResetComplete);
        } else if (ret == MM_ERROR_ASYNC) {
            status = MM_ERROR_ASYNC;
        } else {
            ERROR("%s reset failed", mComponents[i].component->name());
            // FIXME fix component reset failure
            setState(mComponents[i].state2, kComponentResetComplete);
            status = ret;
        }
    }

    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mState2, kComponentResetComplete, true/*pipeline state*/, true/*state2*/);
    } else if (status == MM_ERROR_SUCCESS) {
        setState(mState2, kComponentResetComplete);
        notify(Component::kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    } else {
        notify(Component::kEventResetComplete, status, 0, nilParam);
    }

    // FIXME: destroy all components, restart playback is required to begin from load()
    // always destroy the components here.
    mComponents.clear();
    resetMemberVariables();
    mMediaMeta.reset();
    mClock.reset();
    mDownloadPath.clear();

    return status;
}

mm_status_t PipelinePlayerBase::flush()
{
    FUNC_TRACK();
    mm_status_t status = flushInternal();
    return status;
}
mm_status_t PipelinePlayerBase::flushInternal(bool skipDemuxer)
{
    FUNC_TRACK();
    uint32_t i;
    mm_status_t status = MM_ERROR_SUCCESS;

    setState(mState2, kComponentStateInvalid);

    for (i=0; i<mComponents.size(); i++) {
        if (skipDemuxer && (int32_t)i == mDemuxIndex) {
            setState(mComponents[i].state2, kComponentFlushComplete);
            DEBUG("skip demuxer flush");
            continue;
        }
        setState(mComponents[i].state2, kComponentStateInvalid);
        mm_status_t ret = mComponents[i].component->flush();
        if (ret == MM_ERROR_SUCCESS) {
            setState(mComponents[i].state2, kComponentFlushComplete);
            INFO("%s flush done", mComponents[i].component->name());
        } else if (ret == MM_ERROR_ASYNC) {
            INFO("%s flush async", mComponents[i].component->name());
            status = MM_ERROR_ASYNC;
        } else {
            ERROR("%s flush failed", mComponents[i].component->name());
            setState(mComponents[i].state2, kComponentStateInvalid);
            status = ret;
            break;
        }
    }

    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mState2, kComponentFlushComplete, true/*pipeline state*/, true/*state2*/);
    } else if (status == MM_ERROR_SUCCESS) {
        setState(mState2, kComponentFlushComplete);
        notify(Component::kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    } else {
        notify(Component::kEventFlushComplete, status, 0, nilParam);
    }

    return status;
}

mm_status_t PipelinePlayerBase::getState(ComponentStateType& state)
{
    FUNC_TRACK();
    if (mState3 != kComponentStateInvalid) {
        state = mState3;
    } else {
        state = mState;
    }

    return MM_ERROR_SUCCESS;
}

// handle components message in async mode, runs in PipelinePlayerBase's MMMsgThread
void PipelinePlayerBase::onComponentMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
{
    //FUNC_TRACK();
    if (mState == kComponentStateNull) {
        INFO("Ignore all the events when in null state");
        return;
    }
    ASSERT(rspId == 0);
    PipelineParamRefBase* paramRef = DYNAMIC_CAST<PipelineParamRefBase*>(param3.get());
    if(!paramRef){
        ERROR("paramRef DYNAMIC_CAST fail\n");
        mCondition.broadcast();
        return;
    }
    Component::Event event = static_cast<Component::Event>(paramRef->mMsg);

    //Make sure mSender is valid
    const Component* sender = paramRef->mSender;
    if(!sender){
        ERROR("sender is NULL");
        mCondition.broadcast();
        return;
    }

    ComponentStateType reachedState = kComponentStateInvalid;
    printMsgInfo(event, param1, sender->name());
    INFO("sender: %s, event = %s, param1 = %d\n", sender->name(), Component::sEventStr[event], param1);

    switch(event) {
        case Component::kEventPrepareResult:
            reachedState = kComponentStatePrepared;
            break;
        case Component::kEventStartResult:
        case Component::kEventResumed:
            reachedState = kComponentStatePlaying;
            break;
        case Component::kEventStopped:
            reachedState = kComponentStateStopped;
            break;
        case Component::kEventPaused:
            reachedState = kComponentStatePaused;
            break;
        case Component::kEventSeekComplete:
            reachedState = kComponentSeekComplete;
            break;
        case Component::kEventFlushComplete:
            reachedState = kComponentFlushComplete;
            break;
        case Component::kEventResetComplete:
            reachedState = kComponentResetComplete;
            break;
        case Component::kEventEOS:
            reachedState = kComponentEOSComplete;
            break;
        default:
            break;
    }

    switch (event) {
        case Component::kEventPrepareResult:
        case Component::kEventStartResult:
        case Component::kEventStopped:
        case Component::kEventPaused:
        case Component::kEventResumed:
            {
                ASSERT(reachedState != kComponentStateInvalid);
                uint32_t i=0;
                bool done = true; // pipelineplayerbase state change is done
                bool isSeeking =  (mState3 != kComponentStateInvalid);

                if ((mm_errors_t)param1 != MM_ERROR_SUCCESS) {
                    ERROR("%s fail to reach state %s\n", sender->name(), sInternalStateStr[reachedState]);
                    // do not break, but setup error code
                    setMemberUint32(mErrorCode, param1);
                }
                {
                    {
                         // lock it. when two comp msg come at same time, 'done' may not update because of race condition
                        MMAutoLock locker(mLock);
                        for (i=0; i<mComponents.size(); i++) {
                            if (sender == mComponents[i].component.get()) {
                                INFO("%s reach state %s\n", mComponents[i].component->name(), sInternalStateStr[reachedState]);
                                mComponents[i].state = reachedState;
                            } else if (mComponents[i].state != reachedState) {
                                INFO("%s does NOT reach state %s\n", mComponents[i].component->name(), sInternalStateStr[reachedState]);
                                done = false;
                            }
                        }
                    }
                    if (done && mComponents.size()>1) {  // during pipelineplayerbase construction, we should NOT send kEventPrepareResult when there is demux component only
                        INFO("pipelineplayerbase reach state %s\n", sInternalStateStr[reachedState]);
                        setState(mState, reachedState);
                        // inform upper layer state change is done
                        if (event == Component::kEventResumed) {
                            DEBUG("kEventResumed --> kEventStartResult");
                            event = Component::kEventStartResult;
                        }
                        if (!isSeeking) { // not in the process of seek. state change triggered by seek are suppressed.
                            notify(int(event), mErrorCode, 0, nilParam);
                        }
                    }
                }
            }
            break;
        case Component::kEventResetComplete:
        case Component::kEventSeekComplete:
        case Component::kEventFlushComplete:
        case Component::kEventEOS:
            {
                ASSERT(reachedState != kComponentStateInvalid);
                uint32_t i=0;
                bool done = true; // pipelineplayerbase state change is done

                if ((mm_errors_t)param1 != MM_ERROR_SUCCESS) {
                    ERROR("%s fail to reach state %s\n", sender->name(), sInternalStateStr[reachedState]);
                    // do not break, but setup error code
                    setMemberUint32(mErrorCode, param1);
                }

                {
                    MMAutoLock locker(mLock);
                    for (i=0; i<mComponents.size(); i++) {
                        if (sender == mComponents[i].component.get()) {
                            INFO("%s reach internal state %s\n", mComponents[i].component->name(), sInternalStateStr[reachedState]);
                            mComponents[i].state2 = reachedState;
                            // update component state to Null for kEventResetComplete
                            if (event == Component::kEventResetComplete) {
                                mComponents[i].state = kComponentStateNull;
                            }
                        } else if (mComponents[i].state2 != reachedState) {
                            INFO("%s does NOT reach state %s\n", mComponents[i].component->name(), sInternalStateStr[reachedState]);
                            if (event == Component::kEventSeekComplete) {
                                if (mComponents[i].mType == ComponentInfo::kComponentTypeSource)
                                    done = false;
                            } else if (event == Component::kEventEOS) {
                                if (mComponents[i].mType == ComponentInfo::kComponentTypeSink && i != mSubtitleSinkIndex)
                                    done = false;
                            } else {
                                done = false;
                            }
                        }
                    }
                }

                // FIXME, LPASink may notify kEventSeekComplete
                if (done) {
                    INFO("pipelineplayerbase reach state %s\n", sInternalStateStr[reachedState]);
                    setState(mState2, reachedState);

                    if (event == Component::kEventResetComplete) {
                        setState(mState, kComponentStateNull);
                    }
                    // inform upper layer state change is done
                    if (event == Component::kEventSeekComplete) {
                        MMAutoLock locker(mLock);
                        DEBUG("SeekComplete, clearTargetSeekTime");
                        if (mSeekEventParam) {
                            mSeekPositionMs = -1;
                            mSeekEventParam->getTargetSeekTime(mSeekPositionMs);
                            mSeekEventParam->clearTargetSeekTime();
                        }
                    } else {
                        notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
                        if (event == Component::kEventEOS) {
                            resetMemberVariables();
                        }
                    }
                }
            }
            break;
        case Component::kEventDrainComplete:
            // FIXME: drain is signaled by sink only or by each component? up to now, this event doesn't value much
            // notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
            break;
        case Component::kEventError:
            notify(int(event), param1, 0, nilParam);
            break;
        case Component::kEventGotVideoFormat:
        {
            if (!sender)
                break;

            int32_t left = 0;
            int32_t top = 0;
            int32_t right = 0;
            int32_t bottom = 0;
            int32_t displayWidth = 0;
            int32_t displayHeight = 0;

            {
                MMAutoLock locker(mLock);
                mWidth = (int32_t)param1;
                mHeight = reinterpret_cast<int32_t>(param2);
                if (paramRef->mParam) {
                    left = paramRef->mParam->readInt32(); // x offset
                    top = paramRef->mParam->readInt32();// y offset
                    right = paramRef->mParam->readInt32();
                    bottom = paramRef->mParam->readInt32();


                    displayWidth = right - left + 1;
                    displayHeight = bottom - top + 1;

                    INFO("Video output format changed to %d x %d "
                         "(crop: %d x %d @ (%d, %d))",
                         mWidth, mHeight,
                         displayWidth,
                         displayHeight,
                         left, top);
                } else {
                    displayWidth = mWidth;
                    displayHeight = mHeight;
                }
                //mRotation = paramSP->readInt32();

            }
            //INFO("got video resolution %dx%d, rotation=%d\n", mWidth, mHeight, mRotation);
            int32_t tmp = 0;
            if (mRotation == 90 || mRotation == 270) {
                tmp = displayHeight;
                displayHeight = displayWidth;
                displayWidth = tmp;
            }

            MMParamSP param(new MMParam);
            param->writeInt32(mRotation);
            //Note: notify display width/heigth to upper in priority
            notify(int(event), displayWidth, displayHeight, param);
        }
            break;
        case Component::kEventInfoDuration:
        {
            MMAutoLock locker(mLock);
            mDurationMs = (int64_t)param1;
            INFO("got duration: %" PRId64 " ms\n", mDurationMs);
        }
            notify(int(Component::kEventInfoDuration), param1, 0, nilParam);
            break;

        case Component::kEventInfoBufferingUpdate:
        {
            uint32_t percent = uint32_t(param1);
            INFO("buffer update percent: %d\n", percent);
            if (percent <= 100) {
                if (!mBufferUpdateEventFilterCount || percent % mBufferUpdateEventFilterCount == 0)
                    notify(int(Component::kEventInfoBufferingUpdate), percent, 0, nilParam);
            } else
                WARNING("invalid buffer update: %d\n", percent);
        }
            break;
        case Component::kEventUpdateTextureImage:
        {
            INFO("update texture image: %d\n", param1);
            notify(int(Component::kEventUpdateTextureImage), param1, 0, nilParam);
        }
            break;
        case Component::kEventInfo:
            switch (param1) {
                case PlaySourceComponent::kEventMetaDataUpdate:
                if (!mVideoResolutionReportDone) {
                    // for video resolution from demuxer, we report it just once.
                    // 1. it satify client better to receive the info as early as possible
                    // 2. for video resolution change, the event from video decoder is more accurate since
                    //     there is cached buffer between demuxer and decoder
                    PlaySourceComponent* source = getSourceComponent();
                    ASSERT(source);
                    Component::ReaderSP reader = source->getReader(Component::kMediaTypeVideo);
                    ASSERT(reader);
                    MediaMetaSP meta = reader->getMetaData();
                    ASSERT(meta);
                    {
                        MMAutoLock locker(mLock);
                        int32_t val = -1;
                        if (meta->getInt32(MEDIA_ATTR_WIDTH, val))
                            mWidth = val;

                        if (meta->getInt32(MEDIA_ATTR_HEIGHT, val))
                            mHeight = val;

                        if (meta->getInt32(MEDIA_ATTR_ROTATION, val))
                            mRotation= val;
                    }

                    INFO("got video resolution: %d x %d, rotation %d\n", mWidth, mHeight, mRotation);
                    if (mWidth>0 && mHeight>0) {
                        int32_t displayWidth = mWidth;
                        int32_t displayHeight = mHeight;
                        if (mRotation == 90 || mRotation == 270) {
                            displayHeight = mWidth;
                            displayWidth = mHeight;
                        }
                        notify(int(Component::kEventGotVideoFormat), displayWidth, displayHeight, nilParam);
                        mVideoResolutionReportDone = true;
                    }
                    //notify(int(Component::kEventMediaInfo), 0, 0, meta);
                } else
                    WARNING("skip video resolution update from AVDemuxer");
                    break;
                case Component::kEventInfoVideoRenderStart:
                    INFO("receive and send kEventInfoVideoRenderStart\n");
                    notify(int(Component::kEventInfo), int(Component::kEventInfoVideoRenderStart), 0, nilParam);
                    break;
                case Component::kEventInfoMediaRenderStarted:
                    notify(int(Component::kEventInfo), int(Component::kEventInfoMediaRenderStarted), 0, nilParam);
                    break;

                case Component::kEventInfoSeekPreviewDone:
                    INFO("receive and send kEventInfoSeekPreviewDone\n");
                    {
                        MMAutoLock locker(mLock);
                        mSeekPreviewDone = true;
                        mSeekPreviewDoneCondition.broadcast();
                    }
                    break;
                case Component::kEventCostMemorySize:
                    INFO("receive and send kEventCostMemorySize\n");
                    notify(int(Component::kEventInfo), int(Component::kEventCostMemorySize), reinterpret_cast<int32_t>(param2), nilParam);
                    break;
                default:
                    notify(event, param1, 0, nilParam);
                    break;
            }
            break;
        case Component::kEventRequestIDR:
            notify(int(Component::kEventRequestIDR), param1, 0, nilParam);
            break;
        case Component::kEventVideoRotationDegree:
            INFO("update video rotation degree: %d\n", param1);
            notify(int(Component::kEventVideoRotationDegree), param1, 0, nilParam);
            break;
        case Component::kEventInfoSubtitleData:
            notify(int(Component::kEventInfoSubtitleData), param1, 0, paramRef->mParam);
            break;
        case Component::kEventSeekRequire:
            notify(int(Component::kEventSeekRequire), param1, 0, nilParam);
            break;
        default:
            notify(event, param1, 0, nilParam);
            break;
    }

    {   /* other thread may miss this condition/signal w/o lock, leads to deadly wait
         * for example: condition/signal emits after  waitUntilCondition() checked components status,
         * but before timedWait()
         */
        MMAutoLock locker(mLock);
        mCondition.broadcast();
    }
}

// bridge cowplayer message, used for kEventSeekComplete only for now
void PipelinePlayerBase::onCowMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    PipelineParamRefBase* paramRef = DYNAMIC_CAST<PipelineParamRefBase*>(param3.get());
    if(!paramRef){
        ERROR("paramRef DYNAMIC_CAST fail\n");
        return;
    }
    Component::Event event = static_cast<Component::Event>(paramRef->mMsg);

    printMsgInfo(event, param1, "cowplayer");
    notify(event, param1, 0, nilParam);
}

mm_status_t PipelinePlayerBase::getDuration(int64_t& durationMs)
{
    //FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    if (mState < kComponentStatePrepared) {
        durationMs = -1;
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_SUCCESS;
    }

    if (mDurationMs <= 0) {
        if (mDemuxIndex >= 0 && (uint32_t)mDemuxIndex <= mComponents.size()-1) {
            MMAutoLock locker(mLock);
            PlaySourceComponent* source = getSourceComponent();
            ASSERT_RET(source, MM_ERROR_UNKNOWN);
            status = source->getDuration(mDurationMs);
        }
    }

    durationMs = mDurationMs;
    INFO("duration: %ld ms\n", durationMs);
    return status;
}

mm_status_t PipelinePlayerBase::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    width = 0;
    height = 0;
    if (mState < kComponentStatePrepared) {
        width = -1;
        height = -1;
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_SUCCESS;
    }

    MMAutoLock locker(mLock);
    if (mWidth >0 && mHeight >0) {
        width = mWidth;
        height = mHeight;
        status = MM_ERROR_SUCCESS;
    }

    return status;
}

mm_status_t PipelinePlayerBase::getCurrentPosition(int64_t& positionMs) const
{
    //FUNC_TRACK();

    INFO("mState=%s\n", sInternalStateStr[mState]);
    if (mState < kComponentStatePrepared) {
        positionMs = -1;
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_SUCCESS;
    }

    {
        MMAutoLock locker(mLock);
        if (mSeekEventParam) {
            if (mSeekEventParam->getTargetSeekTime(positionMs)) {
                return MM_ERROR_SUCCESS;
            }
        }
    }

    if (mSinkClockIndex < 0 || mSinkClockIndex >= (int32_t)mComponents.size()) {
        MMLOGE("sink component is not ready\n");
        positionMs = 0;
        return MM_ERROR_SUCCESS;
    }


    PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(mComponents[mSinkClockIndex].component.get());
    if(!sink){
        ERROR("sink DYNAMIC_CAST fail\n");
        return MM_ERROR_NO_COMPONENT;
    }

    positionMs = sink->getCurrentPosition();
    if (positionMs < 0) {//invalid current position
        positionMs = mSeekPositionMs; //during seeking complete and setFirstAudio anchor time,  still report mSeekPositionMs to upper
    } else {
        mSeekPositionMs = -1; //clear position once we got right current position
        positionMs /= 1000;
    }

    DEBUG("getCurrentPosition %" PRId64 " ms", positionMs);

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setAudioStreamType(int type)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    if (type < AS_TYPE_DEFAULT || type > AS_TYPE_CNT)
        ret = MM_ERROR_INVALID_PARAM;
    mAudioStreamType = type;
    return ret;
}
mm_status_t PipelinePlayerBase::getAudioStreamType(int *type)
{
    *type = mAudioStreamType;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setAudioConnectionId(const char * connectionId)
{
    mAudioConnectionId = connectionId;
    return MM_ERROR_SUCCESS;
}

const char * PipelinePlayerBase::getAudioConnectionId() const
{
    return mAudioConnectionId.c_str();
}

mm_status_t PipelinePlayerBase::setVolume(const float left, const float right)
{
    FUNC_TRACK();

    INFO("mState=%s\n", sInternalStateStr[mState]);
    if (mState < kComponentStatePrepared) {
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_INVALID_STATE;
    }

    PlaySinkComponent *sink = getSinkComponent(Component::kMediaTypeAudio);
    ASSERT_RET(sink, MM_ERROR_NO_COMPONENT);
    float volume = (left + right)/2.0f;
    sink->setVolume(volume);
    DEBUG("setVolume %" PRId64 " ms", volume);

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::getVolume(float& left, float& right) const
{
    FUNC_TRACK();

    INFO("mState=%s\n", sInternalStateStr[mState]);
    if (mState < kComponentStatePrepared) {
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_INVALID_STATE;
    }

    PlaySinkComponent *sink = getSinkComponent(Component::kMediaTypeAudio);
    ASSERT_RET(sink, MM_ERROR_NO_COMPONENT);
    float volume = 1.0;
    volume = sink->getVolume();
    left = volume;
    right = volume;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setMute(bool mute)
{
    FUNC_TRACK();

    INFO("mState=%s\n", sInternalStateStr[mState]);
    if (mState < kComponentStatePrepared) {
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_INVALID_STATE;
    }

    PlaySinkComponent *sink = getSinkComponent(Component::kMediaTypeAudio);
    ASSERT_RET(sink, MM_ERROR_NO_COMPONENT);
    sink->setMute(mute);
    DEBUG("setMute %d", mute);
    return MM_ERROR_SUCCESS;

}

mm_status_t PipelinePlayerBase::getMute(bool * mute) const
{
    FUNC_TRACK();

    INFO("mState=%s\n", sInternalStateStr[mState]);
    if (mState < kComponentStatePrepared) {
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_INVALID_STATE;
    }

    PlaySinkComponent *sink = getSinkComponent(Component::kMediaTypeAudio);
    ASSERT_RET(sink, MM_ERROR_NO_COMPONENT);
    bool tmp = false;
    tmp = sink->getMute();
    *mute = tmp;
    return MM_ERROR_SUCCESS;

}

mm_status_t PipelinePlayerBase::selectTrack(Component::MediaType mediaType, int index)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    if (!mStreamInfo[mediaType].size())
        return MM_ERROR_NO_MEDIA_TYPE;

    if (index < 0 || (uint32_t)index >= mStreamInfo[mediaType].size())
        return MM_ERROR_NO_MEDIA_TRACK;

    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);
    mm_status_t status = source->selectTrack(mediaType, index);
    INFO("status = %d\n", status);
    ASSERT(status == MM_ERROR_SUCCESS);

    mSelectedTrack[mediaType] = index;

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelinePlayerBase::setParameter(const MediaMetaSP & meta)
{
    mm_status_t status = MM_ERROR_SUCCESS;
    std::string mime;
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_PALY_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mMediaMeta->setInt32(MEDIA_ATTR_PALY_RATE, item.mValue.ii);

            if (item.mValue.ii == mScaledPlayRate) {
                INFO("mScaledPlayRate already is %d\n", mScaledPlayRate);
                continue;
            }
            mScaledPlayRate = item.mValue.ii;

            INFO("key: %s, value: %d\n", item.mName, mScaledPlayRate);


            //variable play --> normal play
            if (mScaledPlayRate == SCALED_PLAY_RATE) {
                DEBUG("variable play --> normal play\n");

                //need to flush buffer
                flush();

                DEBUG("variable play --> normal play, flush done\n");
            }

            for (uint32_t i=0; i<mComponents.size(); i++) {
                //no need to set play rate to audio or video codec
                if (((int32_t)i == mAudioCodecIndex) || ((int32_t)i == mVideoCodecIndex)) {
                    continue;
                }
                mm_status_t ret = mComponents[i].component->setParameter(mMediaMeta);
                if (ret != MM_ERROR_SUCCESS) {
                    //FIXME: mediacodec component don't support setparmeter before prepare.
                    //continue to set paramere even returned failed
                    ERROR("%s failed\n", mComponents[i].component->name());
                    //return ret;
                    status  = ret;
                }
            }

            //break;
        }

        if (!strcmp(MEDIA_ATTR_MIME, item.mName) &&
            item.mType == MediaMeta::MT_String) {
            MMLOGI("mime: %s\n", item.mValue.str);
            mime = item.mValue.str;
        }
        if (!strcmp(item.mName, MEDIA_ATTR_FILE_DOWNLOAD_PATH) && item.mType == MediaMeta::MT_String) {
            mDownloadPath = item.mValue.str;
        }

        // codec parameters
        if (!strncmp("codec-", item.mName, 6) ||
            !strncmp("video-", item.mName, 6)) {
            if (item.mType == MediaMeta::MT_Pointer) {
                INFO("get %s - %p", item.mName, item.mValue.ptr);
                mMediaMeta->setPointer(item.mName, item.mValue.ptr);
            } else if (item.mType == MediaMeta::MT_Int64) {
                INFO("get %s - %" PRId64 "", item.mName, item.mValue.ld);
                mMediaMeta->setInt64(item.mName, item.mValue.ld);
            } else if (item.mType == MediaMeta::MT_Int32) {
                INFO("get %s - %d", item.mName, item.mValue.ii);
                mMediaMeta->setInt32(item.mName, item.mValue.ii);
            }
        }
    }

    //somtimes, rtp demuxer need the parameters: width, height
    #define GET_WIDTH_PARAMETER   1
    #define GET_HEIGHT_PARAMETER  2
    #define GET_WIDTH_HEIGHT_PARA 3
    int rtpWidthHeight = 0;
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_WIDTH) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mMediaMeta->setInt32(MEDIA_ATTR_WIDTH, item.mValue.ii);
            rtpWidthHeight |= GET_WIDTH_PARAMETER;
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_HEIGHT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mMediaMeta->setInt32(MEDIA_ATTR_HEIGHT, item.mValue.ii);
            rtpWidthHeight |= GET_HEIGHT_PARAMETER;
            continue;
        }
        if ( !strcmp(item.mName, "memory-size") ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            DEBUG("memory-size %d", item.mValue.ii);
            mMediaMeta->setInt32("memory-size", item.mValue.ii);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_DECODE_MODE) ) {
            if ( item.mType != MediaMeta::MT_String) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            DEBUG("decode mode %s", item.mValue.str);
            mMediaMeta->setString(MEDIA_ATTR_DECODE_MODE, item.mValue.str);
            continue;
        }
    }
    INFO("rtpWidthHeight:%d,%d,%d\n", rtpWidthHeight, mComponents.size(), mDemuxIndex);
    if (rtpWidthHeight == GET_WIDTH_HEIGHT_PARA) {
        int sleepDuration = 1000*1000;
        while((mDemuxIndex <= 0) &&(mComponents.size() < 1) && (sleepDuration > 0)) {
            usleep(50*1000);
            sleepDuration -= 50*1000;
        }
        INFO("sleep duration:%d, demuxer idx:%d, components:%d\n",
               1000*1000 - sleepDuration, mDemuxIndex, mComponents.size());
        if ((mDemuxIndex < 0) || (mComponents.size() < 1) || ((uint32_t)mDemuxIndex >= mComponents.size())) {
            ERROR("now not find the rtp demuxer plugins: demuxer:%d, components:%d\n",
                mDemuxIndex, mComponents.size());
            return MM_ERROR_OP_FAILED;
        }
        mm_status_t ret = mComponents[mDemuxIndex].component->setParameter(mMediaMeta);
        if (ret != MM_ERROR_SUCCESS) {
            ERROR("%s failed\n", mComponents[mDemuxIndex].component->name());
            status  = ret;
        }
    }

    PlaySourceComponent* source = getSourceComponent();
    if (source && !strcmp(source->name(), "APPPlaySource")) {
        source->setParameter(meta);
    }

    if (!strncmp(mime.c_str(), "audio", 5)) {
        MMLOGI("got audio mime: %s\n", mime.c_str());
        mAudioMime = mime;
    }

    if (!strncmp(mime.c_str(), "video", 5)) {
        MMLOGI("got video mime: %s\n", mime.c_str());
        mVideoMime = mime;
    }

    return status;
}

mm_status_t PipelinePlayerBase::getParameter(MediaMetaSP & meta)
{
    meta.reset();
    meta = mMediaMeta;

    return MM_ERROR_SUCCESS;
}

int PipelinePlayerBase::getSelectedTrack(Component::MediaType mediaType)
{
    FUNC_TRACK();
    return mSelectedTrack[mediaType];
}

mm_status_t PipelinePlayerBase::pushData(MediaBufferSP & buffer)
{
    FUNC_TRACK();
    PlaySourceComponent* source = getSourceComponent();
    if (!source) {
        MMLOGE("no source componenet\n");
        return MM_ERROR_NO_COMPONENT;
    }

    return source->pushData(buffer);
}

mm_status_t PipelinePlayerBase::enableExternalSubtitleSupport(bool enable)
{
    FUNC_TRACK();
    mExternalSubtitleEnabled = enable;
    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM
