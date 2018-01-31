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

#include <video_consumer_droid.h>
#include <multimedia/mm_debug.h>
#include <mm_surface_compat.h>

#include <VirtualSurface.h>
#include <VirtualConnection.h>

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

MM_LOG_DEFINE_MODULE_NAME("VideoConsumer");

namespace YUNOS_MM {

class FrameAvailableListener : public VirtualDisplayListener {
public:
    FrameAvailableListener(VirtualSurface *surface, VideoConsumerDroid* owner)
        : VirtualDisplayListener(surface),
          mOwner(owner) {

    }

    virtual ~FrameAvailableListener() {}

    virtual void bufferAvailable(MMNativeBuffer* buffer) {
        if (mOwner)
            mOwner->onBufferAvailable((void*)buffer);
    }

private:
    VideoConsumerDroid *mOwner;
};

VideoConsumerDroid::VideoConsumerDroid()
    : MMThread("VideoConsumer"),
      mVirConnection(NULL),
      mVirSurface(NULL),
      mFrameAvailableListener(NULL),
      mIsContinue(false),
      mCondition(mLock) {
    ENTER1();
    EXIT1();
}

VideoConsumerDroid::~VideoConsumerDroid() {
    ENTER1();

    stop();

    if (mFrameAvailableListener)
        delete mFrameAvailableListener;

    EXIT1();
}

bool VideoConsumerDroid::connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum) {
    ENTER1();
    MMAutoLock lock(mLock);
    mVirConnection = new VirtualConnection(NULL);

    mVirSurface = new VirtualSurface(mVirConnection, w, h);
    mVirSurface->setBuffersFormat(format);
    mVirSurface->setBufferCount(bufNum);

    mFrameAvailableListener = new FrameAvailableListener(mVirSurface, this);

    mVirConnection->add_virtual_display_listener(mFrameAvailableListener);

    mVirConnection->create_virtual_display(mVirSurface, "virtual display", 0);
    EXIT_AND_RETURN1(true);
}

void VideoConsumerDroid::disconnect() {
    ENTER1();

    if (!stop()) {
        WARNING("stop fail");
        EXIT1();
    }

    MMAutoLock lock(mLock);

    INFO("virtual display exit");
    if (mVirSurface)
        delete mVirSurface;

    if (mVirConnection) {
        mVirConnection->dec_ref();
        delete mVirConnection;
    }

    EXIT1();
}

bool VideoConsumerDroid::start() {
    ENTER1();
    MMAutoLock lock(mLock);

    mIsContinue = true;
    if (MMThread::create())
        EXIT_AND_RETURN(false);

    EXIT_AND_RETURN1(false);
}

bool VideoConsumerDroid::stop() {
    ENTER1();

    MMAutoLock lock(mLock);

    if (!mIsContinue)
        EXIT_AND_RETURN1(true);

    mIsContinue = false;

    if (mVirConnection) {
        INFO("virtual display trigger a event");
        mVirConnection->dispatch_trigger(mVirConnection);
        mCondition.wait();
    }

    MMThread::destroy();

    EXIT_AND_RETURN1(true);
}

void VideoConsumerDroid::releaseBuffer(void* buffer) {
    VERBOSE("return buffer to bq");

    if (mVirConnection)
        mVirConnection->release_buffer((MMNativeBuffer*)buffer);
}

void VideoConsumerDroid::onBufferAvailable(void* buffer) {
    //AutoLock lock(mLock);
    if (mListener)
        mListener->bufferAvailable(buffer);
}

void VideoConsumerDroid::main() {
    ENTER1();

    INFO("virtual connection is %p, start dispatch", mVirConnection);
    while(mIsContinue && mVirConnection) {
        mVirConnection->dispatch(false);
    }

    {
        MMAutoLock lock(mLock);
        INFO("virtual connection is %p, destroy virtual display", mVirConnection);
        mVirConnection->destroy_virtual_display();
        mCondition.signal();
    }

    EXIT1();
}

MediaBufferSP VideoConsumerDroid::createMediaBuffer(void *buffer) {
    uint8_t *ptr;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;

    ptr = new uint8_t[sizeof(buffer_handle_t)];

    *(buffer_handle_t *)ptr = ((MMNativeBuffer*)buffer)->handle;
    mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_GraphicBufferHandle);
    mediaBuf->setSize(sizeof(buffer_handle_t));

    bufOffset = 0;
    bufStride = sizeof(buffer_handle_t);
    mediaBuf->setBufferInfo((uintptr_t *)&ptr, &bufOffset, &bufStride, 1);

    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

    return mediaBuf;
}

/* static */
bool VideoConsumerDroid::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    uint8_t *ptr = NULL;

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&ptr, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN1(false);
    }

    delete [] ptr;
    EXIT_AND_RETURN(true);
}

} // YUNOS_MM
