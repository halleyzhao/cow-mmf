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

#ifndef video_sink_wayland_h
#define video_sink_wayland_h

#include "multimedia/mmmsgthread.h"
#include "video_sink.h"
#include <wayland-client.h>
#include <wayland-drm-client-protocol.h>
#include <scaler-client-protocol.h>
#include <map>

// #include "mm_surface_compat.h"
#define MAX_COMMIT_BUF_COUNT   4

namespace YUNOS_MM
{
class WlDisplayType;
class VideoSinkWayland : public VideoSink
{

public:
    VideoSinkWayland();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    typedef struct {
        struct wl_buffer *wlBuffer;
        MediaBufferSP mediaBuffer;
    } BufferMap;

protected:
    virtual ~VideoSinkWayland();
    virtual const char * name() const;

private:
    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t flushCanvas();
    virtual mm_status_t uninitCanvas();
    struct wl_buffer* createWlBuffer(WlDisplayType *wlDisplay, MediaBufferSP mediaBuffer);
    int postWlBuffer(struct wl_buffer *wlBuf, MediaBufferSP mediaBuf);
    // wayland callback must run inside the life cycle of VideoSinkWayland
    friend void frame_callback(void *data, struct wl_callback *callback, uint32_t time);
    friend void buffer_release_callback (void *data, struct wl_buffer *wlBuffer);

private:
    WlDisplayType* mDisplay;
    Lock mLock;
    Condition mCond;
    /* usually weston may hold up to 3 wl_buffer submit from client.
     * one is using by composition, another one is in the wait list for next composition.
     * and possible the 3rd one just commited (then the one in wait list will be released/replaced soon)
     */
    BufferMap mWlBufMediaBufMap[MAX_COMMIT_BUF_COUNT];
    std::map<uint32_t, void*> mDrmWlBufMap;

    // debug use only
    uint32_t mInBufferCount;
    uint32_t mPostBufferCount;
};//VideoSinkWayland

}// end of namespace YUNOS_MM
#endif//video_sink_wayland_h
