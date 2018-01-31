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


#ifndef clock_wrapper_h
#define clock_wrapper_h

#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {

class ClockWrapper;
typedef MMSharedPtr<ClockWrapper> ClockWrapperSP;

class ClockWrapper {
public:
    ClockWrapper(int32_t flag = 0);
    virtual ~ClockWrapper();

    //NOTE:
    //1. If audio sink component provides clock and set it to video sink component.
    //These methods are meaningless to Video sink component
    //2. If only have video sink component or audio sink component does not provide clock
    //those mothed will work
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t flush();

    const ClockSP provideClock();
    mm_status_t setClock(ClockSP pClock);
    mm_status_t setPlayRate(int32_t playRate);

    void setAnchorTime(int64_t mediaUs, int64_t realUs, int64_t anchorMaxTime = -1);
    void updateMaxAnchorTime(int64_t anchorMaxTime);
    int64_t getMediaLateUs(int64_t mediaTimeUs);
    mm_status_t getCurrentPosition(int64_t &mediaTimeUs);

    enum ClockFlag {
        kFlagVideoSink = 1 << 0,
    };

private:
    //1. Only video exists, video owns clock
    //2. Only audio exists, audio owns clcok
    //3. Auio and video all exist, audio owns clock
    bool mIsClockOwner; //false means clock provided by other component
    ClockSP mClock;
    int32_t mFlag;
    float mScaledPlayRate;
    bool mIsAudioEos;

};

} // end of namespace YUNOS_MM
#endif//clock_wrapper_h

