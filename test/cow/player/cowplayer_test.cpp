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

typedef int status_t;
#include "multimedia/media_attr_str.h"

#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/component.h"
#include "multimedia/component_factory.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mmmsgthread.h"
#ifdef USE_COWPLAYER
    #include "multimedia/cowplayer.h"
    #ifdef USE_TEST_PIPELINE
    #include "pipeline_player_test.h"
    #endif
#else
    #include "multimedia/mediaplayer.h"
#endif

#include "native_surface_help.h"
//#include <WindowSurface.h>

#ifndef __MM_NATIVE_BUILD__
#define MM_USE_SURFACE_TEXTURE
#include "media_surface_texture.h"
#include "mmwakelocker.h"
#endif
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
#include <multimedia/mm_amhelper.h>
#endif

#ifdef __USING_SYSCAP_MM__
#include <yunhal/VideoSysCapability.h>

using yunos::VideoSysCapability;
using yunos::VideoSysCapabilitySP;
#endif
#define TERM_PROMPT(format, ...)  fprintf(stderr, format "\n", ##__VA_ARGS__)
#if 0
#undef DEBUG
#undef INFO
#undef WARNING
#undef ERROR
#undef TERM_PROMPT
#define DEBUG(format, ...)  fprintf(stderr, "[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define WARNING(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define TERM_PROMPT(...)
#endif
#define DEBUG_IDX(format, ...) DEBUG("idx: %d, " format, mDraw, ##__VA_ARGS__);
#define INFO_IDX(format, ...) INFO("idx: %d, " format, mDraw, ##__VA_ARGS__);
#define WARN_IDX(format, ...) WARNING("idx: %d, " format, mDraw, ##__VA_ARGS__);
#define ERROR_IDX(format, ...) ERROR("idx: %d, " format, mDraw, ##__VA_ARGS__);

