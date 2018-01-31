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
#include <multimedia/webrtc/video_decode_plugin.h>
#include "native_surface_help.h"

//using namespace YunOS;

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include "multimedia/mm_debug.h"


MM_LOG_DEFINE_MODULE_NAME("webrtc_dec_test");

using namespace woogeen::base;
using namespace YUNOS_MM;

static const float ENC_FRAMERATE = 25.0;
static const int ENC_INTERVAL = 1000000 / ENC_FRAMERATE;
static const char * ENC_FILE_NAME = "/data/webrtc_enc.dump";

static woogeen::base::VideoDecoderInterface* s_g_decoder = NULL;
WindowSurface* ws = NULL;
uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;
#define DO_NOT_CREATE_SURFACE   1

static bool createDecoder()
{
    MMLOGI("+\n");
    s_g_decoder = VideoDecodePlugin::create(ws);
    if (!s_g_decoder) {
        MMLOGE("failed to create decoder\n");
        return false;
    }
    if (!s_g_decoder->InitDecodeContext(woogeen::base::MediaCodec::H264)) {
        MMLOGE("failed to InitDecodeContext\n");
        return false;
    }
    MMLOGI("+\n");
    return true;
}

static void destroyDecoder()
{
    MMLOGI("+\n");
    if (!s_g_decoder) {
        MMLOGI("not created\n");
        return;
    }

    s_g_decoder->Release();
    VideoDecodePlugin::destroy(s_g_decoder);
    s_g_decoder = NULL;
    MMLOGI("-\n");
}

static bool createSurface()
{
    MMLOGI("+\n");
#if DO_NOT_CREATE_SURFACE
    return true;
#else
    NativeSurfaceType surfaceType = NST_SimpleWindow;
    ws = createSimpleSurface(ENC_WIDTH, ENC_HEIGHT);
    //WINDOW_API(set_buffers_offset)(ws, 0, 0);
    //ret = WINDOW_API(set_scaling_mode)(ws, SCALING_MODE_PREF(SCALE_TO_WINDOW));
    MMLOGI("-\n");
    return ws != NULL;
#endif
}

static void destroySurface()
{
#if !DO_NOT_CREATE_SURFACE
    destroySimpleSurface(ws);
    ws = NULL;
#endif
}

class VideoDecodeFrame : public woogeen::base::VideoFrame {
  public:
      VideoDecodeFrame() {}
      virtual ~VideoDecodeFrame() {}
      virtual bool CleanUp();
};
bool VideoDecodeFrame::CleanUp()
{
    uint8_t* data = NULL;
    size_t data_size = 0;
    int64_t time_stamp = -1;
    static int releaseFrameCount = 0;

    if (GetFrameInfo(data, data_size, time_stamp)) {
        DEBUG("releaseFrameCount: %03d, data: %p, data_size: %zu, time_stamp: %" PRId64, releaseFrameCount, data, data_size, time_stamp);
        free(data);
        releaseFrameCount++;
    }

    SetFrameInfo(NULL, 0, -1);
    return true;
}

int main(int argc, char * argv[])
{
    MMLOGI("hello webrtc plugins\n");
    FILE * f = NULL;
    int32_t frameCount;

    if (!createSurface()) {
        goto out;
    }

    if (!createDecoder()) {
        goto out;
    }

    MMLOGI("extract frame\n");
    f = fopen(ENC_FILE_NAME, "rb");
    if (!f) {
        MMLOGE("Failed to open %s\n", ENC_FILE_NAME);
        goto out;
    }

    if (fread(&frameCount, 1, 4, f) != 4) {
        MMLOGE("failed to read\n");
        goto out;
    }
    MMLOGI("frame count: %d\n", frameCount);

    for (int32_t i = 0; i < frameCount; ++i) {
        size_t frameSize;
        uint8_t * frameBuf;
        bool ret;
        int64_t timeStamp;
        int32_t isKeyFrame;
        if (fread(&frameSize, 1, 4, f) != 4) {
            MMLOGE("failed to read framesize\n");
            goto out;
        }
        if (fread(&timeStamp, 1, 8, f) != 8) {
            MMLOGE("failed to read timeStamp\n");
            goto out;
        }
        if (fread(&isKeyFrame, 1, 4, f) != 4) {
            MMLOGE("failed to read timeStamp\n");
            goto out;
        }

        MMLOGI("size: %zu, ts: %" PRId64 "\n", frameSize, timeStamp);
        frameBuf = (uint8_t*)malloc(frameSize);
        if (!frameBuf)
            goto out;

        if (fread(frameBuf, 1, frameSize, f) != frameSize) {
            MMLOGE("failed to read\n");
            free(frameBuf);
            frameBuf = NULL;
            goto out;
        }
        hexDump(frameBuf, 64, 32);

        VideoDecodeFrame *frame = new VideoDecodeFrame();
        DEBUG("i: %03d, frame: %p, frameBuf: %p", i, frame, frameBuf);
        if (frame && frame->SetFrameInfo(frameBuf, frameSize, timeStamp) ){
            if (isKeyFrame)
                frame->SetFlag(woogeen::base::VideoFrame::VFF_KeyFrame);
            ret = s_g_decoder->OnFrame(frame);
            MMLOGV("dec ret: %d\n", ret);
        }

        frame = NULL;

        usleep(ENC_INTERVAL);
        timeStamp += ENC_INTERVAL;
    }

out:
    destroyDecoder();
    destroySurface();
    if (f) {
        fclose(f);
    }
    MMLOGI("bye\n");
    return 0;
}

