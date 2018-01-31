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
#include <unistd.h>
#include <gtest/gtest.h>

#include "multimedia/component.h"
#include "multimedia/media_meta.h"
#include "multimedia/mm_debug.h"
//#ifndef __AUDIO_CRAS__
//#define __AUDIO_CRAS__
//#endif
#ifdef __AUDIO_CRAS__
#include "audio_src_cras.h"
#elif defined(__AUDIO_PULSE__)
#include "audio_src_pulse.h"
#endif
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("Cow-Record-Test")
#define ENTER() INFO(">>>\n")
#define EXIT() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)
static const char * MMTHREAD_NAME = "RecordSinkThread";

class StubRecordSink : public Component {

public:
    StubRecordSink():mIsPaused(true),
                     mCondition(mLock),
                     mFileSink(NULL)
    {
        mMetaData = MediaMeta::create();

    }
    virtual ~StubRecordSink() {
        fclose(mFileSink);
    }
    virtual mm_status_t prepare();
    virtual mm_status_t stop();
    virtual mm_status_t start();
    virtual mm_status_t pause();

public:
    virtual const char * name() const {return "stub record";}
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}

public:
    class RecordSinkThread;
    typedef MMSharedPtr <RecordSinkThread> RecordSinkThreadSP;
    class RecordSinkThread : public MMThread {
      public:
        RecordSinkThread(StubRecordSink *record);
        ~RecordSinkThread();
        void signalExit();
        void signalContinue();

      protected:
        virtual void main();

      private:
        StubRecordSink *mRecord;
        bool mContinue;
    };

private:

    MediaMetaSP mMetaData;
    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    FILE* mFileSink;
    ReaderSP mReader;
    RecordSinkThreadSP mRecordSinkThread;
};

//////////////////////// RecordSinkThread
StubRecordSink::RecordSinkThread::RecordSinkThread(StubRecordSink* record)
    : MMThread(MMTHREAD_NAME)
    , mRecord(record)
    , mContinue(true)
{
    ENTER();
    EXIT();
}

StubRecordSink::RecordSinkThread::~RecordSinkThread()
{
    ENTER();
    EXIT();
}

void StubRecordSink::RecordSinkThread::signalExit()
{
    ENTER();
    MMAutoLock locker(mRecord->mLock);
    mContinue = false;
    mRecord->mCondition.signal();
    EXIT();
}

void StubRecordSink::RecordSinkThread::signalContinue()
{
    ENTER();
    mRecord->mCondition.signal();
    EXIT();
}

// save Buffer to file
void StubRecordSink::RecordSinkThread::main()
{
    ENTER();
    uint8_t *sourceBuf = NULL;
    int32_t offset = 0;
    int32_t size = 0;
    while(1) {
        MMAutoLock locker(mRecord->mLock);
        if (!mContinue) {
            break;
        }
        if (mRecord->mIsPaused) {
            INFO("pause wait");
            mRecord->mCondition.wait();
            INFO("pause wait wakeup");
        }
        MediaBufferSP mediaBuffer;
        int ret = mRecord->mReader->read(mediaBuffer);
        if (ret == MM_ERROR_SUCCESS) {
            if (mediaBuffer) {
                mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1);
                fwrite(sourceBuf+offset, 1, size, mRecord->mFileSink);
            }
        } else if (ret == MM_ERROR_NO_MORE) {
            usleep(10*1000);
        }
    }

    INFO("Output thread exited\n");
    EXIT();
}

// /////////////////////////////////////

mm_status_t StubRecordSink::prepare()
{
    #ifndef __MM_NATIVE_BUILD__
    mFileSink = fopen("/data/audio_record_pulse.pcm","wb");
    #else
    mFileSink = fopen("./ut/res/audio_record_pulse.pcm","wb");
    #endif
    return 0;
}

mm_status_t StubRecordSink::start()
{
    MMAutoLock locker(mLock);
    if (!mRecordSinkThread) {
        // create thread to save buffer
        mRecordSinkThread.reset (new RecordSinkThread(this), MMThread::releaseHelper);
        mRecordSinkThread->create();
    }
    mIsPaused = false;
    mRecordSinkThread->signalContinue();
    return 0;
}

mm_status_t StubRecordSink::stop()
{
    mIsPaused = true;
    if (mRecordSinkThread) {
        mRecordSinkThread->signalExit();
        mRecordSinkThread.reset();
    }
    MMAutoLock locker(mLock);
    mReader.reset();
    return 0;
}

mm_status_t StubRecordSink::pause()
{
    MMAutoLock locker(mLock);
    mIsPaused = true;
    return 0;
}

mm_status_t StubRecordSink::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeAudio) {
        mReader = component->getReader(kMediaTypeAudio);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (metaData) {
                mMetaData = metaData->copy();
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}


} // YUNOS_MM

using namespace YUNOS_MM;


class CowrecordTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(CowrecordTest, cowrecrdTest) {
    bool detectError = false;
    Component *source = NULL;
    Component *sink = NULL;
#ifdef __AUDIO_CRAS__
    source = new AudioSrcCras(NULL, false);
#elif defined(__AUDIO_PULSE__)
    source = new AudioSrcPulse(NULL, false);
#endif
    sink = new StubRecordSink();

    mm_status_t status;

    status = source->init();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            printf("source init fail %d \n", status);
            goto EXIT;
        }
        printf("source init ASYNC\n");
    }

    status = source->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            printf("source prepare fail %d \n", status);
           detectError = true;
        }
        printf("source prepare ASYNC\n");
    }
    ASSERT_TRUE(!detectError);

    status = sink->prepare();
    ASSERT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);

    status = sink->addSource(source, Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            printf("sink add source fail %d \n", status);
            detectError = true;
        }
        printf("sink add source ASYNC\n");
    }
    ASSERT_TRUE(!detectError);

    status = source->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            printf("source start fail %d \n", status);
            detectError = true;
        }
        printf("source start ASYNC\n");
    }
    ASSERT_TRUE(!detectError);

    status = sink->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            printf("sink start fail %d \n", status);
            detectError = true;
        }
        printf("sink start ASYN\nC");
    }
    ASSERT_TRUE(!detectError);

    printf("enter to stop...\n");
    usleep(10*1000*1000);

    status = source->pause();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    status = sink->pause();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    status = source->stop();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    status = sink->stop();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);

    source->reset();
    usleep(10*1000);

    source->uninit();

EXIT:
    MM_RELEASE(source);
    MM_RELEASE(sink);
    printf("stoped...\n");

}