#define MY_PROMPT(format, ...)  do {                         \
        INFO(format, ##__VA_ARGS__);                   \
        TERM_PROMPT(format, ##__VA_ARGS__);        \
    } while(0)
#define MY_ERROR(format, ...)  do {                         \
        ERROR(format, ##__VA_ARGS__);                   \
        TERM_PROMPT(format, ##__VA_ARGS__);        \
    } while(0)

using namespace YUNOS_MM;
#ifdef USE_COWPLAYER
    #define MediaPlayer CowPlayer
    #define NORMAL_PLAYBACK_RATE        32
#else
    #define NORMAL_PLAYBACK_RATE        MediaPlayer::PLAYRATE_NORMAL
#endif

MM_LOG_DEFINE_MODULE_NAME("MediaPlayer-VideoTest")
typedef long	    TTime;
#define IMITATE_WEBENGINE_SEQUENCE

#ifdef MM_UT_SURFACE_WIDTH
uint32_t g_surface_width = MM_UT_SURFACE_WIDTH;
#else
uint32_t g_surface_width = 640;
#endif

#ifdef MM_UT_SURFACE_HEIGHT
uint32_t g_surface_height = MM_UT_SURFACE_HEIGHT;
#else
uint32_t g_surface_height = 480;
#endif

int g_player_c = 1;


uint32_t g_video_width = -1;
uint32_t g_video_height = -1; // assumption
bool g_animate_win = false;
// FIXME: sub surface doesn't show video (data goes normally), while main surface shows. seems pagewindow/gui bug
bool g_use_pagewindow = false; //use pagewindow with bufferqueue implementation, main surface is used by default. (otherwise, set g_use_sub_surface)
bool g_use_sub_surface = false;
bool g_disable_hw_render = false;
bool g_use_media_texture = false;
bool g_use_window_surface = false;
bool g_show_surface = false;
bool g_auto_test = false;
bool g_random_test = true; // auto test with random command
bool g_loop_player = false;
bool g_loop_player_recreate_player = false;
bool g_loop_player_recreate_surface = false;
uint32_t g_play_time = 0;
uint32_t g_wait_time = 2;
bool g_use_egl_draw_texture = false;

bool g_setDataSource_fd = false;
int32_t loopedCount = 0;
int32_t g_loop_count = 100;

int32_t g_play_rate_arr_size = 11;
int32_t g_cur_play_rate = 32; // 32 is normal playback
static bool g_quit = false;
static bool g_user_quit = false;
static bool g_eos = false; // FIXME, use one for each player
static bool g_use_ffmpeg = false;
// simple synchronization between main thread and player listener; assumed no lock is required
typedef struct OperationSync {
    bool done;
    int64_t timeout;
} OperationSync;
std::vector<OperationSync> g_opSync;
// we uses WindowSurface* though yunos::libgui::BaseNativeSurface and wl_surface are different type,
// otherwise, there are gui exception; the reason may be gui surface derive from multiple base classes
std::vector<WindowSurface*>ws;
const static char *file_name = NULL;
const static char *subtitle_file_name = NULL;
static gboolean animation = FALSE;
static char *sizestr = NULL;
const static char* autotest_input = NULL;
static int loopplay = -1;
static int loopcount = -1;
static int surfacetype = 0;
static int playtime = 0;
static int waittime = 2;
static int player_c = 1;
static gboolean set_dataSource_with_fd = FALSE;
static int rotationWinDegree = 0;

#if defined(__USING_PLAYER_SERVICE__)
gboolean use_localmediaplayer = false;
#else
gboolean use_localmediaplayer = true;
#endif

gboolean use_local_omx = false;

gboolean g_keep_egl = false;
static bool use_ffmpeg = false;
static bool gl_filter_test = false;
static bool g_use_low_delay = false;
static bool g_use_thumbnail_mode = false;
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
static MMAMHelper * s_g_amHelper = NULL;
#endif

static GOptionEntry entries[] = {
    {"add", 'a', 0, G_OPTION_ARG_STRING, &file_name, " set the file name to play", NULL},
    {"add subtitle", 'd', 0, G_OPTION_ARG_STRING, &subtitle_file_name, " set the subtitle file name to play", NULL},
    {"animation", 'm', 0, G_OPTION_ARG_NONE, &animation,  "animate moving video sub win", NULL},
    {"winsize", 's', 0, G_OPTION_ARG_STRING, &sizestr, "set the video size, e.g. \"1280x720\"", NULL},
    {"autotest", 't', 0, G_OPTION_ARG_STRING, &autotest_input, "use auto test ", NULL},
    {"loopPlayer", 'l', 0, G_OPTION_ARG_INT, &loopplay, "set loop play", NULL},
    {"loopCount", 'c', 0, G_OPTION_ARG_INT, &loopcount, "set loop play count", NULL},
    {"mediaTexture", 'w', 0, G_OPTION_ARG_INT, &surfacetype, "set surface type", NULL},
    {"playtime", 'p', 0, G_OPTION_ARG_INT, &playtime, "each play time when loop play", NULL},
    {"waittime", 'g', 0, G_OPTION_ARG_INT, &waittime, "the interval of two play when loop play", NULL},
    {"setDataSourceWithFd", 'f', 0, G_OPTION_ARG_NONE, &set_dataSource_with_fd, "set datasource with fd", NULL},
    {"player_c", 'n', 0, G_OPTION_ARG_INT, &player_c, "the player count", NULL},
    {"forceWinRotation", 'r', 0, G_OPTION_ARG_INT, &rotationWinDegree, "force window rotation", NULL},
    {"local mediaplayer", 'x', 0, G_OPTION_ARG_NONE, &use_localmediaplayer, "use local media player", NULL},
    {"use local omx", 'y', 1, G_OPTION_ARG_NONE, &use_local_omx, "use local omx", NULL},
    {"keep egl context", 'k', 0, G_OPTION_ARG_NONE, &g_keep_egl, "keep elg context when loop playing", NULL},
    {"use ffmpeg", 'v', 0, G_OPTION_ARG_NONE, &use_ffmpeg, "use ffmpeg componet", NULL},
    {"gl filter test", 'b', 0, G_OPTION_ARG_NONE, &gl_filter_test, "add gl filter between video decoder and sink", NULL},
    {"codec low delay", 'z', 0, G_OPTION_ARG_NONE, &g_use_low_delay, "codec works on low delay mode", NULL},
    {"decode thumbnail", 'u', 0, G_OPTION_ARG_NONE, &g_use_thumbnail_mode, "use thumbnail mode", NULL},
    {NULL}
};

using namespace YunOSMediaCodec;
std::vector<MediaSurfaceTexture*> mst;
static Lock g_draw_anb_lock;
void initMediaTexture(int n) {
    if (!g_use_media_texture) {
        mst[n] = NULL;
        return;
    }

    if (!mst[n]) {
        mst[n] = new MediaSurfaceTexture();
        DEBUG("create mst[%d]: %p", n, mst[n]);
    }
}
WindowSurface* initEgl(int width, int height, int x, int y, int win);
bool drawBuffer(MMNativeBuffer* anb, int win);
void eglUninit(int win, bool destroySurface);

int angle_to_ws_transform(int degree) {

#ifndef __MM_YUNOS_YUNHAL_BUILD__
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    if (degree == 0)
        return 0;
    else if (degree == 90)
        return HAL_TRANSFORM_ROT_90;
    else if (degree == 180)
        return HAL_TRANSFORM_ROT_180;
    else if (degree == 270)
        return HAL_TRANSFORM_ROT_270;
    else
#endif
#endif
        return 0;
}

static int rotationWsTransform = 0;



class TestListener;

class MstThread : public MMMsgThread {
public:
    MstThread(TestListener* p): MMMsgThread("MST-Thread") {
        MMLOGI("+\n");
        mOwner = p;
    }
    ~MstThread()
    {
        MMLOGI("+\n");
        MMMsgThread::exit();
        MMLOGI("-\n");
    }

#define UPDATE 1

    void updateTexImage(int index) {
        postMsg(UPDATE, index, NULL);
    }

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onUpdateTexImage)

    TestListener* mOwner;
};

BEGIN_MSG_LOOP(MstThread)
    MSG_ITEM(UPDATE, onUpdateTexImage)
END_MSG_LOOP()


class TestListener : public MediaPlayer::Listener {
public:
    // FIXME, mEglInited belongs to window, not player
    TestListener(int draw):mDraw(draw),mFrameCount(0),mEglInited(false), mSurface(NULL), mThread(NULL) {}
    ~TestListener()
     {
        if (mThread)
            delete mThread;

        MMLOGV("+\n");
     }

     int mDraw;
     int mFrameCount;
     bool mEglInited;
     WindowSurface* mSurface;
     MstThread *mThread;

#ifdef USE_COWPLAYER
#define MSG_PREPARED                Component::kEventPrepareResult
#define MSG_STARTED                 Component::kEventStartResult
#define MSG_PAUSED                  Component::kEventPaused
#define MSG_STOPPED                 Component::kEventStopped
#define MSG_SEEK_COMPLETE           Component::kEventSeekComplete
#define MSG_SET_VIDEO_SIZE          Component::kEventGotVideoFormat
#define MSG_PLAYBACK_COMPLETE       Component::kEventEOS
#define MSG_BUFFERING_UPDATE        Component::kEventInfoBufferingUpdate
#define MSG_SUBTITLE_UPDATED        Component::kEventInfoSubtitleData
#define MSG_ERROR                   Component::kEventError
#define MSG_UPDATE_TEXTURE_IMAGE    Component::kEventUpdateTextureImage
#define TIME_T  int64_t
#define GET_REFERENCE(x)    (x)
#define TEST_MediaTypeUnknown   Component::kMediaTypeUnknown
#define TEST_MediaTypeVideo     Component::kMediaTypeVideo
#define TEST_MediaTypeAudio     Component::kMediaTypeAudio
#define TEST_MediaTypeTimedText Component::kMediaTypeSubtitle
#define TEST_MediaTypeSubtitle  Component::kMediaTypeImage
#define TEST_MediaTypeCount     Component::kMediaTypeCount
#else
#define TIME_T  int32_t
#define GET_REFERENCE(x)    (&(x))
#define TEST_MediaTypeUnknown   MediaPlayer::TRACK_TYPE_UNKNOWN
#define TEST_MediaTypeVideo     MediaPlayer::TRACK_TYPE_VIDEO
#define TEST_MediaTypeAudio     MediaPlayer::TRACK_TYPE_AUDIO
#define TEST_MediaTypeTimedText MediaPlayer::TRACK_TYPE_TIMEDTEXT
#define TEST_MediaTypeSubtitle  MediaPlayer::TRACK_TYPE_SUBTITLE
#define TEST_MediaTypeCount     (MediaPlayer::TRACK_TYPE_SUBTITLE+1)
#endif

//#define TEST_RETURN_ACQUIRE

    void draw(int param1) {
        VERBOSE("MSG_UPDATE_TEXTURE_IMAGE, param1: %d\n", param1);
        if (g_use_egl_draw_texture && !mEglInited) {
            ASSERT(g_use_media_texture);
            INFO_IDX("first frame, init egl ...");
            if (g_player_c > 1) {
                mSurface = initEgl(320, 240, 100*mDraw, 100*mDraw, mDraw);
    #ifdef MM_ENABLE_PAGEWINDOW
                if (rotationWsTransform)
                    native_set_buffers_transform(mSurface, rotationWsTransform);
    #endif
            }
            else {
                mSurface = initEgl(720, 405, 0, 200, mDraw);

    #ifdef MM_ENABLE_PAGEWINDOW
                if (!g_show_surface)
                    pagewindowShow(g_show_surface);

                if (rotationWsTransform)
                    native_set_buffers_transform(mSurface, rotationWsTransform);
    #endif
            }
            mEglInited = true;
        }

        // draw video as texture
        MMNativeBuffer* anb = NULL;
        anb = mst[mDraw]->acquireBuffer(param1);

        if (!anb)
            return;

        if (mFrameCount%29 == 0)
            INFO_IDX("MSG_UPDATE_TEXTURE_IMAGE, count: %d, param1: %d\n", mFrameCount, param1);

        if (g_use_egl_draw_texture) {
            drawBuffer(anb, mDraw);
        }

        #ifdef TEST_RETURN_ACQUIRE
        if (mFrameCount == 105 || mFrameCount == 140) {
            mFrameCount++;
            INFO_IDX("acquireBuffer %p, but not return buffer", anb);
            return;
        }
        #endif
        int ret = 0;
        ret = mst[mDraw]->returnBuffer(anb);

        if (mFrameCount%29 == 0) // use a prime number to be able to show all anb
            INFO_IDX("ret: %d, acquireBuffer %p", ret, anb);
        mFrameCount++;
    }

#ifdef USE_COWPLAYER
    virtual void onMessage(int msg, int param1, int param2, const MMParamSP param)
#else
    virtual void onMessage(int msg, int param1, int param2, const MMParam *param)
#endif
    {
        if (msg != MSG_BUFFERING_UPDATE || param1 % 20 == 0)
            DEBUG("CPL-TEST %s, %d, msg %d\n", __FUNCTION__, __LINE__, msg);
        switch ( msg ) {
            case MSG_PREPARED:
                INFO_IDX("MSG_PREPARED: param1: %d\n", param1);
                if (param1 == MM_ERROR_SUCCESS) {
                    g_opSync[mDraw].done = true;
                }
                break;
#ifdef USE_COWPLAYER
            case Component::kEventResumed:
#endif
            case MSG_STARTED:
                INFO_IDX("MSG_STARTED: param1: %d\n", param1);
                if (param1 == MM_ERROR_SUCCESS) {
                    g_opSync[mDraw].done = true;
                }
                break;
            case MSG_SET_VIDEO_SIZE:
                INFO_IDX("MSG_SET_VIDEO_SIZE: param1: %d, param2: %d\n", param1, param2);
                if ((g_video_width != (uint32_t)param1) ||
                    (g_video_height != (uint32_t)param2)) {
                    INFO_IDX("width %d-->%d, height %d-->%d", g_video_width, param1, g_video_height, param2);
                    g_video_width = param1;
                    g_video_height = param2;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
                    // does resize here, possible for fit
                    if (ws[mDraw] && !g_use_pagewindow) {
                        yunos::libgui::SurfaceController* control = getSurfaceController((WindowSurface*)ws[mDraw]);
                        if (control)
                            control->resize(g_video_width, g_video_height);
                    }
#endif
                }

                break;
            case MSG_PAUSED:
                MY_PROMPT("MSG_PAUSEED: param1: %d\n", param1);
                if (param1 == MM_ERROR_SUCCESS) {
                    g_opSync[mDraw].done = true;
                }
                break;

            case MSG_STOPPED:
                INFO_IDX("MSG_STOPPED: param1: %d\n", param1);
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
                DEBUG_IDX("mEglInited: %d", mEglInited);
                if (mEglInited) {
                    ASSERT(g_use_media_texture && g_use_egl_draw_texture);
                    DEBUG_IDX("unint egl\n");
                    MMAutoLock locker(g_draw_anb_lock);

                    bool destroyWindow = true;
                    #if 0 // FIXME, only destroy the window for the last loop, there is crash in wayland
                    if (g_loop_player && loopedCount < g_loop_count)
                        destroyWindow = false;
                    #endif

                    if (g_keep_egl)
                        destroyWindow = false;

                    eglUninit(mDraw, destroyWindow);
                    DEBUG_IDX("unint egl done\n");
                    mEglInited = false;
                }
#endif
                g_opSync[mDraw].done = true;
                break;
             case MSG_SEEK_COMPLETE:
                INFO_IDX("MSG_SEEK_COMPLETE\n");
                break;
             case MSG_PLAYBACK_COMPLETE:
                INFO_IDX("MSG_PLAYBACK_COMPLETE, EOS\n");
                g_eos = true;
                break;
             case MSG_BUFFERING_UPDATE:
                VERBOSE("MSG_BUFFERING_UPDATE: param1: %d\n", param1);
                break;
            case MSG_ERROR:
                INFO_IDX("MSG_ERROR, param1: %d\n", param1);
                break;
            case MSG_SUBTITLE_UPDATED:
                if (param) {
                    const char* format = param->readCString();
                    const char* text = param->readCString();
                    INFO(" MSG_SUBTITLE_UPDATED: %s\n", text);
                }
                break;
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
            case MSG_UPDATE_TEXTURE_IMAGE:
                {
                    MMAutoLock locker(g_draw_anb_lock);

                    if (!use_localmediaplayer) {
                        if (!mThread) {
                            mThread = new MstThread(this);
                            mThread->run();
                        }
                        mThread->updateTexImage(param1);
                    } else {
                        draw(param1);
                    }
                }
                break;
#endif
            default:
                INFO_IDX("msg: %d, ignore\n", msg);
                break;
        }
    }
};

void MstThread::onUpdateTexImage(param1_type param1, param2_type param2, uint32_t rspId)
{
    ASSERT_NEED_RSP(!rspId);
    ASSERT(mOwner);

    if(mOwner)
        mOwner->draw(param1);

    postReponse(rspId, 0, 0);
}

#define CHECK_PLAYER_OP_RET(OPERATION, RET) do {               \
    if(RET != MM_ERROR_SUCCESS && RET != MM_ERROR_ASYNC) {      \
        ERROR_IDX("%s fail, ret=%d\n", OPERATION, RET);             \
        return RET;                                             \
    }                                                           \
} while(0)

