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

#include <assert.h>
#include "video_sink_wayland.h"
#include <multimedia/component.h>
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

// 32/24 are Padding Amounts from TI <<H264_Decoder_HDVICP2_UserGuide>>
#define PADDING_AMOUNT_X    32
#define PADDING_AMOUNT_Y    24

MM_LOG_DEFINE_MODULE_NAME("VideoSinkWayland");

namespace YUNOS_MM
{

static const char * COMPONENT_NAME = "VideoSinkWayland";
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

// debug use only
static uint32_t s_ReleaseBufCount = 0;
static uint32_t s_FrameCBCount = 0;

/////////////////////////////////wrapper for wayland connection /////////////////////////////////////////////////
class WlDisplayType
{
  public:
    WlDisplayType();
    ~WlDisplayType();
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_shell *shell;
    struct wl_drm *drm;
    struct wl_viewport *viewport;
    struct wl_scaler *scaler;
    struct wl_callback *callback;
    pthread_t threadID;
    uint32_t threadStatus;  // 0: normal running, 1: main thread inform to quit, 2: event thread quit done
};
WlDisplayType::WlDisplayType()
    : display(NULL)
    , compositor(NULL)
    , surface(NULL)
    , shell(NULL)
    , drm(NULL)
    , callback(NULL)
{
    memset(&threadID, 0, sizeof(threadID));
}

/////////////////////////////////wayland helper func /////////////////////////////////////////////////
static void
sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
    FUNC_TRACK();
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    sync_callback,
    NULL
};

// schedule a dummy event (display sync callback) to unblock wl_display_dispatch()
static bool wl_display_dummy_event(struct wl_display *display)
{
    FUNC_TRACK();
    struct wl_callback *callback = wl_display_sync(display);
    if (callback == NULL)
        return false;

    wl_callback_add_listener(callback, &sync_listener, NULL);
    wl_callback_request_done(callback);
    wl_display_flush(display);
    return true;
}

void* wayland_event_dispatch_task(void *arg)
{
    FUNC_TRACK();
    int ret;
    WlDisplayType* wlDisplay = (WlDisplayType*) arg;

    while(wlDisplay->threadStatus == 0) {
        ret = wl_display_dispatch(wlDisplay->display);
        VERBOSE("wayland_process_events ret: %d", ret);
    }
    wlDisplay->threadStatus = 2;

    return NULL;
}

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
    const char *interface, uint32_t version)
{
    FUNC_TRACK();
   WlDisplayType *wlDisplay = (WlDisplayType *) data;

    if (strcmp(interface, "wl_compositor") == 0) {
        wlDisplay->compositor = (struct wl_compositor*)wl_registry_bind(registry, id,
            &wl_compositor_interface, 3);
    } else if (strcmp(interface, "wl_shell") == 0) {
        wlDisplay->shell = (struct wl_shell*)wl_registry_bind(registry, id,
            &wl_shell_interface, 1);
    } else if (strcmp(interface, "wl_drm") == 0) {
        wlDisplay->drm = (struct wl_drm*)wl_registry_bind(registry, id,
            &wl_drm_interface, 1);
    } else if (strcmp(interface, "wl_scaler") == 0) {
        wlDisplay->scaler = (struct wl_scaler*)wl_registry_bind(registry, id,
            &wl_scaler_interface, 2);
    }
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t name)
{
    FUNC_TRACK();
}
static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};


WlDisplayType *
disp_wayland_open(const char* displayName, uint32_t width, uint32_t height)
{
    FUNC_TRACK();
    WlDisplayType *wlDisplay = NULL;
    struct wl_registry *registry = NULL;
    struct wl_shell_surface *shell_surface = NULL;
    int ret;

    wlDisplay = (WlDisplayType*)calloc(1, sizeof(WlDisplayType));
    // xxx, nested string token, or use app set display handle
    wlDisplay->display = wl_display_connect(displayName);
    if (wlDisplay->display == NULL) {
        ERROR("failed to connect to Wayland display: %m\n");
        return NULL;
    } else {
        INFO("wayland display opened\n");
    }

    /* Find out what interfaces are implemented, and initialize */
    registry = wl_display_get_registry(wlDisplay->display);
    wl_registry_add_listener(registry, &registry_listener, wlDisplay);
    wl_display_roundtrip(wlDisplay->display);
    INFO("wayland registries obtained\n");

    wlDisplay->surface = wl_compositor_create_surface(wlDisplay->compositor);
    shell_surface = wl_shell_get_shell_surface(wlDisplay->shell,
                        wlDisplay->surface);
    wl_shell_surface_set_toplevel(shell_surface);
#if 0
    struct wl_region *region = NULL;
    region = wl_compositor_create_region(wlDisplay->compositor);
    wl_region_add(region, 0, 0, width, height);
#endif
    wlDisplay->viewport = wl_scaler_get_viewport(wlDisplay->scaler,
                        wlDisplay->surface);
    wl_viewport_set_source(wlDisplay->viewport, wl_fixed_from_int(PADDING_AMOUNT_X), wl_fixed_from_int(PADDING_AMOUNT_Y), wl_fixed_from_int(width), wl_fixed_from_int(height));
    wl_viewport_set_destination(wlDisplay->viewport, width, height);
    DEBUG("set surface viewport: %d x %d", width, height);

    ret = pthread_create(&wlDisplay->threadID, NULL, wayland_event_dispatch_task, wlDisplay);
    if(ret) {
        INFO("could not create task for wayland event processing");
    }

    return wlDisplay;
}

