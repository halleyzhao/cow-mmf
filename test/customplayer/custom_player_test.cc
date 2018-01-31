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
#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

#include "multimedia/mediaplayer.h"
#include "multimedia/pipeline.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "mmwakelocker.h"
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
#include "native_surface_help.h"
#endif

MM_LOG_DEFINE_MODULE_NAME("CUSTOM-PLAYER-TEST")

#if 1
#undef DEBUG
#undef INFO
#undef MMLOGW
#undef ERROR
#undef TERM_PROMPT
#define DEBUG(format, ...)  fprintf(stderr, "[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MMLOGW(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define TERM_PROMPT(...)
#endif

#define USE_CUSTOM_PIPELINE     1
static const int WAIT_FOR_COMPLETE = 1;

static const char *g_video_file_path = "/usr/bin/ut/res/video/test.mp4";

using namespace YUNOS_MM;

static int s_g_prepared = 0;
static int s_g_play_completed = 0;
static int s_g_error_occured = 0;
static int s_g_report_video_size =0;

class MyListener : public MediaPlayer::Listener {
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj)
    {
        DEBUG("msg: %d, param1: %d, param2: %d, obj: %p", msg, param1, param2, obj);
        switch ( msg ) {
            case MSG_PREPARED:
                s_g_prepared = 1;
                break;
            case MSG_PLAYBACK_COMPLETE:
                s_g_play_completed = 1;
                break;
            case MSG_ERROR:
                s_g_error_occured = 1;
                break;
            case MSG_SET_VIDEO_SIZE:
                s_g_report_video_size = 1;
                break;
            default:
                break;
        }
    }
};

class MediaplayerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

// wait until prepare is done
void waitUntilPrepareDone()
{
    int sleepCount = 0;
    while(!s_g_prepared && sleepCount++<100)
        usleep(50000);
}

uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;

#if USE_CUSTOM_PIPELINE
Pipeline* createCustomPipeline();
#endif

TEST_F(MediaplayerTest, mediaplayerplayvideo) {
    mm_status_t status;
    int ret = 0;
    MediaPlayer::VolumeInfo vol;
    int duration = -1;
    std::map<std::string,std::string> header;

    INFO("Hello mediaplayer video\n");
    AutoWakeLock awl;
#if USE_CUSTOM_PIPELINE
    PipelineSP pipeline;
#endif

    s_g_prepared = 0;
    s_g_play_completed = 0;
    s_g_error_occured = 0;
    s_g_report_video_size = 0;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    WindowSurface *ws = createSimpleSurface(800, 600);
    if (!ws) {
        PRINTF("init surface fail\n");
        exit(-1);
    }
#endif

    MyListener * listener = new MyListener();
    ASSERT_NE(listener, NULL);

    MediaPlayer * player = MediaPlayer::create(MediaPlayer::PlayerType_COW);
    if ( !player ) {
        ERROR("no mem\n");
        ret  = -1;
        goto error;
    }

#if USE_CUSTOM_PIPELINE
    DEBUG("use customized pipeline");
    pipeline = Pipeline::create(createCustomPipeline());
    if (!pipeline) {
        MMLOGE("failed to create ExamplePipelinePlayer\n");
        goto error;
    }
    player->setPipeline(pipeline);
#endif
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    player->setVideoDisplay(ws);
#endif
    player->setListener(listener);

    INFO("settting datasource\n");

    status = player->setDataSource(g_video_file_path, &header);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        ERROR("setdatasource failed: %d\n", status);
        ret = -1;
        goto error;
    }

    INFO("prepare\n");
    status = player->prepareAsync();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        ERROR("prepare failed: %d\n", status);
        ret = -1;
        goto error;
    }
    // wait until prepare is done
    waitUntilPrepareDone();
    if(!s_g_prepared){
        ERROR("prepareAsync failed after wait 5 seconds\n");
        ret = -1;
        goto error;
    }

    INFO("start\n");
    status = player->start();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        ERROR("start failed: %d\n", status);
        ret = -1;
        goto error;
    }
    INFO("call start over\n");

    if ( WAIT_FOR_COMPLETE ) {
        INFO("waitting for complete\n");
        while ( !s_g_play_completed && !s_g_error_occured ) {
            if(s_g_report_video_size){
                int width , height;
                status = player->getVideoSize(&width, &height);
                if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
                    ERROR("getVideoSize error: %d\n", status);
                }
                else INFO("get video size : %d x %d \n",width, height);
                s_g_report_video_size = 0;
            }
            sleep(15);
        }
    } else {
        INFO("sleep for a while\n");
        sleep(20);
        INFO("sleep over\n");
    }

    INFO("stopping\n");
    player->stop();

    INFO("resetting\n");
    player->reset();

    error:
    INFO("destroying\n");
    MediaPlayer::destroy(player);
    delete listener;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    destroySimpleSurface(ws);
#endif
    EXPECT_EQ(ret, 0);
    INFO("exit\n");
}

int main(int argc, char* const argv[]) {
    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
    } catch (...) {
        ERROR("InitGoogleTest failed!");
        return -1;
    }
    return ret;
}