#define ENSURE_PLAYER_OP_SUCCESS_PREPARE() do { \
    g_opSync[mDraw].done = false;                         \
    g_opSync[mDraw].timeout = 0;                             \
} while(0)

#define ENSURE_PLAYER_OP_SUCCESS(OPERATION, RET, MY_TIMEOUT) do {  \
    if(RET != MM_ERROR_SUCCESS && RET != MM_ERROR_ASYNC){           \
        ERROR_IDX("%s failed, ret=%d\n", OPERATION, RET);               \
        return RET;                                                 \
    }                                                               \
    while (!g_opSync[mDraw].done) {                                           \
        usleep(10000);                                              \
        g_opSync[mDraw].timeout += 10000;                                        \
        if (g_opSync[mDraw].timeout > (MY_TIMEOUT)*1000*1000) {                  \
            ERROR_IDX("%s timeout", OPERATION);                               \
            return MM_ERROR_IVALID_OPERATION;                       \
        }                                                           \
    }                                                               \
}while (0)

class MyPlayer
{
public:
    MyPlayer(int draw)
      : mpUrl(NULL)
      , mFd(-1)
      , mDraw(draw)
      , mPosThreadQuit(false)
     , mPosThreadCreated(false)
    {
#ifdef USE_COWPLAYER
        INFO_IDX("use CowPlayer directly");
        mpPlayer = new CowPlayer();

        #ifdef USE_TEST_PIPELINE
        if (mm_check_env_str("debug.cowplayer.test_pipeline", "COWPLAYER_USE_TEST_PIPELINE")) {
            INFO_IDX("use Test Pipeline of CowPlayer");
            PipelineSP pipeline = Pipeline::create(new PipelinePlayerTest());
            ASSERT(pipeline);
            mpPlayer->setPipeline(pipeline);
        }
        #endif
#else
        if (use_localmediaplayer)
            mpPlayer = MediaPlayer::create(MediaPlayer::PlayerType_COW);
        else
            mpPlayer = MediaPlayer::create(MediaPlayer::PlayerType_PROXY);
#endif

        if (use_local_omx) {
            mm_set_env_str("host.omx.type", NULL, "local");
        } else {
            mm_set_env_str("host.omx.type", NULL, "proxy");
        }

        mpListener= new TestListener(draw);
        mMeta = MediaMeta::create();

        memset(&mPosThreadId, 0, sizeof(mPosThreadId));
#ifdef __USING_SYSCAP_MM__
        VideoSysCapabilitySP cap = VideoSysCapability::create();
        ASSERT(cap);
        DEBUG("%d %d", cap->getMaxWidthSupportedByPlayer(), cap->getMaxHeightSupportedByPlayer());
        int32_t surfaceWidth = cap->getScreenSurfaceWidth();
        int32_t surfaceHeight = cap->getScreenSurfaceHeight();
        DEBUG("surfaceWidth %d, surfaceHeight %d", surfaceWidth, surfaceHeight);
        if (surfaceWidth > 0 && surfaceHeight > 0) {
            g_surface_width = surfaceHeight;
            g_surface_height = surfaceWidth;
            DEBUG("g_surface_width %d, g_surface_height %d", g_surface_width, g_surface_height);
        }
#endif
    }
    virtual ~MyPlayer(){
        DEBUG_IDX();
        if (mFd >= 0) {
            close(mFd);
            mFd = -1;
        }
#ifdef USE_COWPLAYER
        mpPlayer->removeListener();
        mpPlayer->stop();
        mpPlayer->reset();
        delete mpPlayer;
#else
        MediaPlayer::destroy(mpPlayer);
#endif
        delete mpListener;
        DEBUG_IDX("~MyPlayer done");
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

    mm_status_t setDisplayName(const char* name){
        DEBUG_IDX();
        return mpPlayer->setDisplayName(name);
    }

    mm_status_t  setNativeDisplay(void* display){
        DEBUG_IDX();
        return mpPlayer->setNativeDisplay(display);
    }

    mm_status_t  setSurface(int n){
        DEBUG_IDX();
        void *sur = ws[n];
#ifdef USE_COWPLAYER
        return mpPlayer->setVideoSurface(sur);
#else
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_CNTRHAL_BUILD__)
        if (!use_localmediaplayer)
            return mpPlayer->setVideoDisplay((void*)getWindowName((WindowSurface*)sur).c_str());
        else
            return mpPlayer->setVideoDisplay(sur);
#endif
#endif
    }

