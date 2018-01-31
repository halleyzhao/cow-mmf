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
#include <gtest/gtest.h>
#include <multimedia/component.h>

#include <multimedia/mm_debug.h>
#include <multimedia/component_factory.h>
#include "multimedia/media_attr_str.h"
#include "multimedia/media_buffer.h"
//#include <WindowSurface.h>
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "media_codec.h"
#endif


//MM_LOG_DEFINE_MODULE_NAME("video-sink-test");

using namespace YUNOS_MM;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
using namespace YunOSMediaCodec;
#endif
static const int32_t gCount = 100;

static bool releaseMediaBuffer(MediaBuffer *mediaBuf) {

    size_t *pIndex = NULL;
    if (!(mediaBuf->getBufferInfo((uintptr_t *)&pIndex, NULL, NULL, 1))) {
        PRINTF("error in release mediabuffer\n");
        return false;
    }

    delete []pIndex;

    return true;
}

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
typedef MMSharedPtr < MediaCodec > MediaCodecSP;

class MediaMetaCodec : public MediaMeta::MetaBase {
  public:
    MediaMetaCodec(MediaCodecSP mcSP) {sp = mcSP;}
    virtual void* getRawPtr() { return sp.get();};
    MediaCodecSP sp;
};
typedef MMSharedPtr < MediaMetaCodec > MetaMediaCodecSP;
#endif


class FakeFilter : public FilterComponent {

public:
    FakeFilter();
    virtual ~FakeFilter() {}

    const char * name() const { return "FakeFilter"; }
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType){ return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType);

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }

    virtual mm_status_t reset();

    mm_status_t write();

private:
    Component *mVideoSink;
    WriterSP mWriter;


    size_t mWidth;
    size_t mHeight;
    int64_t mTimeStamp;
    int32_t mFrameCount;

    int32_t mOffsets;
    int32_t mBufferSize;
    int32_t mDimension;

};

FakeFilter::FakeFilter() :mVideoSink(NULL)
{
    mWidth = 1280;
    mHeight = 720;
    mTimeStamp = 0;
    mFrameCount = 0;

    mOffsets = 0;
    mBufferSize = 0;
    mDimension = 0;


}

mm_status_t FakeFilter::reset() {
    mWriter.reset();
    return MM_ERROR_SUCCESS;
}


mm_status_t FakeFilter::write() {
    while (1) {
        MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_BufferIndex);

        //int32_t size = mHeight * mWidth * 3 / 2;
        mTimeStamp += 10*1000ll;

        int32_t offset = 0;
        int32_t bufferSize = sizeof(size_t);
        size_t *pIndex = new size_t;

        //index is fake
        *pIndex = 0;
        mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);
        mediaBuf->setSize(sizeof(size_t));
        mediaBuf->setBufferInfo((uintptr_t *)&pIndex, &offset, &bufferSize, 1);
        mediaBuf->setPts(mTimeStamp);

        if (mFrameCount++ > gCount) {
            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
        }

        mm_status_t status = mWriter->write(mediaBuf);
        if (status != MM_ERROR_SUCCESS) {
            if (status != MM_ERROR_ASYNC) {
                PRINTF("decoder add sink fail %d\n", status);
                return MM_ERROR_OP_FAILED;
            }
        }

        if (mFrameCount > gCount) {
            break;
        }


        usleep(10*1000ll);
    }

    mFrameCount = 0;
    PRINTF("Write to end\n");
    return MM_ERROR_SUCCESS;
}


mm_status_t FakeFilter::addSink(Component * component, MediaType mediaType) {
    if (mediaType != Component::kMediaTypeVideo || component == NULL) {
        PRINTF("unsupport media type\n");
        return MM_ERROR_INVALID_PARAM;
    }


    mVideoSink = component;

    if (mVideoSink)
        mWriter = mVideoSink->getWriter(Component::kMediaTypeVideo);

    if (mWriter == NULL) {
        PRINTF("unsupport media type\n");
        return MM_ERROR_INVALID_PARAM;
    }


    return MM_ERROR_SUCCESS;
}



typedef MMSharedPtr<FakeFilter> FakeFilterSP;


class VideosinkTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(VideosinkTest, videosinkTest) {
    mm_status_t status = MM_ERROR_SUCCESS;

    FakeFilterSP fakefilter(new FakeFilter);

    ComponentSP sinkSP;

    sinkSP = ComponentFactory::create(NULL, MEDIA_MIMETYPE_VIDEO_RENDER, false);
    status = sinkSP->prepare();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("sink prepared\n");

    status = fakefilter->addSink(sinkSP.get(), Component::kMediaTypeVideo);
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);

    status = sinkSP->start();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("sink started\n");


    PRINTF("write data to sink...\n");
    EXPECT_TRUE(fakefilter->write() == MM_ERROR_SUCCESS);
    PRINTF("write data to sink done\n");


    status = sinkSP->pause();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("sink paused\n");


    status = sinkSP->start();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("sink re-started\n");


    PRINTF("write data to sink after resume...\n");
    EXPECT_TRUE(fakefilter->write() == MM_ERROR_SUCCESS);
    PRINTF("write data to sink done\n");


    status = sinkSP->flush();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("sink flushed\n");

    status = sinkSP->stop();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(40*1000ll);
    PRINTF("sink stop\n");


    status = sinkSP->reset();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(40*1000ll);
    PRINTF("sink reset\n");


    status = fakefilter->reset();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(40*1000ll);
    PRINTF("fakefilter reset\n");

    PRINTF("sinkSP.reset() begin\n");
    sinkSP.reset();
    PRINTF("sinkSP.reset() done\n");

    PRINTF("fakefilter.reset() begin\n");
    fakefilter.reset();
    PRINTF("fakefilter.reset() done\n");


    PRINTF("done\n");

}


