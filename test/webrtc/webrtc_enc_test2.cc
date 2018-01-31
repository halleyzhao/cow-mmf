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
#include "multimedia/mm_camera_compat.h"

#ifndef MM_LOG_OUTPUT_V
// #define MM_LOG_OUTPUT_V
#endif
#include "multimedia/mm_debug.h"
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

MM_LOG_DEFINE_MODULE_NAME("webrtc_enc_test");

using namespace YUNOS_MM;
using namespace YunOSCameraNS;

static uint32_t ENC_WIDTH = 1280;
static uint32_t ENC_HEIGHT = 720;
static const uint32_t ENC_FRAMERATE = 25;
static const uint32_t ENC_BITRATE = 8000000;
static int g_encodeFrameCount = 100;
static const int ENC_INTERVAL = 1000000 / ENC_FRAMERATE;
static const char * DUMP_FILE_NAME = "/data/webrtc_enc.dump";

static VideoCapture* s_g_camera = NULL;
static woogeen::base::VideoEncoderInterface * s_g_encoder = NULL;

void destroyCamera(VideoCapture* camera)
{
    FUNC_TRACK();
    if (camera) {
        camera->StopStream(STREAM_PREVIEW);
        delete camera;
        camera = NULL;
    }
}

VideoCapture* createCamera()
{
    FUNC_TRACK();
    VideoCapture* camera = NULL;
    int cameraId = 0;
#if MM_USE_CAMERA_VERSION>=20
    video_capture_device_version_e gDevVersion = DEVICE_ENHANCED;
    video_capture_api_version_e    gApiVersion = API_2_0;
    video_capture_version_t version = {gDevVersion, gApiVersion};
    #if MM_USE_CAMERA_VERSION>=30
        camera = VideoCapture::Create(cameraId, gApiVersion);
    #else
        camera = VideoCapture::Create(cameraId, &version);
    #endif
#else
    camera = VideoCapture::Create(cameraId);
#endif


    if (!camera) {
        MMLOGE("failed to create camera");
        return NULL;
    }
    VideoCaptureInfo info;
    VideoCapture::GetVideoCaptureInfo(cameraId, &info);
    MMLOGI("id %d, facing %d, orientation %d", cameraId, info.facing, info.orientation);
    camera->SetDisplayRotation(info.orientation);
    camera->SetStreamSize(STREAM_PREVIEW, ENC_WIDTH/4, ENC_HEIGHT/4);
    camera->SetStreamSize(STREAM_RECORD, ENC_WIDTH, ENC_HEIGHT);

    bool ret = false;
#if MM_USE_CAMERA_VERSION>=20
    int status = STATUS_OK;
    #if MM_USE_CAMERA_VERSION>=30
        int format = VIDEO_CAPTURE_FMT_YUV;
    #else
        int format = VIDEO_CAPTURE_FMT_YUV_NV21_PLANE;
    #endif
    StreamConfig previewconfig = {STREAM_PREVIEW, ENC_WIDTH/4, ENC_HEIGHT/4,
            format, 90, "camera"};
    StreamConfig recordConfig = {STREAM_RECORD, ENC_WIDTH, ENC_HEIGHT,
            format, 0, NULL};
    std::vector<StreamConfig> configs;
    configs.push_back(previewconfig);
    configs.push_back(recordConfig);
    camera->CreateVideoCaptureTaskWithConfigs(configs);

    status = camera->StartCustomStreamWithImageParam(STREAM_PREVIEW, NULL, NULL, true, true);
    DEBUG("status: %d", status);
    ret = (status ==0);
#else
    static const char* name = "hidden"; // disable camera preview for pad, other platform can do it as well.
    ret = camera->StartStream(STREAM_PREVIEW, NULL, name);
    setenv("MM_CAMERA_RECORD_PREVIEW", "1", 1);
#endif
    if (!ret) {
        MMLOGE("failed to StartStream\n");
        delete camera;
        return NULL;
    }

    return camera;
}

static bool createEncoder()
{
    FUNC_TRACK();
     s_g_camera = createCamera();
    s_g_encoder = VideoEncodePlugin::create(s_g_camera, NULL);
    if (!s_g_encoder->InitEncodeContext(woogeen::base::MediaCodec::H264,
        ENC_WIDTH, ENC_HEIGHT, ENC_BITRATE, ENC_FRAMERATE)) {
        MMLOGE("failed to InitEncodeContext ");
        return false;
    }
     return true;
}

static void destroyEncoder()
{
    FUNC_TRACK();
     if (s_g_encoder) {
        s_g_encoder->Release();
        VideoEncodePlugin::destroy(s_g_encoder);
        s_g_encoder = NULL;
    }

    if (s_g_camera) {
        destroyCamera(s_g_camera);
        s_g_camera = NULL;
    }
 }

int main(int argc, char * argv[])
{
    uint32_t frameCount = 0;
    FILE* dumpFile = NULL;

    MMLOGI("hello webrtc plugins");

    if (argc>1)
        g_encodeFrameCount = atoi(argv[1]);
    if (g_encodeFrameCount <30)
        g_encodeFrameCount = 30;

    if (!createEncoder()) {
        goto out;
    }

    dumpFile = fopen(DUMP_FILE_NAME, "wb");
    fwrite(&frameCount, 1, 4, dumpFile);

    MMLOGI("extract frame");
    for (int i = 0; i < g_encodeFrameCount; ++i) {
        woogeen::base::VideoFrame* frame = NULL;
        bool requestKeyFrame = i % 25 == 0;
        bool ret = s_g_encoder->EncodeOneFrame(frame, requestKeyFrame );
        if (frame) {
            uint8_t* data = NULL;
            size_t data_size = 0;
            int64_t time_stamp = -1;
            frame->GetFrameInfo(data, data_size, time_stamp);
            MMLOGI("frameCount: %03d, encframe data: %p, size: %d, time_stamp: %" PRId64, frameCount, data, data_size, time_stamp);
            // dump video stream to file
            do {
                if (fwrite(&data_size, 1, 4, dumpFile) != 4) {
                    MMLOGE("Failed to write file");
                    break;
                }
                if (fwrite(&time_stamp, 1, 8, dumpFile) != 8) {
                    MMLOGE("Failed to write file");
                    break;
                }
                int32_t isKey = frame->IsFlagSet(woogeen::base::VideoFrame::VFF_KeyFrame) ? 1 : 0;
                if (fwrite(&isKey, 1, 4, dumpFile) != 4) {
                    MMLOGE("Failed to write file");
                    break;
                }
                if (fwrite(data, 1, data_size, dumpFile) != (size_t)data_size) {
                    MMLOGE("Failed to write file");
                    break;
                }
            } while(0);

            woogeen::base::VideoFrame::ReleaseVieoFrame(frame);
            frameCount++;
        }
        usleep(ENC_INTERVAL);
    }

    // update frame count to dump file
    if (dumpFile) {
        MMLOGI("close file, framecount: %d", frameCount);
        fseek(dumpFile, 0, SEEK_SET);
        if (fwrite(&frameCount, 1, 4, dumpFile) != 4) {
            MMLOGE("Failed to write file");
        }
        fclose(dumpFile);
        dumpFile = NULL;
    }

out:
    destroyEncoder();
    MMLOGI("bye");
    return 0;
}

