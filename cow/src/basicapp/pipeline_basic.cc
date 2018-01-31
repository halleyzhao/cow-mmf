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
#include "multimedia/pipeline_basic.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/elapsedtimer.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("PLBasic")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

BEGIN_MSG_LOOP(PipelineBasic)
    MSG_ITEM2(PL_MSG_componentMessage, onComponentMessage)
END_MSG_LOOP()

void PipelineBasic::resetMemberVariables()
{
    mDurationMs = 0;
    mWidth = 0;
    mHeight = 0;
    mEOSStreamCount = 0;
}

PipelineBasic::PipelineBasic()
    : mState(kComponentStateNull)
    , mConnectedStreamCount(0)
{
    FUNC_TRACK();
    int i=0;

    resetMemberVariables();
    mMediaMeta = MediaMeta::create();
    if (!mMediaMeta) {
        ERROR("create MediaMeta failed, no mem\n");
    }
}

PipelineBasic::~PipelineBasic()
{
    FUNC_TRACK();
    mMediaMeta.reset(); // for debug
}

mm_status_t PipelineBasic::prepare()
{
    FUNC_TRACK();

    {
        MMAutoLock locker(mLock);
        if (mState >= kComponentStatePreparing) {
            WARNING("pipelien is aready in preparing state: %d", mState);
            notify(Component::kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
            return MM_ERROR_SUCCESS;
        }
    }
    setState(mState, kComponentStateLoaded);

    mm_status_t status = MM_ERROR_SUCCESS;
    mConnectedStreamCount = 0;
    status = prepareInternal();
    if (status != MM_ERROR_SUCCESS) {
        ERROR("prepareInternal() status: %d", status);
        notify(Component::kEventPrepareResult, status, 0, nilParam);
    }

    SET_PIPELINE_STATE(status, prepare, kComponentStatePreparing, kComponentStatePrepared, Component::kEventPrepareResult);
    return status;
}

mm_status_t PipelineBasic::start()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventStartResult);

    SET_PIPELINE_STATE(status, start, kComponentStatePlay, kComponentStatePlaying, Component::kEventStartResult);

    return status;
}

mm_status_t PipelineBasic::stop()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    CHECK_PIPELINE_STATE(kComponentStateStopped, Component::kEventStopped);
    CHECK_PIPELINE_STATE(kComponentStateNull, Component::kEventStopped);
    SET_PIPELINE_STATE(status, stop, kComponentStateStop, kComponentStateStopped, Component::kEventStopped);
    resetMemberVariables();

    return status;
}

mm_status_t PipelineBasic::pause()
{
    FUNC_TRACK();

    CHECK_PIPELINE_STATE(kComponentStatePaused, Component::kEventPaused);

    mm_status_t status = MM_ERROR_SUCCESS;
    SET_PIPELINE_STATE(status, pause, kComponentStatePausing, kComponentStatePaused, Component::kEventPaused);

    return status;
}

mm_status_t PipelineBasic::resume()
{
    FUNC_TRACK();
    CHECK_PIPELINE_STATE(kComponentStatePlaying, Component::kEventResumed);

    mm_status_t status = MM_ERROR_SUCCESS;
    SET_PIPELINE_STATE(status, resume, kComponentStatePlay, kComponentStatePlaying, Component::kEventResumed);

    return status;
}

mm_status_t PipelineBasic::unblock()
{
    {
        MMAutoLock locker(mLock);
        mEscapeWait = true;
    }
    mCondition.broadcast();
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineBasic::reset()
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

    // XXX, FIXME, what's difference comparing to start/stop?
    setState(mState, kComponentStateInvalid);

    for (i=0; i<mComponents.size(); i++) {
        setState(mComponents[i].state, kComponentStateInvalid);
        mm_status_t ret = mComponents[i].component->reset();
        if (ret == MM_ERROR_SUCCESS) {
            INFO("%s reset done", mComponents[i].component->name());
            setState(mComponents[i].state, kComponentStateNull);
        } else if (ret == MM_ERROR_ASYNC) {
            status = MM_ERROR_ASYNC;
        } else {
            ERROR("%s reset failed", mComponents[i].component->name());
            // FIXME fix component reset failure
            setState(mComponents[i].state, kComponentStateNull);
            status = ret;
        }
    }

    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mState, kComponentStateNull, true/*pipeline state*/, false/*state*/);
    } else if (status == MM_ERROR_SUCCESS) {
        setState(mState, kComponentStateNull);
        notify(Component::kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    } else {
        notify(Component::kEventResetComplete, status, 0, nilParam);
    }

    // FIXME: destroy all components, restart playback is required to begin from load()
    // always destroy the components here.
    mComponents.clear();
    resetMemberVariables();

    return status;
}

