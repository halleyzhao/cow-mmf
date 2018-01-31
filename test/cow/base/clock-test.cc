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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <gtest/gtest.h>

#include <multimedia/mm_debug.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

#include <multimedia/clock.h>

#include <clock_wrapper.h>



MM_LOG_DEFINE_MODULE_NAME("clock-test");

using namespace YUNOS_MM;

///////////////////////////////////////////////////////////////////////////////
class SinkClockTest {
public:
    SinkClockTest(bool isAudio, ClockSP clockSP);

    int TestCase01();

    ClockSP provideClock();

    int checkLateUs() ;


    int TestCaseInner01();

    static void * sinkThread(void *param);

private:
    ClockWrapperSP mClockWrapper;
    int64_t mStartPts;
    int64_t mCurrentPts;
    int32_t mFrameCount;
    int64_t mDeltaUs;
    int64_t mCurrentPositionUs;
    int64_t mLateUs;
    bool mIsAudio;
    pthread_t mSinkThread;

    MM_DISALLOW_COPY(SinkClockTest);
};

////////////////////////////////////////////////////////////////////////////////////
//implementation
SinkClockTest::SinkClockTest(bool isAudio, ClockSP clockSP) :mStartPts(0),
                                            mCurrentPts(0),
                                            mFrameCount(0),
                                            mDeltaUs(10*1000ll),
                                            mCurrentPositionUs(-1ll),
                                            mLateUs(-1ll),
                                            mIsAudio(isAudio)
{
    mClockWrapper.reset(new ClockWrapper(!isAudio? ClockWrapper::kFlagVideoSink:0));
    INFO("%s sink clock test\n", isAudio ? "Audio" : "Video");

    if (mClockWrapper && clockSP) {
        mClockWrapper->setClock(clockSP);
    }

    memset(&mSinkThread, 0, sizeof(mSinkThread));
}

int  SinkClockTest::TestCase01() {
    int ret = pthread_create(&mSinkThread, NULL, (void *(*)(void *))sinkThread, this);

    return ret;
}

ClockSP SinkClockTest::provideClock() {
    //only audio sink component can provide clock
    ASSERT(mIsAudio);
    return mClockWrapper->provideClock();

}

int SinkClockTest::checkLateUs() {
    int64_t start = Clock::getNowUs();
    while(1) {
        mClockWrapper->setAnchorTime(mCurrentPts, Clock::getNowUs());
        mLateUs = mClockWrapper->getMediaLateUs(mCurrentPts);

        if (mClockWrapper && mClockWrapper->getCurrentPosition(mCurrentPositionUs) != MM_ERROR_SUCCESS) {
            ERROR("[%d]: getCurrentPosition failed\n", mIsAudio);
            return -1;
        }
        INFO("[%d]: frame#%d: mCurrentPts %" PRId64 ", mLateUs %" PRId64 ",  mCurrentPositionUs %" PRId64 "\n",
            mIsAudio, mFrameCount, mCurrentPts/1000ll, mLateUs/1000ll, mCurrentPositionUs/1000ll);



        if (++mFrameCount > 100)
            break;


        usleep(mDeltaUs);

        mCurrentPts =  Clock::getNowUs()- start + mStartPts;
    }

    mStartPts = mCurrentPts;

    return 1;
}


int SinkClockTest::TestCaseInner01() {
    checkLateUs();
    mFrameCount--;
    if (mClockWrapper) {
        INFO("[%d]: pause\n", mIsAudio);
        mClockWrapper->pause();
    }

    INFO("[%d]: go to sleep 10s\n", mIsAudio);
    usleep(1000ll*1000ll*10);

    int32_t count0 = 0;
    while (++count0 < 10) {
        if (mClockWrapper && mClockWrapper->getCurrentPosition(mCurrentPositionUs) != MM_ERROR_SUCCESS) {
            ERROR("getCurrentPosition failed\n", mIsAudio);
            mCurrentPositionUs = -1;
            return -1;
        }

        mLateUs = mClockWrapper->getMediaLateUs(mCurrentPts);

        INFO("[%d]: in pause state, frame#%d: mCurrentPts %" PRId64 ", mLateUs %" PRId64 ", mCurrentPositionUs %" PRId64 "\n",
            mIsAudio, mFrameCount, mCurrentPts/1000ll, mLateUs/1000ll, mCurrentPositionUs/1000ll);

    }


    if (mClockWrapper) {
        INFO("[%d]: resume\n", mIsAudio);
        mClockWrapper->resume();
    }

    mFrameCount = 0;
    checkLateUs();

    if (mClockWrapper) {
        mClockWrapper->flush();
    }

    return 1;

}

void * SinkClockTest::sinkThread(void *param) {
    SinkClockTest * me = static_cast<SinkClockTest*>(param);
    me->TestCaseInner01();

    INFO("[%d]: thread exit\n", me->mIsAudio);
    return NULL;
}


////////////////////////////////////////////////////////////////////////////////
//Test

class ClockTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(ClockTest, clockTest) {
    int ret;
    #if 1
    {
        //test only audio sink
        ClockSP clockSP;
        clockSP.reset();
        SinkClockTest testSinkClock(false, clockSP);
        ret = testSinkClock.TestCase01();
        EXPECT_TRUE(ret == 0);
        //wait thread exits
        usleep(20ll*1000ll*1000ll);
    }

    {
        //test only video sink
        ClockSP clockSP;
        clockSP.reset();
        SinkClockTest testSinkClock(true, clockSP);
        ret = testSinkClock.TestCase01();
        EXPECT_TRUE(ret == 0);
        //wait thread exits
        usleep(20ll*1000ll*1000ll);
    }
    #endif

    #if 1
    {
        ClockSP clockSP;
        clockSP.reset();
        SinkClockTest audioSink(true, clockSP);
        clockSP = audioSink.provideClock();
        EXPECT_NE(clockSP.get(), NULL);

        SinkClockTest videoSink(false, clockSP);

        ret = audioSink.TestCase01();
        EXPECT_TRUE(ret == 0);

        ret = videoSink.TestCase01();
        EXPECT_TRUE(ret == 0);
        //wait 30s
        usleep(30ll*1000ll*1000ll);

    }
    #endif
    PRINTF("done\n");
}

