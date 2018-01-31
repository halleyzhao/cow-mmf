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

#ifndef __media_native_buffer_h__
#define __media_native_buffer_h__

struct wl_buffer;
struct omap_bo;

struct MMWlDrmBuffer {
    uint32_t fourcc, width, height;
    struct omap_bo *bo[4];
    int32_t pitches[4];
    int32_t offsets[4];
    bool multiBo;   /* True when Y and U/V are in separate buffers/drm-bo. */
    int fd[4];      /* dmabuf */
    int size;       /* bo size used by EGL_RAW_VIDEO_TI_DMABUF */
    bool usedByCodec; /* true: since send to codec, until queueBuffer; freeBufID[] is tracked by v4l2device */
    bool usedByDisplay; /* true: since send to weston, until wl_buffer call_back */
    uint32_t drmName;
    wl_buffer *wl_buf;
    int32_t cropX;
    int32_t cropY;
    int32_t cropW;
    int32_t cropH;
};

struct MMVpuBuffer {
    uint32_t fourcc;
    uint32_t width;
    uint32_t height;


    int32_t cropX;
    int32_t cropY;
    int32_t cropW;
    int32_t cropH;

    int32_t reserved[4];
};

#endif