mm_status_t PipelineBasic::getState(ComponentStateType& state)
{
    FUNC_TRACK();
    state = mState;

    return MM_ERROR_SUCCESS;
}

// handle components message in async mode, runs in PipelineBasic's MMMsgThread
void PipelineBasic::onComponentMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
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
            reachedState = kComponentStateNull;
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
        case Component::kEventResetComplete:
            {
                ASSERT(reachedState != kComponentStateInvalid);
                uint32_t i=0;
                bool done = true; // pipelineplayerbase state change is done

                setMemberUint32(mErrorCode, param1);

                //don't break when stopped error
                if (event != Component::kEventStopped &&
                    (mm_errors_t)param1 != MM_ERROR_SUCCESS) {
                    ERROR("%s fail to reach state %s\n", sender->name(), sInternalStateStr[reachedState]);
                    notify(int(event), param1, 0, nilParam);

                    unblock();
                } else {
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
                    if (done ) {  // during pipelineplayerbase construction, we should NOT send kEventPrepareResult when there is demux component only
                        INFO("PipelineBasic reach state %s\n", sInternalStateStr[reachedState]);
                        setState(mState, reachedState);
                        // inform upper layer state change is done
                        if (event == Component::kEventResumed) {
                            DEBUG("kEventResumed --> kEventStartResult");
                            event = Component::kEventStartResult;
                        }
                    }
                }
            }
            break;
        case Component::kEventEOS:
        {
            MMAutoLock locker(mLock);
            mEOSStreamCount++;
            INFO("mEOSStreamCount: %d, mConnectedStreamCount: %d", mEOSStreamCount, mConnectedStreamCount);
            if (mEOSStreamCount == mConnectedStreamCount) {
                notify(int(event), MM_ERROR_SUCCESS, 0, nilParam);
                resetMemberVariables();
            }
        }
            break;
        case Component::kEventError:
            notify(int(event), param1, 0, nilParam);
            break;
        case Component::kEventGotVideoFormat:
        {
            if (!sender)
                break;
            {
                MMAutoLock locker(mLock);
                mWidth = (int32_t)param1;
                mHeight = reinterpret_cast<int32_t>(param2);
            }
            notify(int(event), mWidth, mHeight, nilParam);
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

mm_status_t PipelineBasic::getDuration(int64_t& durationMs)
{
    //FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    if (mState < kComponentStatePrepared) {
        durationMs = -1;
        // FIXME, CowPlayerWrapper assert the ret value
        // return MM_ERROR_NOT_INITED;
        return MM_ERROR_SUCCESS;
    }

    durationMs = mDurationMs;
    INFO("duration: %ld ms\n", durationMs);
    return status;
}

mm_status_t PipelineBasic::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    width = -1;
    height = -1;
    if (mState < kComponentStatePrepared)
        return MM_ERROR_SUCCESS;

    MMAutoLock locker(mLock);
    width = mWidth;
    height = mHeight;
    status = MM_ERROR_SUCCESS;

    return status;
}

mm_status_t PipelineBasic::getCurrentPosition(int64_t& positionMs) const
{
    //FUNC_TRACK();
    positionMs = -1;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineBasic::setParameter(const MediaMetaSP & meta)
{
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineBasic::getParameter(MediaMetaSP & meta)
{
    meta = mMediaMeta;
    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM

