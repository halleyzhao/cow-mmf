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
#include "multimedia/media_monitor.h"
#include "multimedia/mm_debug.h"
#include <stdint.h>
#include <string.h>

MM_LOG_DEFINE_MODULE_NAME("Cow-MediaMonitor");

// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

namespace YUNOS_MM {

//// Monitor
bool Monitor::produceOne()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mProducedCount++;
    return onProducedOne_l();
}

bool Monitor::consumeOne()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mConsumedCount++;
    return onConsumedOne_l();
}

uint32_t Monitor::aliveCount()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    return pendingCount_l();
}

//// TrafficControl
/* explicit */ TrafficControl::TrafficControl(uint32_t lowBar, uint32_t highBar, const char* logTag)
    : Monitor(logTag)
    , mLowBar(lowBar)
    , mHighBar(highBar)
    , mCondition(mLock)
    , mEscapeWait(false)
{
    FUNC_TRACK();
}

bool TrafficControl::isFull()
{
    MMAutoLock locker(mLock);
    if (pendingCount_l() >= mHighBar)
        return true;

    return false;
}

bool TrafficControl::waitOnFull()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    if (pendingCount_l() >= mHighBar) {
        mCondition.signal();
        INFO("%s too many buffers produced but not consumed, let's wait ...\n", mLogTag.c_str());
        while (!mEscapeWait) {
            mCondition.wait();
            if ((pendingCount_l()<= mLowBar) || mEscapeWait)
                break;
        }
    }

    return mEscapeWait;
}

bool TrafficControl::waitOnEmpty()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    if (pendingCount_l() <= mLowBar) {
        INFO("%s few buffers to consume, let's wakeup procuder ...\n", mLogTag.c_str());
        mCondition.signal();
        if (!pendingCount_l()) {
            while (!mEscapeWait) {
                INFO("%s no buffers to consume, let's wait ...\n", mLogTag.c_str());
                mCondition.wait();
                // FIXME, it's better break earlier
                if ((pendingCount_l() >= mHighBar) || mEscapeWait) {
                    INFO("got enough data, continue ...");
                    break;
                }
            }
        }
    }

    return mEscapeWait;
}

void TrafficControl::unblockWait(bool unblock)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mEscapeWait = unblock;
    mCondition.broadcast();
};

/* virtual */ bool TrafficControl::onProducedOne_l()
{
    FUNC_TRACK();
    VERBOSE("%s pending buffer count: %d\n", mLogTag.c_str(), pendingCount_l());
    if (pendingCount_l() >= mHighBar) {
        INFO("%s produced enough data, wakeup consumer\n", mLogTag.c_str());
        mCondition.signal();
    }

    return true;
}

/* virtual */ bool TrafficControl::onConsumedOne_l()
{
    FUNC_TRACK();
    VERBOSE("%s pending buffer count: %d\n", mLogTag.c_str(), pendingCount_l());
    if (pendingCount_l() <= mLowBar) {
        INFO("%s consumed enough data, wakeup producer\n", mLogTag.c_str());
        mCondition.signal();
    }
    return true;
}

//// Statics
/* explicit */ TimeStatics::TimeStatics(uint32_t window, uint32_t alertProduceTime, uint32_t alertConsumeTime /* in ms */, const char* logTag)
    : Monitor(logTag)
    , mWindowSize(window)
    , mAlertProduceTime(alertProduceTime)
    , mAlertConsumeTime(alertConsumeTime)
    , mStartTime(0)
    , mPreviousProduceTime(0)
    , mPreviousConsumeTime(0)
    , mCount(0)
{
    FUNC_TRACK();
}

