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

#include <unistd.h>
#include <semaphore.h>

#include <multimedia/mmthread.h>
#include <multimedia/component.h>
#include <dlfcn.h>
#include <multimedia/media_attr_str.h>
#include <gtest/gtest.h>
#include "multimedia/av_buffer_helper.h"

extern "C" {
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <inttypes.h>
#include <libavformat/avformat.h>
}

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

using namespace YUNOS_MM;

static sem_t sgSem;
static bool sgErrorOccured = false;
static bool sgVideoEos = false;


class FakeSource : public Component {
public:
    FakeSource() {}
    virtual ~FakeSource(){}

public:
    virtual const char * name() const { return "FakeSource"; }
    COMPONENT_VERSION;
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {
        mSinkWriter = component->getWriter(Component::kMediaTypeVideo);
        return MM_ERROR_SUCCESS;
    }

public:
    bool transferData() {
        MMLOGI("+\n");
        static const int TRANSFER_COUNT = 100;
        for (int i = 0; i < TRANSFER_COUNT; ++i) {
            MMLOGV("creating %d/%d\n", i, TRANSFER_COUNT);
            MediaBufferSP buf = createOneBuffer(i);
            if ( !buf ) {
                MMLOGE("failed to createMediaBuffer\n");
                return false;
            }

            MMLOGV("writing %d/%d\n", i, TRANSFER_COUNT);
            mSinkWriter->write(buf);
            MMLOGV("writing %d/%d over\n", i, TRANSFER_COUNT);
            usleep(10000);
        }

        MediaBufferSP buf = createLastBuffer();
        if (!buf) {
            MMLOGE("failed to createMediaBuffer\n");
            return false;
        }
        mSinkWriter->write(buf);
        return true;
    }

private:
    MediaBufferSP createOneBuffer(int seq) {
        MMLOGV("creating buffer: %d\n", seq);
        AVPacket * packet = (AVPacket*)malloc(sizeof(AVPacket));
        if (!packet) {
            MMLOGE("no mem\n");
            return MediaBufferSP((MediaBuffer*)NULL);
        }

        memset(packet, 0, sizeof(AVPacket));

        packet->pts = seq * 1000000;
        packet->dts = seq * 1000000;
        static const size_t buffSize = 64;
        packet->data = (uint8_t*)malloc(buffSize);
        if (!packet->data) {
            MMLOGE("no mem\n");
            free(packet);
            return MediaBufferSP((MediaBuffer*)NULL);
        }
        memcpy(packet->data, "fdsfdsfdsfadsaf", strlen("fdsfdsfdsfadsaf"));
        packet->size = buffSize;
        packet->stream_index = 1;
        packet->flags = seq == 1 ? AV_PKT_FLAG_KEY : 0;
        packet->duration = 0;

        MediaBufferSP buf = AVBufferHelper::createMediaBuffer(packet, false);
        buf->addReleaseBufferFunc(bufferReleaser);

        return buf;
    }

    MediaBufferSP createLastBuffer() {
        MediaBufferSP buf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
        if ( !buf ) {
            MMLOGE("failed to createMediaBuffer\n");
            return MediaBufferSP((MediaBuffer*)NULL);
        }

        buf->setFlag(MediaBuffer::MBFT_EOS);
        buf->setSize(0);
        return buf;
    }

    static bool bufferReleaser(MediaBuffer* mediaBuffer) {
        MMLOGV("+\n");
        AVPacket *avpkt = NULL;
        MediaMetaSP meta = mediaBuffer->getMediaMeta();

        if (meta && mediaBuffer->isFlagSet(MediaBuffer::MBFT_AVPacket)) {
            void *ptr = NULL;
            meta->getPointer("AVPacketPointer", ptr);
            avpkt = (AVPacket*)ptr;
        }

        if (avpkt) {
            MMLOGV("released\n");
            if (avpkt->data) free(avpkt->data);
            free(avpkt);
        }

        return true;
    }


private:
    Component::WriterSP mSinkWriter;

