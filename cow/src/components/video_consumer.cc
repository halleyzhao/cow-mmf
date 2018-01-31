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

#include <video_consumer.h>
#include <multimedia/mm_debug.h>

#include <VirtualConnectionYun.h>
#include <YunAllocator.h>
#include <ServerBuffer.h>
#include <Errcode.h>
#include <NativeWindowBuffer.h>

#include <string>
#include <sstream>

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ALIGN16(a) ((a + 0xf) & (~0xf))

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("VideoConsumer");

using namespace yunos::libgui;

class VConsumerListener : public IConsumerObserver
{
public:
    explicit VConsumerListener(VideoConsumer *consumer)
        : mOwner(consumer)
    {
    }

    virtual ~VConsumerListener() {
        onFrameAvailable();
    }

    virtual void onFrameAvailable() override {
        INFO("");
        if (mOwner)
            mOwner->onBufferAvailable();
    }

    virtual void onPropertyChanged(uint32_t flag, const prop_type& prop) override {
        INFO("%u\n", flag);
        if (flag & IConsumerObserver::PC_TRANSFORM) {
            uint32_t trs;
            //if (mOwner)
                //mOwner->getCurrentTransform(trs);
            INFO("get transform %u\n", trs);
            INFO("get transform %u\n", prop.transform);
        }
        if (flag & IConsumerObserver::PC_CROP) {
            Rect rect;
            //if (mOwner)
                //mOwner->getCurrentCrop(rect);
            INFO("get crop (%d, %d, %d, %d)\n",
                  rect.left, rect.top, rect.right, rect.bottom);

            INFO("get crop (%d, %d, %d, %d)\n",
                  prop.rect.left, prop.rect.top, prop.rect.right, prop.rect.bottom);

        }
    }

private:
    VideoConsumer* mOwner;
};

VideoConsumer::VideoConsumer()
    : mVirConnection(NULL),
      mIsContinue(false),
      mWidth(0),
      mHeight(0),
      mFormat(0),
      mBufCnt(0),
      mCondition(mLock) {
    ENTER1();
    mCListener.reset(new VConsumerListener(this));
    EXIT1();
}

VideoConsumer::~VideoConsumer() {
    ENTER1();

    stop();
    /* destroy it before VConsumerListener */
    mGuiConsumer.reset();

    EXIT1();
}

bool VideoConsumer::connect(int32_t w, int32_t h, uint32_t format, int32_t bufNum) {
    ENTER1();
    MMAutoLock lock(mLock);
    // TODO create buffer queue and send bqp to virtual display
    // TODO use a flag to decide to create virtual display
    mWidth = w;
    mHeight = h;
    mFormat = format;
    mBufCnt = bufNum;

    /*
    mVirConnection = new VirtualConnectionYun(NULL);
    VirtualSurfaceConfig vsc;
    vsc.width = mWidth;
    vsc.height = mHeight;
    // currently yunhal only supports RGBX8888
    vsc.format = YUN_HAL_FORMAT_RGBX_8888;

    mVirConnection->create_virtual_display(&vsc, "virtual display", 0);
    mGuiConsumer = std::make_shared<VirtualDisplayConsumer>();
    INFO("init ConsumerCore");
    mGuiConsumer->init(mVirConnection->getBufferQueue());
    INFO("init ConsumerCore done");
    mGuiConsumer->setConsumerListener(mCListener);
    */

    EXIT_AND_RETURN1(true);
}

void VideoConsumer::disconnect() {
    ENTER1();

    if (!stop()) {
        WARNING("stop fail");
        EXIT1();
    }

    MMAutoLock lock(mLock);

    INFO("virtual display exit");

    if (mGuiConsumer)
        mGuiConsumer->dispose();

    if (mVirConnection) {
        mVirConnection->destroy_virtual_display();
        //mVirConnection->dec_ref();
        delete mVirConnection;
    }

    EXIT1();
}

bool VideoConsumer::start() {
    ENTER1();
    MMAutoLock lock(mLock);

    if (mIsContinue)
        EXIT_AND_RETURN1(true);

    mVirConnection = new VirtualConnectionYun(NULL);
    VirtualSurfaceConfig vsc;
    vsc.width = mWidth;
    vsc.height = mHeight;
    // currently yunhal only supports RGBX8888
    vsc.format = YUN_HAL_FORMAT_RGBX_8888;

    mVirConnection->create_virtual_display(&vsc, "virtual display", 0);
    mGuiConsumer = std::make_shared<VirtualDisplayConsumer>();
    INFO("init ConsumerCore");
    mGuiConsumer->init(mVirConnection->getBufferQueue());
    INFO("init ConsumerCore done");
    mGuiConsumer->setConsumerListener(mCListener);
    mIsContinue = true;

    EXIT_AND_RETURN1(true);
}

bool VideoConsumer::stop() {
    ENTER1();

    MMAutoLock lock(mLock);

    mAcquiredBuffers.clear();

    if (!mIsContinue)
        EXIT_AND_RETURN1(true);

    mIsContinue = false;

    EXIT_AND_RETURN1(true);
}

void VideoConsumer::releaseBuffer(void* buffer) {
    INFO("return buffer holder %p to bq, %d buffers acquired currently",
             buffer, mAcquiredBuffers.size());

    MMAutoLock lock(mLock);

    std::list<BufferHolderSP>::iterator it = mAcquiredBuffers.begin();
    for (; it != mAcquiredBuffers.end(); it++) {
        if ((*it).get() == ((BufferHolder*)buffer))
            break;
    }

    if (it != mAcquiredBuffers.end())
        mAcquiredBuffers.erase(it);
    else
        ERROR("release unknown buffer");
}