    mm_status_t setSurfaceTexture(void* sur){
        DEBUG_IDX();
#ifdef USE_COWPLAYER
        return mpPlayer->setVideoSurface(sur, true);
#else
        return mpPlayer->setVideoSurfaceTexture(sur);
#endif
    }

    mm_status_t quit()
    {
        mm_status_t ret = MM_ERROR_SUCCESS;
        DEBUG_IDX();
        // stop and deinit (clean up egl when there is msg of StopResult
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->stop();
        ENSURE_PLAYER_OP_SUCCESS("stop", ret, 10);

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
        if (g_use_media_texture) {
            ASSERT(mst[mDraw]);
            mst[mDraw]->returnAcquiredBuffers();
        }
#endif
        DEBUG_IDX();
        // usleep(100000);
        ensureStopGetCurrentPosThread();

        mpPlayer->reset();
        DEBUG_IDX();
        // mpPlayer->release();
        return ret;
    }

    mm_status_t pause()
    {
        DEBUG_IDX();
        mm_status_t ret = MM_ERROR_SUCCESS;
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        DEBUG_IDX("isPlaying %d\n", mpPlayer->isPlaying());
        ret = mpPlayer->pause();
        DEBUG_IDX("isPlaying %d\n", mpPlayer->isPlaying());
        ENSURE_PLAYER_OP_SUCCESS("pause", ret, 10);
        DEBUG_IDX("received paused msg, isPlaying %d\n", mpPlayer->isPlaying());
        return ret;
    }

    mm_status_t resume(){
        DEBUG_IDX();
        return mpPlayer->start();
    }

    mm_status_t  seek(TTime tar){
        DEBUG_IDX();
        return mpPlayer->seek(tar);
    }

    mm_status_t  setLoop(){
        static bool loop = true;

        int ret = mpPlayer->setLoop(loop);
        loop = !loop;
        return ret;
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

        DEBUG_IDX("status: %d, playing %d\n", status, mpPlayer->isPlaying());
    }


    void selectTrackTest() {
#ifdef USE_COWPLAYER
        MMParamSP param = mpPlayer->getTrackInfo();
        DEBUG("use cowplayer test");
#else
        mm_status_t status = MM_ERROR_SUCCESS;
        MMParam* request = new MMParam();
        MMParam* param = new MMParam();

        request->writeInt32(MediaPlayer::INVOKE_ID_GET_TRACK_INFO);
        status = mpPlayer->invoke(request, param);
        ASSERT(status == MM_ERROR_SUCCESS);
        DEBUG("use mediaplayer test");
#endif

        // stream count
        int32_t streamCount = param->readInt32();
        DEBUG("streamCount: %d", streamCount);

        int32_t videoTrackCount = 0;
        int32_t i=0, j=0;
        for (i=0; i<streamCount; i++) {
            // media type & track count
            int32_t mediaType = TEST_MediaTypeUnknown;
            int32_t trackCount = 0;
            mediaType = param->readInt32();
            trackCount = param->readInt32();
            DEBUG("mediaType: %d, trackCount: %d", mediaType, trackCount);

            if (mediaType == TEST_MediaTypeVideo)
                videoTrackCount = trackCount;

            for (j=0; j<trackCount; j++) {
                int32_t index = param->readInt32();
                int32_t codecId = param->readInt32();
                const char* codecName = param->readCString();
                const char* mime = param->readCString();
                const char* title = param->readCString();
                const char* lang = param->readCString();
                int32_t width = 0, height = 0;
                if (mediaType == TEST_MediaTypeVideo) {
                    width = param->readInt32();
                    height = param->readInt32();
                }

                DEBUG("index: %d, size: %dx%d, codecId: %d, codecName: %s, mime: %s, title: %s, lang: %s",
                    index, width, height, codecId, PRINTABLE_STR(codecName), PRINTABLE_STR(mime), PRINTABLE_STR(title), PRINTABLE_STR(lang));
            }
        }

        while (videoTrackCount) {
            static int32_t selectTrack = 0;
            selectTrack++;
            selectTrack %= videoTrackCount;
#ifdef USE_COWPLAYER
            mpPlayer->selectTrack(Component::kMediaTypeVideo, selectTrack);
#else
            MMParam* request2 = new MMParam();
            request2->writeInt32(MediaPlayer::INVOKE_ID_SELECT_TRACK);
            request2->writeInt32(TEST_MediaTypeVideo);
            request2->writeInt32(selectTrack);
            status = mpPlayer->invoke(request2, param);
            DEBUG("status: %d", status);
            ASSERT(status == MM_ERROR_SUCCESS);
            delete request2;
#endif
            break; // always break;
        }

#ifndef USE_COWPLAYER
        delete request;
        delete param;
#endif
    }

    void test(int32_t mode){
        DEBUG_IDX();

        switch (mode)
        {
            case 0:
                selectTrackTest();
                break;
            case 1:
                getPositionTest();
                break;
            default:
                selectTrackTest();
                break;
        }
    }
    void setPlayRate(bool increased){
        if (increased)
            g_cur_play_rate <<= 1;
        else
            g_cur_play_rate >>= 1;

        if (g_cur_play_rate > 1024)
            g_cur_play_rate = 1024;
        if (g_cur_play_rate == 0)
            g_cur_play_rate = 1;

        DEBUG_IDX();
        mMeta->setInt32(MEDIA_ATTR_PALY_RATE, g_cur_play_rate);
        mpPlayer->setParameter(mMeta);

        MY_PROMPT("setPlayRate (%d/32)", g_cur_play_rate);
    }

    mm_status_t  setSubtitleSource(char *subtitleUrl){

        int ret = mpPlayer->setSubtitleSource(subtitleUrl);
        return ret;
    }

