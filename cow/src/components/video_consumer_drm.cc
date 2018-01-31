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

#include <video_consumer_drm.h>
#include <multimedia/mm_debug.h>

#include <YVirtualSurface.h>
#include <YVirtualConnection.h>

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

MM_LOG_DEFINE_MODULE_NAME("VideoConsumer");

namespace YUNOS_MM {

class FrameAvailableListener : public YVirtualDisplayListener {
public:
    FrameAvailableListener(YVirtualSurface *surface, VideoConsumerDrm* owner)
        : YVirtualDisplayListener(surface),
          mOwner(owner) {

    }

    virtual ~FrameAvailableListener() {}

    //virtual void bufferAvailable(MMNativeBuffer* buffer) {
    virtual void bufferAvailable(DrmBuffer* buffer) {
        if (mOwner)
            mOwner->onBufferAvailable(buffer);
    }

private:
    VideoConsumerDrm *mOwner;
};

VideoConsumerDrm::VideoConsumerDrm()
    : MMThread("VideoConsumer"),
      mVirConnection(NULL),
      mVirSurface(NULL),
      mFrameAvailableListener(NULL),
      mIsContinue(false),
      mCondition(mLock),
      mDrmAlloc(NULL) {
    ENTER1();
#ifdef MM_VIDEOSOURCE_SW_CSC
    mFilter = new CscFilter();
    mFilter->setListener(this);
#endif
    EXIT1();
}

VideoConsumerDrm::~VideoConsumerDrm() {
    ENTER1();

    stop();

    if (mFrameAvailableListener)
        delete mFrameAvailableListener;

    {
        MMAutoLock lock(mLock);
        std::map<DrmBuffer*, MMNativeBuffer*>::iterator it = mNativeBufferMap.begin();
        for (; it != mNativeBufferMap.end(); it++) {
            delete it->second;
            it->second = NULL;
        }
    }

#ifdef MM_VIDEOSOURCE_SW_CSC
    delete mFilter;
#endif
    EXIT1();
}

bool VideoConsumerDrm::connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum) {
    ENTER1();
    MMAutoLock lock(mLock);
    INFO("%dx%d format %x, bufnum %d", w, h, format, bufNum);
    mVirConnection = new YVirtualConnection(NULL);

    //int screenWidth = MM_SCREEN_WIDTH;
    //int screenHeight = MM_SCREEN_HEIGHT;
    int screenWidth = w;
    int screenHeight = h;
    INFO("create virtual display %dx%d", screenWidth, screenHeight);

    mVirSurface = new YVirtualSurface(mVirConnection, screenWidth, screenHeight);
    //mVirSurface->setBuffersFormat(format);
    mVirSurface->setBuffersFormat(WL_DRM_FORMAT_ARGB8888);
    mVirSurface->setBufferCount(bufNum);

    mFrameAvailableListener = new FrameAvailableListener(mVirSurface, this);

    mVirConnection->add_virtual_display_listener(mFrameAvailableListener);

    mVirConnection->create_virtual_display(mVirSurface, "virtual display", 0);

#ifdef MM_VIDEOSOURCE_SW_CSC
    int ret = mFilter->configureInput(screenWidth, screenHeight, 0, bufNum, 0);
    if (!ret) {
        ERROR("fail to config input buffer");
        return false;
    }

    if (!mDrmAlloc) {
        mDrmAlloc = new DrmAllocator(mVirConnection->get_wayland_display());
    }
    int fd = mDrmAlloc->getDrmDevice();
    ret = mFilter->configureOutput(w, h, 0, fd);
    if (!ret) {
        ERROR("fail to config output buffer");
        return false;
    }
#endif
    EXIT_AND_RETURN1(true);
}

void VideoConsumerDrm::disconnect() {
    ENTER1();

    if (!stop()) {
        WARNING("stop fail");
        EXIT1();
    }

    MMAutoLock lock(mLock);

    INFO("virtual display exit");
    if (mVirSurface)
        delete mVirSurface;

    if (mDrmAlloc)
        delete mDrmAlloc;

    if (mVirConnection) {
        mVirConnection->dec_ref();
        delete mVirConnection;
    }

    EXIT1();
}

bool VideoConsumerDrm::start() {
    ENTER1();

#ifdef MM_VIDEOSOURCE_SW_CSC
    mFilter->start();
#endif

    MMAutoLock lock(mLock);

    mIsContinue = true;
    if (MMThread::create())
        EXIT_AND_RETURN(false);

    EXIT_AND_RETURN1(false);
}

