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
#include <multimedia/webrtc/video_encode_plugin.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include "multimedia/mm_debug.h"


MM_LOG_DEFINE_MODULE_NAME("webrtc_enc_test");

using namespace YUNOS_MM;

static uint32_t ENC_WIDTH = 1280;
static uint32_t ENC_HEIGHT = 720;
static const uint32_t ENC_FRAMERATE = 25;
static const uint32_t ENC_BITRATE = 8000000;
static int g_encodeFrameCount = 100;
static const int ENC_INTERVAL = 1000000 / ENC_FRAMERATE;

static woogeen::base::VideoEncoderInterface* s_g_encoder = NULL;

static bool createEncoder()
{
    MMLOGI("+\n");
    s_g_encoder = VideoEncodePlugin::create();
    if (!s_g_encoder->InitEncoderContext(
        woogeen::base::MediaCodec::H264,
        ENC_WIDTH, ENC_HEIGHT,
        ENC_BITRATE,
        ENC_FRAMERATE)) {
        MMLOGE("failed to InitEncodeContext\n");
        return false;
    }
    MMLOGI("+\n");
    return true;
}

static void destroyEncoder()
{
    MMLOGI("+\n");
    if (!s_g_encoder) {
        MMLOGI("not created\n");
        return;
    }

    s_g_encoder->Release();
    VideoEncodePlugin::destroy(s_g_encoder);
    s_g_encoder = NULL;
    MMLOGI("-\n");
}

int main(int argc, char * argv[])
{
    MMLOGI("hello webrtc plugins\n");
    setenv("MM_WEBRTC_UT_DUMP", "1", 1);

    if (argc>1)
        g_encodeFrameCount = atoi(argv[1]);
    if (g_encodeFrameCount <30)
        g_encodeFrameCount = 30;

    if (!createEncoder()) {
        goto out;
    }

    MMLOGI("extract frame\n");
    for (int i = 0; i < g_encodeFrameCount; ++i) {
        std::vector<uint8_t> buffer;
        bool keyFrame = i % 25 == 0;
        bool ret = s_g_encoder->EncodeOneFrame(buffer, 0/* FIXME, should be uint32& (not uint32) to get timestamp */, keyFrame);
        int32_t sz = (int32_t)buffer.size();
        MMLOGI("i: %03d, encframe result: %d, size: %d", i, ret, sz);
        usleep(ENC_INTERVAL);
    }

out:
    destroyEncoder();
    MMLOGI("bye\n");
    return 0;
}

