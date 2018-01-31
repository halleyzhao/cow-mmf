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

using namespace YUNOS_MM;

class VideosourceTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(VideosourceTest, videosourceTest) {
    mm_status_t status = MM_ERROR_SUCCESS;

    ComponentSP sourceSP;
    int count = 300;

    sourceSP = ComponentFactory::create(NULL, MEDIA_MIMETYPE_VIDEO_SURFACE_SOURCE, false);

    MediaMetaSP meta = MediaMeta::create();
    meta->setInt32(MEDIA_ATTR_WIDTH, 1280);
    meta->setInt32(MEDIA_ATTR_HEIGHT, 720);
    meta->setFloat(MEDIA_ATTR_FRAME_RATE, 30.0f);
    status = sourceSP->setParameter(meta);
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("source setParameter done\n");

    status = sourceSP->prepare();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(500*1000ll);
    PRINTF("source prepared\n");

    status = sourceSP->start();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(20*1000ll);
    PRINTF("source started\n");

    PRINTF("read source...\n");
    int i = 0;
    Component::ReaderSP reader = sourceSP->getReader(Component::kMediaTypeVideo);
    while ((i++ < count) && reader) {
        MediaBufferSP buf;
        EXPECT_TRUE(reader->read(buf) == MM_ERROR_SUCCESS);
        if (!(i % 20))
            PRINTF("read %d buffers from source\n", i);
    }

    status = sourceSP->stop();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(40*1000ll);
    PRINTF("source stop\n");

    status = sourceSP->reset();
    EXPECT_FALSE(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC);
    usleep(40*1000ll);
    PRINTF("source reset\n");

    PRINTF("sourceSP.reset() begin\n");
    sourceSP.reset();
    PRINTF("sourceSP.reset() done\n");

    PRINTF("done\n");
}
