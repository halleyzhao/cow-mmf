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
#include <sys/time.h>
#include <vector>
#include <queue>
#include <string>
#include "multimedia/mm_cpp_utils.h"

#ifndef media_monitor_h
#define media_monitor_h

namespace YUNOS_MM {

class Monitor;
typedef MMSharedPtr <Monitor> MonitorSP;
class Tracker;
typedef MMSharedPtr <Tracker> TrackerSP;

class Monitor {
  public:
    Monitor(const char* logTag)
        : mProducedCount(0)
        , mConsumedCount(0)
        { mLogTag = std::string(logTag); }
    virtual ~Monitor() {}
    virtual bool onProducedOne_l() { return true; }
    virtual bool onConsumedOne_l() { return true; }

    bool produceOne();
    bool consumeOne();

    inline uint32_t producedCount() { return mProducedCount; }
    inline uint32_t consumedCount() { return mConsumedCount; }
    uint32_t aliveCount();

  protected:
    inline uint32_t pendingCount_l() { return mProducedCount-mConsumedCount; }
    Lock mLock;
    std::string mLogTag;

  private:
    uint32_t mProducedCount;
    uint32_t mConsumedCount;
    MM_DISALLOW_COPY(Monitor)
};

class Tracker {
  public:
    static TrackerSP create (MonitorSP monitor) { return TrackerSP(new Tracker(monitor));}
    ~Tracker() {mMonitor->consumeOne();}

  private:
    MonitorSP mMonitor;
    explicit Tracker(MonitorSP monitor) { monitor->produceOne(); mMonitor = monitor;}
    MM_DISALLOW_COPY(Tracker)
};

class TrafficControl : public Monitor
{
  public:
    explicit TrafficControl(uint32_t lowBar, uint32_t highBar, const char* logTag=NULL);
    virtual ~TrafficControl() { };

    bool isFull();
    bool waitOnFull();
    bool waitOnEmpty();
    void unblockWait(bool unblock=true);

    void updateLowBar(uint32_t bar) { /* MMAutoLock locker(mLock); */ mLowBar = bar; }
    void updateHighBar(uint32_t bar) { /* MMAutoLock locker(mLock); */ mHighBar = bar;}
    bool calibrate(uint32_t bufferCount) { return true; }

  protected:
    virtual bool onProducedOne_l();
    virtual bool onConsumedOne_l();

  private:
    uint32_t mLowBar;
    uint32_t mHighBar;
    Condition mCondition;
    bool mEscapeWait;

};

class TimeStatics : public Monitor
{
  public:
    explicit TimeStatics(uint32_t window, uint32_t alertProduceTime, uint32_t alertConsumeTime /* in ms */, const char* logTag=NULL);
    virtual ~TimeStatics() { };

  protected:
    virtual bool onProducedOne_l();
    virtual bool onConsumedOne_l();

  private:
    uint32_t mWindowSize;
    uint32_t mAlertProduceTime;
    uint32_t mAlertConsumeTime;
    uint64_t mStartTime;
    uint64_t mPreviousProduceTime;
    uint64_t mPreviousConsumeTime;
    uint32_t mCount;
};

class PerformanceStatics
{
  public:
    explicit PerformanceStatics(const char* name = "PerfStatics", uint32_t extraThreashHold = 0, uint32_t window = 50);
    ~PerformanceStatics() {}
    void updateSample(uint32_t sample);
    void reset();

  protected:
    std::string mName;
    uint32_t mWindowSize;
    std::queue<uint32_t > mQueue;
    uint32_t mExtraThreshHold; // we are in a multiple-thread system, ms fluctuate isn't important. however, not acceptable for audio rendering
    uint64_t mSum;  // sum of recent 'mWindowSize' of samples
    uint64_t mTotalSum; // total sum of all samples
    uint32_t mAvg;
    uint32_t mSampleCount;
};

class CallFrequencyStatics : public PerformanceStatics
{
  public:
    explicit CallFrequencyStatics(const char* name = "CallFrequencyStatic", uint32_t extraThreashHold = 0, uint32_t window = 50);
    ~CallFrequencyStatics() {}
    void updateSample();
  private:
    int64_t mLastCallTime;
};

class TimeCostStatics : public PerformanceStatics
{
  public:
    explicit TimeCostStatics(const char* name = "TimeCostStatics", uint32_t extraThreashHold = 0, uint32_t window = 50);
    ~TimeCostStatics() {}
    void sampleBegin();
    void sampleEnd();
  private:
    int64_t mBeginTime;
};
class AutoTimeCost
{
  public:
    explicit AutoTimeCost(TimeCostStatics &statics) :mStatics (statics) { mStatics.sampleBegin(); }
    ~AutoTimeCost() { mStatics.sampleEnd(); }
  private:
    TimeCostStatics &mStatics;
};

typedef MMSharedPtr<PerformanceStatics> PerformanceStaticsSP;
typedef MMSharedPtr<CallFrequencyStatics> CallFrequencyStaticsSP;
typedef MMSharedPtr<TimeCostStatics> TimeCostStaticsSP;

} // namespace YUNOS_MM
#define MM_CALCULATE_AVG_FPS(TAG)  do {         \
        static CallFrequencyStatics fps(TAG);   \
        fps.updateSample();                     \
    } while (0)

#endif // media_monitor_h

