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
#include <unistd.h>

#include "pipeline_recorder_base.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("PLR-BASE")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

BEGIN_MSG_LOOP(PipelineRecorderBase)
    MSG_ITEM2(PL_MSG_componentMessage, onComponentMessage)
    MSG_ITEM2(PL_MSG_cowMessage, onCowMessage)
END_MSG_LOOP()


#define SET_PARAMETER_INT32(key, from, to) do {   \
    int32_t i = 0;                                \
    if (from->getInt32(key, i)) {                 \
        DEBUG("set %s, value %d", #key, i);       \
        to->setInt32(key, i);                     \
    }                                             \
}while(0)

#define SET_PARAMETER_FLOAT(key, from, to) do {   \
    float i = 0;                                  \
    if (from->getFloat(key, i)) {                 \
        DEBUG("set %s, value %0.4f", #key, i);    \
        to->setFloat(key, i);                     \
    }                                             \
}while(0)

#define SET_PARAMETER_STRING(key, from, to) do {  \
    const char * str = NULL;                      \
    if (from->getString(key, str)) {              \
        DEBUG("set %s, value %s", #key, str);     \
        to->setString(key, str);                  \
    }                                             \
}while(0)

#define SET_PARAMETER_POINTER(key, from, to) do { \
    void *p = NULL;                               \
    if (from->getPointer(key, p)) {               \
        DEBUG("set %s, value %p", #key, p);       \
        to->setPointer(key, p);                   \
    }                                             \
}while(0)

PipelineRecorderBase::PipelineRecorderBase()
    : mSurface(NULL)
    , mState(kComponentStateNull)
    , mState2(kComponentStateInvalid)
    , mMuxIndex(-1)
    , mSinkIndex(-1)
    , mSinkClockIndex(-1)
    , mAudioSourceIndex(-1)
    , mVideoSourceIndex(-1)
    , mAudioCodecIndex(-1)
    , mVideoCodecIndex(-1)
    , mAudioBitRate(-1)
    , mChannelCount(-1)
    , mSampleRate(-1)
#ifndef __DISABLE_AUDIO_STREAM__
    , mAudioSource(ADEV_SOURCE_MIC)
    , mAudioFormat(SND_FORMAT_PCM_16_BIT)
#endif
    , mEscapeWait(false)
    , mMusicSpectrum(0)
    , mDuration(-1)
    , mVideoWidth(-1)
    , mVideoHeight(-1)
    , mRotation(0)
    , mConnectedStreamCount(0)
    , mEOSStreamCount(0)
    , mEosReceived(false)
    , mVideoColorFormat('NV12')
    , mUsage(RU_None)
    , mDelayTimeUs(0)
{
    FUNC_TRACK();
    // mComponents.reserve(5);
    mMediaMetaVideo = MediaMeta::create();
    mMediaMetaAudio = MediaMeta::create();
    mMediaMetaFile = MediaMeta::create();
    mMediaMetaOutput = MediaMeta::create();

    //for video
    mMediaMetaVideo->setInt32(MEDIA_ATTR_PREVIEW_WIDTH, 720);
    mMediaMetaVideo->setInt32(MEDIA_ATTR_PREVIEW_HEIGHT, 544);
    mMediaMetaVideo->setInt32(MEDIA_ATTR_WIDTH, 1280);
    mMediaMetaVideo->setInt32(MEDIA_ATTR_HEIGHT, 720);
    mMediaMetaVideo->setFloat(MEDIA_ATTR_FRAME_RATE, 30.0f);
    // mMediaMetaVideo->setInt32(MEDIA_ATTR_BIT_RATE, 8000000);

    //for audio
    mMediaMetaAudio->setInt32(MEDIA_ATTR_SAMPLE_RATE, 48000);
    mMediaMetaAudio->setInt32(MEDIA_ATTR_CHANNEL_COUNT, 2);
    mMediaMetaAudio->setInt32(MEDIA_ATTR_BIT_RATE, 128000);
#ifndef __DISABLE_AUDIO_STREAM__
    mMediaMetaAudio->setInt32(MEDIA_ATTR_AUDIO_SOURCE, mAudioSource);
    mMediaMetaAudio->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, mAudioFormat);
