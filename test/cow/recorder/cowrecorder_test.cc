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
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <glib.h>
#include <gtest/gtest.h>

#include <dirent.h>
#include <unistd.h>
 #include <sys/types.h>

typedef int status_t;
#define TIME_T  int64_t

#ifndef __MM_YUNOS_DRM_BUILD__
#include "native_surface_help.h"
#endif
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/cowrecorder.h"
#include "multimedia/component.h"
#include "multimedia/component_factory.h"
#include "multimedia/mediarecorder.h"
#include <multimedia/mmthread.h>
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "mmwakelocker.h"
#include "cutils/ywindow.h"
#endif
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
#include "native_surface_help.h"
#endif
#include "cowrecorder_helper.h"
#include "multimedia/mm_camera_compat.h"

#include "pipeline_ar_recorder.h"

using namespace YUNOS_MM;
using namespace YunOSCameraNS;

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "SimpleSubWindow.h"
#endif
#endif

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
#include <multimedia/mm_amhelper.h>
#endif
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
static MMAMHelper * s_g_amHelper = NULL;
#endif

#if 0
#undef DEBUG
#undef INFO
#undef WARNING
#undef ERROR
#undef MY_ERROR
#define DEBUG(format, ...)  fprintf(stderr, "[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define WARNING(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MY_ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif
#define MY_PROMPT(format, ...)  fprintf(stderr, format, ##__VA_ARGS__)
#define MY_ERROR(format, ...)  do {                         \
        ERROR(format, ##__VA_ARGS__);                   \
        MY_PROMPT(format, ##__VA_ARGS__);        \
    } while(0)

MM_LOG_DEFINE_MODULE_NAME("CowRecorder-Test")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()


#define CHECK_FREE_GSTRING(str) do{   \
    if (str) {            \
         g_free(str);     \
    }                     \
}while(0)

static bool g_quit = false;
static bool g_error_quit = false;

static bool g_is_audio = false;
static bool g_is_video = false;
static int32_t g_camera_id = 0;
static bool g_file_description = false;
static bool g_new_camera = false;
static int64_t g_max_file_size = 0;
static int64_t g_max_duration = 0; //second

uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;
int32_t loopedCount = 0;
bool g_loop_recorder_recreate_surface = true;
static uint32_t g_recorder_time = 0;
static bool g_record_complete = false;

static int32_t g_loop_count = 1000;
static bool g_loop_recorder = false;
static bool g_use_camera_source = false;
static double g_default_frame_rate = 30.0f; //30.0
static double g_capture_frame_rate = 0;
static int32_t g_video_hevc = 0;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
WindowSurface* ws = NULL;
#endif

class MyRecorder;
MMSharedPtr<MyRecorder> gRecorder;

//recorder
MMSharedPtr<RecordParam> g_video_param;
static bool g_use_camera_api2 = true;
static bool g_use_ffmpeg = false;
static bool g_use_raw_video_source = false;
static bool g_eis_enable = false;
static uint32_t g_preview_mode = 2; // 1: use record stream for preview, 2: use camera preview stream, 3: both preview window with record stream and preview stream
#define RECORD_STREAM_PREVIEW   1
#define CAMERA_STREAM_PREVIEW   2
#define RECORD_STREAM_PREVIEW2  4 // copy/convert camera buffer to a surface buffer and display it

