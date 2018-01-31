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


#ifndef buffer_pool_surface_h
#define buffer_pool_surface_h

#include <vector>
#include <queue>

#include <multimedia/mm_types.h>
#include <multimedia/mm_cpp_utils.h>
#include <media_surface_utils.h>
// #include <cutils/graphics.h>

namespace YUNOS_MM {
class BufferPoolSurface {
  public:
    BufferPoolSurface();
    ~BufferPoolSurface();
    bool configure(uint32_t width, uint32_t height, uint32_t format, uint32_t count,
        YunOSMediaCodec::SurfaceWrapper* surface, uint32_t mFlags = 0, uint32_t transform = 0);
    /** get one free buffer from pool */
    MMNativeBuffer* getBuffer();
    /** release one buffer to pool */
    /* todo, when buffer is returned back to pool, there could be 3 options: render it: enque to surface; discard it: cancle to surface; simple put it back to pool, not surface */
    bool putBuffer(MMNativeBuffer* buffer, uint32_t renderIt);
    /** reset buffer status of pool */
    bool resetPool();

  private:
    bool mConfiged;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mFormat;
    uint32_t mCount;
    YunOSMediaCodec::SurfaceWrapper * mSurface;
    // bool mSelfSurface; // we do not create surface by the pool. it introduces additional complexity and dependency
    uint32_t mFlags;
    uint32_t mTransform;
    std::queue<MMNativeBuffer*> mFreeBuffers;

    // debug use for buffer status track
     typedef enum {
         OwnedUnknown,
         OwnedByClient,
         OwnedBySurface,
         OwnedByPoolFree,
    }BufferStatusT;
    class InternalBufferT {
      public:
        MMNativeBuffer* buffer;
        BufferStatusT status;
        explicit InternalBufferT(MMNativeBuffer* buf, BufferStatusT st)
            : buffer(buf), status(st) {}
    };
    std::vector<InternalBufferT> mBuffers;
    void updateBufferStatus(MMNativeBuffer *buffer, BufferStatusT status);
    BufferStatusT bufferStatus(MMNativeBuffer *buffer);
};

} // end of namespace YUNOS_MM
#endif//buffer_pool_surface_h

