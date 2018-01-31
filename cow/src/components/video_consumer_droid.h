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

#ifndef __vide_consumer_droid_H
#define __vide_consumer_droid_H

#include <video_consumer_base.h>
#include <multimedia/mmthread.h>

class VirtualConnection;
class VirtualSurface;

namespace YUNOS_MM {

class FrameAvailableListener;

class VideoConsumerDroid : public VideoConsumerCore,
                           public MMThread {

public:
    VideoConsumerDroid();
    virtual ~VideoConsumerDroid();

    virtual bool connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum);
    virtual void disconnect();

    virtual bool start();
    virtual bool stop();

    virtual void releaseBuffer(void *);

    virtual MediaBufferSP createMediaBuffer(void *buffer);

friend class FrameAvailableListener;

protected:
    virtual void main();

private:
    void onBufferAvailable(void *buffer);

    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

    VirtualConnection* mVirConnection;
    VirtualSurface* mVirSurface;
    FrameAvailableListener *mFrameAvailableListener;
    bool mIsContinue;

    Lock mLock;
    Condition mCondition;
};

} // YUNOS_MM

#endif // __video_consumer_droid_H