#define CHACK_MSG_PARAM(event, param1) do{\
    if (param1 != MM_ERROR_SUCCESS) {\
        DEBUG("%s failed, %d\n", #event, param1);\
    } else {\
        DEBUG("%s success\n", #event);\
    }\
}while(0)

class TestListener : public MediaRecorder::Listener {
public:
    TestListener(){}
    ~TestListener()
     {
        MMLOGV("+\n");
     }

    virtual void onMessage(int msg, int param1, int param2, const MMParam *meta)
    {
        DEBUG("CPL-TEST %s, %d, msg %d\n", __FUNCTION__, __LINE__, msg);
        switch ( msg ) {
            case MSG_PREPARED:
                CHACK_MSG_PARAM(MSG_PREPARED, param1);
                break;
            case MSG_STARTED:
                CHACK_MSG_PARAM(MSG_STARTED, param1);
                break;
            case MSG_PAUSED:
                CHACK_MSG_PARAM(MSG_PAUSED, param1);
                break;
            case MSG_STOPPED:
                CHACK_MSG_PARAM(MSG_STOPPED, param1);
                break;
            case MSG_RECORDER_COMPLETE:
                INFO("MSG_RECORDER_COMPLETE\n");
                g_record_complete = true;
                break;
            case MSG_ERROR:
                MY_ERROR("MSG_ERROR, param1: %d\n", param1);
                MY_PROMPT("quiting due to error occur\n");
                g_error_quit = true;
                break;
            case MSG_INFO:
                INFO("MSG_INFO, param1: %d\n", param1);
                break;
            default:
                DEBUG("msg: %d, ignore\n", msg);
                break;
        }
    }
};

#define CHECK_RET_VALUE_RETURN(ret, func) do{ \
    if(ret !=MM_ERROR_SUCCESS  && ret != MM_ERROR_ASYNC){\
        MY_ERROR("%s fail, ret %d\n", #func, ret);\
        return ret;\
    }\
}while(0)

extern gboolean use_local_mediarecorder;
extern gboolean use_local_omx;

class MyRecorder
{
public:
    MyRecorder() : mPosThreadQuit(false)
                     , mPosThreadCreated(false)
                     , mFd(-1)
    {
        FUNC_TRACK();
        ASSERT(g_is_audio || g_is_video);
        if (g_is_audio && !g_is_video) {
            if (use_local_mediarecorder) {
                mm_set_env_str("host.media.recorder.type", NULL, "local");
            } else {
                mm_set_env_str("host.media.recorder.type", NULL, NULL);
            }

            mRecorder = MediaRecorder::create(RecorderType_COWAudio);
        } else if (g_is_video) {
            mRecorder = MediaRecorder::create(RecorderType_DEFAULT);
        }
        if (use_local_omx) {
            mm_set_env_str("host.omx.type", NULL, "local");
        } else {
            mm_set_env_str("host.omx.type", NULL, "proxy");
        }

        bool externalPipeline = mm_check_env_str("enable.native.recorder.pipeline",
            "ENABLE_NATIVE_RECORDER_PIPELINE", "1", false);
        if (externalPipeline) {
            DEBUG("use external Pipeline of recorder");
            PipelineSP pipeline = Pipeline::create(new PipelineArRecorder());
            ASSERT(pipeline);
            mRecorder->setPipeline(pipeline);
        }

        mrListener= new TestListener();
        memset(&mPosThreadId, 0, sizeof(mPosThreadId));
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
        ensureCamera();
#endif
        mMeta = MediaMeta::create();
        printUsage();
    }
    virtual ~MyRecorder(){
        FUNC_TRACK();

        //mRecorder->removeListener();
        MediaRecorder::destroy(mRecorder);
        delete mrListener;
        mCamera.reset();
        INFO("~MyRecorder done");
    }

    mm_status_t setSurface() {
        FUNC_TRACK();
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        mm_status_t ret = MM_ERROR_SUCCESS;
        void *surface = ws;

        if (!g_is_video)
            return MM_ERROR_SUCCESS;

        if (g_use_camera_source && !(g_preview_mode & RECORD_STREAM_PREVIEW || g_preview_mode & RECORD_STREAM_PREVIEW2))
            return MM_ERROR_SUCCESS;

        ASSERT(ws);
        if (g_preview_mode & RECORD_STREAM_PREVIEW) {
            surface = getBQProducer(ws).get();
        }

        ret = mRecorder->setPreviewSurface(surface);
        return ret;
#else
        MMLOGE("unsurppoted\n");
        return MM_ERROR_UNSUPPORTED;
#endif
    }

    static void printUsage() {
        if (g_is_video) {
            MY_PROMPT("\n\n\n######### usage ############\n");
            MY_PROMPT("now we are in video record mode\n");
        } else if (g_is_audio) {
            MY_PROMPT("\n\n\n######### usage ############\n");
            MY_PROMPT("now we are in audio record mode\n");
        }

        MY_PROMPT("input 'p' to pause recorder\n");
        MY_PROMPT("input 'r' to resume recorder\n");
        MY_PROMPT("input 't' to get current time\n");
        MY_PROMPT("input 's' to stop recorder\n");
        MY_PROMPT("input 'k' to request IDR frame\n");
        MY_PROMPT("input 'q' to quit cowrecorder-test\n");
        MY_PROMPT("##################################\n\n\n");
    }

    void ensureStopGetCurrentPosThread()
    {
        if (mPosThreadCreated) {
            void * aaa = NULL;
            mPosThreadQuit = true;
            pthread_join(mPosThreadId, &aaa);
            mPosThreadCreated = false;
        }
    }

    mm_status_t reset()
    {
        FUNC_TRACK();
        mm_status_t ret = MM_ERROR_SUCCESS;
        ensureStopGetCurrentPosThread();
        // ret = mRecorder->stop();//stop recording
        //CHECK_RET_VALUE_RETURN(ret, stop);
        ret = mRecorder->reset();
        CHECK_RET_VALUE_RETURN(ret, reset);

        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }

        return MM_ERROR_SUCCESS;
    }

    mm_status_t pause()
    {
        FUNC_TRACK();
        mm_status_t ret = mRecorder->pause();
        CHECK_RET_VALUE_RETURN(ret, pause);
        return MM_ERROR_SUCCESS;
    }

    mm_status_t resume(){
        FUNC_TRACK();
        mm_status_t ret = mRecorder->start();
        CHECK_RET_VALUE_RETURN(ret, start);
        return MM_ERROR_SUCCESS;
    }

    mm_status_t  test(){
        FUNC_TRACK();
        getPositionTest();
        return MM_ERROR_SUCCESS;
    }

   mm_status_t  requestIDR(){
        FUNC_TRACK();
        mMeta->setString(MEDIA_ATTR_IDR_FRAME, "yes");
        mm_status_t ret = mRecorder->setParameter(mMeta);

        return MM_ERROR_SUCCESS;
    }

   void getPositionTest() {
        mm_status_t status = MM_ERROR_UNKNOWN;

        if (mPosThreadCreated)
            return;

        if ( pthread_create(&mPosThreadId, NULL, getCurrentPositionThread, this) ) {
            MY_ERROR("failed to start thread\n");
            return;
        }
        mPosThreadCreated = true;

        MY_PROMPT("status: %d, recording %d\n", status, mRecorder->isRecording());
    }

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
    mm_status_t ensureCamera()
    {
        if (g_is_video && g_use_camera_source && !g_new_camera) {
            //create video capture
            // First check camera numbers
            int32_t numberCamera = VideoCapture::GetNumberOfVideoCapture();
            if (g_camera_id >= numberCamera) {
                fprintf(stderr, "numberCamera is %d, change camera id %d --> %d\n", numberCamera, g_camera_id, numberCamera - 1);
                g_camera_id = numberCamera - 1;
            }

        #if MM_USE_CAMERA_VERSION>=20
            video_capture_device_version_e gDevVersion = DEVICE_ENHANCED;
            video_capture_api_version_e    gApiVersion = API_1_0;
            if (g_use_camera_api2) {
                gApiVersion = API_2_0;
            }
            video_capture_version_t version = {gDevVersion, gApiVersion};
            #if MM_USE_CAMERA_VERSION>=30
                VideoCapture* camera = VideoCapture::Create(g_camera_id, gApiVersion);
            #else
                VideoCapture* camera = VideoCapture::Create(g_camera_id, &version);
            #endif
        #else
            VideoCapture* camera = VideoCapture::Create(g_camera_id);
        #endif
            mCamera.reset(camera);
            if (mCamera == NULL) {
                MY_ERROR("error to open camera id %d\n", g_camera_id);
                return MM_ERROR_UNKNOWN;
            }

            DEBUG("numbers of camera is %d\n", VideoCapture::GetNumberOfVideoCapture());

            VideoCaptureInfo info;
            VideoCapture::GetVideoCaptureInfo(g_camera_id, &info);
            DEBUG("id %d, info->facing %d, info->orientation %d\n", g_camera_id, info.facing, info.orientation);

            mCamera->SetDisplayRotation(info.orientation);
            mCamera->SetPreviewFrameRate(30, true);

            Size dSize;
            mCamera->GetStreamSize(YunOSCameraNS::STREAM_RECORD, dSize);
            INFO("using default size %dX%d\n", dSize.width, dSize.height);
            g_video_param->mVideoWidth = dSize.width;
            g_video_param->mVideoHeight = dSize.height;

          #ifndef __USING_USB_CAMERA__
            mCamera->SetStreamSize(YunOSCameraNS::STREAM_PREVIEW, g_video_param->mVideoWidth/4, g_video_param->mVideoHeight/4);
          #endif
        #ifdef HAVE_EIS_AUDIO_DELAY
            if (g_video_param->mVideoWidth == 1920 &&
                g_video_param->mVideoHeight == 1080) {
                mCamera->SetExtensionValue(0x801a000d, "1", true);
                DEBUG("enable EIS in 1920x1080");
                g_eis_enable = true;
            } else {
                mCamera->SetExtensionValue(0x801a000d, "0", true);
            }
        #endif
            if (!mCamera->IsPreviewEnabled()) {

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
                if (g_use_camera_api2) {
                    status_t status = STATUS_OK;
                    if (g_capture_frame_rate < 120 + 0.00001 &&
                        g_capture_frame_rate > 120 - 0.00001) {
                        mCamera->SetCaptureMode(2);
                        mCamera->SetPreviewFrameRate(120, true);
                        MY_PROMPT("enable slow motion, g_capture_frame_rate %f\n", g_capture_frame_rate);
                    }
                    #if MM_USE_CAMERA_VERSION>=30
                        int format = VIDEO_CAPTURE_FMT_YUV;
                    #else
                        int format = VIDEO_CAPTURE_FMT_YUV_NV21_PLANE;
                    #endif
                    // todo: do not set preview size, use default
                    StreamConfig previewconfig = {STREAM_PREVIEW, 1920, 1080,
                            format, 90, "camera"};
                    StreamConfig recordConfig = {STREAM_RECORD, int32_t(g_video_param->mVideoWidth), int32_t(g_video_param->mVideoHeight),
                            format, 0, NULL};
                    std::vector<StreamConfig> configs;
                    configs.push_back(previewconfig);
                    configs.push_back(recordConfig);
                    mCamera->CreateVideoCaptureTaskWithConfigs(configs);

                    status = mCamera->StartCustomStreamWithImageParam(STREAM_PREVIEW, NULL, NULL, true, true);
                    if (status) {
                        MY_ERROR("start preview stream failed\n");
                        // return MM_ERROR_UNKNOWN;
                    }
                } else
#endif
                {
                    bool ret = false;
                    // there should be some way to disable camera preview. for example: set name to "" or NULL; but not supported by camera server yet
                    static const char* name = "camera";
                    if (!(g_preview_mode & CAMERA_STREAM_PREVIEW))
                        name = "hidden";

                    ret = mCamera->StartStream(STREAM_PREVIEW, NULL, (SURFACE_TOKEN_TYPE)name);
                    if (!ret) {
                        MY_ERROR("start preview stream failed\n");
                        return MM_ERROR_UNKNOWN;
                    }
                }
            }
        }

        return MM_ERROR_UNKNOWN;
    }
#endif

    mm_status_t prepare(){
        FUNC_TRACK();
        mm_status_t ret;

        int usage = RU_None;
        if (g_is_audio) {
            usage |= RU_AudioRecorder;
        }
        if (g_is_video) {
            usage |= RU_VideoRecorder;
        }

        g_record_complete = false;
        DEBUG("usage %d\n", usage);
        ret = mRecorder->setRecorderUsage((RecorderUsage)usage);
        CHECK_RET_VALUE_RETURN(ret, setRecorderUsage);

        ret = mRecorder->setListener(mrListener);
        CHECK_RET_VALUE_RETURN(ret, setListener);

        mMeta->setFloat(MEDIA_ATTR_LOCATION_LATITUDE, 31.2154f);
        mMeta->setFloat(MEDIA_ATTR_LOCATION_LONGITUDE, 121.5301f);
        if (!g_eis_enable) {
            mMeta->setInt64(MEDIA_ATTR_START_DELAY_TIME, 300000);
        }
        ret = mRecorder->setParameter(mMeta);
        CHECK_RET_VALUE_RETURN(ret, setParameter);
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        if (strncmp(g_video_param->mAudioInputFilePath.c_str(), "rtsp://", 7)) {
            MMLOGI("set connectionId: %s\n", s_g_amHelper->getConnectionId());
            mRecorder->setAudioConnectionId(s_g_amHelper->getConnectionId());
        }
#endif

        if (g_max_file_size > 0) {
            DEBUG("set max file size %" PRId64 "\n", g_max_file_size);
            mRecorder->setMaxFileSize(g_max_file_size);
        }
        if (g_max_duration > 0) {
            mRecorder->setMaxDuration(g_max_duration * 1000l);
            DEBUG("set max file duration %" PRId64 "sec\n", g_max_duration);
        }

        if (g_is_video) {
            if (mCamera == NULL) {
                DEBUG("URI %s", g_video_param->mVideoInputFilePath.c_str());
                ret = mRecorder->setVideoSourceUri(g_video_param->mVideoInputFilePath.c_str());
                CHECK_RET_VALUE_RETURN(ret, setVideoSourceUri);
            }
            if (!strncmp(g_video_param->mAudioInputFilePath.c_str(), "rtsp://", 7)) {
                MMLOGI("audio is rtsp\n");
                ret = mRecorder->setAudioSourceUri(g_video_param->mAudioInputFilePath.c_str());
                CHECK_RET_VALUE_RETURN(ret, setAudioSourceUri);
            }

            ret = mRecorder->setVideoEncoder(g_video_param->mVideoMime.c_str());
            CHECK_RET_VALUE_RETURN(ret, setVideoEncoder);

            ret= mRecorder->setVideoSourceFormat(
                g_video_param->mVideoWidth,
                g_video_param->mVideoHeight,
                g_video_param->mVideoFormat);
            CHECK_RET_VALUE_RETURN(ret, setVideoSourceFormat);

            mMeta->setInt32(MEDIA_ATTR_ROTATION, 90);

            if (!strncmp(g_video_param->mVideoInputFilePath.c_str(), "surface", 7) ||
                !strncmp(g_video_param->mVideoInputFilePath.c_str(), "wfd", 3) ||
                !strncmp(g_video_param->mVideoInputFilePath.c_str(), "screen", 6)) {
                INFO("set fps to 24.0 for video source %s", g_video_param->mVideoInputFilePath.c_str());
                mMeta->setFloat(MEDIA_ATTR_FRAME_RATE, 24.0f);
                mMeta->setInt32(MEDIA_ATTR_ROTATION, 0);
            }

            if (g_use_raw_video_source) {
                DEBUG("using video raw data");
                mMeta->setInt32("raw-data", 1);
            }

            if (g_capture_frame_rate > 0.00001f) {
                mMeta->setInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, 1);
                mMeta->setFloat(MEDIA_ATTR_TIME_LAPSE_FPS, g_capture_frame_rate);

                // for debug, simulte capture frame rate is higher than video frame rate
                if (g_capture_frame_rate - g_default_frame_rate < 0.00001f &&
                    g_capture_frame_rate - g_default_frame_rate > -0.00001f) {
                    // g_default_frame_rate = 10.0f;
                }
                mMeta->setFloat(MEDIA_ATTR_FRAME_RATE, g_default_frame_rate);
                DEBUG("time lapse enable, g_capture_frame_rate: %0.2f, g_default_frame_rate %0.2f\n",
                    g_capture_frame_rate, g_default_frame_rate);
            }
            g_video_param->mVideoBitRate = g_video_param->mVideoWidth * g_video_param->mVideoHeight / 2 * 8;
            mMeta->setInt32(MEDIA_ATTR_BIT_RATE_VIDEO, g_video_param->mVideoBitRate);

            ret = mRecorder->setParameter(mMeta);
            CHECK_RET_VALUE_RETURN(ret, setParameter);
        }

        if (g_is_audio) {
            //mRecorder->setAudioSource(((RecordVideoParam*)g_video_param.get())->mAudioSourceUrl);
            ret = mRecorder->setAudioSourceUri(g_video_param->mAudioInputFilePath.c_str());
            CHECK_RET_VALUE_RETURN(ret, setAudioSourceUri);
            ret = mRecorder->setAudioEncoder(g_video_param->mAudioMime.c_str());
            CHECK_RET_VALUE_RETURN(ret, setAudioEncoder);
            mMeta->setInt32(MEDIA_ATTR_SAMPLE_RATE, g_video_param->mSampleRate);
            mMeta->setInt32(MEDIA_ATTR_CHANNEL_COUNT, g_video_param->mChannels);
            mMeta->setInt32(MEDIA_ATTR_BIT_RATE_AUDIO, g_video_param->mAudioBitRate);
            ret = mRecorder->setParameter(mMeta);
            CHECK_RET_VALUE_RETURN(ret, setParameter);
        }

        if (g_file_description) {
            char path[256] = {0};
            if (g_is_audio && !g_is_video) {
                memcpy(path, "/tmp/test.m4a", strlen("/tmp/test.m4a"));
            } else {
                memcpy(path, "/tmp/test.mp4", strlen("/tmp/test.mp4"));
            }

            mFd = ::open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
            if (mFd < 0) {
                MY_ERROR("open %s failed\n", path);
                return MM_ERROR_INVALID_PARAM;
            }

            ret = mRecorder->setOutputFile(mFd);

        } else {
            ret = mRecorder->setOutputFile(g_video_param->mRecorderOutputFilePath.c_str());
        }
        CHECK_RET_VALUE_RETURN(ret, setOutputFile);

        ret = mRecorder->setOutputFormat(g_video_param->mVideoExtension.c_str());
        CHECK_RET_VALUE_RETURN(ret, setOutputFormat);

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
        if (!g_new_camera && g_use_camera_source) {
            RecordingProxy *proxy = mCamera->getRecordingProxy();
            DEBUG("proxy %p, camera %p\n", proxy, mCamera.get());
            mRecorder->setCamera(mCamera.get(), proxy);
        }
#endif
        ret = mRecorder->prepare();
        CHECK_RET_VALUE_RETURN(ret, prepare);
        mRecorder->start();
        CHECK_RET_VALUE_RETURN(ret, start);

        if(g_use_ffmpeg){
            mRecorder->pause();
            ret = mRecorder->start();
            CHECK_RET_VALUE_RETURN(ret, start);
        }

        return ret;
    }

    static void* getCurrentPositionThread(void *context) {
        DEBUG("currentTimeTestThread enter\n");
        MyRecorder *recorder = static_cast<MyRecorder *>(context);
        if (!recorder) {
            return NULL;
        }

        int ret = 0;
        TIME_T msec;
        while (1) {
            if (recorder->mPosThreadQuit) {
                return NULL;
            }
            ret = recorder->getRecorder()->getCurrentPosition(&msec);
            if (ret != MM_ERROR_SUCCESS) {
                MY_ERROR("getCurrentPosition failed\n");
            }

            MY_PROMPT("current position %0.3f\n", msec/1000.0f);
            usleep(500*1000);//sleep 500ms for next getCurrentPosition
        }

        return NULL;
    }

    MediaRecorder* getRecorder(){
        return mRecorder;
    }