#endif
}

PipelineRecorderBase::~PipelineRecorderBase()
{
    FUNC_TRACK();
}

#ifndef __DISABLE_AUDIO_STREAM__
/*static*/adev_source_t PipelineRecorderBase::getAudioSourceType(const char* uri)
{
    DEBUG("uri: %s\n", uri);
    adev_source_t audioSourceType = ADEV_SOURCE_MIC;
    if (!strncmp(uri, "cras", 4)) {
        uri = uri + 7;
        if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_MIC, 3)) {
            audioSourceType = ADEV_SOURCE_MIC;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_VOICE_UPLINK, 12)) {
            audioSourceType = ADEV_SOURCE_VOICE_UPLINK;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_VOICE_DOWNLINK, 14)) {
            audioSourceType = ADEV_SOURCE_VOICE_DOWNLINK;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_VOICE_CALL, 10)) {
            audioSourceType = ADEV_SOURCE_VOICE_CALL;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_CAMCORDER, 9)) {
            audioSourceType = ADEV_SOURCE_CAMCORDER;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_VOICE_RECOGNITION, 17)) {
            audioSourceType = ADEV_SOURCE_VOICE_RECOGNITION;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_VOICE_COMMUNICATION, 19)) {
            audioSourceType = ADEV_SOURCE_VOICE_COMMUNICATION;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_REMOTE_SUBMIX, 13)) {
            audioSourceType = ADEV_SOURCE_REMOTE_SUBMIX;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_FM_TUNER, strlen(MEDIA_ATTR_AUDIO_SOURCE_FM_TUNER))) {
            audioSourceType = ADEV_SOURCE_FM_TUNER;
        } else if (!strncmp(uri, MEDIA_ATTR_AUDIO_SOURCE_CNT, 3)) {
            audioSourceType = ADEV_SOURCE_CNT;
        }
    }

    return audioSourceType;
}
#endif

mm_status_t PipelineRecorderBase::start()
{
    FUNC_TRACK();

    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventStartResult);

    mm_status_t status = MM_ERROR_SUCCESS;
    mEosReceived = false;

    uint32_t i=0;
    MediaMetaSP meta = MediaMeta::create();
    // delay time helps us to align a/v start time
    // also helps us to eliminate the unnessary recording sound
    meta->setInt64(MEDIA_ATTR_START_TIME, MMMsgThread::getTimeUs() + mDelayTimeUs);

    for (i=0; i<mComponents.size(); i++) {
        if (i == (uint32_t)mAudioSourceIndex ||
            i == (uint32_t)mVideoSourceIndex) {
            mComponents[i].component->setParameter(meta);
        }
    }

    SET_PIPELINE_STATE(status, start, kComponentStatePlay, kComponentStatePlaying, Component::kEventStartResult);

    return status;
}

mm_status_t PipelineRecorderBase::resume()
{
    FUNC_TRACK();

    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventResumed);

    mm_status_t status = MM_ERROR_SUCCESS;
    SET_PIPELINE_STATE(status, resume, kComponentStatePlay, kComponentStatePlaying, Component::kEventStartResult);

    return status;
}

mm_status_t PipelineRecorderBase::setPreviewSurface(void *handle)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    mSurface = handle;

    // used by Video Test Source
    mMediaMetaVideo->setPointer(MEDIA_ATTR_VIDEO_SURFACE, handle);
    // gui plans to map attachBuffer() API to surface as well, we temply use it from BQ producer now
    // used by VideoSinkSurface to render camera frame
    mMediaMetaVideo->setPointer(MEDIA_ATTR_VIDEO_BQ_PRODUCER, handle);
    return status;
}


void PipelineRecorderBase::dump()
{
    for (uint32_t i = 0; i < mComponents.size(); i++) {
        DEBUG("component[%d]: %s, refcount %d\n",
            i, mComponents[i].component->name(), mComponents[i].component.use_count());
    }
}

