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


class FakeSink : public PlaySinkComponent, MMThread {
public:
    FakeSink() : MMThread("FakeSink")
                                  , mContinue(false)
    {}
    virtual ~FakeSink(){}

public:
    virtual const char * name() const { return "FakeSink"; }
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual int64_t getCurrentPosition() { return 0; }

    virtual mm_status_t addSource(Component * component, MediaType mediaType)
    {
        MMASSERT(component != NULL);
        if ( !component ) {
            MMLOGE("invalid param\n");
            return MM_ERROR_INVALID_PARAM;
        }
        mSourceReader = component->getReader(mediaType);
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t start()
    {
        mContinue = true;
        create();
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t stop()
    {
        mContinue = false;
        destroy();
        return MM_ERROR_SUCCESS;
    }

    virtual void main()
    {
        MMLOGV("+\n");
        while ( mContinue ) {
            MediaBufferSP buffer(MediaBufferSP((MediaBuffer*)NULL));
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
        MMLOGV("-\n");
    }


protected:
    ReaderSP mSourceReader;
    bool mContinue;

    MM_DISALLOW_COPY(FakeSink);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(FakeSink);



class APPSourceUser {
public:
    APPSourceUser(Component::WriterSP sourceWriter) : mSourceWriter(sourceWriter) {}
    ~APPSourceUser(){}

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
            mSourceWriter->write(buf);
            MMLOGV("writing %d/%d over\n", i, TRANSFER_COUNT);
            usleep(10000);
        }

        MediaBufferSP buf = createLastBuffer();
        if (!buf) {
            MMLOGE("failed to createMediaBuffer\n");
            return false;
        }
        mSourceWriter->write(buf);
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
    APPSourceUser(){}

private:
    Component::WriterSP mSourceWriter;

    MM_DISALLOW_COPY(APPSourceUser);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(APPSourceUser)

class APPPlaySourceTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

extern "C" Component * createComponent(const char* mimeType, bool isEncoder);
extern "C" void releaseComponent(Component * component);


static Component * sgVideoSource = NULL;
static Component * sgFakeSink = NULL;
static APPSourceUser * sgAPPSourceUser = NULL;

MM_LOG_DEFINE_MODULE_NAME("APPPlaySourceTest")
    
TEST_F(APPPlaySourceTest, normalTest) {
    MMLOGI("Hello APPPlaySource\n");
    sem_init(&sgSem, 0, 0);

    sgVideoSource = createComponent(NULL, false);
    EXPECT_NE(sgVideoSource, (Component*)NULL);
    if ( !sgVideoSource ) {
        MMLOGE("failed to create APPPlaySource\n");
        goto init;
    }

    EXPECT_EQ(sgVideoSource->init(), MM_ERROR_SUCCESS);

    {
        Component::WriterSP sourceWriter = sgVideoSource->getWriter(Component::kMediaTypeVideo);
        MediaMetaSP meta = MediaMeta::create();
        meta->setInt32(MEDIA_ATTR_MEDIA_TYPE, (int32_t)Component::kMediaTypeVideo);
        meta->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_VIDEO_AVC);
        meta->setInt32(MEDIA_ATTR_WIDTH, 1920);
        meta->setInt32(MEDIA_ATTR_HEIGHT, 1080);
        meta->setInt32(MEDIA_ATTR_CODECID, (int32_t)kCodecIDH264);
        sourceWriter->setMetaData(meta);
        try {
            sgAPPSourceUser = new APPSourceUser(sourceWriter);
        } catch (...) {
            MMLOGE("failed to create APPSourceUser\n");
            EXPECT_EQ(1,0);
            goto init;
        }
    }

    try {
        sgFakeSink = new FakeSink();
    } catch (...) {
        MMLOGE("failed to create FakeSink\n");
        EXPECT_EQ(1,0);
        goto init;
    }
    EXPECT_NE(sgFakeSink, (Component*)NULL);
    if ( !sgFakeSink ) {
        MMLOGE("failed to create source\n");
        goto init;
    }
    EXPECT_EQ(sgFakeSink->start(), MM_ERROR_SUCCESS);

    EXPECT_EQ(sgFakeSink->addSource(sgVideoSource, Component::kMediaTypeVideo), MM_ERROR_SUCCESS);

    sgAPPSourceUser->transferData();

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
    sgFakeSink->stop();

init:
    MM_RELEASE(sgAPPSourceUser);
    MM_RELEASE(sgFakeSink);
    if (sgVideoSource) {
        sgVideoSource->uninit();
        releaseComponent(sgVideoSource);
        sgVideoSource = NULL;
    }
    sem_destroy(&sgSem);
    MMLOGI("bye\n");
}


