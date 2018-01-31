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
#include <getopt.h>
#include <time.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "multimedia/cowplayer.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
#include <multimedia/mm_amhelper.h>
#endif

// #include "native_surface_help.h"

MM_LOG_DEFINE_MODULE_NAME("smp-cowplayer")

#if 0
#undef MMLOGD
#undef MMLOGI
#undef MMLOGW
#undef MMLOGE
#undef TERM_PROMPT
#define MMLOGD(format, ...)  fprintf(stderr, "[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MMLOGI(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MMLOGW(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MMLOGE(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define TERM_PROMPT(...)
#endif

static const char *g_video_file_path = "/usr/bin/ut/res/video/test.mp4";
static uint32_t g_play_time = 5;

using namespace YUNOS_MM;

static int g_prepared = 0;
static int g_started = 0;
static int g_stopped = 0;
static int g_play_completed = 0;
static int g_reset = 0;
static int g_error_occured = 0;
static int g_report_video_size =0;
// static uint32_t g_surface_width = 1280;
// static uint32_t g_surface_height = 720;

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
static MMAMHelper * s_g_amHelper = NULL;
#endif

class MyListener : public CowPlayer::Listener {
    virtual void  onMessage(int msg, int param1, int param2, const MMParamSP param)
    {
        if (msg != Component::kEventInfoBufferingUpdate || param1%20 == 0)
            MMLOGD("msg: %d, param1: %d, param2: %d, param: %p", msg, param1, param2, param.get());

        switch ( msg ) {
            case Component::kEventPrepareResult:
                g_prepared = 1;
                break;
            case Component::kEventStartResult:
                g_started = 1;
                INFO("player started");
                break;
            case Component::kEventPaused:
                g_started = 0;
                INFO("player paused");
                break;
            case Component::kEventStopped:
                g_stopped = 1;
                INFO("player stopped");
                break;
            case Component::kEventEOS:
                g_play_completed = 1;
                break;
            case Component::kEventResetComplete:
                g_reset = 1;
                break;
            case Component::kEventError:
                g_error_occured = 1;
                break;
            case Component::kEventGotVideoFormat:
                g_report_video_size = 1;
                break;
            default:
                break;
        }
    }
};

class SimpleCowPlayer : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

// wait until prepare is done
void waitUntilDone(int &done)
{
    int sleepCount = 0;
    while(!done && sleepCount++<100)
        usleep(50000);
}

#define CHECK_PLAYER_RET(status, command_str) do {                      \
        if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){     \
            MMLOGE("%s failed: %d", command_str, status);               \
            ret = -1;                                                   \
            goto error;                                                 \
        }                                                               \
    }while (0)

TEST_F(SimpleCowPlayer, simpleCowPlayer) {
    mm_status_t status;
    int ret = 0;
    std::map<std::string,std::string> header;

    MMLOGI("Hello SimpleCowPlayer");

    // create cowplayer & listener
    MyListener * listener = new MyListener();
    ASSERT_NE(listener, (void*)NULL);

    CowPlayer * player = new CowPlayer();
    if ( !player ) {
        MMLOGE("no mem");
        ret  = -1;
        goto error;
    }
    player->setListener(listener);

#if 0 // set rendering surface
        WindowSurface *ws = createSimpleSurface(800, 600);
        if (!ws) {
            PRINTF("init surface fail\n");
            exit(-1);
        }
        player->setVideoDisplay(ws);
#endif

    MMLOGI("settting datasource ...");
    status = player->setDataSource(g_video_file_path, &header);
    CHECK_PLAYER_RET(status, "setDataSource");

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    MMLOGI("set connectionId: %s\n", s_g_amHelper->getConnectionId());
    player->setAudioConnectionId(s_g_amHelper->getConnectionId());
#endif

    MMLOGI("prepare ...");
    status = player->prepareAsync();
    CHECK_PLAYER_RET(status, "prepareAsync");
    waitUntilDone(g_prepared);
    if(!g_prepared){
        MMLOGE("prepareAsync failed after wait 5 seconds");
        ret = -1;
        goto error;
    }

    MMLOGI("start ...");
    status = player->start();
    CHECK_PLAYER_RET (status, "start");

    while ( !g_play_completed && !g_error_occured ) {
        // check video resolution
        if(g_report_video_size){
            int width , height;
            status = player->getVideoSize(width, height);
            if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
                MMLOGE("getVideoSize error: %d", status);
            }
            else MMLOGI("get video size : %d x %d",width, height);
            g_report_video_size = 0;
        }

        sleep(1);
        g_play_time--;
        if (!g_play_time)
            break;
    }

    MMLOGI("stopping ...");
    player->stop();
    waitUntilDone(g_stopped);
    MMLOGI("resetting ...");
    player->reset();
    waitUntilDone(g_reset);

    error:
    MMLOGI("destroying ...");
    delete player;
    delete listener;
    // destroySimpleSurface(ws);
    EXPECT_EQ(ret, 0);
    MMLOGI("exit");
}

int parseCommandLine(int argc, char* const argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    const char* file_name = NULL;
    uint32_t playtime = g_play_time;
    static GOptionEntry entries[] = {
        {"add", 'a', 0, G_OPTION_ARG_STRING, &file_name, " set the file name to play", NULL},
        {"playtime", 'p', 0, G_OPTION_ARG_INT, &playtime, "quit after play # seconds", NULL},
        {NULL}
    };

    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (gchar***)&argv, &error)) {
            ERROR("option parsing failed: %s\n", error->message);
            return -1;
    }
    g_option_context_free(context);

    if (file_name) {
        g_video_file_path = file_name;
    }
    DEBUG("g_video_file_name: %s", g_video_file_path);

    if (playtime >= 0)
        g_play_time = playtime;

    return 0;
}
int main(int argc, char* const argv[]) {
    int ret = 0;

    parseCommandLine(argc, argv);

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    try {
        s_g_amHelper = new MMAMHelper();
        if (s_g_amHelper->connect() != MM_ERROR_SUCCESS) {
            MMLOGE("failed to connect audiomanger\n");
            delete s_g_amHelper;
            return -1;
        }
    } catch (...) {
        MMLOGE("failed to new amhelper\n");
        return -1;
    }
    MMLOGD("connect am success\n");
#endif

    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
    } catch (...) {
        ERROR("InitGoogleTest failed!");
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        s_g_amHelper->disconnect();
#endif
        return -1;
    }
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    s_g_amHelper->disconnect();
#endif
    return ret;
}
