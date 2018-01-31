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

#ifndef __vide_consumer_base_H
#define __vide_consumer_base_H

#include <multimedia/mm_cpp_utils.h>
#include <multimedia/media_buffer.h>

class DataDump;
class NativeWindowBuffer;

namespace YUNOS_MM {

class VideoConsumerListener {
public:
    VideoConsumerListener() {}
    virtual ~VideoConsumerListener() {}

    virtual void bufferAvailable(void*, int64_t pts = -1ll) = 0;
};

class VideoConsumerCore {

public:
    static VideoConsumerCore* createVideoConsumer();

    virtual ~VideoConsumerCore();

    /* implement two methods in base class after surface and surface_v2 are merged */
    virtual bool createBufferQueue() { return false; }
    virtual const char* getSurfaceName() { return NULL; }

    /* connect to buffer queue producer
     * create virtual display if bq/consumer is not created
     */
    virtual bool connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum) = 0;

    /* disconnect to buffer queue producer */
    virtual void disconnect() = 0;

    virtual bool start() = 0;
    virtual bool stop() = 0;

    virtual void releaseBuffer(void *) = 0;

    void setListener(VideoConsumerListener *listener) {
        mListener = listener;
    }

    virtual MediaBufferSP createMediaBuffer(void *buffer) = 0;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    virtual MediaBufferSP createMediaBuffer(NativeWindowBuffer* buffer);
#endif

    virtual void doColorConvert(void*, void*, int32_t, int32_t, uint32_t, uint32_t) { return; }

protected:
    VideoConsumerCore();

protected:
    VideoConsumerListener* mListener;
    bool mForceDump;
    DataDump *mDataDump;
};

} // YUNOS_MM

#endif // __video_consumer_base_H
