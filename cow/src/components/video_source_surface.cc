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

#include "video_source_surface.h"
#include "video_source_base_impl.h"

//TODO clean up some header includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <ywindow.h>
#include <NativeWindowBuffer.h>

#include "mm_vendor_format.h"

#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   720

namespace YUNOS_MM {

#undef DEBUG_PERF
#undef DEBUG_BUFFER
#define MULTI_THREAD_RETURN

#define __SUPPORT_FORMAT_CONVERT__ 1
//#define DEBUG_PERF

#ifdef DEBUG_PERF
#define TIME_TAG(str) printTimeMs(str)
void printTimeMs(const char *str) {

    uint64_t nowMs;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    nowMs = t.tv_sec * 1000LL + t.tv_nsec / 1000000LL;

    INFO("%s at %" PRId64 "ms\n", str, (nowMs % 10000));
}
#else
#define TIME_TAG(str)
#endif

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

static const char * COMPONENT_NAME = "VideoSourceSurface";

BEGIN_MSG_LOOP(VideoSourceSurface)
    MSG_ITEM(CAMERA_MSG_prepare, onPrepare)
    MSG_ITEM(CAMERA_MSG_stop, onStop)
END_MSG_LOOP()

VideoSourceSurface::VideoSourceSurface(const char* mime) : VideoSourceBase(COMPONENT_NAME) {
    ENTER();

    mComponentName = COMPONENT_NAME;
    mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, MediaBuffer::MBT_GraphicBufferHandle);
    mMime = mime;

    EXIT();
}

VideoSourceSurface::~VideoSourceSurface() {
    ENTER();

    EXIT();
}

/*
mm_status_t VideoSourceSurface::stop() {
    ENTER();

    if (frameSourceSP)
        frameSourceSP->stop();

    CAMERA_MSG_BRIDGE(CAMERA_MSG_stop, 0, NULL, mAsyncStop);

    EXIT_AND_RETURN(status);
}
*/

mm_status_t VideoSourceSurface::prepare() {
    ENTER1();

    mm_status_t status;

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        // do DRM auth in source onPrepare before other plugins
        INFO("sync prepare video source on DRM platform");
        status = processMsg(CAMERA_MSG_prepare, 0, NULL);
#else
        status = VideoSourceBase<SurfaceFrameGenerator>::prepare();
#endif

    EXIT_AND_RETURN(status);
}

void VideoSourceSurface::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO add specific prepare
    VideoSourceBase<SurfaceFrameGenerator>::onPrepare(param1, param2, rspId);

    EXIT();
}

mm_status_t VideoSourceSurface::setParameter(const MediaMetaSP & meta) {
    ENTER();
    mm_status_t status;
    status = VideoSourceBase<SurfaceFrameGenerator>::setParameter(meta);

    if (status == MM_ERROR_SUCCESS) {
        int32_t width, height;
        float fps;
        bool ret = 0;

        ret = mSourceFormat->getInt32(MEDIA_ATTR_WIDTH, width);
        ret &= mSourceFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
        ret &= mSourceFormat->getFloat(MEDIA_ATTR_FRAME_RATE, fps);

        if (!ret)
            EXIT_AND_RETURN(status);
    }

    EXIT_AND_RETURN(status);
}

SurfaceFrameGenerator::SurfaceFrameGenerator(SourceComponent *comp)
    : VideoConsumerListener(),
      mInputBufferCount(6),
      mWidth(0),
      mHeight(0),
      mSourceColorFormat(0x15), //HAL_PIXEL_FORMAT_YCbCr_420_SP
      mEncoderColorFormat(0x15), //HAL_PIXEL_FORMAT_YCbCr_420_SP
      mFrameNum(0),
      mFramePts(0),
      mFrameRate(24.0f),
      mStartMediaTime(-1ll),
      mNextMediaTime(-1ll),
      mDuration(0),
      mEncoderLastIdx(-1),
      mNeedRepeat(true),
      mFillFrameCondition(mFrameLock),
      mEmptyFrameCondition(mFrameLock),
      mIsContinue(true),
      mRepeatCount(0)  {
    ENTER();

    mComponent = DYNAMIC_CAST<VideoSourceSurface *>(comp);

    mConsumer = VideoConsumerCore::createVideoConsumer();
    mConsumer->setListener(this);

    memset(&mBufferInfo,0, MAX_BUFFER*sizeof(BufferInfo));

#ifdef __MM_YUNOS_YUNHAL_BUILD__
    //mNeedRepeat = false;
      mInputBufferCount = 10;
#endif

    if (!strncmp(mComponent->mMime.c_str(),
                MEDIA_MIMETYPE_VIDEO_SURFACE_SOURCE,
                strlen(MEDIA_MIMETYPE_VIDEO_SURFACE_SOURCE))) {
        mNeedRepeat = true;
    }

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
      mInputBufferCount = 5;
      mNeedRepeat = false;
#endif

    mComponent->mSourceFormat->setInt32("repeat", mNeedRepeat);

    INFO("mime is %s, set repeat flag is %d", mComponent->mMime.c_str(), mNeedRepeat);

    EXIT();
}

