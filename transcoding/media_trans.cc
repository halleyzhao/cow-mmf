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
#include <getopt.h>
#include <glib.h>

MM_LOG_DEFINE_MODULE_NAME("CUSTOM-PLAYER-TEST")

#if 1
#undef DEBUG
#undef INFO
#undef MMLOGW
#undef ERROR
#undef TERM_PROMPT
#define DEBUG(format, ...)  fprintf(stderr, "[D] %s, line: %d:" format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define MMLOGW(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define TERM_PROMPT(...)
#endif

static const int WAIT_FOR_COMPLETE = 1;

using namespace YUNOS_MM;

static int s_g_prepared = 0;
static int s_g_play_completed = 0;
static int s_g_error_occured = 0;
static int s_g_report_video_size =0;

// command line options
static const char *g_video_file_path = "/usr/bin/ut/res/video/trailer_short.mp4";
const char *g_out_video_file_path = NULL;
static uint32_t g_trans_mode = 1;
static uint32_t g_encode_video_width = 0;
static uint32_t g_encode_video_height = 0;

static GOptionEntry entries[] = {
    {"add", 'a', 0, G_OPTION_ARG_STRING, &g_video_file_path, " set the file name to convert", NULL},
    {"out", 'o', 0, G_OPTION_ARG_STRING, &g_out_video_file_path, " set the output file name", NULL},
    {"video_transcode_mode", 'm', 0, G_OPTION_ARG_INT, &g_trans_mode, "process mode: 1: remux, 2: video transcoding", NULL},
    {"encode_video_width", 'w', 0, G_OPTION_ARG_INT, &g_encode_video_width, "set scaled video width (default is input video width)", NULL},
    {"encode_video_height", 'h', 0, G_OPTION_ARG_INT, &g_encode_video_height, "scaled video height(default is input video height)", NULL},
    {NULL}
};

class MyListener : public MediaPlayer::Listener {
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj)
    {
        if (msg != MSG_BUFFERING_UPDATE || param1 % 20 == 0)
            DEBUG("msg: %d, param1: %d, param2: %d, obj: %p", msg, param1, param2, obj);
        switch ( msg ) {
            case MSG_PREPARED:
                s_g_prepared = 1;
                break;
            case MSG_PLAYBACK_COMPLETE:
                INFO("reach playback complete");
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

Pipeline* createRemuxPipeline();
Pipeline* createTranscodePipeline();

TEST_F(MediaplayerTest, mediaplayerplayvideo) {
    mm_status_t status;
    int ret = 0;
    MediaPlayer::VolumeInfo vol;
    int duration = -1;
    std::map<std::string,std::string> header;

    INFO("Hello mediaplayer video\n");
    AutoWakeLock awl;
    PipelineSP pipeline;

    s_g_prepared = 0;
    s_g_play_completed = 0;
    s_g_error_occured = 0;
    s_g_report_video_size = 0;

    MyListener * listener = new MyListener();
    ASSERT_NE(listener, NULL);

    MediaPlayer * player = MediaPlayer::create(MediaPlayer::PlayerType_COW);
    if ( !player ) {
        ERROR("no mem\n");
        ret  = -1;
        goto error;
    }

    DEBUG("use customized pipeline");
    if (g_trans_mode == 1)
        pipeline = Pipeline::create(createRemuxPipeline());
    else
        pipeline = Pipeline::create(createTranscodePipeline());
    if (!pipeline) {
        MMLOGE("failed to create ExamplePipelinePlayer\n");
        goto error;
    }
    player->setPipeline(pipeline);

    // player->setVideoDisplay(ws);
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
        sleep(1);
        DEBUG("s_g_play_completed: %d, s_g_error_occured: %d", s_g_play_completed, s_g_error_occured);
    }

    INFO("stopping\n");
    player->stop();

    INFO("resetting\n");
    player->reset();

    error:
    INFO("destroying\n");
    MediaPlayer::destroy(player);
    delete listener;
    // destroySimpleSurface(ws);
    EXPECT_EQ(ret, 0);
    INFO("exit\n");
}

int main(int argc, char* const argv[]) {
    int ret;
    std::string outFileName;

    GError *error = NULL;
    GOptionContext *context;

    setenv("MM_MUX_2_AVCC", "0", 1);
    context = g_option_context_new("MediaTranscode");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (gchar***)&argv, &error)) {
            ERROR("option parsing failed: %s\n", error->message);
            return -1;
    }
    g_option_context_free(context);

    INFO("input file is: %s", g_video_file_path);
    if (!g_out_video_file_path) {
        outFileName = g_video_file_path;
        outFileName += ".tr.mp4";
        g_out_video_file_path = outFileName.c_str();
    }

    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
    } catch (...) {
        ERROR("InitGoogleTest failed!");
        return -1;
    }
    return ret;
}
