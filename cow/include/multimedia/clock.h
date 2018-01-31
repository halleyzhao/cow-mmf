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

#ifndef clock_h
#define clock_h

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

#include <sys/time.h>
namespace YUNOS_MM {

class Clock;
typedef MMSharedPtr<Clock> ClockSP;

class Clock {
public:
    Clock();

    virtual ~Clock();

    static int64_t getNowUs();


    enum ClockType {
        kAudioClock,//default
        kVideoClock,
        kExternalClock,
    };

    friend class ClockWrapper;

private:
    //protected by mLock
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t flush();

    int64_t getAnchorTimeMediaUs();
    int64_t getMediaLateUs(int64_t mediaTimeUs);
    void setAnchorTime(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime = -1);
    mm_status_t getCurrentPosition(int64_t &mediaUs);
    mm_status_t setPlayRate(int32_t playRate);
    void updateMaxAnchorTime(int64_t anchorMaxTime);

    //lock by caller
    mm_status_t getCurrentPosition_l(int64_t &mediaUs);
    mm_status_t getCurrentPositionFromAnchor_l(int64_t &mediaUs);
    void setAnchorTime_l(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime = -1);
    mm_status_t flush_l();

private:
    int64_t mAnchorTimeMediaUs;
    int64_t mAnchorTimeRealUs;
    int64_t mAnchorTimeMaxUs;

    int64_t mPauseStartedTimeRealUs;

    bool mPaused;
    int64_t mPausePositionMediaTimeUs;

    Lock mLock;
    int32_t mScaledPlayRate;

    MM_DISALLOW_COPY(Clock);
};

}

#endif // clock_h

