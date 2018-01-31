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
#include <string.h>
#include <string>

#include "multimedia/mediaplayer.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/component_factory.h"
#include "multimedia/pipeline.h"
#include "example_pipeline.h"
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#include "native_surface_help.h"
#endif
MM_LOG_DEFINE_MODULE_NAME("Example_PIPELINE_TEST")
static const int WAIT_FOR_COMPLETE = 1;


using namespace YUNOS_MM;

static int s_exit = 0;
static int s_g_error_occured = 0;
static int s_g_report_video_size =0;
uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;
const char *g_video_file_path = NULL;

void parseCommandLine(int argc, char **argv){
    int res;
    if (argc == 2) { // To continue support default output file
        g_video_file_path = argv[1];
        return;
    }

    while ((res = getopt(argc, argv, "i:w:h")) >= 0) {
        switch (res) {
            case 'i':
                g_video_file_path = optarg;
                break;
            case 'w':
                g_surface_width = optarg;
                break;
            case 'h':
                g_surface_height = optarg;
            default:
                MMLOGE("bad parameter!");
                break;
        }

    }
}


class MyListener : public MediaPlayer::Listener {
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj)
    {
        MMLOGD("msg: %d, param1: %d, param2: %d, obj: %p", msg, param1, param2, obj);
        switch ( msg ) {
            case MSG_PLAYBACK_COMPLETE:
            case    MSG_STOPPED:
                s_exit = 1;
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

int main(int argc, char** argv){
    mm_status_t status;
    int ret = 0;
    std::map<std::string,std::string> header;
    PipelineSP pipeline;

    MMLOGI("Hello example pipeline player play video\n");

    if (argc < 2) {
        MMLOGE("no input media file\n");
        return -1;
    }
    parseCommandLine(argc, argv);

    MMLOGI("\n test inpute file path = %s, appen xml path = \n", g_video_file_path);

#ifndef __MM_YUNOS_CNTRHAL_BUILD__
    std::string appendXml =  _APPEND_XML_PATH;
    appendXml.append("/usr/lib/");
    appendXml.append("example_plugins.xml");
    ComponentFactory::appendPluginsXml(appendXml.c_str());
#else
    ComponentFactory::appendPluginsXml("/etc/example_plugins.xml");
    ComponentFactory::appendPluginsXml("/usr/bin/ut/use_ffmpeg_plugins.xml");
#endif
    s_exit = 0;
    s_g_error_occured = 0;
    s_g_report_video_size = 0;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    WindowSurface *ws = createSimpleSurface(800, 600);
    if (!ws) {
        PRINTF("init surface fail\n");
        exit(-1);
    }
#endif
    MyListener * listener = new MyListener();

    MediaPlayer * player = MediaPlayer::create(MediaPlayer::PlayerType_COW);
    if ( !player ) {
        MMLOGE("no mem\n");
        ret  = -1;
        goto release;
    }
    pipeline = Pipeline::create(new ExamplePipelinePlayer());
    if (!pipeline) {
            MMLOGE("failed to create ExamplePipelinePlayer\n");
            goto release;
    }
    player->setPipeline(pipeline);
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    player->setVideoDisplay(ws);
#endif
    player->setListener(listener);

    MMLOGI("settting datasource  %s \n", g_video_file_path);
    status = player->setDataSource(g_video_file_path, &header);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setdatasource failed: %d\n", status);
        ret = -1;
        goto release;
    }

    MMLOGI("prepare\n");
    status = player->prepare();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("prepare failed: %d\n", status);
        ret = -1;
        goto release;
    }

    MMLOGI("start\n");
    status = player->start();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("start failed: %d\n", status);
        ret = -1;
        goto release;
    }
    usleep(100*1000);
    MMLOGI("call start over\n");

    if ( WAIT_FOR_COMPLETE ) {
        MMLOGI("waitting for complete\n");
        while ( !s_exit && !s_g_error_occured ) {
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
        sleep(20);
        MMLOGI("sleep over\n");
    }

    MMLOGI("stopping\n");
    player->stop();

    MMLOGI("resetting\n");
    player->reset();

    release:
    MMLOGI("destroying\n");
    MediaPlayer::destroy(player);
    delete listener;
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    destroySimpleSurface(ws);
#endif
    MMLOGI("exit\n");
    return ret;
}