mm_status_t PipelineRecorderBase::stop()
{
    FUNC_TRACK();

    CHECK_PIPELINE_STATE(kComponentStateStopped, Component::kEventStopped);
    CHECK_PIPELINE_STATE(kComponentStateNull, Component::kEventStopped);

    mm_status_t status = MM_ERROR_SUCCESS;

    if (mState == kComponentStatePlay ||
            mState == kComponentStatePlaying ||
            mState == kComponentStatePaused ||
            mState == kComponentStatePausing) {
        status = stopInternal();
    }

    if (status != MM_ERROR_SUCCESS) {
        notify(Component::kEventStopped, status, 0, nilParam);
    } else {
        SET_PIPELINE_STATE(status, stop, kComponentStateStop, kComponentStateStopped, Component::kEventStopped);
    }
    return status;
}

mm_status_t PipelineRecorderBase::pause()
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

mm_status_t PipelineRecorderBase::prepare()
{
    FUNC_TRACK();

    // CHECK_PIPELINE_STATE(kComponentStatePrepared, Component::kEventPrepareResult);
    {
        MMAutoLock locker(mLock);
        if (mState >= kComponentStatePreparing) {
            WARNING("pipelien is in state: %s", sInternalStateStr[mState]);
            notify(Component::kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
            return MM_ERROR_SUCCESS;
        }
    }

    mConnectedStreamCount = 0;
    mm_status_t status = prepareInternal();
    if (status != MM_ERROR_SUCCESS) {
        notify(Component::kEventPrepareResult, status, 0, nilParam);
    }

    SET_PIPELINE_STATE(status, prepare, kComponentStatePreparing, kComponentStatePrepared, Component::kEventPrepareResult);

    return status;
}

// FIXME, does unblock really take effect? we do need unblock CowRecorder::MMMsgThread
// it seems correct, CowRecorder::MMMsgThread is Pipeline main thread
mm_status_t PipelineRecorderBase::unblock()
{
    {
        MMAutoLock locker(mLock);
        mEscapeWait = true;
    }
    mCondition.broadcast();
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::reset()
{
    FUNC_TRACK();
    uint32_t i=0;
    mm_status_t status = MM_ERROR_SUCCESS;

    CHECK_PIPELINE_STATE(kComponentStateNull, Component::kEventResetComplete);

    {
        MMAutoLock locker(mLock);
        mEscapeWait = false;
    }

    if (mComponents.size()== 0)
        return status;

    status = resetInternal();
    if (status != MM_ERROR_SUCCESS) {
        notify(Component::kEventResetComplete, status, 0, nilParam);
    }

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

    //In order to support prepare->reset->prepare call sequence, set pipeline state to Null.
    setState(mState, kComponentStateNull);
    // FIXME: destroy all components, restart recorder is required to begin from prepare()
    // always destroy the components here.
    mComponents.clear();
    return status;
}

// FIXME, signal EOS instead, to exit gracefully
mm_status_t PipelineRecorderBase::flush()
{
    FUNC_TRACK();
    uint32_t i;
    mm_status_t status = MM_ERROR_SUCCESS;

    setState(mState2, kComponentStateInvalid);

    for (i=0; i<mComponents.size(); i++) {
        setState(mComponents[i].state2, kComponentStateInvalid);
        mm_status_t ret = mComponents[i].component->flush();
        if (ret == MM_ERROR_SUCCESS) {
            setState(mComponents[i].state2, kComponentFlushComplete);
            INFO("%s flush done", mComponents[i].component->name());
        } else if (ret == MM_ERROR_ASYNC) {
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
    }

    return status;
}

mm_status_t PipelineRecorderBase::getState(ComponentStateType& state)
{
    FUNC_TRACK();
    state = mState;

    return MM_ERROR_SUCCESS;
}

// handle components message in async mode, runs in PipelineRecorderBase's MMMsgThread
void PipelineRecorderBase::onComponentMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
{
    //FUNC_TRACK();
    ASSERT(rspId == 0);
    PipelineParamRefBase* paramRef = DYNAMIC_CAST<PipelineParamRefBase*>(param3.get());
    if(!paramRef){
        ERROR("paramRef DYNAMIC_CAST fail\n");
        return;
    }

    //Make sure mSender is valid
    Component::Event event = static_cast<Component::Event>(paramRef->mMsg);
    const Component* sender = paramRef->mSender;
    MMParamSP paramSP = paramRef->mParam;
    if(!sender){
        ERROR("sender is NULL \n");
        mCondition.broadcast();
        return;
    }
    ComponentStateType reachedState = kComponentStateInvalid;
    printMsgInfo(event, param1, sender->name());
    DEBUG("sender: %s, event = %s, param1 = %d\n", sender->name(), Component::sEventStr[event], param1);

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
        case Component::kEventFlushComplete:
            reachedState = kComponentFlushComplete;
            break;
        case Component::kEventResetComplete:
            reachedState = kComponentResetComplete;
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
                bool done = true; // pipeline state change is done

                //don't break when operation error
                if ((mm_errors_t)param1 != MM_ERROR_SUCCESS) {
                    ERROR("%s fail to reach state %s\n", sender->name(), sInternalStateStr[reachedState]);
                    //set error code to report to upper later
                    setMemberUint32(mErrorCode, param1);

                }

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
                if (done && mComponents.size()>1) {  // durating pipeline construction, we should NOT send kEventPrepareResult when there is demux component only
                    INFO("pipeline reach state %s\n", sInternalStateStr[reachedState]);
                    setState(mState, reachedState);
                    // inform upper layer state change is done
                    if (event == Component::kEventResumed) {
                        DEBUG("kEventResumed --> kEventStartResult");
                        event = Component::kEventStartResult;
                    }
                    notify(int(event), mErrorCode, 0, nilParam);
                }
            }
            break;
        case Component::kEventResetComplete:
        case Component::kEventFlushComplete:
            {
                ASSERT(reachedState != kComponentStateInvalid);
                uint32_t i=0;
                bool done = true; // pipeline state change is done

                if ((mm_errors_t)param1 != MM_ERROR_SUCCESS) {
                    ERROR("%s fail to reach state %s\n", sender->name(), sInternalStateStr[reachedState]);
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
                            done = false;
                        }
                    }
                }
                if (done) {
                    INFO("pipeline reach state %s\n", sInternalStateStr[reachedState]);
                    setState(mState2, reachedState);

                    if (event == Component::kEventResetComplete) {
                        setState(mState, kComponentStateNull);
                    }


                    // inform upper layer state change is done
                    notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
                }
            }
            break;
        case Component::kEventDrainComplete:
            // FIXME: drain is signaled by sink only or by each component? up to now, this event doesn't value much
            // notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
            break;
        case Component::kEventEOS:
            {
                MMAutoLock locker(mLock);

                INFO("got EOS from %s, mEOSStreamCount %d, mConnectedStreamCount %d\n",
                    sender->name(), mEOSStreamCount, mConnectedStreamCount);
                mEOSStreamCount++;

                if (mEOSStreamCount == mConnectedStreamCount) {
                    notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
                    mEOSStreamCount = 0;
                    mEosReceived = true;
                }
            }
            break;
        case Component::kEventError:
        {
            setMemberUint32(mErrorCode, param1);
            notify(int(event), param1, 0, nilParam);
            break;
        }
        case Component::kEventGotVideoFormat:
        {
            if (!sender)
                break;

            paramSP->readInt32(); // x offset
            paramSP->readInt32(); // y offset
            {
                MMAutoLock locker(mLock);
                mVideoWidth = paramSP->readInt32();
                mVideoHeight = paramSP->readInt32();
            }
            mRotation = paramSP->readInt32();
            INFO("got video resolution %dx%d, rotation=%d\n", mVideoWidth, mVideoHeight, mRotation);

            MMParamSP param(new MMParam);
            param->writeInt32(mRotation);

            notify(int(event), mVideoWidth, mVideoHeight, param);
        }
            break;
        case Component::kEventInfoDuration:
        {
            MMAutoLock locker(mLock);
            mDuration = (int64_t)param1 / 1000;
            INFO("got duration: %" PRId64 " usec\n", mDuration);
        }
            notify(int(Component::kEventInfoDuration), mDuration, 0, nilParam);
            break;
        case Component::kEventInfo:
            switch (int(param1)) {
                case Component::kEventInfoVideoRenderStart:
                    INFO("receive and send kEventInfoVideoRenderStart\n");
                    notify(int(Component::kEventInfo), int(Component::kEventInfoVideoRenderStart), 0, nilParam);
                    break;
                case Component::kEventInfoMediaRenderStarted:
                    notify(int(Component::kEventInfo), int(Component::kEventInfoMediaRenderStarted), 0, nilParam);
                    break;
                default:
                    notify(event, param1, 0, nilParam);
                    break;
            }
            break;
        case Component::kEventMusicSpectrum:
            INFO("update music spectrum: %d\n", param1);
            notify(int(Component::kEventMusicSpectrum), param1, 0, nilParam);
            break;
        default:
            notify(event, param1, 0, nilParam);
            break;
    }

    MMAutoLock locker(mLock);
    mCondition.broadcast();
}

