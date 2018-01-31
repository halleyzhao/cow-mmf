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

#include "buffer_pool_surface.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_errors.h"

MM_LOG_DEFINE_MODULE_NAME("BufferPoolSurface");

#define CHECK_SURFACE_OPS_RET(ret, funcName) do {                       \
        uint32_t my_errno = errno;                                                                          \
        VERBOSE("%s ret: %d", funcName, ret);                                 \
        if (ret) {                                                          \
            ERROR("%s failed: %s (%d)", funcName, strerror(my_errno), my_errno);  \
            return MM_ERROR_UNKNOWN;                                        \
        } else {                                                            \
            VERBOSE("%s done success\n");                                      \
        }                                                                   \
    } while(0)
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

namespace YUNOS_MM {
BufferPoolSurface ::BufferPoolSurface()
    : mConfiged(false)
    , mWidth(0)
    , mHeight(0)
    , mFormat(0)
    , mCount(0)
    , mSurface(NULL)
    , mFlags(0)
{ }
BufferPoolSurface ::~BufferPoolSurface()
{ }
bool BufferPoolSurface::configure(uint32_t width, uint32_t height, uint32_t format, uint32_t count,
        YunOSMediaCodec::SurfaceWrapper* surface, uint32_t mFlags, uint32_t transform )
{
    int ret = 0;
    uint32_t i=0;

    FUNC_TRACK();
    if (!width || !height || !format || !count || !surface) {
        ERROR("invalid parameter to init buffer pool: width: %d, height: %d, format: 0x%x, count: %d, surface: %p",
            width, height, format, count, surface);
        return false;
    }

    // 1. config WindowSurface
    INFO("config mSurface with size %dx%d", mWidth, mHeight);
    ret = mSurface->set_buffers_dimensions(mWidth, mHeight, mFlags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_dimensions");

    if (mTransform > 0) {
        ret = mSurface->set_buffers_transform(mTransform, mFlags);
        CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_transform");
    }

    INFO("native_set_buffers_format 0x%x", mFormat);
    ret = mSurface->set_buffers_format(mFormat, mFlags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_format");

    #if  defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    uint32_t bufferUsage = YALLOC_FLAG_HW_VIDEO_DECODER | YALLOC_FLAG_HW_TEXTURE | YALLOC_FLAG_HW_RENDER;
    #else
    uint32_t bufferUsage = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP;
    #endif
    ret = mSurface->set_usage(bufferUsage, mFlags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_usage");

    // set buffer count to 0 to release previous buffers
    ret = mSurface->set_buffer_count(0, mFlags);
    ret = mSurface->set_buffer_count(mCount, mFlags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffer_count");

    // 2. alloc/populate surface buffers
    mBuffers.clear();
    while (!mFreeBuffers.empty())
        mFreeBuffers.pop();
    MMNativeBuffer *nwb = NULL;
    for (i = 0; i < mCount; i++) {
        ret = mSurface->dequeue_buffer_and_wait(&nwb, mFlags);
        //handle_fence(fencefd);
        if (ret) {
            ERROR("dequeue buffer failed, %s(%d)", strerror(ret), ret);
            return false;
        }
        mFreeBuffers.push(nwb);
        mBuffers.push_back(InternalBufferT(nwb, OwnedByPoolFree));
    }

    mConfiged = true;
     return mConfiged;
}

/** get one free buffer from pool */
MMNativeBuffer* BufferPoolSurface ::getBuffer()
{
    FUNC_TRACK();
    if (mFreeBuffers.empty())
        return NULL;

    MMNativeBuffer* nwb = mFreeBuffers.front();
    mFreeBuffers.pop();
    updateBufferStatus(nwb, OwnedByClient);
    return nwb;
}

/** release one buffer to pool */
bool BufferPoolSurface ::putBuffer(MMNativeBuffer* buffer, uint32_t renderIt)
{
    int ret = 0;

     FUNC_TRACK();
    if (renderIt > 2)
        return false;

    if (renderIt ==0 || renderIt == 1) {
        // put buffer to surface
        ret = renderIt ? mSurface->queueBuffer(buffer, -1, mFlags) : mSurface->cancel_buffer(buffer, -1, mFlags);
        CHECK_SURFACE_OPS_RET(ret, "cancel_buffer/queueBuffer");
        updateBufferStatus(buffer, OwnedBySurface);
        // retrieve one buffer from surface
        ret = mSurface->dequeue_buffer_and_wait(&buffer, mFlags);
        CHECK_SURFACE_OPS_RET(ret, "dequeue_buffer_and_wait");
    }

    mFreeBuffers.push(buffer);
    updateBufferStatus(buffer, OwnedByPoolFree);

    return true;
}

/** reset buffer status of pool */
bool BufferPoolSurface ::resetPool()
{
     FUNC_TRACK();
     WARNING("resetPool, not impl yet");
     return false;
}

/** for debug use */
void BufferPoolSurface::updateBufferStatus(MMNativeBuffer *buffer, BufferStatusT status)
{
    uint32_t i = 0;
    bool found = false;

    FUNC_TRACK();
    for (i=0; i<mCount; i++) {
        if (mBuffers[i].buffer == buffer) {
            switch(status) {
            case OwnedByClient:
                MMASSERT(mBuffers[i].status == OwnedByPoolFree);
            break;
            case OwnedBySurface:
                MMASSERT(mBuffers[i].status == OwnedByClient);
                break;
            case OwnedByPoolFree:
                MMASSERT(mBuffers[i].status != OwnedByPoolFree);
                break;
            default:
                MMASSERT(0 && "invalid status");
                break;
            }
            found = true;
            mBuffers[i].status = status;
        }
    }

    if (!found) {
        ERROR("the buffer: %p doesn't belong to surface buffer pool", buffer);
    }
}

BufferPoolSurface::BufferStatusT BufferPoolSurface::bufferStatus(MMNativeBuffer *buffer)
{
    uint32_t i = 0;
    BufferStatusT status = OwnedUnknown;

    FUNC_TRACK();
    for (i=0; i<mCount; i++) {
        if (mBuffers[i].buffer == buffer) {
            status = mBuffers[i].status;
            break;
        }
    }

    return status;
}

} // end of namespace YUNOS_MM

