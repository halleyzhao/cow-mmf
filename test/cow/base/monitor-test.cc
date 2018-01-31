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
#include <unistd.h>
#include <stdio.h>
#include <cstdlib>
#include <gtest/gtest.h>
#include "multimedia/mm_debug.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_buffer.h"

MM_LOG_DEFINE_MODULE_NAME("Cow-MediaMonitorTest");

using namespace YUNOS_MM;

// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

#define TEST_MESSAGE_1 (msg_type)1

int32_t testCount = 500;

const int randomMin = 300;
const int randomMax = 5000;

const int staticsCount = 50;
const int staticsProduceAlert = randomMin + (randomMax-randomMin)*3/4;
const int staticsConsumeAlert = randomMin + (randomMax-randomMin)*3/4;

const int TrafficControlLowBar = 10;
const int TrafficControlHighBar = 30;
static const char * MMMSGTHREAD_NAME = "MediaMonitorTest";

uint32_t getRandom(uint32_t low, uint32_t high)
{
    return rand()%(high-low+1)+low;
}

class MediaMonitorTest : public MMMsgThread {
  public:
    MediaMonitorTest(MonitorSP monitor, int32_t producerFast = 0)
        : MMMsgThread(MMMSGTHREAD_NAME)
        , mMonitor(monitor)
        , mProducerFast(producerFast)
    {
        FUNC_TRACK();
    }
    ~MediaMonitorTest() {
        FUNC_TRACK();
    }
    bool produceBuffer() {
        FUNC_TRACK();
        MediaBufferSP buffer = MediaBuffer::createMediaBuffer();
        buffer->setMonitor(mMonitor);
        mBuffers.push_back(buffer);
        postMsg(TEST_MESSAGE_1, 0, NULL);

        if (mProducerFast > 1) {
            TrafficControl* trafficControl = DYNAMIC_CAST<TrafficControl*> (mMonitor.get());
            ASSERT(trafficControl);
            usleep(getRandom(randomMin, randomMax)/mProducerFast);
            trafficControl->waitOnFull();
        }

        return true;
    }
    bool consumeBuffer() {
        FUNC_TRACK();
        mBuffers.pop_front();

        if (mProducerFast < -1) {
            TrafficControl* trafficControl = DYNAMIC_CAST<TrafficControl*> (mMonitor.get());
            ASSERT(trafficControl);
            usleep(getRandom(randomMin, randomMax)/(-mProducerFast));
            trafficControl->waitOnEmpty();
        }

        return true;
    }
    bool isDone() {
        return mBuffers.empty();
    }

    bool init() {
        FUNC_TRACK();
        int ret = MMMsgThread::run();
        return ret == 0;
    }

    void uninit() {
        FUNC_TRACK();
        MMMsgThread::exit();
    }

  private:
    void onMessage1(param1_type param1, param2_type param2, uint32_t rspId) {
        FUNC_TRACK();
        consumeBuffer();
        usleep(getRandom(randomMin, randomMax));
    }
    MonitorSP mMonitor;
    int32_t mProducerFast;
    std::list<MediaBufferSP> mBuffers;

    DECLARE_MSG_LOOP()
};

BEGIN_MSG_LOOP(MediaMonitorTest)
    MSG_ITEM(TEST_MESSAGE_1, onMessage1)
END_MSG_LOOP()

int timeStaticsTest(int32_t count)
{
    FUNC_TRACK();
    int i=0;
    MonitorSP monitor(new TimeStatics(staticsCount, staticsProduceAlert, staticsConsumeAlert, "TimeStaticsTest"));
    MediaMonitorTest monitorTest(monitor);

    if (!monitorTest.init()) {
        ERROR("fail to start msg thread\n");
        return -1;
    }

    while(i++<testCount) {
        monitorTest.produceBuffer();
        usleep(getRandom(randomMin, randomMax));
    }

    INFO();
    while(!monitorTest.isDone()) {
        usleep(500);
    }
    monitorTest.uninit();

    INFO("successfully done\n");
    return 0;
}

int trafficControlTest(int32_t count, int32_t producerFast)
{
    FUNC_TRACK();
    int i=0;
    TrafficControl* trafficControl = new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "TrafficControlTest");
    MonitorSP monitor(trafficControl);
    MediaMonitorTest monitorTest(monitor, producerFast);

    if (!monitorTest.init()) {
        ERROR("fail to start msg thread\n");
        return -1;
    }

    while(i++<testCount) {
        monitorTest.produceBuffer();
    }

    INFO();
    while(!monitorTest.isDone()) {
        usleep(500);
    }

    INFO();
    trafficControl->unblockWait(true);

    INFO();
    monitorTest.uninit();

    INFO("successfully done\n");
    return 0;
}

class MonitorTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(MonitorTest, monitorTest) {
    int ret;
    ret = timeStaticsTest(testCount);
    EXPECT_TRUE(ret == 0);

    ret = trafficControlTest(testCount, 5); // producer is 5x faster than consumer
    EXPECT_TRUE(ret == 0);

    ret = trafficControlTest(testCount, -5); // consumer is 5x faster than producer
    EXPECT_TRUE(ret == 0);

    INFO("successfully exit\n");
}

int main(int argc, char* const argv[]) {
    int ret;
    if (argc>=2)
        testCount = atoi(argv[1]);

    INFO("test buffer count: %d\n", testCount);
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
    } catch (...) {
        ERROR("InitGoogleTest failed!");
        return -1;
    }
    return ret;
}