SurfaceFrameGenerator::~SurfaceFrameGenerator() {
    ENTER();

    MMAutoLock locker(mFrameLock);
    mIsContinue = false;
    mFillFrameCondition.broadcast();
    mEmptyFrameCondition.broadcast();
    delete mConsumer;
    EXIT();
}

#define CHECK_RET(ret, str) do {                    \
        if (ret) {                                  \
            ERROR("%s failed: (%d)\n", str, ret);   \
            EXIT_AND_RETURN(MM_ERROR_NO_MEM);       \
        }                                           \
    } while(0)

/* on VirtualDisplayListener */
void SurfaceFrameGenerator::bufferAvailable(void* buffer, int64_t pts) {
    ENTER1();

    if (!buffer) {
        ERROR("buffer is NULL");
        EXIT1();
    }

    int slot = -1, i;
    bool found = false;
    int freeCnt = 0;
    void *freeBuffer[MAX_BUFFER];

    {
        MMAutoLock locker(mFrameLock);

        if (pts < 0)
            pts = getTimeUs();

        for (i = 0; i < mInputBufferCount; i++) {
            if (buffer == mBufferInfo[i].bnb) {
                found = true;
                mBufferInfo[i].state = OWNED_BY_US_FILLED;
                mBufferInfo[i].pts = pts - mStartMediaTime;
                mBufferInfo[i].pts1 = getTimeUs() - mStartMediaTime;
                mBufferInfo[i].empty = false;
                INFO("receive buffer from weston, index is %d pts is %04fs",
                     i, mBufferInfo[i].pts/1000000.0f);
                mFillFrameCondition.signal();
            }

            if ((slot < 0) && (mBufferInfo[i].bnb == NULL))
                slot = i;

            if (mBufferInfo[i].state == OWNED_BY_US_EMPTY &&
                mBufferInfo[i].bnb &&
                mBufferInfo[i].empty) {
                if (!mNeedRepeat || i != mEncoderLastIdx) {
                    freeBuffer[freeCnt++] = mBufferInfo[i].bnb;
                    mBufferInfo[i].state = OWNED_BY_WESTON;
                }
            }
        }

        if (!found) {
            if (slot < 0) {
                WARNING("buffer %p, exceeds input buffer count", buffer);
                slot = mInputBufferCount++;
            }

            mBufferInfo[slot].state = OWNED_BY_US_FILLED;
            mBufferInfo[slot].bnb = buffer;
            mBufferInfo[slot].pts = pts - mStartMediaTime;
            mBufferInfo[i].pts1 = getTimeUs() - mStartMediaTime;
            mBufferInfo[slot].empty = false;
            mFillFrameCondition.signal();

            INFO("add buffer %p to buffer info slot %d",
                 buffer, slot);
            INFO("receive buffer from weston, index is %d pts is %04fs",
                 slot, mBufferInfo[slot].pts/1000000.0f);
        }

#ifdef DEBUG_BUFFER
        INFO("");
        dumpBufferInfo();
#endif
    }

    /* leave the IPC call output lock */
    for (i = 0; i < freeCnt; i++) {
        if (mConsumer && freeBuffer[i]) {
            mConsumer->releaseBuffer(freeBuffer[i]);
        }
    }

    EXIT();
}

