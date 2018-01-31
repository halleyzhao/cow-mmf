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
#include <multimedia/mm_errors.h>
#include <multimedia/mm_debug.h>
#include <multimedia/clock.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("CLOCK")

#define ENTER() INFO(">>>\n")
#define EXIT() do {INFO(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

////////////////////////////////////////////////////////////////////////
//Clock define
Clock::Clock():   mAnchorTimeMediaUs(-1ll),
                    mAnchorTimeRealUs(-1ll),
                    mAnchorTimeMaxUs(-1ll),
                    mPauseStartedTimeRealUs(-1ll),
                    mPaused(false),
                    mPausePositionMediaTimeUs(-1ll),
                    mScaledPlayRate(SCALED_PLAY_RATE)
{
    ENTER();
    EXIT();

}

Clock::~Clock() {
    ENTER();
    EXIT();
}


mm_status_t Clock::getCurrentPosition(int64_t &mediaUs) {
    MMAutoLock locker(mLock);
    return getCurrentPosition_l(mediaUs);
}

mm_status_t Clock::getCurrentPosition_l(int64_t &mediaUs) {

    //in paused state
    if (mPaused && mPausePositionMediaTimeUs >= 0ll) {
        mediaUs = mPausePositionMediaTimeUs;
        return MM_ERROR_SUCCESS;
    }

    //otherwise, in playing state
    return getCurrentPositionFromAnchor_l(mediaUs);
}

mm_status_t Clock::getCurrentPositionFromAnchor_l(int64_t &mediaUs) {
    int64_t nowUs = Clock::getNowUs();

    if (mAnchorTimeMediaUs < 0) {
        ERROR("invalid position, first frame not set");
        mediaUs = 0;
        return MM_ERROR_INVALID_PARAM;
    }

    int64_t positionUs = (nowUs - mAnchorTimeRealUs) + mAnchorTimeMediaUs;
    VERBOSE("nowUs %0.3f, mAnchorTimeRealUs %0.3f, mAnchorTimeMediaUs %0.3f, positionUs %0.3f, mAnchorTimeMaxUs %0.3f\n",
        nowUs/1000000.0f, mAnchorTimeRealUs/1000000.0f, mAnchorTimeMediaUs/1000000.0f, positionUs/1000000.0f, mAnchorTimeMaxUs/1000000.0f);

    //mAnchorTimeMaxUs is invalid(set to -1) for video stream
    //and it is always for audio stream, which is set by setAnchorTime
    //Using compensation for audio stream when encouting long audio frame
    //NOte: Make sure we are NOT in paused state when we set positionUs to mAnchorTimeMaxUs
    if (mAnchorTimeMaxUs >= 0 && !mPaused &&
        (positionUs > mAnchorTimeMaxUs)) {
        positionUs = mAnchorTimeMaxUs;
    }

    mediaUs = (positionUs <= 0) ? 0 : positionUs;
    return MM_ERROR_SUCCESS;
}

mm_status_t Clock::setPlayRate(int32_t playRate) {
    MMAutoLock locker(mLock);
    if (mScaledPlayRate == playRate) {
        DEBUG("play rate is already %d, just return\n", playRate);
        return MM_ERROR_SUCCESS;
    }

    DEBUG("play-rate %d\n", playRate);
    mScaledPlayRate = playRate;

    //reset anchorTime
    flush_l();
    return MM_ERROR_SUCCESS;
}

int64_t Clock::getAnchorTimeMediaUs() {
    //MMAutoLock locker(mLock);
    return mAnchorTimeMediaUs;
}

void Clock::setAnchorTime(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime) {
    MMAutoLock locker(mLock);
    if (mPaused) {
        INFO("in puased state, just return");
        return;
    }

    setAnchorTime_l(mediaUs, realUs, anchorMaxTime);
}

void Clock::updateMaxAnchorTime(int64_t anchorMaxTime) {
    MMAutoLock locker(mLock);
    mAnchorTimeMaxUs = anchorMaxTime;
    VERBOSE("max anchor time is %0.3f", anchorMaxTime/1000000.0f);
}

void Clock::setAnchorTime_l(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime) {
    mAnchorTimeMediaUs = mediaUs;
    mAnchorTimeRealUs = realUs;
    mAnchorTimeMaxUs = anchorMaxTime;

    VERBOSE("mAnchorTimeMediaUs %0.3f, mAnchorTimeRealUs %0.3f, mAnchorTimeMaxUs %0.3f\n",
        mAnchorTimeMediaUs/1000000.0f, mAnchorTimeRealUs/1000000.0f, mAnchorTimeMaxUs/1000000.0f);
}