    mm_status_t play(const char* url, const char* subtitleUrl){
        DEBUG_IDX();
        mpUrl=url;
        int  ret ;
        std::map<std::string,std::string> header;


        std::string download_str = mm_get_env_str("mm.test.download","MM_TEST_DOWNLOAD");
        if (!download_str.empty()) {
            mMeta->setString(MEDIA_ATTR_FILE_DOWNLOAD_PATH, "/storage/emulated/0/test.mp4");
            mpPlayer->setParameter(mMeta);
        }

        g_eos = false;
        INFO("subtitleUrl = %s", subtitleUrl);
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        if (use_localmediaplayer) {
            mpPlayer->setNativeDisplay(getWaylandDisplay());
        }
#endif
        if (g_setDataSource_fd) {
            mFd = open(url, O_LARGEFILE | O_RDONLY);
            if (mFd < 0) {
                DEBUG_IDX("open file (%s) fail", url);
                return MM_ERROR_IO;
            }
            int64_t fileLength = lseek64(mFd, 0, SEEK_END);
            DEBUG_IDX("file length %" PRId64 "\n", fileLength);
            if (fileLength == -1) {
                ERROR_IDX("seek file header failed");
                return MM_ERROR_IO;
            }
            //NOTE: seek to begin of the file
            lseek64(mFd, 0, SEEK_SET);
            ret = mpPlayer->setDataSource(mFd, 0, fileLength);
        } else {
            ret = mpPlayer->setDataSource(url,&header);
        }
        CHECK_PLAYER_OP_RET("setDataSource", ret);

        ret = mpPlayer->setListener(mpListener);
        CHECK_PLAYER_OP_RET("setListener", ret);
        if (subtitleUrl)
            mpPlayer->setSubtitleSource(subtitleUrl);

        if (g_loop_player && !g_loop_player_recreate_player) {
            mpPlayer->setLoop(true);
        }

        bool hasParam = false;
        if (g_use_low_delay) {
            mMeta->setInt32(MEDIA_ATTR_CODEC_LOW_DELAY, true);
            hasParam = true;
        }
        if (g_disable_hw_render) {
            mMeta->setInt32(MEDIA_ATTR_CODEC_DISABLE_HW_RENDER, true);
            hasParam = true;
        }
        if (g_use_thumbnail_mode) {
            mMeta->setString(MEDIA_ATTR_DECODE_MODE, MEDIA_ATTR_DECODE_THUMBNAIL);
            hasParam = true;
        }


        // mMeta->setInt32("memory-size", 5*1024*1024);
        // hasParam = true;
        if (hasParam)
            mpPlayer->setParameter(mMeta);

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        MMLOGI("set connectionId: %s\n", s_g_amHelper->getConnectionId());
        mpPlayer->setAudioConnectionId(s_g_amHelper->getConnectionId());
#endif

        DEBUG_IDX("use prepareAsync\n");
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->prepareAsync();
        ENSURE_PLAYER_OP_SUCCESS("prepare", ret, 60);

        DEBUG_IDX("set playRate %d\n", g_cur_play_rate);
        mMeta->setInt32(MEDIA_ATTR_PALY_RATE, NORMAL_PLAYBACK_RATE);
        // FIXME, add setParameter back
        //mpPlayer->setParameter(mMeta);

        // selectTrackTest();

#ifdef IMITATE_WEBENGINE_SEQUENCE
        TIME_T msec = -1;
        ret = mpPlayer->getDuration(GET_REFERENCE(msec));
        CHECK_PLAYER_OP_RET("getDuration", ret);

        DEBUG_IDX();
        ret = mpPlayer->seek(0);
        CHECK_PLAYER_OP_RET("seek", ret);
#endif
        DEBUG_IDX();
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->start();
        ENSURE_PLAYER_OP_SUCCESS("start", ret, 60);

#ifdef IMITATE_WEBENGINE_SEQUENCE
        DEBUG_IDX();
        ret = mpPlayer->getCurrentPosition(GET_REFERENCE(msec));
        CHECK_PLAYER_OP_RET("getCurrentPosition", ret);
#endif

        // dummy test for code coverage
        if (use_localmediaplayer) {
            mpPlayer->setNativeDisplay(NULL);
#ifndef USE_COWPLAYER
            mpPlayer->setVideoSurfaceTexture(NULL);
#endif
        }

        PipelineSP pipeline;
        pipeline.reset();
        mpPlayer->setPipeline(pipeline);
        mpPlayer->setDataSource(0, 0, 0);
        mpPlayer->setDisplayName(NULL);

#ifndef USE_COWPLAYER
        mpPlayer->setAudioStreamType(MediaPlayer::AS_TYPE_MUSIC);
        MediaPlayer::as_type_t streamtype;
        mpPlayer->getAudioStreamType(&streamtype);
        MediaPlayer::VolumeInfo volume = {0, 0};
        mpPlayer->getVolume(&volume);
        mpPlayer->setVolume(volume);
        bool mute = false;
        mpPlayer->getMute(&mute);
        mpPlayer->setMute(mute);
#endif
        bool loop = false;
        loop = mpPlayer->isLooping();
        mpPlayer->setLoop(loop);
        int width = 1;
        int height = 1;
        mpPlayer->getVideoSize(GET_REFERENCE(width), GET_REFERENCE(height));
        mpPlayer->pause();
        bool isPlaying = mpPlayer->isPlaying();
        ret = mpPlayer->start();
        ENSURE_PLAYER_OP_SUCCESS("start", ret, 60);

        DEBUG_IDX();
        return MM_ERROR_SUCCESS;
    }


    static void* getCurrentPositionThread(void *context) {
        DEBUG("currentTimeTestThread enter\n");
        MyPlayer *player = static_cast<MyPlayer *>(context);
        if (!player) {
            return NULL;
        }

        int ret = 0;
        TIME_T msec = -1;
        while (1) {
            if (player->mPosThreadQuit) {
                return NULL;
            }
            ret = player->getPlayer()->getCurrentPosition(GET_REFERENCE(msec));
            if (ret != MM_ERROR_SUCCESS) {
                MY_ERROR("getCurrentPosition failed\n");
            }

            MY_PROMPT("current position %0.3f\n", msec/1000.0f);
            usleep(500*1000);//sleep 200ms for next getCurrentPosition
        }

        return NULL;
    }

    MediaPlayer* getPlayer(){
        return mpPlayer;
    }

private:
    const char* mpUrl;
    MediaPlayer* mpPlayer;
    TestListener* mpListener;
    MediaMetaSP mMeta;
    int mFd;
    int mDraw;
    pthread_t mPosThreadId;
    bool mPosThreadQuit;
    bool mPosThreadCreated;
};

//MyPlayer* gPlayer;
//MyPlayer* gPlayer_x;
std::vector<MyPlayer* > gPlayer;
int curPlayer = 0;
#define MAX_SOURCE_NAME_LEN 256
#define MAX_SOURCE_NUM 16

char g_source[MAX_SOURCE_NAME_LEN];
char g_subtitle_source[MAX_SOURCE_NAME_LEN];
int get_source(const char* par)
{
    if(strlen(par) >= MAX_SOURCE_NAME_LEN){
        DEBUG("get_source err: too long source name\n");
        return -1;
    }

    int rval = 0;
    const char* ptr = par;
    int filename_len = 0;

    for(; *ptr != '\0'; ptr++){
        if(*ptr == ' ')
            break;
    }
    filename_len = ptr - par;

    DEBUG("filename_len: %d\n", filename_len);

    strncpy(g_source, par, filename_len);
    //g_source[filename_len];
    //const char *tmp = "http://d.tv.taobao.com/TB2kh.pbFXXXXbfXpXXXXXXXXXX/0.m3u8?auth_key=9A05722F7B23C53BBE52BC9F2BD80D03-1448716836&ttr=free";
    //memcpy(g_source, tmp, strlen(tmp));
    DEBUG("get_source %s, filename_len:%d, parsed:%s.\n", par, filename_len, g_source);
    return rval;
}

int get_subtitle_source(const char* par)
{
    if(strlen(par) >= MAX_SOURCE_NAME_LEN){
        DEBUG("get_subtitle_source err: too long source name\n");
        return -1;
    }

    int rval = 0;
    const char* ptr = par;
    int filename_len = 0;

    for(; *ptr != '\0'; ptr++){
        if(*ptr == ' ')
            break;
    }
    filename_len = ptr - par;

    DEBUG("filename_len: %d\n", filename_len);

    strncpy(g_subtitle_source, par, filename_len);

    DEBUG("g_subtitle_source %s, filename_len:%d, parsed:%s.\n", par, filename_len, g_subtitle_source);
    return rval;
}