    MM_DISALLOW_COPY(FakeSource);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(FakeSource);



class APPSinkUser : public MMThread {
public:
    APPSinkUser(Component::ReaderSP sourceReader) : MMThread("APPSinkUser"),
        mSourceReader(sourceReader), mContinue(false) {}
    ~APPSinkUser(){}

public:
    bool start() {
        MMLOGI("+\n");
        mContinue = true;
        if (MMThread::create()) {
            MMLOGE("failed to create thread\n");
            return false;
        }

        return true;
    }

    void stop() {
        MMLOGI("+\n");
        mContinue = false;
        destroy();
        MMLOGI("-\n");
    }

    virtual void main() {
        MMLOGI("+\n");
        while (mContinue) {
            MediaBufferSP buffer;
            mm_status_t ret = mSourceReader->read(buffer);
            if ( ret == MM_ERROR_SUCCESS ) {
                MMLOGV("[read]size: %" PRId64 "\n", buffer->size());
                if ( buffer->isFlagSet(MediaBuffer::MBFT_EOS) ) {
                    sgVideoEos = true;
                    MMLOGI("video eos\n");
                    sem_post(&sgSem);
                    break;
                }
            } else if ( ret == MM_ERROR_NO_MORE ) {
                MMLOGV("[read]nomore\n");
            } else if ( ret == MM_ERROR_INVALID_STATE ) {
                MMLOGV("[read]source not started yet\n");
            } else {
                MMLOGV("[read]failed: %d\n", ret);
                sgErrorOccured = true;
                sem_post(&sgSem);
                break;
            }
            usleep(10000);
        }
        MMLOGI("-\n");
    }

private:
    APPSinkUser() : MMThread("aa"){}

private:
    Component::ReaderSP mSourceReader;
    bool mContinue;

    MM_DISALLOW_COPY(APPSinkUser);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(APPSinkUser)

class APPSinkTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

extern "C" Component * createComponent(const char* mimeType, bool isEncoder);
extern "C" void releaseComponent(Component * component);


static FakeSource * sgFakeSource = NULL;
static Component * sgVideoSink = NULL;
static APPSinkUser * sgAPPSinkUser = NULL;

MM_LOG_DEFINE_MODULE_NAME("APPSinkTest")

TEST_F(APPSinkTest, normalTest) {
    MMLOGI("Hello APPSink\n");
    sem_init(&sgSem, 0, 0);

    try {
        sgFakeSource = new FakeSource();
    } catch (...) {
        EXPECT_EQ(1, 0);
        goto init;
    }

    sgVideoSink = createComponent(NULL, false);
    EXPECT_NE(sgVideoSink, (Component*)NULL);
    if ( !sgVideoSink ) {
        MMLOGE("failed to create APPSink\n");
        goto init;
    }

    EXPECT_EQ(sgVideoSink->init(), MM_ERROR_SUCCESS);
    sgFakeSource->addSink(sgVideoSink, Component::kMediaTypeVideo);

    {
        Component::ReaderSP sinkReader = sgVideoSink->getReader(Component::kMediaTypeVideo);

        try {
            sgAPPSinkUser = new APPSinkUser(sinkReader);
            EXPECT_EQ(sgAPPSinkUser->start(), true);
        } catch (...) {
            MMLOGE("failed to create APPSinkUser\n");
            EXPECT_EQ(1,0);
            goto init;
        }
    }

    sgFakeSource->transferData();

    {
        int i = 0;
        while (1) {
            if (i > 100) {
                MMLOGE("time out\n");
                EXPECT_EQ(1, 0);
                break;
            }

            if (sgVideoEos) {
                MMLOGI("success\n");
                EXPECT_EQ(1,1);
                break;
            }

            if (sgErrorOccured) {
                MMLOGE("sgErrorOccured\n");
                EXPECT_EQ(1, 0);
                break;
            }

            sem_wait(&sgSem);
        }
    }
    sgAPPSinkUser->stop();

init:
    MM_RELEASE(sgAPPSinkUser);
    MM_RELEASE(sgFakeSource);
    if (sgVideoSink) {
        sgVideoSink->uninit();
        releaseComponent(sgVideoSink);
        sgVideoSink = NULL;
    }
    sem_destroy(&sgSem);
    MMLOGI("bye\n");
}