/* virtual */ bool TimeStatics::onProducedOne_l()
{
    FUNC_TRACK();
    if (!mCount) {
        mStartTime = getTimeUs();
        mPreviousProduceTime = mStartTime;
    } else if (mAlertProduceTime) {
        uint64_t current = getTimeUs();
        uint64_t elapsed = current - mPreviousProduceTime;
        mPreviousProduceTime = current;
        if (elapsed >= mAlertProduceTime) {
            WARNING("%s, current buffer is produced %ld ms\n", mLogTag.c_str(), elapsed/1000);
        }
    }

    mCount++;

    return true;
}
/* virtual */ bool TimeStatics::onConsumedOne_l()
{
    FUNC_TRACK();
    if (mCount >= mWindowSize) {
        uint64_t totalTime = getTimeUs() - mStartTime;
        // uint64_t avgTime = totalTime/1000/mWindowSize;
        float fps = 1000000.0f * mCount / (float)totalTime;
        INFO("%s, average fps of recent %d buffer is: %.2f\n", mLogTag.c_str(), mCount, fps);
        mCount = 0;
    } else if (mAlertConsumeTime) {
        uint64_t current = getTimeUs();
        if (mCount > 2) {
            uint64_t elapsed = current - mPreviousConsumeTime;
            if (elapsed >= mAlertConsumeTime) {
                WARNING("%s, current buffer is consumed %ld ms\n", mLogTag.c_str(), elapsed/1000);
            }
        }
        mPreviousConsumeTime = current;
    }

    return true;
}

PerformanceStatics::PerformanceStatics(const char* name, uint32_t extraThreshHold, uint32_t window)
{
    mName = name;
    mExtraThreshHold = extraThreshHold;
    mWindowSize = window;
    reset();
}

void PerformanceStatics::reset()
{
    mSum = 0;
    mTotalSum = 0;
    mAvg = 0;
    mSampleCount = 0;
    while(!mQueue.empty())
        mQueue.pop();
}

void PerformanceStatics::updateSample(uint32_t sample)
{
    mQueue.push(sample);
    mSum += sample;
    mTotalSum += sample;
    mSampleCount++;

    VERBOSE("%s current sample: %d, %d samples total cost: %" PRId64, mName.c_str(), sample, mSampleCount,  mTotalSum);
    if (mQueue.size() < mWindowSize) {
        return;
    }

    if (mQueue.size() > mWindowSize) {
        uint32_t oldest = mQueue.front();
        mQueue.pop();
        mSum -= oldest;
    }

    ASSERT(mQueue.size() == mWindowSize);
    mAvg = mSum / mWindowSize;

    if (sample > mAvg*3/2 || sample < mAvg/2) {
        bool warnIt = true;
        if (mExtraThreshHold) {
            uint32_t delta = sample > mAvg ? sample-mAvg : mAvg-sample;
            if (delta < mExtraThreshHold)
                warnIt = false;
        }
        if (warnIt)
            WARNING("%s current sample: %d is oddy, avg is: %d", mName.c_str(), sample, mAvg);
    }

    if (mSampleCount % mWindowSize == 0) {
        DEBUG("recent %d samples' avg period is: %d, %s-fps is: %.1f", mWindowSize, mAvg, mName.c_str(), 1000000.f/mAvg);
    }
}

CallFrequencyStatics::CallFrequencyStatics(const char* name, uint32_t extraThreshHold, uint32_t window)
    : PerformanceStatics(name, extraThreshHold, window)
{
    mLastCallTime = 0;
}

void CallFrequencyStatics::updateSample()
{
    int64_t currentTime = getTimeUs();
    if (mLastCallTime) {
        uint32_t elapsed = (uint32_t) (currentTime-mLastCallTime);
        PerformanceStatics::updateSample(elapsed);
    }
    mLastCallTime = currentTime;
}

TimeCostStatics::TimeCostStatics(const char* name, uint32_t extraThreshHold, uint32_t window)
    : PerformanceStatics(name, extraThreshHold, window)
{
    mBeginTime = 0;
}

void TimeCostStatics::sampleBegin()
{
    mBeginTime = getTimeUs();
}

void TimeCostStatics::sampleEnd()
{
    int64_t currentTime = getTimeUs();
    if (mBeginTime == 0) {
        WARNING("TimeCostStatics::begin() should be called before end() to set time anchor");
        return;
    }

    uint32_t elapsed = (uint32_t) (currentTime - mBeginTime);
    PerformanceStatics::updateSample(elapsed);
    mBeginTime = 0;
}

} // end of namespace YUNOS_MM