private:
    MediaRecorder* mRecorder;
    TestListener* mrListener;
    pthread_t mPosThreadId;
    bool mPosThreadQuit;
    bool mPosThreadCreated;
    MMSharedPtr<VideoCapture> mCamera;
    MediaMetaSP mMeta;
    int mFd;
};

void show_usage()
{
    MY_PROMPT("\n");
    MY_PROMPT("-m <mode> : mode: 'v'/'a'to enter audio/video pipeline\n");
    MY_PROMPT("-i <filename>    : get source from specific file.\n");
    MY_PROMPT("-a <filename>    : set recording file name.\n");
    MY_PROMPT("-l               : recording loop.\n");
    MY_PROMPT("-c <count>       : set recording loop count.\n");
    MY_PROMPT("-g <format>      : set output format, mp4/m4a/aac/mp3/amr\n");
    MY_PROMPT("-x               : use media recorder service : C/S\n");
    MY_PROMPT("-n               : create new camera instance when using host camera\n");
    MY_PROMPT("-v <1|0>         : use hevc as video codec\n");
    MY_PROMPT("\n");
    MY_PROMPT("runtime command:\n"
           "            g     : start recording \n"
           "            p     : pause recording\n"
           "            r     : resume recording\n"
           "            s     : stop recording\n"
           "            q     : quit recording\n"
           "            t     : some test, such as getCurrentPosition \n"
           "            m     : switch between front and back camera\n");
    MY_PROMPT("\n");
    MY_PROMPT("e.g.\n");
    MY_PROMPT("    cowrecorder-test -m v -i auto:// \n");
    MY_PROMPT("    cowrecorder-test -m v -i camera://0 \n");
    MY_PROMPT("    cowrecorder-test -m a -i cras://mix \n");
    MY_PROMPT("    cowrecorder-test -m av -i camera:// \n");
}