bool VideoConsumerDrm::stop() {
    ENTER1();

#ifdef MM_VIDEOSOURCE_SW_CSC
    mFilter->setListener(NULL);
    mFilter->stop();
#endif

    MMAutoLock lock(mLock);

    if (!mIsContinue)
        EXIT_AND_RETURN1(true);

    mIsContinue = false;

    if (mVirConnection) {
        INFO("virtual display trigger a event");
        // TODO need to quit dispatch thread
        // mVirConnection->dispatch_trigger(mVirConnection);
        mCondition.wait();
    }

    MMThread::destroy();

    EXIT_AND_RETURN1(true);
}

void VideoConsumerDrm::releaseBuffer(void* buffer) {
    // encoder return buffer
#ifdef MM_VIDEOSOURCE_SW_CSC
    mFilter->fillBuffer((MMNativeBuffer*)buffer);
#else
    releaseBuffer_l(buffer);
#endif
}

void VideoConsumerDrm::releaseBuffer_l(void* buffer) {
    DEBUG("return buffer to virtual display %p", buffer);
    DrmBuffer* buf = NULL;
    {
        MMAutoLock lock(mLock);
        std::map<DrmBuffer*, MMNativeBuffer*>::iterator it = mNativeBufferMap.begin();
        for (; it != mNativeBufferMap.end(); it++) {
            if (it->second == (MMNativeBuffer*)buffer) {
                buf = it->first;
                break;
            }
        }
        if (!buf)
            WARNING("invalid buffer %p to return", buffer);
    }

    if (mVirConnection && buf)
        mVirConnection->release_buffer(buf);
}

void VideoConsumerDrm::onBufferAvailable(DrmBuffer* buffer) {
    MMNativeBuffer *buf = NULL;
    if (buffer) {
        MMAutoLock lock(mLock);
        std::map<DrmBuffer*, MMNativeBuffer*>::iterator it = mNativeBufferMap.find(buffer);
        if (it == mNativeBufferMap.end()) {
            INFO("new drm buffer%p: fd %d, dimension %dx%d, stride %d, index %d",
                 buffer, buffer->fd, buffer->width, buffer->height, buffer->stride, mNativeBufferMap.size());
            MMNativeBuffer *tmp = new MMNativeBuffer;
            memset(tmp, 0, sizeof(MMNativeBuffer));
            buf = tmp;
            tmp->width = buffer->width;
            tmp->height = buffer->height;
            tmp->pitches[0] = tmp->pitches[1] = buffer->stride;
            tmp->offsets[0] = tmp->offsets[1] = 0;
            tmp->fd[0] = buffer->fd;
#ifdef MM_VIDEOSOURCE_SW_CSC
            tmp->bo[0] = mFilter->getMapBoFromName(buffer->handle);
#endif
            mNativeBufferMap[buffer] = tmp;
        } else
            buf = it->second;
    }

#ifdef MM_VIDEOSOURCE_SW_CSC
    DEBUG("DrmBuffer %p", buffer);
    int64_t pts = getTimeUs();
    int ret = mFilter->emptyBuffer(buf, pts);
    if (!ret)
        ERROR("emptyBuffer err");
#else
    if (mListener)
        mListener->bufferAvailable((void*)buf);
#endif

}

#ifdef MM_VIDEOSOURCE_SW_CSC
void VideoConsumerDrm::onBufferEmptied(MMNativeBuffer* buffer) {
    ENTER1();

    // return to weston
    releaseBuffer_l(buffer);
}

void VideoConsumerDrm::onBufferFilled(MMNativeBuffer* buffer, int64_t pts) {
    ENTER1();

    if (mListener)
        mListener->bufferAvailable((void*)buffer, pts);
}
#endif

void VideoConsumerDrm::main() {
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

MediaBufferSP VideoConsumerDrm::createMediaBuffer(void *buffer) {
    uint8_t *ptr;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;

    ptr = new uint8_t[sizeof(MMBufferHandleT)];
    *(MMBufferHandleT *)ptr = (MMBufferHandleT)mm_getBufferHandle((MMNativeBuffer*)buffer);

    mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_DrmBufName);
    mediaBuf->setSize(sizeof(MMBufferHandleT));

    bufOffset = 0;
    bufStride = sizeof(MMBufferHandleT);
    mediaBuf->setBufferInfo((uintptr_t *)&ptr, &bufOffset, &bufStride, 1);

    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

    return mediaBuf;
}

/* static */
bool VideoConsumerDrm::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    uint8_t *ptr = NULL;

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&ptr, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN1(false);
    }

    delete [] ptr;
    EXIT_AND_RETURN(true);
}

} // YUNOS_MM