int64_t Clock::getMediaLateUs(int64_t mediaTimeUs) {
    MMAutoLock locker(mLock);
    int64_t mediaLateUs = -1;

    if (mPaused) {
        INFO("ClockWrapper::getMediaLateUs() is on pause");
        return 0;
    }

    if (mAnchorTimeMediaUs < 0) {
        ERROR("invalid position, first frame not set");
        return 0;
    }

    int64_t nowUs = Clock::getNowUs();
    int64_t mediaDiffUs = (mediaTimeUs- mAnchorTimeMediaUs) * SCALED_PLAY_RATE / mScaledPlayRate;
    int64_t anchorDiffUs = (nowUs - mAnchorTimeRealUs);
    mediaLateUs = anchorDiffUs - mediaDiffUs;

    //int64_t positionUs = (nowUs - mAnchorTimeRealUs)*num/10 + mAnchorTimeMediaUs;
    VERBOSE("nowUs %0.3f, mAnchorTimeRealUs %0.3f, anchorDiffUs %0.3f, mAnchorTimeMediaUs %0.3f, mediaTimeUs %0.3f, mediaDiffUs %0.3f\n",
        nowUs/1000000.0f, mAnchorTimeRealUs/1000000.0f, anchorDiffUs/1000000.0f, mAnchorTimeMediaUs/1000000.0f, mediaTimeUs/1000000.0f, mediaDiffUs/1000000.0f);

    return mediaLateUs;
}

mm_status_t Clock::pause() {
    MMAutoLock locker(mLock);
    ENTER();

    if (mPaused) {
        INFO("already paused, just return");
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
    }

    int64_t currentPositionUs;
    int64_t pausePositionMediaTimeUs;
    if (getCurrentPositionFromAnchor_l(currentPositionUs) == MM_ERROR_SUCCESS) {
        pausePositionMediaTimeUs = currentPositionUs;
    } else {
        // Set paused position to -1 (unavailabe) if we don't have anchor time
        // This could happen if client does a seekTo() immediately followed by
        // pause(). Renderer will be flushed with anchor time cleared. We don't
        // want to leave stale value in mPausePositionMediaTimeUs.
        pausePositionMediaTimeUs = -1ll;
    }

    mPausePositionMediaTimeUs = pausePositionMediaTimeUs;
    mPauseStartedTimeRealUs = Clock::getNowUs();
    mPaused = true;
    INFO("mPausePositionMediaTimeUs %0.3f, mPauseStartedTimeRealUs %0.3f\n",
        mPausePositionMediaTimeUs/1000000.0f, mPauseStartedTimeRealUs/1000000.0f);

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t Clock::resume() {
    MMAutoLock locker(mLock);
    ENTER();
    if (!mPaused) {
        INFO("already started, just return");
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);;
    }

    mPaused = false;
    if (mPauseStartedTimeRealUs != -1ll) {
        int64_t newAnchorRealUs =
            mAnchorTimeRealUs + Clock::getNowUs() - mPauseStartedTimeRealUs;

        INFO("mAnchorTimeMediaUs %0.3f, mAnchorTimeRealUs %0.3f, newAnchorRealUs %0.3f, paused time %0.3f\n",
            mAnchorTimeMediaUs/1000000.0f, mAnchorTimeRealUs/1000000.0f, newAnchorRealUs/1000000.0f,
            (newAnchorRealUs-mAnchorTimeRealUs)/1000000.0f);
        //FIXME: whether need to set mAnchorTimeMaxUs??
        setAnchorTime_l(mAnchorTimeMediaUs, newAnchorRealUs);
        mPauseStartedTimeRealUs = -1ll;
        mPausePositionMediaTimeUs = -1ll;
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t Clock::flush() {
    MMAutoLock locker(mLock);
    ENTER();
    return flush_l();
}

mm_status_t Clock::flush_l() {
    mPauseStartedTimeRealUs = -1ll;
    mPausePositionMediaTimeUs = -1ll;
    setAnchorTime_l(-1ll, -1ll);

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

/*static*/int64_t Clock::getNowUs()  {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000LL + t.tv_nsec / 1000LL;
}
}