// bridge cowrecorder message, not used for now
void PipelineRecorderBase::onCowMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    PipelineParamRefBase* paramRef = DYNAMIC_CAST<PipelineParamRefBase*>(param3.get());
    if(!paramRef){
        ERROR("paramRef DYNAMIC_CAST fail\n");
        return;
    }
    Component::Event event = static_cast<Component::Event>(paramRef->mMsg);

    printMsgInfo(event, param1, "cowrecorder");
    notify(event, param1, 0, nilParam);
}

mm_status_t PipelineRecorderBase::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();

    MMAutoLock locker(mLock);
    if (mVideoWidth >0 && mVideoHeight >0) {
        width = mVideoWidth;
        height = mVideoHeight;
    }

    return MM_ERROR_SUCCESS;
}

// FIXME, how long has been recorded
mm_status_t PipelineRecorderBase::getCurrentPosition(int64_t& position) const
{
    FUNC_TRACK();

    if (mSinkClockIndex < 0 || (uint32_t)mSinkClockIndex > mComponents.size()) {
        WARNING("invalid mSinkClockIndex %d\n", mSinkClockIndex);
        return MM_ERROR_INVALID_PARAM;
    }

    SinkComponent *sink = DYNAMIC_CAST<SinkComponent*>(mComponents[mSinkClockIndex].component.get());
    if(!sink){
        ERROR("sink DYNAMIC_CAST fail\n");
        return MM_ERROR_NO_COMPONENT;
    }
    position = sink->getCurrentPosition() / 1000;
    DEBUG("getCurrentPosition %" PRId64 " ms", position);

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setParameter(const MediaMetaSP & meta)
{
    if (!meta)
        return MM_ERROR_INVALID_PARAM;

    //FIXME: for getParameters method
    mMediaMetaOutput->merge(meta);

    int32_t bitrate = -1;

    //for video
    SET_PARAMETER_INT32(MEDIA_ATTR_PREVIEW_WIDTH, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32(MEDIA_ATTR_PREVIEW_HEIGHT, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32(MEDIA_ATTR_WIDTH, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32(MEDIA_ATTR_HEIGHT, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32(MEDIA_ATTR_ROTATION, meta, mMediaMetaVideo);
    SET_PARAMETER_POINTER(MEDIA_ATTR_VIDEO_SURFACE, meta, mMediaMetaVideo);
    if (meta->getInt32(MEDIA_ATTR_BIT_RATE_VIDEO, bitrate) && bitrate > 0) {
        mMediaMetaVideo->setInt32(MEDIA_ATTR_BIT_RATE, bitrate);
    }
    SET_PARAMETER_FLOAT(MEDIA_ATTR_FRAME_RATE, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32(MEDIA_ATTR_TIME_LAPSE_ENABLE, meta, mMediaMetaVideo);
    SET_PARAMETER_INT32("raw-data", meta, mMediaMetaVideo);
    SET_PARAMETER_FLOAT(MEDIA_ATTR_TIME_LAPSE_FPS, meta, mMediaMetaVideo);
    SET_PARAMETER_STRING(MEDIA_ATTR_IDR_FRAME, meta, mMediaMetaVideo);

    SET_PARAMETER_STRING(MEDIA_ATTR_FILE_PATH, meta, mMediaMetaFile);
    SET_PARAMETER_STRING(MEDIA_ATTR_OUTPUT_FORMAT, meta, mMediaMetaFile);
    SET_PARAMETER_INT32(MEDIA_ATTR_ROTATION, meta, mMediaMetaFile);

    //for audio
    SET_PARAMETER_INT32(MEDIA_ATTR_SAMPLE_RATE, meta, mMediaMetaAudio);
    SET_PARAMETER_INT32(MEDIA_ATTR_CHANNEL_COUNT, meta, mMediaMetaAudio);
    SET_PARAMETER_INT32(MEDIA_ATTR_MUSIC_SPECTRUM, meta, mMediaMetaAudio);
    bitrate = -1;
    if (meta->getInt32(MEDIA_ATTR_BIT_RATE_AUDIO, bitrate) && bitrate > 0) {
        mMediaMetaAudio->setInt32(MEDIA_ATTR_BIT_RATE, bitrate);
    }

    // recording delay time
    int64_t delayTimeUs = 0;
    if (meta->getInt64(MEDIA_ATTR_START_DELAY_TIME, delayTimeUs) && delayTimeUs >= 0) {
        mDelayTimeUs = delayTimeUs;
        DEBUG("start delay time %" PRId64 "", delayTimeUs);
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::getParameter(MediaMetaSP & meta)
{
    meta = mMediaMetaOutput;
    return MM_ERROR_SUCCESS;
}

Component* PipelineRecorderBase::getSourceComponent(bool isAudio)
{
    dump();
    Component *component = NULL;
    int32_t index = isAudio ? mAudioSourceIndex : mVideoSourceIndex;
    if (index >= 0 && (uint32_t)index < mComponents.size()) {
        component = mComponents[index].component.get();
    }
    return component;
}

mm_status_t PipelineRecorderBase::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    mm_status_t status = MM_ERROR_SUCCESS;
    DEBUG("camera %p, recordingProxy %p", camera, recordingProxy);
    mMediaMetaVideo->setPointer(MEDIA_ATTR_CAMERA_OBJECT, camera);
    mMediaMetaVideo->setPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, recordingProxy);

    //It's no necessary to set source uri when camera instance is set already
    if (camera && recordingProxy) {
        mVideoSourceUri = "camera://";
    }
    return status;
}

mm_status_t PipelineRecorderBase::setVideoSourceFormat(int width, int height, uint32_t format)
{
    mVideoWidth = width;
    mVideoHeight = height;
    mVideoColorFormat = format;

    mMediaMetaVideo->setInt32(MEDIA_ATTR_WIDTH, mVideoWidth);
    mMediaMetaVideo->setInt32(MEDIA_ATTR_HEIGHT, mVideoHeight);

    DEBUG("width %d, height %d, format 0x%0x\n", width, height, format);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setVideoSource(const char* uri)
{
    if (!uri) {
        ERROR("invalid video source mime %s\n", uri);
        return MM_ERROR_INVALID_PARAM;
    }
    mVideoSourceUri = uri;
    DEBUG("uri %s\n", uri);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setAudioSource(const char* uri)
{
    if (!uri) {
        ERROR("invalid audio source mime %s\n", uri);
        return MM_ERROR_INVALID_PARAM;
    }
    DEBUG("uri %s\n", uri);
    mAudioSourceUri = uri;
#ifndef __DISABLE_AUDIO_STREAM__
    mAudioSource = getAudioSourceType(uri);
    mMediaMetaAudio->setInt32(MEDIA_ATTR_AUDIO_SOURCE, mAudioSource);//TODO: for wfd
#endif
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setAudioEncoder(const char* mime)
{
    FUNC_TRACK();

    if (!mime ||
        (strcmp(mime, MEDIA_MIMETYPE_AUDIO_AAC) &&
        strcmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_NB) &&
        strcmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB) &&
        strcmp(mime, MEDIA_MIMETYPE_AUDIO_MPEG) &&
        strcmp(mime, MEDIA_MIMETYPE_AUDIO_OPUS))) {
        ERROR("invalid audio encoder mime %s\n", mime);
        return MM_ERROR_INVALID_PARAM;
    }

    DEBUG("mAudioEncoder %s\n", mime);
    mAudioEncoderMime = mime;
    mMediaMetaAudio->setString(MEDIA_ATTR_MIME, mime);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setVideoEncoder(const char* mime)
{
    FUNC_TRACK();

    if (!mime || (strcmp(mime, MEDIA_MIMETYPE_VIDEO_AVC) && strcmp(mime, MEDIA_MIMETYPE_VIDEO_HEVC))) {
        ERROR("invalid video mime %s\n", mime);
        return MM_ERROR_INVALID_PARAM;
    }

    DEBUG("mVideoEncoder %s\n", mime);
    mVideoEncoderMime = mime;
    mMediaMetaVideo->setString(MEDIA_ATTR_MIME, mime);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setOutputFormat(const char* extension)
{
    FUNC_TRACK();
    if (!extension) {
        ERROR("invalid output format mime %s\n", extension);
        return MM_ERROR_INVALID_PARAM;
    }
    DEBUG("output format %s\n", extension);
    mOutputFormat = extension;

    mMediaMetaFile->setString(MEDIA_ATTR_OUTPUT_FORMAT, extension);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setOutputFile(const char* filePath)
{
    FUNC_TRACK();
    if (!filePath) {
        ERROR("invalid outputfile filePath %s\n", filePath);
        return MM_ERROR_INVALID_PARAM;
    }
    DEBUG("filePath %s\n", filePath);

    mMediaMetaFile->setString(MEDIA_ATTR_FILE_PATH, filePath);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setOutputFile(int fd)
{
    FUNC_TRACK();
    if (fd < 0) {
        ERROR("invalid file handle %d", fd);
        return MM_ERROR_INVALID_PARAM;
    }

    #define F_MAX_LEN 1024
    char temp[F_MAX_LEN] = {0};
    char url[F_MAX_LEN] = {0};
    snprintf(temp, sizeof (temp), "/proc/self/fd/%d", fd);
    if (readlink(temp, url, sizeof(url) - 1) < 0) {
        ERROR("get file path fail \n");
    } else {
        DEBUG("file handle %d, url %s", fd, url);
    }

    mMediaMetaFile->setInt32(MEDIA_ATTR_FILE_HANDLE, fd);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setMaxDuration(int64_t msec)
{
    FUNC_TRACK();
    if (msec < 0) {
        ERROR("invalid max duration %" PRId64 "\n", msec);
        return MM_ERROR_INVALID_PARAM;
    }
    DEBUG("max duration %" PRId64 "\n", msec);

    mMediaMetaFile->setInt64(MEDIA_ATTR_MAX_DURATION, msec);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setMaxFileSize(int64_t bytes)
{
    FUNC_TRACK();
    if (!bytes) {
        ERROR("invalid max file size %" PRId64 "\n", bytes);
        return MM_ERROR_INVALID_PARAM;
    }
    DEBUG("max file size %" PRId64 "\n", bytes);

    mMediaMetaFile->setInt64(MEDIA_ATTR_MAX_FILE_SIZE, bytes);
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setRecorderUsage(RecorderUsage usage)
{
    mUsage = usage;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineRecorderBase::setAudioConnectionId(const char * connectionId)
{
    mAudioConnectionId = connectionId;
    return MM_ERROR_SUCCESS;
}

const char * PipelineRecorderBase::getAudioConnectionId() const
{
    return mAudioConnectionId.c_str();
}


} // YUNOS_MM