void show_usage()
{
    MY_PROMPT("\n");
    MY_PROMPT("-a <name> : Name of file to play.\n");
    MY_PROMPT("-n <N>    : Create N players.\n");
    MY_PROMPT("-t        : Automation test.\n");
    MY_PROMPT("-l <N>    : loop option.\n"
           "            -l 0  : seek 0 to loop play.\n"
           "            -l 1  : recreate player to loop play.\n"
           "            -l 2  : recreate player and surface to loop play.\n");
    MY_PROMPT("-w <N>    : surface option. \n"
           "            -w 0  : normal surface.\n"
           "            -w 1  : sub surface.\n"
           "            -w 2  : MediaSurfaceTexture, no draw(no EGL).\n"
           "            -w 3  : MediaSurfaceTexture with Surface, no draw (no EGL).\n"
           "            -w 4  : MediaSurfaceTexture, draw to another Surface with gles.\n"
           "            -w 5  : MediaSurfaceTexture with Surface, draw to another Surface with gles.\n"
           "            -w 51  : MediaSurfaceTexture with SubSurface of PageWindow, draw to another Surface with gles.\n"
           "            -w 56  : MediaSurfaceTexture with main Surface of PageWindow, draw to another Surface with gles.\n"
           "            -w 6  : use main surface of pagewindow\n");
    MY_PROMPT("-p <N>    : Set play time.\n");
    MY_PROMPT("-g <N>    : Set wait time to start the next play.\n");
    MY_PROMPT("-v        : Use ffmpeg.\n");
    MY_PROMPT("\n");
    MY_PROMPT("runtime command:\n"
           "            q     : quit \n"
           "            p     : pause \n"
           "            s <N> : seek to N \n"
           "            l     : change loop type \n"
           "            t     : some test(0: print current position, 1: select track(for hls)) \n"
           "            i     : increase play rate \n"
           "            d     : decrease play rate \n"
           "            w     : switch textureview/surfaceview \n"
           "            m     : only use in -w 1,switch video size \n");
    MY_PROMPT("\n");
    MY_PROMPT("e.g.\n");
    MY_PROMPT("    cowplayer-test -a /data/H264.mp4 \n");
    MY_PROMPT("    cowplayer-test -a /data/H264.mp4 -l 2 -w 5 -p 10 -g 2\n");
}

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

    sscanf(sizeOption, "%dx%d", &g_surface_width, &g_surface_height);

    DEBUG("parse window size %dx%d\n", g_surface_width, g_surface_height);
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

static uint32_t g_testCount = 10;
char getRandomOp()
{
    static uint32_t testCount = 0;
    const int randomOpMin = 0;
    const int randomOpMax = 4;
    char randomOps[]={'p','r','s','l','t'};
    uint32_t i = rand()%(randomOpMax-randomOpMin+1)+randomOpMin;
    sleep(2);

    testCount++;
    if (testCount > g_testCount)
        return 'q';

    return randomOps[i];
}

TIME_T getRandomSeekTo()
{
    MediaPlayer* mp = gPlayer[curPlayer]->getPlayer();
    TIME_T randomSeekMin = 0;
    TIME_T randomSeekMax = 0;
    mp->getDuration(GET_REFERENCE(randomSeekMax));
    int64_t tmp = randomSeekMax;
    DEBUG("max auto seek to =%" PRId64,tmp);
    TIME_T i = rand()%(randomSeekMax-randomSeekMin+1)+randomSeekMin;
    return i;
}

typedef struct {
    uint32_t delayBeforeOp; // ms
    char testOp;
    TIME_T seekPos;
} AutoTestParameter;
std::vector<AutoTestParameter> ATParams;
bool parseAutoTestFile(const char* autoTestFile)
{
    #define MAX_LINE_SIZE   128
    #define BLANK "%*[ ]"
    FILE *fp = NULL;
    char oneLineData[MAX_LINE_SIZE];
    AutoTestParameter param;

    fp = fopen(autoTestFile, "r");
    if (!fp)
        return false;

    while(fgets(oneLineData, MAX_LINE_SIZE-1, fp)) {
        uint32_t delay = 0;
        char command;
        uint32_t seekPos = 0;

        int ret = sscanf(oneLineData, "%dms," BLANK "%c," BLANK "%dms", &delay, &command, &seekPos);
        DEBUG("ret: %d, delay: %dms, command: %c, seek: %dms", ret, delay, command, seekPos);
        if ((command == 's' && ret !=3) || (command != 's' && ret != 2)) {
            MY_ERROR("incorrect line data: %s", oneLineData);
            continue;
        }
        param.delayBeforeOp = delay;
        param.testOp = command;
        param.seekPos = seekPos;
        ATParams.push_back(param);
    }
    fclose(fp);
    fp = NULL;

    param.delayBeforeOp = 5000;
    param.testOp = 'q';
    ATParams.push_back(param);
    return true;
}

char getAutoTestOp(uint32_t &seekPos)
{
    static uint32_t testCount = 0;

    if (testCount > ATParams.size()-1)
        return 0;

    usleep(ATParams[testCount].delayBeforeOp*1000);
    seekPos = ATParams[testCount].seekPos;

    return ATParams[testCount++].testOp;
}