mm_status_t SurfaceFrameGenerator::configure(SourceType type,  const char *fileName,
                                          int width, int height,
                                          float fps, uint32_t fourcc)
{
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    MMAutoLock locker(mFrameLock);

    mFrameRate = fps;
    mWidth = width;
    mHeight = height;

    INFO("wfd frame generator config: %dx%d %0.2ffps",
         mWidth, mHeight, mFrameRate);

    // hardcode here, hw specified video output format
#ifdef _PLATFORM_TV
    #define DEFAULT_VIDEO_SURFACE_FORMAT HAL_PIXEL_FORMAT_YV12
#elif defined(__PHONE_BOARD_MTK__)
    #define DEFAULT_VIDEO_SURFACE_FORMAT HAL_PIXEL_FORMAT_YV12
#elif defined(__PHONE_BOARD_SPRD__)
    #define DEFAULT_VIDEO_SURFACE_FORMAT 0x15 //HAL_PIXEL_FORMAT_YCbCr_420_SP
#elif defined (_BOARD_N1)
    #define HAL_PIXEL_FORMAT_NV12 0x3231564e
    #define DEFAULT_VIDEO_SURFACE_FORMAT HAL_PIXEL_FORMAT_NV12
#elif defined (__MM_YUNOS_LINUX_BSP_BUILD__)
    #define DEFAULT_VIDEO_SURFACE_FORMAT mm_getVendorDefaultFormat()
    mSourceColorFormat = mm_getVendorDefaultFormat();
#else
    #define HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL 0x100
    //#define DEFAULT_VIDEO_SURFACE_FORMAT HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
    //#define DEFAULT_VIDEO_SURFACE_FORMAT FMT_PREF(NV12)
    #define DEFAULT_VIDEO_SURFACE_FORMAT FMT_PREF(RGBX_8888)
    mComponent->mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FOURCC, VIDEO_SOURCE_CORLOR_FMT_RGBX);
    mSourceColorFormat = FMT_PREF(RGBX_8888);
#endif
    uint32_t format = DEFAULT_VIDEO_SURFACE_FORMAT;
    mEncoderColorFormat = format;

    {
        MMAutoLock locker(mLock);
        DEBUG("connect video consumer");
        if (!mConsumer->connect(width,
                           height,
                           mSourceColorFormat,
                           mInputBufferCount)) {
            ERROR("consumer connect fail");
            EXIT_AND_RETURN(MM_ERROR_MALFORMED);
        }

        status = prepareInputBuffer();
    }

    mComponent->mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, mEncoderColorFormat);

    EXIT_AND_RETURN(status);
}

