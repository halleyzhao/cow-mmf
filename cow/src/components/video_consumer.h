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

#ifndef __vide_consumer_H
#define __vide_consumer_H

#include <video_consumer_base.h>
#include <ConsumerCore.h>

#include <memory>
#include <list>

class VirtualConnectionYun;

namespace yunos {
namespace libgui {
class BufferHolder;
}
}

namespace YUNOS_MM {

class VConsumerListener;

class VirtualDisplayConsumer : public yunos::libgui::ConsumerCore {
public:
    VirtualDisplayConsumer()
        : yunos::libgui::ConsumerCore() {
    }

    virtual ~VirtualDisplayConsumer() {
    }

    int acquireBuffer(std::shared_ptr<yunos::libgui::BufferHolder>& out) {
        return fetchBufferLocked(out);
    }
};

class VideoConsumer : public VideoConsumerCore {

public:
    VideoConsumer();
    virtual ~VideoConsumer();

    virtual bool connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum);
    virtual void disconnect();

    virtual bool start();
    virtual bool stop();

    virtual void releaseBuffer(void *);

    virtual MediaBufferSP createMediaBuffer(void *buffer);

    virtual void doColorConvert(void*, void*, int32_t, int32_t, uint32_t, uint32_t);

friend class VConsumerListener;

private:
    void onBufferAvailable();
    void dumpBuffer(gb_target_t, int, int, int);
    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

    VirtualConnectionYun* mVirConnection;
    bool mIsContinue;
    typedef std::shared_ptr<yunos::libgui::BufferHolder> BufferHolderSP;
    std::list<BufferHolderSP> mAcquiredBuffers;
    std::shared_ptr<VConsumerListener> mCListener;
    std::shared_ptr<VirtualDisplayConsumer> mGuiConsumer;

    int32_t mWidth;
    int32_t mHeight;
    uint32_t mFormat;
    int32_t mBufCnt;

    Lock mLock;
    Condition mCondition;
};

} // YUNOS_MM

#endif // __video_consumer_droid_H