#define MAX_SOURCE_NAME_LEN 256
int get_size(const char* par)
{
    char sizeOption[MAX_SOURCE_NAME_LEN];

    if(strlen(par) >= MAX_SOURCE_NAME_LEN){
        MY_ERROR("get_source err: too long source name\n");
        return -1;
    }

    int rval = 0;
    const char* ptr = par;
    int param_len = 0;

    for(; *ptr != '\0'; ptr++){
        if(*ptr == ' ')
            break;
    }
    param_len = ptr - par;

    DEBUG("get_size: param len is %d\n", param_len);

    strncpy(sizeOption, par, param_len);
    sizeOption[param_len] = '\0';
    DEBUG("get_size: sizeOption %s\n", sizeOption);

    sscanf(sizeOption, "%dx%d", &g_video_param->mVideoWidth, &g_video_param->mVideoHeight);

    DEBUG("parse video size %dx%d\n", g_video_param->mVideoWidth, g_video_param->mVideoHeight);
    return rval;
}

char* trim(char *str)
{
    char *ptr = str;
    int i;

    // skip leading spaces
    for (; *ptr != '\0'; ptr++)
        if (*ptr != ' ')
            break;

    //skip (-a    xxx) to xxx
    for(; *ptr != '\0'; ptr++){
        if(*ptr != ' ')
            continue;
        for(; *ptr != '\0'; ptr++){
            if(*ptr == ' ')
                continue;
            break;
        }
        break;
    }

    // remove trailing blanks, tabs, newlines
    for (i = strlen(str) - 1; i >= 0; i--)
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
            break;

    str[i + 1] = '\0';
    return ptr;
}