// after frame_callback_listener callback, we can submit another wl_buffer
void frame_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    FUNC_TRACK();
    DEBUG("s_FrameCBCount: %d, callback: %p", s_FrameCBCount, callback);
    s_FrameCBCount++;

    VideoSinkWayland *sink = (VideoSinkWayland*) data;
    WlDisplayType *wlDisplay = sink->mDisplay;
    YUNOS_MM::MMAutoLock locker(sink->mLock);

    if (callback == wlDisplay->callback) {
        wl_callback_destroy(wlDisplay->callback);
        wlDisplay->callback = NULL;
        sink->mCond.signal();
    }
}
const struct wl_callback_listener frame_listener = { .done = frame_callback };

/* after buffer_release_callback, we can release wl_buffer (and its corresponding MediaBuffer)
 * it avoids  flicker: wayland server is still using the buffer but video driver updates the frame
 */
void buffer_release_callback (void *data, struct wl_buffer *wlBuffer)
{
    DEBUG("s_ReleaseBufCount: %d, wl_buffer: %p", s_ReleaseBufCount, wlBuffer);
    s_ReleaseBufCount++;

    if (!data || !wlBuffer) {
        ERROR("invalid data or wlBuffer");
        return;
    }

    if (data) {
        VideoSinkWayland *sink = (VideoSinkWayland*) data;
        VideoSinkWayland::BufferMap* map = sink->mWlBufMediaBufMap;
        MMAutoLock locker(sink->mLock);
        int i = 0;
        for (i=0; i<MAX_COMMIT_BUF_COUNT; i++) {
            if (map[i].wlBuffer == wlBuffer) {
                map[i].wlBuffer = NULL;
                map[i].mediaBuffer.reset();
                break;
            }
        }
    }
}

static const struct wl_buffer_listener frame_buffer_listener = {
    buffer_release_callback
};


///////////////////////////////////// VideoSinkWayland /////////////////////////////////////////////
VideoSinkWayland::VideoSinkWayland()
    : mDisplay(NULL)
    , mCond(mLock)
    , mInBufferCount(0)
    , mPostBufferCount(0)
{
    FUNC_TRACK();
    int i=0;
    for (i=0; i<MAX_COMMIT_BUF_COUNT; i++) {
        mWlBufMediaBufMap[i].wlBuffer = NULL;
    }
}

VideoSinkWayland::~VideoSinkWayland()
{
    FUNC_TRACK();
}

const char *VideoSinkWayland::name() const
{
    return COMPONENT_NAME;
}

