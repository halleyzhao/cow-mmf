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

#ifndef __vide_consumer_drm_H
#define __vide_consumer_drm_H

#include <video_consumer_base.h>
#include <multimedia/mmthread.h>
#include <mm_surface_compat.h>
#include <map>

#include <csc_filter.h>

namespace yunos {
class YVirtualConnection;
class YVirtualSurface;
struct DrmBuffer;
class DrmAllocator;
}

using namespace yunos;

namespace YUNOS_MM {

class FrameAvailableListener;

//#define MM_VIDEOSOURCE_SW_CSC

class VideoConsumerDrm : public VideoConsumerCore,
#ifdef MM_VIDEOSOURCE_SW_CSC
                         public CscFilter::Listener,
#endif
                           public MMThread {

public:
    VideoConsumerDrm();
    virtual ~VideoConsumerDrm();

    virtual bool connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum);
    virtual void disconnect();

    virtual bool start();
    virtual bool stop();

    virtual void releaseBuffer(void *);

    virtual MediaBufferSP createMediaBuffer(void *buffer);

#ifdef MM_VIDEOSOURCE_SW_CSC
    // CscFilter::Listener
    virtual void onBufferEmptied(MMNativeBuffer* buffer);
    virtual void onBufferFilled(MMNativeBuffer* buffer, int64_t pts);
#endif

friend class FrameAvailableListener;

protected:
    virtual void main();

private:
    void onBufferAvailable(DrmBuffer *buffer);

    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);
    void releaseBuffer_l(void *);

    YVirtualConnection* mVirConnection;
    YVirtualSurface* mVirSurface;
    FrameAvailableListener *mFrameAvailableListener;
    bool mIsContinue;

    Lock mLock;
    Condition mCondition;
    std::map<DrmBuffer*, MMNativeBuffer*> mNativeBufferMap;
#ifdef MM_VIDEOSOURCE_SW_CSC
    CscFilter *mFilter;
#endif
    DrmAllocator *mDrmAlloc;
};

} // YUNOS_MM

#endif // __video_consumer_drm_H