void VideoConsumer::onBufferAvailable() {
    int ret = -1;
    BufferHolderSP bufferHolder;

    if (mGuiConsumer)
        ret = mGuiConsumer->acquireBuffer(bufferHolder);
    else
        ERROR("gui consumer is NULL");

    if (OK != ret) {
        if (BufferPipeConsumer::NO_BUFFER == ret) {
            ERROR("no buffer to acquire");
            return;
        } else {
            ERROR("Error acquireBuffer: %s\n", strerror(ret));
            return;
        }
    }

#if 0
    std::shared_ptr<ServerBuffer> serBuffer;
    serBuffer = bufferHolder->getBuffer();
    dumpBuffer(serBuffer->getNativeBuffer()->target,
               serBuffer->getWidth(),
               serBuffer->getHeight(),
               serBuffer->getFormat());
    INFO("fd is %d", serBuffer->getNativeBuffer()->target->fds.data[0]);
#endif

    {
        MMAutoLock lock(mLock);
        mAcquiredBuffers.push_back(bufferHolder);
    }

    if (mListener)
        mListener->bufferAvailable(bufferHolder.get());
}

MediaBufferSP VideoConsumer::createMediaBuffer(void *buffer) {
    uint8_t *ptr;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;
    std::shared_ptr<ServerBuffer> serBuffer;

    {
        MMAutoLock lock(mLock);

        std::list<BufferHolderSP>::iterator it = mAcquiredBuffers.begin();
        for (; it != mAcquiredBuffers.end(); it++) {
            if ((*it).get() == ((BufferHolder*)buffer)) {
                serBuffer = ((BufferHolder*)buffer)->getBuffer();
                break;
            }
        }

        if (!serBuffer) {
            ERROR("unknown buffer %p", buffer);
            return mediaBuf;
        }
    }

    ptr = new uint8_t[sizeof(gb_target_t)];

    *(gb_target_t*)ptr = serBuffer->getNativeBuffer()->target;
    mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_GraphicBufferHandle);
    mediaBuf->setSize(sizeof(gb_target_t));

    bufOffset = 0;
    bufStride = sizeof(gb_target_t);
    mediaBuf->setBufferInfo((uintptr_t *)&ptr, &bufOffset, &bufStride, 1);

    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

    if (mForceDump)
        dumpBuffer(serBuffer->getNativeBuffer()->target,
                   serBuffer->getWidth(),
                   serBuffer->getHeight(),
                   serBuffer->getFormat());

    return mediaBuf;
}

void VideoConsumer::dumpBuffer(gb_target_t target, int w, int h, int format) {
    void *vaddr = NULL;
    YunAllocator &allocator(YunAllocator::get());

    allocator.map(target, YALLOC_FLAG_SW_READ_OFTEN|YALLOC_FLAG_SW_WRITE_OFTEN,
                  0, 0, w, h, &vaddr);

    if (!vaddr) {
        ERROR("map fail, abort dump");
        return;
    }

    if (!mDataDump) {
        std::string filename;
        std::stringstream ss;

        if (format == FMT_PREF(RGBX_8888))
            ss << "/data/vc_" << w << "x" << h << "_RGBA" << std::endl;
        else
            ss << "/data/vc_" << w << "x" << h << "_YUV420" << std::endl;

        ss >> filename;
        mDataDump = new DataDump(filename.c_str());
    }

    int size = w * h * 4;

    if (format != FMT_PREF(RGBX_8888))
        size = w * h * 3 / 2; // assume yuv420

    mDataDump->dump(vaddr, size);
    allocator.unmap(target);
}

/* static */
bool VideoConsumer::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    uint8_t *ptr = NULL;

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&ptr, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN1(false);
    }

    delete [] ptr;
    EXIT_AND_RETURN(true);
}

void VideoConsumer::doColorConvert(void* srcBuf, void* destBuf, int32_t w, int32_t h, uint32_t srcFormat, uint32_t destFormat) {
    ENTER();
    YunAllocator &allocator(YunAllocator::get());

    if (!srcBuf || !destBuf) {
        ERROR("NULL buffer, src %p, dest %p", srcBuf, destBuf);
        EXIT();
    }

    void *src = NULL;
    void *dest = NULL;
    NativeWindowBuffer *wnbDest;
    BufferHolder *wnbSrc;

    Rect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = w;
    rect.bottom = h;

    wnbSrc = static_cast<BufferHolder*>(srcBuf);
    gb_target_t target = wnbSrc->getBuffer()->getNativeBuffer()->target;

    allocator.map(target, YALLOC_FLAG_SW_READ_OFTEN|YALLOC_FLAG_SW_WRITE_OFTEN,
                  0, 0, w, h, &dest);

    wnbDest = static_cast<NativeWindowBuffer*>(destBuf);
    wnbDest->lock(ALLOC_USAGE_PREF(SW_WRITE_OFTEN), rect, &dest);
    // only copy Y plane
    if (dest && src) {
        memcpy(dest, src, mWidth*mHeight);
    }

    allocator.unmap(target);
    wnbDest->unlock();

    EXIT();
}

} // YUNOS_MM