char getRandomOp()
{
    const int randomOpMin = 0;
    const int randomOpMax = 4;
    char randomOps[]={'p','r','s','l','t'};
    uint32_t i = rand()%(randomOpMax-randomOpMin+1)+randomOpMin;
    sleep(2);
    return randomOps[i];
}

void RunMainLoop()
{
    char ch;
    char buffer[128] = {0};
    static int flag_stdin = 0;
    char buffer_back[128] = {0};
    ssize_t back_count = 0;
    ssize_t count = 0;
    uint32_t  recordtime = 0;

    flag_stdin = fcntl(STDIN_FILENO, F_GETFL);
    if(fcntl(STDIN_FILENO, F_SETFL, flag_stdin | O_NONBLOCK)== -1)
        MY_ERROR("stdin_fileno set error");

    while (1)
    {
        if(g_quit)
            break;

        if (g_error_quit) {
            MY_PROMPT("got error, quiting...\n");
            gRecorder->reset();
            g_error_quit = false;
            break;
        }
        memset(buffer, 0, sizeof(buffer));
        if ((count = read(STDIN_FILENO, buffer, sizeof(buffer)-1) < 0)) {
            usleep(100000);
            recordtime += 100000;
            if ((g_recorder_time && recordtime > g_recorder_time * 1000*1000) ||g_record_complete ) {
                buffer[0] = 'q';
                DEBUG("recorded %d seconds, quit ...", g_recorder_time);
            } else
                continue;
        }

        buffer[sizeof(buffer) - 1] = '\0';
        count = strlen(buffer);
        if (count)
            INFO("read return %zu bytes, %s\n", count, buffer);
        ch = buffer[0];
        if(ch == '-'){
            if (count < 2)
                continue;
             else
                ch = buffer[1];
        }
        if(ch == '\n' && buffer_back[0] != 0){
            DEBUG("Repeat Cmd:%s", buffer_back);//buffer_back has a line change
            memcpy(buffer, buffer_back, sizeof(buffer));
            ch = buffer[0];
            if(ch == '-'){
                if (back_count < 2)
                    continue;
                else
                    ch = buffer[1];
            }
        }else{
            memset(buffer_back, 0, sizeof(buffer_back));
            //DEBUG("deb%sg\n", buffer_back);
            memcpy(buffer_back, buffer, sizeof(buffer));
            back_count = count;
        }
        switch (tolower(ch)) {
        case 'q':    // exit
            DEBUG("quit...\n");
            MY_PROMPT("quiting...\n");
            gRecorder->reset();
            g_quit = true;
            break;

        case 'p':
            gRecorder->pause();
            break;

        case 'r':
            gRecorder->resume();
            break;

        case 't':
            gRecorder->test();
            break;
        case 'k':
            gRecorder->requestIDR();
            break;
        case 's':
            gRecorder->reset();
            break;

        case 'm':
        {
            const char *tmp = g_video_param->mVideoInputFilePath.c_str();
            if (!strncmp(tmp, CAMERA, strlen(CAMERA))) {
                gRecorder->reset();

                if (!strncmp(tmp, BACK_CAMERA, strlen(BACK_CAMERA))) {
                    DEBUG("camera back --> front\n");
                    g_video_param->mVideoInputFilePath = FRONT_CAMERA;
                } else {
                    DEBUG("camera front --> back\n");
                    g_video_param->mVideoInputFilePath = BACK_CAMERA;
                }
                g_camera_id = (g_camera_id == 0) ? 1 : 0;

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
                gRecorder->setSurface();
#endif
                gRecorder->prepare();
            }

            break;
        }
        default:
            VERBOSE("Unkonw cmd line:%c\n", ch);
            break;
        }
    }

    if(fcntl(STDIN_FILENO,F_SETFL,flag_stdin) == -1)
        DEBUG("stdin_fileno set error");
    DEBUG("\nExit RunMainLoop on yunostest.\n");
}

