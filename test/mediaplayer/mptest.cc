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
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "mmwakelocker.h"

#include "native_surface_help.h"

MM_LOG_DEFINE_MODULE_NAME("MPTEST")

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

static const int WAIT_FOR_COMPLETE = 1;

static const char *g_audio_file_path = "/usr/bin/ut/res/audio/sp.mp3";
static const char *g_video_file_path = "/usr/bin/ut/res/video/test.mp4";

using namespace YUNOS_MM;

static int s_g_prepared = 0;
static int s_g_play_completed = 0;
static int s_g_error_occured = 0;
static int s_g_report_video_size =0;

class MyListener : public MediaPlayer::Listener {
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj)
    {
        if (msg != MSG_BUFFERING_UPDATE || param1%20 == 0)
            MMLOGD("msg: %d, param1: %d, param2: %d, obj: %p", msg, param1, param2, obj);

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

TEST_F(MediaplayerTest, mediaplayer_playaudio) {
    mm_status_t status;
    int ret = 0;
    MediaPlayer::VolumeInfo vol;
    int duration = -1;
    std::map<std::string,std::string> header;

    MMLOGI("Hello mediaplayer audio\n");
    s_g_prepared = 0;
    s_g_play_completed = 0;
    s_g_error_occured = 0;
    MyListener * listener = new MyListener();
    ASSERT_NE(listener, NULL);

    MediaPlayer * player = MediaPlayer::create(MediaPlayer::PlayerType_COWAudio);
    if ( !player ) {
        MMLOGE("no mem\n");
        ret  = -1;
        goto error;
    }
    player->setListener(listener);

    MMLOGI("settting datasource\n");

    status = player->setDataSource(g_audio_file_path, &header);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setdatasource failed: %d\n", status);
        ret = -1;
        goto error;
    }

    MMLOGI("prepare\n");
    status = player->prepareAsync();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("prepare failed: %d\n", status);
        ret = -1;
        goto error;
    }
    // wait until prepare is done
    waitUntilPrepareDone();
    if(!s_g_prepared){
        MMLOGE("prepareAsync failed after wait 5 seconds\n");
        ret = -1;
        goto error;
    }

    MMLOGI("setVolume\n");
    vol.left = vol.right = 0.5;
    status = player->setVolume(vol);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setVolume failed: %d\n", status);
    }
    {
        MMLOGI("getVolume\n");
        MediaPlayer::VolumeInfo volget = {0};
        status = player->getVolume(&volget);
        if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
            MMLOGE("getVolume failed: %d\n", status);
        }
        MMLOGI("volget.left = %d, volget.right=%d \n ",volget.left,volget.right);

        MMLOGI("setParameter\n");
        int32_t play_rate = MediaPlayer::PLAYRATE_NORMAL;
        MediaMetaSP meta = MediaMeta::create();
        MMLOGI("set playRate %d\n", play_rate);
        meta->setInt32(MEDIA_ATTR_PALY_RATE, play_rate);
        player->setParameter(meta);
        MMLOGI("getParameter\n");
        status =  player->getParameter(meta);
        if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
            MMLOGE("getParameter failed: %d\n", status);
        }

        int32_t play_rate_get ;
        ret = !(meta->getInt32(MEDIA_ATTR_PALY_RATE, play_rate_get));
        MMLOGI("play_rate_get = %d\n",play_rate_get);
    }

    MMLOGI("start\n");
    status = player->start();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("start failed: %d\n", status);
        ret = -1;
        goto error;
    }

    MMLOGI("call start over\n");

    MMLOGI("call getDuration\n");
    status = player->getDuration(&duration);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("getDuration error: %d\n", status);
        ret = -1;
        goto error;
    }
    MMLOGI("getDuration over: %d\n", duration);

    MMLOGI("getAudioStreamType\n");
    MediaPlayer::as_type_t stream_type;
    status = player->getAudioStreamType(&stream_type);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("getAudioStreamType error: %d\n", status);
    }
    MMLOGD("getAudioStreamType is %d\n",stream_type);
    {
        player->setLoop(true);
        MMLOGI("set loop\n");
        bool loop = player->isLooping();
        MMLOGD("check islooping is %d\n", loop);
        player->setLoop(false);
        MMLOGI("cancle loop\n");
        loop = player->isLooping();
        MMLOGD("check islooping is %d\n", loop);

        MMLOGI("check isPlaying\n");
        bool playing = player->isPlaying();
        MMLOGI("is playing is %d\n",playing);
    }
    //unspported now,just for cover api
    {
        MMLOGI("set mute\n");
        player->setMute(true);
        MMLOGI("get mute\n");
        bool mute;
        player->getMute(&mute);
        player->invoke(NULL, NULL);
    }
    if ( WAIT_FOR_COMPLETE ) {
        MMLOGI("waitting for complete\n");
        while ( !s_g_play_completed && !s_g_error_occured ) {
            sleep(3);
        }
    } else {
        MMLOGI("sleep for a while\n");
        sleep(20);
        MMLOGI("sleep over\n");
    }

    MMLOGI("stopping\n");
    player->stop();

    MMLOGI("resetting\n");
    player->reset();

    error:
    MMLOGI("destroying\n");
    MediaPlayer::destroy(player);
    delete listener;
    EXPECT_EQ(ret, 0);
    MMLOGI("exit\n");
}

uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;

TEST_F(MediaplayerTest, mediaplayerplayvideo) {
    mm_status_t status;
    int ret = 0;
    MediaPlayer::VolumeInfo vol;
    int duration = -1;
    std::map<std::string,std::string> header;

    MMLOGI("Hello mediaplayer video\n");
    AutoWakeLock awl;

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
        MMLOGE("no mem\n");
        ret  = -1;
        goto error;
    }

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    player->setVideoDisplay(ws);
#endif
    player->setListener(listener);

    MMLOGI("settting datasource\n");

    status = player->setDataSource(g_video_file_path, &header);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setdatasource failed: %d\n", status);
        ret = -1;
        goto error;
    }

    MMLOGI("prepare\n");
    status = player->prepareAsync();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("prepare failed: %d\n", status);
        ret = -1;
        goto error;
    }
    // wait until prepare is done
    waitUntilPrepareDone();
    if(!s_g_prepared){
        MMLOGE("prepareAsync failed after wait 5 seconds\n");
        ret = -1;
        goto error;
    }

    MMLOGI("setVolume\n");
    vol.left = vol.right = 0.5;
    status = player->setVolume(vol);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setVolume failed: %d\n", status);
    }
    {
        MMLOGI("setParameter\n");
        float play_rate = 1.0f;
        MediaMetaSP meta = MediaMeta::create();
        printf("set playRate %.2f\n", play_rate);
        meta->setFloat(MEDIA_ATTR_PALY_RATE, play_rate);
        player->setParameter(meta);
        MMLOGI("getParameter\n");
        status =  player->getParameter(meta);
        if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
            MMLOGE("getParameter failed: %d\n", status);
        }

        float play_rate_get ;
        ret = meta->getFloat(MEDIA_ATTR_PALY_RATE, play_rate_get);
        MMLOGI("play_rate_get = %d\n",play_rate_get);
    }
    MMLOGI("start\n");
    status = player->start();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("start failed: %d\n", status);
        ret = -1;
        goto error;
    }

    MMLOGI("call start over\n");

    MMLOGI("call getDuration\n");
    status = player->getDuration(&duration);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("getDuration error: %d\n", status);
        ret = -1;
        goto error;
    }
    MMLOGI("getDuration over: %d\n", duration);
    //unspported now,just for cover api
    player->captureVideo();

    if ( WAIT_FOR_COMPLETE ) {
        MMLOGI("waitting for complete\n");
        while ( !s_g_play_completed && !s_g_error_occured ) {
            if(s_g_report_video_size){
                int width , height;
                status = player->getVideoSize(&width, &height);
                if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
                    MMLOGE("getVideoSize error: %d\n", status);
                }
                else MMLOGI("get video size : %d x %d \n",width, height);
                s_g_report_video_size = 0;
            }
            sleep(3);
        }
    } else {
        MMLOGI("sleep for a while\n");
        sleep(200);
        MMLOGI("sleep over\n");
    }

    MMLOGI("stopping\n");
    player->stop();

    MMLOGI("resetting\n");
    player->reset();

    error:
    MMLOGI("destroying\n");
    MediaPlayer::destroy(player);
    delete listener;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    destroySimpleSurface(ws);
#endif
    EXPECT_EQ(ret, 0);
    MMLOGI("exit\n");
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