mm_status_t VideoSinkWayland::initCanvas()
{
    FUNC_TRACK();

    if (!mDisplay) {
        mDisplay = disp_wayland_open(NULL, mWidth, mHeight);
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkWayland::flushCanvas()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    int i = 0;
    for (i = 0; i < MAX_COMMIT_BUF_COUNT; i++) {
        mWlBufMediaBufMap[i].wlBuffer = NULL;
        mWlBufMediaBufMap[i].mediaBuffer.reset();
    }
    return MM_ERROR_SUCCESS;
}
mm_status_t VideoSinkWayland::uninitCanvas()
{
    FUNC_TRACK();
    if (!mDisplay)
        return MM_ERROR_SUCCESS;

    // terminate wayland event dispatch thread
    mDisplay->threadStatus = 1;
    void * aaa = NULL;
    wl_display_dummy_event(mDisplay->display);
    pthread_join(mDisplay->threadID, &aaa);

    // destroy wl_buffer
    std::map<uint32_t, void*>::iterator it = mDrmWlBufMap.begin();
    while (it != mDrmWlBufMap.end()) {
        wl_buffer_destroy((struct wl_buffer*)it->second);
        it++;
     }
    mDrmWlBufMap.clear();

    // destroy wl_surface and disconnect wl_display
    wl_surface_destroy(mDisplay->surface);
    wl_display_disconnect(mDisplay->display);
    free(mDisplay);
    mDisplay = NULL;

    return MM_ERROR_SUCCESS;
}

struct wl_buffer*
VideoSinkWayland::createWlBuffer(WlDisplayType *wlDisplay, MediaBufferSP mediaBuffer)
{
    FUNC_TRACK();
    // xxx, other fourcc support
    uint32_t wl_fmt = WL_DRM_FORMAT_NV12;
    int32_t drmName = -1;
    struct wl_buffer* wl_buf = NULL;

    if (!mediaBuffer)
        return NULL;

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    if (!meta)
        return NULL;

    // create wl_buffer from drm name
    uintptr_t buffers[3];
    int32_t offsets[3], strides[3];
    mediaBuffer->getBufferInfo((uintptr_t *)buffers, offsets, strides, 2);
    drmName = (int32_t)buffers[0];
    DEBUG("mInBufferCount: %d, drmName: %d, width: %d, height: %d, offsets[0]: %d, ,strides[0]: %d, offsets[1]: %d, strides[1]: %d",
        mInBufferCount, drmName, mWidth, mHeight, offsets[0], strides[0], offsets[1], strides[1]);
    mInBufferCount++;

    // if wl_buffer has been created already, just use it
    std::map<uint32_t, void*>::iterator it = mDrmWlBufMap.find(drmName);
    if (it != mDrmWlBufMap.end())
        return (struct wl_buffer*)it->second;

    // wl_buffer is created with padded resolution, then uses mWidth/mHeight to crop it (wl_viewport_set_source/wl_viewport_set_destination)
    int32_t w = strides[0];
    int32_t h = offsets[1]/w;
    wl_buf = wl_drm_create_planar_buffer(wlDisplay->drm,
        drmName, w, h, wl_fmt,
        offsets[0], strides[0],
        offsets[1], strides[1],
        0, 0);

    mDrmWlBufMap[drmName] = wl_buf;

    if (mDrmWlBufMap.size() >50) {
        WARNING("mDrmWlBufMap size: %zu", mDrmWlBufMap.size());
    }
    wl_buffer_add_listener (wl_buf, &frame_buffer_listener, this);

    return wl_buf;
}

int VideoSinkWayland::postWlBuffer(struct wl_buffer *wlBuf, MediaBufferSP mediaBuf)
{
    FUNC_TRACK();
    DEBUG("mPostBufferCount: %d, wl_buffer: %p", mPostBufferCount, wlBuf);
    mPostBufferCount++;

    int ret = 0, i = 0;
    YUNOS_MM::MMAutoLock locker(mLock);

    // check that the previous wl_buffer has been accepted by weston
    if (mDisplay->callback)
        mCond.timedWait(80000);

    // attach the wl_buffer to wl_surface
    wl_surface_attach(mDisplay->surface, wlBuf, 0, 0);

    // register frame callback -- be called when the buffer is accepted by weston
    if (mDisplay->callback) {
        WARNING("previous callback isn't released in time, callback: %p", mDisplay->callback);
        wl_callback_destroy(mDisplay->callback);
    }
    mDisplay->callback = wl_surface_frame(mDisplay->surface);
    wl_callback_add_listener(mDisplay->callback, &frame_listener, this);

    // register buffer release callback -- be called when the wl_buffer isn't used by weston any more
    // we'd hold a reference of media buffer before weston release it
    bool inserted2Map = false;
    for (i=0; i<MAX_COMMIT_BUF_COUNT; i++) {
        if (mWlBufMediaBufMap[i].wlBuffer == NULL) {
            mWlBufMediaBufMap[i].wlBuffer = wlBuf;
            mWlBufMediaBufMap[i].mediaBuffer = mediaBuf;
            inserted2Map = true;
            break;
        }
    }
    ASSERT(inserted2Map);
    // move it to createWlBuffer // wl_buffer_add_listener (wlBuf, &frame_buffer_listener, this);

    wl_surface_commit(mDisplay->surface);
    wl_display_flush(mDisplay->display);

    return ret;
}

mm_status_t VideoSinkWayland::drawCanvas(MediaBufferSP mediaBuffer)
{
    FUNC_TRACK();
    mm_status_t ret = MM_ERROR_SUCCESS;
    MediaBuffer::MediaBufferType type = mediaBuffer->type();
    struct wl_buffer* wl_buf = NULL;

    // usually, we'd check that previous frame_callback has been done before render another frame. (i.e. wlDisplay->callback is NULL)
    switch (type) {
    case MediaBuffer::MBT_DrmBufName:
        wl_buf = createWlBuffer(mDisplay, mediaBuffer);
        if (wl_buf)
            postWlBuffer(wl_buf, mediaBuffer);
        break;
    default:
        break;
    }

    return ret;
}

mm_status_t VideoSinkWayland::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();

    return VideoSink::setParameter(meta);
}


} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C"
{
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::VideoSinkWayland *sinkComponent = new YUNOS_MM::VideoSinkWayland();
    if (sinkComponent == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}

void releaseComponent(YUNOS_MM::Component *component)
{
    delete component;
    DEBUG();
}

}