mm_status_t SurfaceFrameGenerator::start() {
    ENTER();
    {
        MMAutoLock locker(mLock);
        mIsContinue = true;

        mStartMediaTime = mComponent->getTimeUs();
        mDuration = (1000.0f / mFrameRate) * 1000;
        mNextMediaTime = mStartMediaTime + mDuration;
    }

    INFO("wfd frame source starts, fps(%0.2f), continue is %d", mFrameRate, mIsContinue);

    mConsumer->start();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t SurfaceFrameGenerator::stop() {
    ENTER();

    mConsumer->stop();

    {
        MMAutoLock locker(mLock);

        if (!mIsContinue)
            EXIT_AND_RETURN(MM_ERROR_SUCCESS);

        mIsContinue = false;

        mFillFrameCondition.broadcast();
        mEmptyFrameCondition.broadcast();
    }

    {
        MMAutoLock locker(mFrameLock);
        int i;
        for (i = 0; i < mInputBufferCount; i++) {
            if (mBufferInfo[i].state == OWNED_BY_US_FILLED) {
                void *bnb =  mBufferInfo[i].bnb;

                DEBUG("reset buffer index %d: bnb: %p\n", i, bnb);
                //mVirSurface->releaseBuffer(((NativeWindowBuffer *)bnb)->wlbuffer);
                mBufferInfo[i].state = OWNED_BY_US_EMPTY;
                mBufferInfo[i].pts = START_PTS;
            }
        }
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t SurfaceFrameGenerator::flush() {
    ENTER();
    MMAutoLock locker(mFrameLock);

    int i;
    for (i = 0; i < mInputBufferCount; i++) {

        if (mBufferInfo[i].state == OWNED_BY_US_FILLED) {
            mBufferInfo[i].state = OWNED_BY_US_EMPTY;
            mBufferInfo[i].pts = START_PTS;
        }
    }

    mFillFrameCondition.broadcast();
    mEmptyFrameCondition.broadcast();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t SurfaceFrameGenerator::reset() {
    ENTER1();

    mm_status_t status = stop();
    {
        MMAutoLock locker(mFrameLock);
        MMAutoLock locker1(mLock);

        mFrameNum = 0;
        mFramePts = 0;

        mIsContinue = false;
        mFillFrameCondition.broadcast();
        mEmptyFrameCondition.broadcast();

        mConsumer->disconnect();
    }
    EXIT_AND_RETURN1(status);
}

void SurfaceFrameGenerator::signalBufferReturned(void* buffer, int64_t pts) {
    ENTER();
    int32_t i;
    void *returnBuf = NULL;

    {
        MMAutoLock locker(mFrameLock);
        for (i = 0; i < mInputBufferCount; i++) {
            if (buffer == mBufferInfo[i].bnb)
                break;
        }

        if (i == mInputBufferCount) {
            WARNING("cannot find buffer %p to release\n", buffer);
            EXIT();
        }

        mBufferInfo[i].state = OWNED_BY_US_EMPTY;

        //mBufferInfo[i].pts = pts;
        DEBUG("signal buffer %p return index %d, pts %04fs, buffer pts %04fs\n",
               buffer, i, pts/1000000.0f, mBufferInfo[i].pts/1000000.0f);

        /*
         * buffer is repeated n times (n > 1) in consumer's queue
         * make sure buffer is completely consumed
         */
        if (pts == mBufferInfo[i].pts)
            mBufferInfo[i].empty = true;

        if (i != mEncoderLastIdx && mBufferInfo[i].empty) {
#ifdef MULTI_THREAD_RETURN
            mBufferInfo[i].state = OWNED_BY_WESTON;
            returnBuf = buffer;
#endif
        }

        mEmptyFrameCondition.broadcast();

#ifdef DEBUG_BUFFER
        INFO("");
        dumpBufferInfo();
#endif
    }

    if (mConsumer && returnBuf) {
        mConsumer->releaseBuffer(returnBuf);
    }

    EXIT();
}

/*static*/ const char* SurfaceFrameGenerator::getOwnerName(int state) {

    if (state == SurfaceFrameGenerator::OWNED_BY_US_EMPTY)
        return "OWNED_BY_US_EMPTY";
    else if (state == SurfaceFrameGenerator::OWNED_BY_US_FILLED)
        return "OWNED_BY_US_FILLED";
    else if (state == SurfaceFrameGenerator::OWNED_BY_READER)
        return "OWNED_BY_READER";
    else if (state == SurfaceFrameGenerator::OWNED_BY_WESTON)
        return "OWNED_BY_WESTON";
    else
        return "UNKNOWN";
}

MediaBufferSP SurfaceFrameGenerator::getMediaBuffer() {
    ENTER();

    void *bnb = NULL;
    void *anb = NULL;
    void *buf;

    bool found = false;
    int i;
    MediaBufferSP mediaBuf;
    MediaMetaSP meta;
    bool isNew = false;
    int Idx = -1;
    int64_t prevPts = mFramePts;


    int64_t now = mComponent->getTimeUs();
    if (now < mNextMediaTime) {
        int64_t diff = mNextMediaTime - now;
        INFO("now is early, schedule waiting %dms", diff/1000);
        usleep(diff);
    }

    TIME_TAG("\ngetBuffer start");
    while (!found) {
        MMAutoLock lock(mFrameLock);

        if (!mIsContinue || mComponent->mStreamEOS) {
            WARNING("Exiting, mIsContinue %d, mStreamEOS %d",
                mIsContinue, mComponent->mStreamEOS);

            // provide empty MediaBuffer with MBFT_EOS flag to signal EOS
            mediaBuf = MediaBuffer::createMediaBuffer(
                                    MediaBuffer::MBT_GraphicBufferHandle);

            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);

            return mediaBuf;
        }

        int64_t maxPts = MIN_PTS;
        void *freeBuffer[MAX_BUFFER];
        int freeCnt = 0;
        for (i = 0; i < mInputBufferCount; i++) {
            if (mBufferInfo[i].state == OWNED_BY_US_FILLED) {

                /*
                if (mBufferInfo[i].pts > maxPts) {
                    maxPts = mBufferInfo[i].pts;

                    if (Idx >= 0) {
                        INFO("fps is %0.2f, drop frame pts %" PRId64 "", mFrameRate, mBufferInfo[Idx].pts);
                        mBufferInfo[Idx].state = OWNED_BY_US_EMPTY;
                    }

                    Idx = i;
                }
                */
                if (Idx < 0) {
                    Idx = i;
                    maxPts = mBufferInfo[i].pts;
                } else if (mBufferInfo[i].pts > maxPts) {
                    INFO("fps is %0.2f, drop frame(idx %d) pts %" PRId64 "", mFrameRate, Idx, mBufferInfo[Idx].pts);
                    mBufferInfo[Idx].state = OWNED_BY_US_EMPTY;
                    mBufferInfo[Idx].empty = true;
#ifdef MULTI_THREAD_RETURN
                    freeBuffer[freeCnt++] = mBufferInfo[Idx].bnb;
                    mBufferInfo[Idx].state = OWNED_BY_WESTON;
#endif
                    Idx = i;
                    maxPts = mBufferInfo[i].pts;
                } else {
                    INFO("fps is %0.2f, drop frame(idx %d) pts %" PRId64 "", mFrameRate, i, mBufferInfo[i].pts);
                    mBufferInfo[i].state = OWNED_BY_US_EMPTY;
                    mBufferInfo[i].empty = true;
#ifdef MULTI_THREAD_RETURN
                    freeBuffer[freeCnt++] = mBufferInfo[i].bnb;
                    mBufferInfo[i].state = OWNED_BY_WESTON;
#endif
                }

                found = true;
            }
        }

        /* leave the IPC call output lock */
        for (i = 0; i < freeCnt; i++) {
            if (mConsumer && freeBuffer[i]) {
                mConsumer->releaseBuffer(freeBuffer[i]);
            }
        }

        if (!found) {
            int64_t timeout = (int64_t)5 * mDuration;

            if (!mNeedRepeat) {
                INFO("no frame available and repeating previous frame is not allowed, wait...");
                int ret;
                ret = mFillFrameCondition.timedWait(timeout);
                if (ret) {
                    INFO("wait frame timeout");
                    dumpBufferInfo();
                }
                break;
            } else if (mEncoderLastIdx < 0) {
                INFO("first frame is not coming, wait...");
                mFillFrameCondition.timedWait(timeout);
                break;
            } else {
                isNew = false;
                found = true;
                mRepeatCount++;
                bnb = mBufferInfo[mEncoderLastIdx].bnb;
                anb = mBufferInfo[mEncoderLastIdx].yuvBuffer.get();
                mBufferInfo[mEncoderLastIdx].empty = false;

                if (mBufferInfo[mEncoderLastIdx].state == OWNED_BY_WESTON)
                    ERROR("repeat buffer(idx %d) is owned by %s",
                          mEncoderLastIdx,
                          getOwnerName((int)mBufferInfo[mEncoderLastIdx].state));

                mBufferInfo[mEncoderLastIdx].state = OWNED_BY_READER;

                if (!(mRepeatCount % 50)) {
                    INFO("repeat frame 50 times, check buffer status");
                    dumpBufferInfo();
                }
            }

            TIME_TAG("getBuf buffer returned");
        }
        else {
            bnb = mBufferInfo[Idx].bnb;
            anb = mBufferInfo[Idx].yuvBuffer.get();
            mBufferInfo[Idx].state = OWNED_BY_READER;
            mEncoderLastIdx = Idx;
            isNew = true;
            mRepeatCount = 0;
        }

#ifdef DEBUG_BUFFER
        INFO("mEncoderLastIdx is %d", mEncoderLastIdx);
        dumpBufferInfo();
#endif

        if (isNew)
            mFramePts = mBufferInfo[mEncoderLastIdx].pts;
        else {
            mFramePts = mComponent->getTimeUs() - mStartMediaTime - (mBufferInfo[mEncoderLastIdx].pts1 - mBufferInfo[mEncoderLastIdx].pts);
            mBufferInfo[mEncoderLastIdx].pts = mFramePts;
        }

        if (prevPts >= mFramePts) {
            mFramePts = mComponent->getTimeUs() - mStartMediaTime;
            WARNING("time stamp cannot roll back, use now time");
        }
    }

    if (!bnb) {
        ERROR("getMediaBuffer invalid NativeBuffer %p", bnb);
        return mediaBuf;
    }

    if (mSourceColorFormat != mEncoderColorFormat && anb) {
        INFO("source format(0x%x) dones't match encoder(0x%x), need color space conversion",
              mSourceColorFormat, mEncoderColorFormat);

        buf = anb;
        if (isNew) {
#ifdef __SUPPORT_FORMAT_CONVERT__
            // in this way, return buffer to weston after encode, cannot return buffer immedicately after csc
            mConsumer->doColorConvert(bnb, anb, mWidth, mHeight, mSourceColorFormat, mEncoderColorFormat);
#endif
        }
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        mediaBuf = mConsumer->createMediaBuffer((NativeWindowBuffer*)buf);
#endif
    } else
        buf = bnb;

    int64_t drift = mComponent->getTimeUs() - mStartMediaTime - mFramePts;

    DEBUG("bnb %p anb %p pts %04fs interval %04fms drift %04fms idx:%d\n",
           bnb,
           anb,
           mFramePts/1000000.0f,
           (mFramePts - prevPts) / 1000.0f,
           drift / 1000.0f,
           mEncoderLastIdx);

    //mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_GraphicBufferHandle);
    if (!mediaBuf)
        mediaBuf = mConsumer->createMediaBuffer(buf);

    mediaBuf->setPts(mFramePts);
    mediaBuf->setDts(mFramePts);
    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

    if (mComponent->mStreamEOS) {
        INFO("source get to EOS");
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    }

    /*
    if ((mRepeatCount % 10) == 9) {
        INFO("WFD repeat frame count %d, request IDR frame", mRepeatCount);
        mediaBuf->setFlag(MediaBuffer::MBFT_KeyFrame);
    }
    */

    meta = mediaBuf->getMediaMeta();
    meta->setPointer("buffer", bnb);
    meta->setPointer("component", mComponent);

    mNextMediaTime += mDuration;

    return mediaBuf;
}

mm_status_t SurfaceFrameGenerator::prepareInputBuffer() {
    ENTER();

    INFO("init %d input surfaces, this can take some time\n",
         mInputBufferCount);

    int i;
    for (i = 0; i < mInputBufferCount; i++) {

        mBufferInfo[i].bnb = NULL;
        mBufferInfo[i].state = OWNED_BY_WESTON;
        mBufferInfo[i].pts = START_PTS;
        mBufferInfo[i].empty = true;

        if (mSourceColorFormat != mEncoderColorFormat) {
#ifdef __SUPPORT_FORMAT_CONVERT__
            NativeWindowBuffer *nwb;
            NativeBufferSP sharedPointer;

            nwb = new NativeWindowBuffer(mWidth, mHeight, mEncoderColorFormat,
                                         ALLOC_USAGE_PREF(HW_VIDEO_ENCODER) | ALLOC_USAGE_PREF(HW_TEXTURE));
            int err = nwb->bufInit();
            if (err) {
                ERROR("fail to allocate native window buffer");
                delete nwb;
                nwb = NULL;
                EXIT_AND_RETURN(MM_ERROR_NO_MEM);
            }
            sharedPointer.reset((void*)nwb);
            mBufferInfo[i].yuvBuffer = sharedPointer;
            void *dest = NULL;
            Rect rect;
            rect.left = 0;
            rect.top = 0;
            rect.right = mWidth;
            rect.bottom = mHeight;
            nwb->lock(ALLOC_USAGE_PREF(SW_READ_OFTEN), rect, &dest);
            if (dest)
                memset(dest, 128, mWidth*mHeight*3/2);
            nwb->unlock();
            INFO("new yuv buffer %p in format %d\n", nwb, mEncoderColorFormat);
#endif
        }
    }

    INFO("init input surface finished\n");
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

/* static */ bool SurfaceFrameGenerator::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    ENTER();
    MediaMetaSP meta;

    void *bnb = NULL;
    VideoSourceSurface *component = NULL;
    int64_t pts;

    meta = mediaBuf->getMediaMeta();
    pts = mediaBuf->pts();

    if (meta) {
        meta->getPointer("buffer", (void *&)bnb);
        meta->getPointer("component", (void *&)component);
    }

    // Host surface works on single thread mode
    if (bnb && component) {
        component->signalBufferReturned((void*)bnb, pts);
     } else
        WARNING("bnb is %p, component is %p", bnb, component);

    EXIT_AND_RETURN(true);
}

void SurfaceFrameGenerator::dumpBufferInfo() {
    for (int i = 0; i < mInputBufferCount; i++)
        INFO("buffer slot %d, state is %s, empty is %d, bnb: %p",
             i, bufferStateToString(mBufferInfo[i].state),
             mBufferInfo[i].empty, mBufferInfo[i].bnb);
}

const char* SurfaceFrameGenerator::bufferStateToString(BufferState state) {
    if (state == OWNED_BY_US_EMPTY)
        return "empty";
    else if (state == OWNED_BY_US_FILLED)
        return "filled";
    else if (state == OWNED_BY_READER)
        return "consumer (.e.g. encoder)";
    else
        return "weston";
}

} // YUNOS_MM

extern "C" {

YUNOS_MM::Component *createComponent(const char* mime, bool encode) {
    YUNOS_MM::Component *comp;

    comp = new YUNOS_MM::VideoSourceSurface(mime);

    INFO("Video Source Component is created");

    return comp;
}

void releaseComponent(YUNOS_MM::Component *component) {
    INFO("Video Source Wfd Component is deleted");
    delete component;
}

}
