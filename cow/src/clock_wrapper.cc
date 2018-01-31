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
#include <multimedia/mm_debug.h>
#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>

#include <multimedia/clock.h>
#include "clock_wrapper.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("CLOCK-WRAPPER")

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define RETURN_WITH_CODE(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

////////////////////////////////////////////////////////////////////////
//ClockWrapper define
ClockWrapper::ClockWrapper(int32_t flag): mIsClockOwner(true)
                                          , mScaledPlayRate(SCALED_PLAY_RATE)
                                          , mIsAudioEos(false)
{
    mClock.reset(new Clock);
    mFlag = flag;
}

ClockWrapper::~ClockWrapper() {
    ENTER();
    EXIT();
}

const ClockSP ClockWrapper::provideClock() {
    ENTER();
    if (mFlag & kFlagVideoSink) {
        INFO("video sink provide clock??\n");
    }
    return mClock;
}

mm_status_t ClockWrapper::setClock(ClockSP clock) {
    ENTER();

    if (!clock) {
        return MM_ERROR_INVALID_PARAM;
    }

    if (!(mFlag & kFlagVideoSink)) {
        INFO("set clock to audio sink??\n");
    }

    mClock = clock;
    mIsClockOwner = false;
    RETURN_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t ClockWrapper::setPlayRate(int32_t playRate) {
    ENTER();

    if (!(mFlag & kFlagVideoSink)) {
        ERROR("audio sink SHOULD NOT set playRate\n");
        RETURN_WITH_CODE(MM_ERROR_UNSUPPORTED);
    }

    if (mScaledPlayRate == playRate) {
        DEBUG("play rate is already %d, just return\n", playRate);
        return MM_ERROR_SUCCESS;
    }

    mScaledPlayRate = playRate;
    if (mClock) {
         mClock->setPlayRate(playRate);
    }

    RETURN_WITH_CODE(MM_ERROR_SUCCESS);
}

// for video sink if no audio sink
void ClockWrapper::updateMaxAnchorTime(int64_t anchorMaxTime) {
    if (((mFlag & kFlagVideoSink) && mIsClockOwner) ||
            mIsAudioEos) {
        mClock->updateMaxAnchorTime(anchorMaxTime);
    }
}

void ClockWrapper::setAnchorTime(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime) {
    if (mFlag & kFlagVideoSink) {
        if (mClock && mClock->getAnchorTimeMediaUs() < 0) {
           if (!mIsClockOwner) {
               INFO("Video setAnchor time\n");
           }
           mClock->setAnchorTime(mediaUs, realUs, anchorMaxTime);
        }
    } else {

        if (mScaledPlayRate != SCALED_PLAY_RATE) {
            VERBOSE("audio sink should not setAnchorTime at variable rate, just return\n");
            return;
        }

        if (mClock) {
            mClock->setAnchorTime(mediaUs, realUs, anchorMaxTime);
        }
    }
 }

//Only for video sink component
int64_t ClockWrapper::getMediaLateUs(int64_t mediaTimeUs) {
    if ((mFlag & kFlagVideoSink) && mClock) {
        return mClock->getMediaLateUs(mediaTimeUs);
    }


    return -1ll;
}

//For video and audio sink component both
mm_status_t ClockWrapper::getCurrentPosition(int64_t &mediaTimeUs) {
    mediaTimeUs = -1;

    if (mClock) {
        return mClock->getCurrentPosition(mediaTimeUs);
    }

    RETURN_WITH_CODE(MM_ERROR_UNSUPPORTED);
}

mm_status_t ClockWrapper::pause() {
    ENTER();

    //using macro for the same code in future.
    if ((mScaledPlayRate != SCALED_PLAY_RATE) && (mFlag & kFlagVideoSink)) {
        return mClock->pause();
    }

    if (mClock && mIsClockOwner) {
        return mClock->pause();
    }


    RETURN_WITH_CODE(MM_ERROR_UNSUPPORTED);
}

mm_status_t ClockWrapper::resume() {
    ENTER();

    if ((mScaledPlayRate != SCALED_PLAY_RATE) && (mFlag & kFlagVideoSink)) {
        return mClock->resume();
    }

    if (mClock && mIsClockOwner) {
        return mClock->resume();

    }
    RETURN_WITH_CODE(MM_ERROR_UNSUPPORTED);
}


mm_status_t ClockWrapper::flush() {
    ENTER();
    if ((mScaledPlayRate != SCALED_PLAY_RATE) && (mFlag & kFlagVideoSink)) {
        return mClock->flush();
    }

    if (mClock && mIsClockOwner) {
        return mClock->flush();
    }

    RETURN_WITH_CODE(MM_ERROR_UNSUPPORTED);

}
} // YUNOS_MM
