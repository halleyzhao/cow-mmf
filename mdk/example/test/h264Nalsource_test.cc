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
#include "multimedia/component.h"
#include "multimedia/component_factory.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"
#include <libavutil/pixfmt.h>

#include "h264Nal_source.h"

using namespace YUNOS_MM;

static const char *input_file = NULL;
static const char *output_file = "/tmp/";
static SourceComponent* source;
static ComponentSP decoder;
static ComponentSP sink;
static int32_t g_width = 176;
static int32_t g_height = 144;

MM_LOG_DEFINE_MODULE_NAME("H264NalSource-Test");


void parseCommandLine(int argc, char **argv){
    int res;
    if (argc == 2) { // To continue support default output file
        input_file = argv[1];
        return;
    }

    while ((res = getopt(argc, argv, "i:o:w:h")) >= 0) {
        switch (res) {
            case 'i':
                input_file = optarg;
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'w':
                g_width = optarg;
                break;
            case 'h':
                g_height = optarg;
            default:
                MMLOGE("bad parameter!");
                break;
        }

    }
}

static void release(){

   if(source) {
       delete source;
   }
}

int main(int argc, char** argv) {

    if (argc < 2) {
        MMLOGE("no input media file\n");
        return -1;
    }

    parseCommandLine(argc, argv);
    MMLOGI("\n test inpute file path = %s\n", input_file);

    source = new H264NalSource();
    source->setUri(input_file);
    decoder = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_VIDEO_AVC, false);

    mm_status_t status;
    status = decoder->init();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("decoder init fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("decoder init ASYNC\n");
            usleep(10*1000);
        }
    }

    MediaMetaSP paramSP(MediaMeta::create());
    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_WIDTH, g_width);
    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_HEIGHT,g_height);
    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_COLOR_FORMAT,(int)AV_PIX_FMT_YUV420P);
    status = decoder->setParameter(paramSP);
    if (status != MM_ERROR_SUCCESS) {
        MMLOGE("decoder setparameter fail %d \n", status);
        release();
        return -1;
    }

    sink = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_MEDIA_FILE_SINK, false);
    paramSP->setString(YUNOS_MM::MEDIA_ATTR_FILE_PATH,output_file);
    status = sink->setParameter(paramSP);
    if (status != MM_ERROR_SUCCESS) {
        MMLOGE("sink setparameter fail %d \n", status);
        release();
        return -1;
    }

    status = source->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("source prepare fail %d \n", status);
            release();
            return -1;
        }
        MMLOGD("source  prepare ASYNC \n");
        usleep(10*1000);
    }

    status = decoder->addSource(source, Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("decoder add source fail %d \n", status);
            release();
            return -1;
        }
        MMLOGD("decoder add source ASYNC");
        usleep(10*1000);
    }
    printf("decoder had been addSource\n");

    status = decoder->addSink(sink.get(), Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("decoder add sink fail %d \n", status);
            release();
            return -1;
        }
            MMLOGD("decoder add sink ASYNC\n");
            usleep(10*1000);
    }

    printf("decoder had been addSink\n");


    status = decoder->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("decoder prepare fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("decoder prepare ASYNC, wait 10ms for prepare completion\n");
            usleep(10*1000);
        }
    }

    status = sink->init();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("sink init fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("decoder add sink ASYNC\n");
        }
    }

    status = sink->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("sink prepare fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("sink prepare ASYNC, wait 10ms for prepare completion\n");
            usleep(10*1000);
        }
    }

    status = source->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("source start fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("source start ASYNC\n");
            usleep(10*1000);
        }
    }

    status = decoder->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("decoder start fail %d \n", status);
            release();
            return -1;
        }
        else{
            MMLOGD("decoder start ASYNC\n");
            usleep(10*1000);
        }
    }

    status = sink->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            MMLOGE("sink start fail %d \n", status);
            release();
            return -1;
        }
        else{
             MMLOGD("sink start ASYNC\n");
             usleep(10*1000);
        }
    }

    MMLOGD("wait to end \n");
    sleep(10);
    MMLOGD("sleep over...\n");

    decoder->stop();
    usleep(100*1000);
    MMLOGD("decoder stoped \n");
    decoder->reset();
    usleep(100*1000);
    MMLOGD("decoder reseted \n");
    decoder->uninit();
    usleep(100*1000);
    MMLOGD("decoder uninited \n");
    source->stop();
    MMLOGD("source stoped \n");

    sink->stop();
    MMLOGD("sink stoped \n");

    sink->uninit();
    MMLOGD("sink uninited \n");

    delete source;
    return 0;
}