void RunMainLoop()
{
    char ch;
    char buffer[128] = {0};
    int flag_stdin = 0;
    int par;
    int cur = 0;
    TIME_T target;
    //
    char buffer_back[128] = {0};
    ssize_t back_count = 0;

    //playback zoom    int video_width=0;
    // int video_height=0;
    ssize_t count = 0;

    flag_stdin = fcntl(STDIN_FILENO, F_GETFL);
    if(fcntl(STDIN_FILENO, F_SETFL, flag_stdin | O_NONBLOCK)== -1)
        MY_ERROR("stdin_fileno set error");

    uint32_t  playtime = 0;
    if(g_auto_test && g_random_test){
        int64_t seed = time(0);
        std::string env_str = mm_get_env_str("mm.test.srand.seed","MM_TEST_SRAND_SEED");
        if (!env_str.empty())
            seed = atoi(env_str.c_str());
        uint32_t real_seed = (uint32_t) (seed &0x7fffffff); // shrink the seed range to int32
        MY_PROMPT("auto test seed =%d", real_seed);
        srand(seed);
    }

    while (1)
    {
        if(g_quit)
            break;
        if(!g_auto_test){
            memset(buffer, 0, sizeof(buffer));
            if ((count = read(STDIN_FILENO, buffer, sizeof(buffer)-1) < 0)) {
               //  if ((g_eos && (!g_loop_player || g_loop_player_recreate_player)) || (g_play_time && g_play_time*1000*1000 <= playtime)) {
               if (g_eos || (g_play_time && g_play_time*1000*1000 <= playtime)) { // terminate playback of this time (play to the end or reach g_play_time)
                    if (g_eos && (g_loop_player && !g_loop_player_recreate_player)) { // player loop playback
                        loopedCount++;
                        MY_PROMPT("loopedCount: %d", loopedCount);
                        g_eos = false;
                        if (loopedCount < g_loop_count)
                            continue;
                    }

                    buffer[0] = 'q';
                } else {
                    usleep(100000);
                    playtime += 100000;
                    continue;
                }
            } else if (buffer[0] == 'q') {
                g_user_quit = true;
            }

            buffer[sizeof(buffer) - 1] = '\0';
            count = strlen(buffer);
            //DEBUG("read return %zu bytes, %s\n", count, buffer);
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
        } else{ //auto test get rand op
            if (g_random_test)
                ch = getRandomOp();
            else {
                uint32_t temp = 0;
                ch = getAutoTestOp(temp);
                target = temp;
            }
        }
        //g_last_cmd = ch;
        switch (ch) {
        case 'q':    // exit
        case 'Q':
            MY_PROMPT("receive 'q', quit ...");
            g_quit = true;
            for (int n = 0; n < g_player_c; n++)
            {
                gPlayer[n]->quit();
            }
            break;

        case 'p':
            gPlayer[curPlayer]->pause();
            break;
        case 'r':
            gPlayer[curPlayer]->resume();
            break;

        case 's':
            if(!g_auto_test){
                if(sscanf(trim(buffer), "%d", &par) != 1){
                    MY_ERROR("Please set the seek target in 90k unit\n");
                    break;
                }
                target = par;
            }
            else {
                if (g_random_test)
                    target = getRandomSeekTo();
            }
            {
                int64_t temp = target;
                MY_PROMPT("seek to %" PRId64 "\n",temp);
            }
            gPlayer[curPlayer]->seek(target);
            break;

        case 'c':
                if(sscanf(trim(buffer), "%d", &cur) != 1){
                    MY_ERROR("Please set curplayer in 1 - %d\n",g_player_c);
                    break;
                }
                if (cur < 1 || cur > g_player_c)
                {
                    MY_ERROR("Please set curplayer in 1 - %d\n",g_player_c);
                    break;
                }
                MY_PROMPT("set %d as current player", cur);
                curPlayer = cur - 1;
                break;

        case 'l':
            gPlayer[curPlayer]->setLoop();
            break;

        case 't':
            if(sscanf(trim(buffer), "%d", &par) != 1) {
                MY_ERROR("Please select the test mode\n");
                break;
            }
            gPlayer[curPlayer]->test(par);
            break;

        case 'i':
            MY_PROMPT("increase play rate");
            gPlayer[curPlayer]->setPlayRate(true);
            break;

        case 'd':
            MY_PROMPT("decrease play rate");
            gPlayer[curPlayer]->setPlayRate(false);
            break;

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
        case 'w':
            g_show_surface = !g_show_surface;
    #ifdef MM_ENABLE_PAGEWINDOW
            pagewindowShow(g_show_surface);
    #endif
            mst[curPlayer]->setShowFlag(g_show_surface);
            break;

        case 'm':
            if (g_use_sub_surface) {
    #ifdef MM_ENABLE_PAGEWINDOW
                // a little hack: SimpleSubWindow uses g_surface_width/height as video resolution for resize
                g_surface_width = g_video_width;
                g_surface_height = g_video_height;
                setSubWindowFit(ws[curPlayer]);
    #endif
            }
            break;
#endif
        case '-':
            //some misc cmdline
            break;

        case 'a':
            INFO("switch subtitle file");
            gPlayer[curPlayer]->setSubtitleSource(trim(buffer));
            break;

        default:
            //MY_ERROR("Unkonw cmd line:%c\n", ch);
            break;
        }
    }

    if(fcntl(STDIN_FILENO,F_SETFL,flag_stdin) == -1)
        MY_ERROR("stdin_fileno set error");
    DEBUG("\nExit RunMainLoop on yunostest.\n");
}

class CowplayerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};
/* the DISPLAY in cowplayer-test
    - WindowSurface and EGL uses gui module directly, share the singleton wayland connection from WindowSurfaceTestWindow
    - SubSurface use PageWindow, and PageWindow hides the detail of wayland connection
    - TODO: add rendering mode: MediaCodec uses pagewindow SubSurface while rendering uese egl_texture bases on libgui
    - TODO: uses SubSurface from libgui instead of PageWindow
*/
TEST_F(CowplayerTest, cowplaytest) {
    int ret = MM_ERROR_SUCCESS;
    gPlayer.resize(g_player_c);
    ws.resize(g_player_c);
    g_opSync.resize(g_player_c);

    for (int n = 0; n < g_player_c; n++) {
        gPlayer[n] = NULL;
        ws[n] = NULL;
    }

    mst.resize(g_player_c);
    for (int n = 0; n < g_player_c; n++) {
        mst[n] = NULL;
    }
    if (setenv("HYBRIS_EGLPLATFORM", "wayland", 0)) {
        MY_PROMPT("setenv fail, errno %d", errno);
    }

#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    if(g_use_ffmpeg){
         ComponentFactory::appendPluginsXml("/usr/bin/ut/use_ffmpeg_plugins.xml");
     }
#endif
    if (gl_filter_test)  {
        ComponentFactory::appendPluginsXml("/etc/cow_plugins_example.xml");
        setenv("MM_PLAYER_GL_FILTER", "1", 1);
    }


BEGIN_OF_LOOP_FROM_CREATE_SURFACE:
#if defined(__MM_YUNOS_YUNHAL_BUILD__) ||defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    // check whether use nested/raw wayland surface
    std::string nestedDisplayName = YUNOS_MM::mm_get_env_str("mm.wayland.nested.display", "MM_WAYLAND_NESTED_DISPLAY");

    DEBUG();
    NativeSurfaceType surfaceType = NST_SimpleWindow;
    if (g_use_pagewindow)
        surfaceType = NST_PagewindowMain;
    if (g_use_sub_surface) {
        surfaceType = NST_PagewindowSub;
    }

    rotationWsTransform = angle_to_ws_transform(rotationWinDegree);

    // simple surface doesn't support media player service
    if (!use_localmediaplayer)
        surfaceType = (NativeSurfaceType) (surfaceType | NST_PagewindowMain);

    if (surfaceType & NST_PagewindowMain || surfaceType & NST_PagewindowSub) {
        if (!use_localmediaplayer)
            surfaceType = (NativeSurfaceType) (surfaceType | NST_PagewindowRemote);
    }
    MY_PROMPT("create surface with surfaceType 0x%x\n", surfaceType);

    for (int n=0; n<g_player_c; n++) {
        if (g_use_media_texture && !g_use_window_surface) {
                initMediaTexture(n); // necessary to pass below check !ws[n] && !mst[n]
                continue; // use mst only (no surface).
        }

        // create surface
        ws[n] = createSimpleSurface(g_surface_width, g_surface_height, surfaceType);
    #if defined(__MM_YUNOS_YUNHAL_BUILD__) ||defined(__MM_YUNOS_CNTRHAL_BUILD__)
        if (ws[n] && surfaceType == NST_SimpleWindow) {
            //ret = WINDOW_API(set_buffers_offset)((WindowSurface*)ws[n], n*100, n*100);
            ((yunos::libgui::Surface*)ws[n])->setOffset(n*100, n*100);
            if (rotationWsTransform)
                ret = WINDOW_API(set_buffers_transform)(ws[n], rotationWsTransform);
        }
    #endif
    }

    for (int n = 0; n < g_player_c; n++) {
        if(!ws[n] && !mst[n]) {
            MY_ERROR("ws[%d] or mst[%d] is NULL\n",n, n);
            goto RELEASE;
        }
    }
#endif

BEGIN_OF_LOOP_FROM_CREATE_PLAYER:
    // connect player with rendering surface
    for (int n = 0; n < g_player_c; n++)
    {
        gPlayer[n] = new MyPlayer(n);
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    if (g_use_media_texture) {
        // MediaSurfaceTexture doesn't support destroyBuffer/restoreBuffer, create it for each playback. FIXME ???
        initMediaTexture(n);
        if (g_use_window_surface) {
    #ifdef MM_ENABLE_PAGEWINDOW
            if (!use_localmediaplayer)
                mst[n]->setSurfaceName(getWindowName(ws[n]).c_str());
            else
                mst[n]->setWindowSurface(ws[n]);
            pagewindowShow(true);
    #endif
            mst[n]->setShowFlag(g_show_surface);
        }
        ret = gPlayer[n]->setSurfaceTexture(mst[n]);
     }else {
        ret = gPlayer[n]->setSurface(n);
     }
#endif
        EXPECT_TRUE(ret == MM_ERROR_SUCCESS);

        ret = gPlayer[n]->play(g_source, g_subtitle_source);
        EXPECT_TRUE(ret == MM_ERROR_SUCCESS);
    }

    RunMainLoop();

    usleep(10000);