static const char *output_url = NULL;
static const char *input_url = NULL;
static const char *input_url_audio = NULL;
static const char *record_mode = NULL;
static const char *sizestr = NULL;
static const char *format = NULL;
static const bool loop_record =false;
static const int loop_count = 0;
static const char *output_format = NULL;
#ifdef __USING_RECORDER_SERVICE__
gboolean use_local_mediarecorder = FALSE;
#else
gboolean use_local_mediarecorder = TRUE;
#endif
gboolean use_local_omx = FALSE;

const static GOptionEntry entries[] = {
    {"output_url", 'a', 0, G_OPTION_ARG_STRING, &output_url, "Set output filepath", NULL},
    {"input_url", 'i', 0, G_OPTION_ARG_STRING, &input_url,  "Set input filepath", NULL},
    {"input_url_audio", 'j', 0, G_OPTION_ARG_STRING, &input_url_audio,  "Set audio input url", NULL},
    {"record_mode", 'm', 0, G_OPTION_ARG_STRING, &record_mode, "Set record mode: v--video,  a--audio", NULL},
    {"size", 's', 0, G_OPTION_ARG_STRING, &sizestr, "Set video size", NULL},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &format, "Set video color format", NULL},
    {"loop", 'l', 0, G_OPTION_ARG_NONE, &g_loop_recorder, "Set loop record", NULL},
    {"loop_count", 'c', 0, G_OPTION_ARG_INT,&g_loop_count, "Set loop count", NULL},
    {"output_format", 'g', 0, G_OPTION_ARG_STRING, &output_format, "Set output format", NULL},
    {"use local mediarecorder", 'x', 1, G_OPTION_ARG_NONE, &use_local_mediarecorder, "use local media recorder", NULL},
    {"use local omx", 'y', 1, G_OPTION_ARG_NONE, &use_local_omx, "use local omx", NULL},
    {"new_camera", 'n', 0, G_OPTION_ARG_NONE, &g_new_camera, "create new camera", NULL},
    {"file description mode", 'h', 0, G_OPTION_ARG_NONE, &g_file_description, "set file description", NULL},
    {"max file size limit", 'z', 0, G_OPTION_ARG_INT64, &g_max_file_size, "set max file size limit", NULL},
    {"max duration limit", 't', 0, G_OPTION_ARG_INT64, &g_max_duration, "set max duration limit", NULL},
    {"max duration limit", 'T', 0, G_OPTION_ARG_INT, &g_recorder_time, "set record duration", NULL},
    {"capture frame rate", 'r', 0, G_OPTION_ARG_DOUBLE, &g_capture_frame_rate, "get capture frame rate", NULL},
    {"use HEVC for video", 'v', 0, G_OPTION_ARG_INT, &g_video_hevc, "set video codec", NULL},
    {"use ffmpeg", 'p', 0, G_OPTION_ARG_NONE, &g_use_ffmpeg, "use ffmpeg componet", NULL},
    {"use raw video source", 'w', 0, G_OPTION_ARG_NONE, &g_use_raw_video_source, "use video raw", NULL},
    {"preview mode", 'd', 2, G_OPTION_ARG_INT, &g_preview_mode, "1:record stream preview, 2: preview stream, 3: both", NULL},
    {NULL}
};

class CowrecorderTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(CowrecorderTest, cowrecorderTest) {
    int ret = MM_ERROR_SUCCESS;

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    ws = NULL;
#endif
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    if(g_use_ffmpeg){
        ComponentFactory::appendPluginsXml("/usr/bin/ut/use_ffmpeg_plugins.xml");
    }
#endif


BEGIN_OF_LOOP_FROM_CREATE_SURFACE:
    if (g_is_video) {
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
        if (!g_use_camera_source || g_preview_mode & RECORD_STREAM_PREVIEW) {
            ws = createSimpleSurface(1280, 720);
            ASSERT_NE(ws, (void*)NULL);
        }
#endif
    }

BEGIN_OF_LOOP_FROM_CREATE_RECORDER:
    gRecorder.reset(new MyRecorder());

    if (g_is_video) {
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        ret = gRecorder->setSurface();
#endif
        EXPECT_TRUE(ret == MM_ERROR_SUCCESS);
    }

    gRecorder->prepare();
    if (g_loop_recorder) {
        if (g_recorder_time)
            usleep(g_recorder_time*1000*1000);
        else
            usleep(15*1000*1000);//recorder 15s, then call quit
        gRecorder->reset();
    } else {
        RunMainLoop();
    }

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
    usleep(10000);

#endif

    //getThreadInProc();
    INFO("delete gRecorder...\n");
    gRecorder.reset();
    INFO("delete gRecorder done, loopedCount %d\n", loopedCount);

    if (!g_loop_recorder || g_loop_recorder_recreate_surface) {
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
        if (ws) {
            destroySimpleSurface(ws);
            ws = NULL;
            DEBUG("delete ws done\n");
        }
#endif
    }

    DEBUG();
    if (g_loop_recorder) {
        usleep(2*1000*1000);

    #ifdef __MM_YUNOS_CNTRHAL_BUILD__
        if (!g_loop_recorder_recreate_surface && !g_use_camera_source && ws) {
            DEBUG("call WindowSurface destroyBuffers()\n");
            ((WindowSurface*)ws)->destroyBuffers();
        }
    #endif

        loopedCount++;
        MY_PROMPT("loopedCount: %d, g_loop_count: %d", loopedCount, g_loop_count);
        if (loopedCount < g_loop_count) {
             if (g_loop_recorder_recreate_surface)
                goto BEGIN_OF_LOOP_FROM_CREATE_SURFACE;
             else
                goto BEGIN_OF_LOOP_FROM_CREATE_RECORDER;
        }
    }
#ifndef __MM_NATIVE_BUILD__
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    cleanupSurfaces();
#endif
#endif
    DEBUG();
}

extern "C" int main(int argc, char* const argv[]) {
    DEBUG("CowRecorder test for YunOS!\n");
    if (argc < 2) {
        MY_ERROR("Must specify play file \n");
        show_usage();
        return 0;
    }

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (char ***)&argv, &error)) {
        MY_ERROR("option parsing failed: %s\n", error->message);
        return -1;
    }
    g_option_context_free(context);

    g_video_param.reset(new RecordParam);
#ifdef __USING_CAMERA_API2__
    g_use_camera_api2 = mm_check_env_str("mm.camera.api2.enable", "MM_CAMERA_API2_ENABLE", "1", true);
#else
    g_use_camera_api2 = false;