RELEASE:
    DEBUG("delete gPlayer...\n");
    for (int n = 0; n < g_player_c; n++) {
        delete gPlayer[n];
    }
    gPlayer.clear();
    DEBUG("delete gPlayer done, loopedCount %d\n", loopedCount);

    bool finalTimePlayback = !g_loop_player || loopedCount == g_loop_count - 1 || g_user_quit;
    if ( finalTimePlayback || g_loop_player_recreate_surface) {
        #if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
        for (int n = 0; n < g_player_c; n++) {
            if (g_use_media_texture) {
                if (mst[n]) {
                    DEBUG("delete mst %p\n", mst[n]);
                    delete mst[n];
                    mst[n] = NULL;
                    DEBUG("delete mst done\n");
                }
            }
            if (ws[n]) {
                destroySimpleSurface(ws[n]);
                ws[n] = NULL;
                DEBUG("delete ws done\n");
            }
        }

        #endif
    }

    loopedCount++;
    DEBUG("loopedCount: %d, g_loop_count: %d", loopedCount, g_loop_count);
    if (!finalTimePlayback) {
        usleep(g_wait_time*1000*1000);
        g_quit = false;
         if (g_loop_player_recreate_surface)
            goto BEGIN_OF_LOOP_FROM_CREATE_SURFACE;
         else
            goto BEGIN_OF_LOOP_FROM_CREATE_PLAYER;
    }
#ifndef __MM_NATIVE_BUILD__
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    cleanupSurfaces();
#endif
#endif
    DEBUG();
}


extern "C" int main(int argc, char* const argv[]) {
    DEBUG("MediaPlayer video test for YunOS!\n");
    if (argc < 2) {
        MY_ERROR("Must specify play file \n");
        show_usage();
        return 0;
    }

#ifdef USE_COWPLAYER
    use_localmediaplayer = true;
#endif

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (gchar***)&argv, &error)) {
            MY_ERROR("option parsing failed: %s\n", error->message);
            show_usage();
            return -1;
    }
    g_option_context_free(context);

    if(file_name){
        DEBUG("set file name  %s\n", file_name);
        if (get_source(file_name) < 0){
                MY_ERROR("invalid file name\n");
                return -1;
        }
    } else {
        MY_ERROR("Must specify play file \n");
        show_usage();
        return 0;
    }

    if(subtitle_file_name){
        DEBUG("set file name  %s\n", subtitle_file_name);
        if (get_subtitle_source(subtitle_file_name) < 0){
                MY_ERROR("invalid file name\n");
                return -1;
        }
    }

    if(animation){
        DEBUG("set animation \n");
        g_animate_win = true;
    }

    if(sizestr){
        DEBUG("set size = %s\n",sizestr);
        if (get_size(sizestr) < 0){
                MY_ERROR("invalid size \n");
                return -1;
        }
    }

    if (autotest_input) {
        g_auto_test = true;
        if (access(autotest_input, F_OK) == 0) {
            g_random_test = false;
            if (!parseAutoTestFile(autotest_input))
                g_auto_test = false;
        } else {
            g_testCount = atoi(autotest_input);
            g_random_test = true;
            if (!g_testCount)
                g_auto_test = false;
        }
    }
    MY_PROMPT("use_localmediaplayer %d", use_localmediaplayer);

    if(loopplay != -1) {
/***************************************************************
 *-l 0 : seek 0 to loop play
 *-l 1 : recreate player to loop play
 *-l 2 : recreate player and surface to loop play
 */
        DEBUG("set loop play loopplay = %d\n",loopplay);
        switch (loopplay) {
        case 0:
            g_loop_player = true;
            g_loop_player_recreate_player = false;
            break;
        case 1:
            g_loop_player = true;
            g_loop_player_recreate_player = true;
            break;
        case 2:
            g_loop_player = true;
            g_loop_player_recreate_player = true;
            g_loop_player_recreate_surface = true;
            break;
        default:
            MY_ERROR("\n");
            break;
        }
    }

    if(loopcount >0){
        DEBUG("set loop play count =%d \n",loopcount);
        g_loop_count = loopcount;
    }

/***************************************************************
 *-w 0 : normal surface
 *-w 1 : pagewindow sub surface
 *-w 11 : pagewindow sub surface, disable video hw render
 *-w 2 : media_texture, skip video rendering
 *-w 3 : media_texture and window surface. (window surface display the video)
 *-w 4 : media_texture , window_surface and egl to draw
 *-w 5 : media_texture with surface(share native buffers) for codec , window_surface and egl to draw
 *-w 50 : media_texture with normal surface(share native buffers) for codec , window_surface and egl to draw
 *-w 51 : media_texture with pagewindow sub surface(share native buffers) for codec , window_surface and egl to draw
 *-w 56 : media_texture with pagewindow main surface(share native buffers) for codec , window_surface and egl to draw
 *-w 6 : pagewindow main surface
  */

    DEBUG("set surface type surfacetype = %d\n",surfacetype);
    switch (surfacetype) {
    case 0:
        break;
    case 11:
        g_disable_hw_render = true;
    case 1:
        g_use_pagewindow = true;
        g_use_sub_surface = true;
        break;
    case 2:
        g_use_media_texture = true;
        break;
    case 3:
        g_use_media_texture = true;
        g_use_window_surface = true;
        g_show_surface = true;
        break;
    case 4:
        g_use_media_texture = true;
        g_use_egl_draw_texture = true;
        break;
    case 5:
    case 50:
        g_use_media_texture = true;
        g_use_window_surface = true;
        g_use_egl_draw_texture = true;
        break;
    case 51:
        g_use_pagewindow = true;
        g_use_sub_surface = true;
        g_use_media_texture = true;
        g_use_window_surface = true;
        g_use_egl_draw_texture = true;
        break;
    case 56:
        g_use_pagewindow = true;
        g_use_media_texture = true;
        g_use_window_surface = true;
        g_use_egl_draw_texture = true;
        break;
    case 6:
        g_use_pagewindow = true;
        break;
    default:
        MY_ERROR("error surfacetype:%d\n", surfacetype);
        break;
    }

    g_play_time = playtime;
    g_wait_time = waittime;

    if(set_dataSource_with_fd){
        DEBUG("set data source with fd\n");
        g_setDataSource_fd = true;
    }

    if(use_ffmpeg){
        DEBUG("set use ffmpeg\n");
        g_use_ffmpeg = true;
   }
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
    g_player_c = player_c;
#else
    g_player_c = 1;
#endif

#ifdef __PLATFORM_TV__
    g_player_c = 1;
#endif

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
#endif

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    AutoWakeLock awl;
#else
    MMLOGI("When wake lock ready, use it\n");
#endif
    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        MY_ERROR("InitGoogleTest failed!");
        ret = -1;
    }
    if (file_name)
        g_free((char *)file_name);
    if (sizestr)
        g_free(sizestr);
    if (subtitle_file_name)
        g_free((char *)subtitle_file_name);
    DEBUG("cowplayyer-test done");

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    s_g_amHelper->disconnect();
#endif

    return ret;
}