#endif
    if (record_mode) {
        DEBUG("record_mode %s\n", record_mode);

        if (strstr(record_mode, "v")) {
            g_is_video = true;
        }

        if (strstr(record_mode, "a")) {
            g_is_audio = true;
        }

        if (!g_is_video && !g_is_audio) {
            MY_ERROR("invalid cmd\n");
            return -1;
        }
    }else {
        MY_ERROR("Should set recorder mode\n");
        return -1;
    }

    DEBUG("g_is_video %d, g_is_audio %d\n",
           g_is_video, g_is_audio);

    if (input_url ) {
        g_video_param->mVideoInputFilePath = input_url;
        DEBUG("set input file %s \n ",input_url);
    }

    if (input_url_audio ) {
        g_video_param->mAudioInputFilePath = input_url_audio;
        DEBUG("set audio input url %s \n ",g_video_param->mAudioInputFilePath.c_str());
    }

    if (g_video_hevc) {
        MY_PROMPT("use HEVC codec\n");
        g_video_param->mVideoMime = "video/hevc";
    }

    DEBUG("g_use_ffmpeg: %d", g_use_ffmpeg);
    DEBUG("g_preview_mode: %d", g_preview_mode);

    //force audio input to cras://mix
    if (!input_url_audio)
        g_video_param->mAudioInputFilePath = "cras://mix";

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
    if (!strncmp(g_video_param->mVideoInputFilePath.c_str(),
        CAMERA, strlen(CAMERA))) {
        g_use_camera_source = true;
        DEBUG("using nested surface\n");
        if (!strncmp(g_video_param->mVideoInputFilePath.c_str(),
        FRONT_CAMERA, strlen(FRONT_CAMERA))) {
            g_camera_id = 1;
        } else if (!strncmp(g_video_param->mVideoInputFilePath.c_str(),
        FRONT_CAMERA, strlen(FRONT_CAMERA))) {
            g_camera_id = 0;
        }
        DEBUG("open camera id : %d\n", g_camera_id);
    } else if (!strncmp(g_video_param->mVideoInputFilePath.c_str(),
        WFD, strlen(WFD))) {
        g_video_param->mAudioInputFilePath = "cras://remote_submix";
    }
#endif

    if (output_url) {
        g_video_param->mRecorderOutputFilePath = output_url;
        DEBUG("set output file %s \n ",output_url);
    }

    if (sizestr) {
        if (get_size(sizestr) < 0) {
            MY_ERROR("get size fail \n");
            return -1;
        }
    }

    if (format) {
        if (!strcmp(format, "I420") || !strcmp(format, "YV12")) {
            g_video_param->mVideoFormat = 'YV12';
        } else if (!strcmp(format, "YV21")) {
            g_video_param->mVideoFormat = 'YV21';
        } else if (!strcmp(format, "NV12")) {
            g_video_param->mVideoFormat = 'NV12';
        } else if (!strcmp(format, "NV21")) {
            g_video_param->mVideoFormat = 'NV21';
        } else if (!strcmp(format, "YUY2") || !strcmp(format, "YUYV")) {
            g_video_param->mVideoFormat = 'YUYV';
        } else if (!strcmp(format, "YVYU")) {
            g_video_param->mVideoFormat = 'YVYU';
        } else {
            MY_ERROR("unsupported video input format: %s", format);
            return -1;
        }
    }

    //FIXME: Don't set format to output_format, which will cause to crash in gtest
    const char *out_format = output_format;
    if (g_is_audio && !g_is_video && !out_format) {
        out_format = "m4a";
    } if (g_is_video && !out_format) {
        out_format = "mp4";
    }
    if (out_format) {
        g_video_param->mVideoExtension = out_format;

        //determine audio encoder mime type according to output format
        if (!strcmp(out_format, "amr") ||
            !strcmp(out_format, "3gp")) {
            g_video_param->mAudioMime = MEDIA_MIMETYPE_AUDIO_AMR_NB;
            g_video_param->mSampleRate = 8000;
            g_video_param->mChannels = 1;
            g_video_param->mAudioBitRate = 12200;
        } else if (!strcmp(out_format, "mp4") ||
            !strcmp(out_format, "m4a")) {
            g_video_param->mAudioMime = MEDIA_MIMETYPE_AUDIO_AAC;
        } else if (!strcmp(out_format, "mp3")) {
            g_video_param->mAudioMime = MEDIA_MIMETYPE_AUDIO_MPEG;
        } else if (!strcmp(out_format, "opus")) {
            g_video_param->mAudioMime = MEDIA_MIMETYPE_AUDIO_OPUS;
            g_video_param->mSampleRate = 16000;
            g_video_param->mChannels = 1;
        }
        MY_ERROR("set output format %s, audio mime %s\n", out_format, g_video_param->mAudioMime.c_str());
    }

    int ret;

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
    // AutoWakeLock awl;
#endif
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    try {
        s_g_amHelper = new MMAMHelper();
        if (s_g_amHelper->connect(MMAMHelper::recordChnnelMic()) != MM_ERROR_SUCCESS) {
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
        MY_ERROR("InitGoogleTest failed!");
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        s_g_amHelper->disconnect();
#endif
        ret = -1;
    }

    CHECK_FREE_GSTRING((char*)input_url);
    CHECK_FREE_GSTRING((char*)output_url);
    CHECK_FREE_GSTRING((char*)record_mode);
    CHECK_FREE_GSTRING((char*)sizestr);
    CHECK_FREE_GSTRING((char*)format);
    CHECK_FREE_GSTRING((char*)output_format);

    // reset to null
    mm_set_env_str("host.media.recorder.type", NULL, NULL);
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    s_g_amHelper->disconnect();
#endif

    return ret;
}


