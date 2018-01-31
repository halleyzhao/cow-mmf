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

#include "video_test_source.h"
#include "video_source_base_impl.h"
#include "native_surface_help.h"
#include "media_surface_utils.h"

#if defined(YUNOS_BOARD_intel)
#include <yalloc_drm.h>
#endif

namespace YUNOS_MM {
#undef DEBUG_PERF
//#define DEBUG_PERF
#define LOOP_FILE_DATA  0
#if LOOP_FILE_DATA
    #define OP_ON_FILE_END(streamEosIndicator, fd)  fseek(fd, 0, SEEK_SET)
#else
    #define OP_ON_FILE_END(streamEosIndicator, fd) do { \
            streamEosIndicator = true;                  \
            return false;                               \
        } while(0)
#endif

#define FOURCC(ch0, ch1, ch2, ch3) \
        ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
         ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))


static inline void handle_fence(int &fencefd)
{
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    if (-1 != fencefd) {
        // sync_wait
        ::close(fencefd);
        fencefd = -1;
    }
#endif
}
static const char * COMPONENT_NAME = "VideoTestSource";

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

#if 0
#define ENTER() INFO(">>>\n")
#define EXIT() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)
#endif

BEGIN_MSG_LOOP(VideoTestSource)
    MSG_ITEM(CAMERA_MSG_prepare, onPrepare)
    MSG_ITEM(CAMERA_MSG_stop, onStop)
END_MSG_LOOP()

VideoTestSource::VideoTestSource() : VideoSourceBase(COMPONENT_NAME){
    ENTER();

    mComponentName = COMPONENT_NAME;

    mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, MediaBuffer::MBT_GraphicBufferHandle);

    EXIT();
}

VideoTestSource::~VideoTestSource() {
    ENTER();

    EXIT();
}

void VideoTestSource::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO add specific prepare

    VideoSourceBase<AutoFrameGenerator>::onPrepare(param1, param2, rspId);

    EXIT();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//AutoFrameGenerator
AutoFrameGenerator::AutoFrameGenerator(SourceComponent *comp)
    : mInputYUVFile(NULL)
    , mInputBufferCount(8)
    , mMaxFrameCount(1000)
    , mNativeWindow(NULL)
    , mSurfaceWrapper(NULL)
    , mWidth(0)
    , mHeight(0)
    , mFrameNum(0)
    , mFramePts(0)
    , mDefaultFrameRate(10.0f)
    , mStartTimeUs(-1ll)
    , mStartMediaTime(-1ll)
    , mNextMediaTime(-1ll)
    , mDefaultDuration(0)
    , mFrameCondition(mFrameLock)
    , mIsContinue(true)
    , mIsFileSource(false)
    , mFile(NULL)
    , mSourceColorFormat(0)
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    , mYalloc(NULL)
#endif
    , mSlowMotionEnable(false)
    , mFrameCaptureRate(120.0)
    , mCaptureFrameDurationUs(1000000LL/120)
{
    ENTER();

    mComponent = DYNAMIC_CAST<VideoTestSource *>(comp);
    memset(&mBufferInfo,0,MAX_BUFFER*sizeof(BufferInfo));

#ifdef __MM_YUNOS_YUNHAL_BUILD__
#if defined(__USING_YUNOS_MODULE_LOAD_FW__)
    VendorModule* module =
                (VendorModule*) LOAD_VENDOR_MODULE(YALLOC_VENDOR_MODULE_ID);
    yalloc_open(module,&mYalloc);
#else
    int ret = yalloc_open(&mYalloc);
#endif
    ASSERT(mYalloc);
#endif

    EXIT();
}

AutoFrameGenerator::~AutoFrameGenerator() {
    ENTER();

    MMAutoLock locker(mFrameLock);
    mIsContinue = false;
    mFrameCondition.broadcast();

    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }

#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    int ret = yalloc_close(mYalloc);
    MMASSERT(ret == 0);
#endif
    if (mSurfaceWrapper)
        delete mSurfaceWrapper;
    EXIT();
}

#define CHECK_RET(ret, str) do {                    \
        if (ret) {                                  \
            ERROR("%s failed: (%d)\n", str, ret);   \
            EXIT_AND_RETURN(MM_ERROR_NO_MEM);       \
        }                                           \
    } while(0)

mm_status_t AutoFrameGenerator::configure(SourceType type,  const char *fileName,
                                          int width, int height,
                                          float fps, uint32_t fourcc)
{
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    ASSERT(fourcc == 'NV12');

    MMAutoLock locker(mFrameLock);


#if defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    mSourceBufferType = MediaBuffer::MBT_DrmBufName;
#else
    if (mIsRawData) {
        mSourceBufferType = MediaBuffer::MBT_RawVideo;
    } else {
        mSourceBufferType = MediaBuffer::MBT_GraphicBufferHandle;
    }
#endif
    mComponent->mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, mSourceBufferType);

    mDefaultFrameRate = fps;
    mWidth = width;
    mHeight = height;

    INFO("auto frame generator config: type: %d, url: %s, size: %dx%d %0.2ffps",
         type, fileName, mWidth, mHeight, mDefaultFrameRate);
    mIsFileSource = type == YUVFILE;
    if (mIsFileSource && !mFile) {
         mFile = fopen(fileName+7, "rb"); // skip the "file://" prefix
         if (!mFile) {
            ERROR("open %s failed", fileName+7);
            return MM_ERROR_INVALID_PARAM;
         }
         ASSERT(mFile != nullptr);
    }

    mSourceColorFormat = mm_getVendorDefaultFormat(false);
    DEBUG("mSourceColorFormat 0x%0x", mSourceColorFormat);

#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    if (mNativeWindow == NULL){
        mNativeWindow = createSimpleSurface(mWidth, mHeight);
    }
    ASSERT(mNativeWindow);
    mSurfaceWrapper = new YunOSMediaCodec::WindowSurfaceWrapper(mNativeWindow);
#elif defined(__MM_BUILD_DRM_SURFACE__)
    mSurfaceWrapper = new YunOSMediaCodec::WlDrmSurfaceWrapper((omap_device *)NULL);
#elif defined(__MM_BUILD_VPU_SURFACE__)
    mSurfaceWrapper = new YunOSMediaCodec::VpuSurfaceWrapper();
#endif
    DEBUG("mSurfaceWrapper: %p\n", mSurfaceWrapper);
    ASSERT(mSurfaceWrapper);

#if defined(__PHONE_BOARD_SPRD__)
    int omxFormat = -1;
    if (mSourceColorFormat == YUN_HAL_FORMAT_YV12 || mSourceColorFormat == 0x13) {   //HAL_PIXEL_FORMAT_YCbCr_420_P
        omxFormat = 0x13; //OMX_COLOR_FormatYUV420Planar
    } else if (mSourceColorFormat == YUN_HAL_FORMAT_NV12 || mSourceColorFormat == 0x15) { //HAL_PIXEL_FORMAT_YCbCr_420_SP
        omxFormat = 0x15;//OMX_COLOR_FormatYUV420SemiPlanar
    } else {
        WARNING("format %0x is not support, add support here\n", mSourceColorFormat);
        return MM_ERROR_UNSUPPORTED;
    }

    INFO("convert hal format to omx format, 0x%0x-->0x%0x\n", mSourceColorFormat, omxFormat);

    mComponent->mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, omxFormat);
#endif


    int ret;
    uint32_t flags;
    INFO("set buffers geometry %dx%d\n", mWidth, mHeight);
    //ret = WINDOW_API(set_buffers_dimensions)(GET_ANATIVEWINDOW(mNativeWindow), mWidth, mHeight);
    ret = mSurfaceWrapper->set_buffers_dimensions(mWidth, mHeight, flags);
    CHECK_RET(ret, "WINDOW_API(set_buffers_dimensions)");

    INFO("set buffers format 0x%x\n", mSourceColorFormat);
    //ret = WINDOW_API(set_buffers_format)(GET_ANATIVEWINDOW(mNativeWindow),mSourceColorFormat);
    ret = mSurfaceWrapper->set_buffers_format(mSourceColorFormat, flags);
    CHECK_RET(ret, "WINDOW_API(set_buffers_format)");

    INFO("set buffer usage\n");
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) && defined(__PHONE_BOARD_QCOM__)
    ret = mm_setSurfaceUsage (mNativeWindow, ALLOC_USAGE_PREF(HW_CAMERA_WRITE)); //TODO: fix in future
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    ret = mSurfaceWrapper->set_usage(DRM_NAME_USAGE_TI_HW_ENCODE, flags);
#endif
    CHECK_RET(ret, "mm_setSurfaceUsage");


    INFO("set buffer count %d\n", mInputBufferCount);
    //ret = WINDOW_API(set_buffer_count)(GET_ANATIVEWINDOW(mNativeWindow), mInputBufferCount);
    ret = mSurfaceWrapper->set_buffer_count(mInputBufferCount, flags);
    CHECK_RET(ret, "WINDOW_API(set_buffer_count)");

    status = prepareInputBuffer();

    EXIT_AND_RETURN(status);
}

mm_status_t AutoFrameGenerator::start() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    mIsContinue = true;

    mStartMediaTime = mComponent->getTimeUs();
    mDefaultDuration = (1000.0f / mDefaultFrameRate) * 1000;
    // mNextMediaTime = mStartMediaTime + mDefaultDuration;
    mNextMediaTime = mStartMediaTime + mCaptureFrameDurationUs;

    INFO("auto frame source starts, fps(%0.2f), duration %d ms", mDefaultFrameRate, mDefaultDuration);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AutoFrameGenerator::stop() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    mIsContinue = false;
    mStrideX = 0;
    mStrideY = 0;
    mFrameCondition.signal();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AutoFrameGenerator::flush() {
    ENTER();
    MMAutoLock locker(mFrameLock);

    int i;
    for (i = 0; i < mInputBufferCount; i++) {

        mBufferInfo[i].state = OWNED_BY_US;
        mBufferInfo[i].pts = START_PTS;
    }

    mFrameCondition.signal();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AutoFrameGenerator::reset() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    uint32_t flags;

    mFrameNum = 0;
    mFramePts = 0;

    if (mInputYUVFile) {
        fclose(mInputYUVFile);
        mInputYUVFile = NULL;
    }

    int i;
    for (i = 0; i < mInputBufferCount; i++) {
        if (mBufferInfo[i].state == OWNED_BY_US)
            //mm_cancelBuffer(mNativeWindow, mBufferInfo[i].anb, -1);
            mSurfaceWrapper->cancel_buffer( mBufferInfo[i].anb, -1, flags);

        mBufferInfo[i].state = OWNED_BY_NATIVEWINDOW;
        mBufferInfo[i].pts = START_PTS;
    }
    mIsContinue = false;
    mFrameCondition.signal();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AutoFrameGenerator::signalBufferReturned(void* buffer, int64_t pts) {
    ENTER();
    int32_t i;

    MMNativeBuffer* anb = (MMNativeBuffer*)buffer;
    {
        MMAutoLock locker(mFrameLock);
        for (i = 0; i < mInputBufferCount; i++) {
            if (anb == mBufferInfo[i].anb)
                break;
        }

        if (i == mInputBufferCount) {
            WARNING("cannot find anb to release\n");
            EXIT();
        }

        mBufferInfo[i].state = OWNED_BY_US;
        mBufferInfo[i].pts = pts;
        DEBUG("mBufferInfo[%d]: signal buffer return %0.3f\n", i, pts/1000000.0f);
    }
    mFrameCondition.broadcast();
    EXIT();
}

mm_status_t AutoFrameGenerator::setParameters(const MediaMetaSP & meta)
{
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    void *surface = NULL;
    if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE, surface)) {
        mNativeWindow = static_cast<WindowSurface*>(surface);
        DEBUG("mNativeWindow %p\n", surface);
    }

    int32_t tlEnable = 0;
    if (meta->getInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, tlEnable) && tlEnable) {
        DEBUG("time lapse enable");
        mSlowMotionEnable = true;
    }

    if (meta->getFloat(MEDIA_ATTR_TIME_LAPSE_FPS, mFrameCaptureRate)) {
        mCaptureFrameDurationUs = 1000000LL/mFrameCaptureRate;
        DEBUG("mFrameCaptureRate %.2f, mCaptureFrameDurationUs %" PRId64 "", mFrameCaptureRate, mCaptureFrameDurationUs);
    }

    int64_t startTime = 0;
    if (meta->getInt64(MEDIA_ATTR_START_TIME, startTime)) {
        mStartTimeUs = startTime;
        DEBUG("mStartTimeUs %" PRId64 "", mStartTimeUs);
    }

    int32_t rawData = 0;
    if (meta->getInt32("raw-data", rawData)) {
        mIsRawData = (rawData == 1);
        DEBUG("mIsRawData %d", mIsRawData);
    }

    EXIT_AND_RETURN(status);
}

MediaBufferSP AutoFrameGenerator::getMediaBuffer() {
    ENTER();

    MMNativeBuffer *anb = NULL;

    bool found = false;
    int i;
    uint8_t *buf;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;
    MediaMetaSP meta;
    bool isTimeOut = false;
    uint32_t flags;

    if (!mSurfaceWrapper) {
        ERROR("buffer source is not prepared, mSurfaceWrapper is null\n");
        //EXIT_AND_RETURN(mediaBuf);
        return mediaBuf;
    }

    TIME_TAG("\ngetBuffer start");
    while (!found) {
        MMAutoLock lock(mFrameLock);

        if (!mIsContinue || mComponent->mStreamEOS) {
            WARNING("Exiting, mIsContinue %d, mStreamEOS %d",
                mIsContinue, mComponent->mStreamEOS);

            // provide empty MediaBuffer with MBFT_EOS flag to signal EOS
            mediaBuf = MediaBuffer::createMediaBuffer((MediaBuffer::MediaBufferType)mSourceBufferType);

            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);

            return mediaBuf;
        }

        int Idx = 0;
        int64_t minPts = MAX_PTS;
        for (i = 0; i < mInputBufferCount; i++) {
            if (mBufferInfo[i].state == OWNED_BY_US) {

                if (mBufferInfo[i].pts < minPts) {
                    minPts = mBufferInfo[i].pts;
                    Idx = i;
                }
                found = true;
            }
        }

        if (!found) {
            TIME_TAG("getBuf wait buffer return");

            int64_t timeout = 2 * mDefaultDuration;

            if (isTimeOut) {
                return mediaBuf;
            }

            mFrameCondition.timedWait(timeout);
            TIME_TAG("getBuf buffer returned");
            isTimeOut = true;
        } else {
            anb = mBufferInfo[Idx].anb;
            if (mSlowMotionEnable) {
                mQueuedIndex.push_back(Idx);
            } else {
                //int ret = mm_queueBuffer(mNativeWindow, anb, -1);
                int ret = mSurfaceWrapper->queueBuffer(anb, -1, flags);
                if (ret != 0) {
                    ERROR("queueBuffer failed: %s (%d)", strerror(-ret), -ret);
                    return mediaBuf;
                }
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
                //mNativeWindow->finishSwap();
                mSurfaceWrapper->finishSwap(flags);
#endif
                mBufferInfo[Idx].state = OWNED_BY_NATIVEWINDOW;
            }
        }
    }

    TIME_TAG("getMediaBuffer queueBuffer");
    anb = NULL;
    if (mSlowMotionEnable) {
        int32_t front = mQueuedIndex.front();
        anb = mBufferInfo[front].anb;
        mQueuedIndex.pop_front();
        DEBUG("front %d, pts %0.3f\n", front, mBufferInfo[front].pts/1000000.0f);
    } else {
        //int ret = mm_dequeueBufferWait(mNativeWindow, &anb);
        int ret = mSurfaceWrapper->dequeue_buffer_and_wait(&anb, flags);
        if (ret != 0) {
            ERROR("obtainBuffer failed: (%d)\n", ret);
            //EXIT_AND_RETURN(mediaBuf);

            return mediaBuf;
        }
    }
    TIME_TAG("getMediaBuffer dequeueBuffer");
    {
        MMAutoLock lock(mFrameLock);

        for (i = 0; i < mInputBufferCount; i++) {
            if (anb == mBufferInfo[i].anb) {
                INFO("getMediaBuffer index %d\n", i);
                break;
            }
        }

        if (i < mInputBufferCount)
            mBufferInfo[i].state = OWNED_BY_US;
        else {
            ERROR("getMediaBuffer invalid MMNativeBuffer %p", anb);
            //EXIT_AND_RETURN(mediaBuf);

            return mediaBuf;
        }

        mBufferInfo[i].state = OWNED_BY_COMPONENT;
    }

#ifdef __MM_YUNOS_YUNHAL_BUILD__
    fillSourceBuffer(anb);
#endif

    mFramePts += mDefaultDuration;

    mediaBuf = MediaBuffer::createMediaBuffer((MediaBuffer::MediaBufferType)mSourceBufferType);
    // buffer with raw data
    if (mIsRawData) {
        buf = new uint8_t[sizeof(uintptr_t)];
        *(uintptr_t *)buf = (uintptr_t)getAddressFromHandle(anb);
        DEBUG("mBufferInfo[%d]: %p, buf %p\n",
            i, *(uintptr_t *)buf, buf);


        bufOffset = 0;
        bufStride = sizeof(uintptr_t);

    } else {
        buf = new uint8_t[sizeof(MMBufferHandleT)];
        *(MMBufferHandleT *)buf = (MMBufferHandleT)mm_getBufferHandle(anb);
        DEBUG("mBufferInfo[%d] MMBufferHandleT: %p buf: %p\n",
            i, mm_getBufferHandle(anb), buf);


        bufOffset = 0;
        bufStride = sizeof(MMBufferHandleT);
    }

    mediaBuf->setBufferInfo((uintptr_t *)&buf, &bufOffset, &bufStride, 1);
    mediaBuf->setPts(mFramePts);
    mediaBuf->setDts(mFramePts);
    mediaBuf->setSize(bufStride);
    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

    if (!mComponent->mStreamEOS)
        mComponent->mStreamEOS = (mFrameNum > mMaxFrameCount);

    if (mComponent->mStreamEOS) {
        INFO("source get to EOS");
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    }

    meta = mediaBuf->getMediaMeta();
    meta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, GET_ANATIVEWINDOW(mNativeWindow));
    meta->setPointer("buffer", anb);
    meta->setPointer("component", mComponent);

    int64_t now = mComponent->getTimeUs();
    if (mFrameNum == 0 && now < mStartTimeUs) {
        DEBUG("drop video frame, %" PRId64 " < %" PRId64 "", now, mStartTimeUs);
        mediaBuf.reset();
        return mediaBuf;
    }

    if (now < mNextMediaTime) {
        int64_t diff = mNextMediaTime - now;
        INFO("we are earlier than schedule, wait %dms", diff/1000);
        usleep(diff);
    }

    if (mSlowMotionEnable) {
        mNextMediaTime += mCaptureFrameDurationUs;
    } else {
        mNextMediaTime += mDefaultDuration;
    }

    mFrameNum++;
    return mediaBuf;
}

mm_status_t AutoFrameGenerator::prepareInputBuffer() {
    ENTER();

    INFO("init %d input surfaces, this can take some time\n",
         mInputBufferCount);

    int i;
    int ret;
    uint32_t flags;
    for (i = 0; i < mInputBufferCount; i++) {
        MMNativeBuffer *anb = NULL;
        DEBUG("GET_ANATIVEWINDOW(mNativeWindow): %p\n", GET_ANATIVEWINDOW(mNativeWindow));
        //ret = mm_dequeueBufferWait(mNativeWindow, &anb);
        ret = mSurfaceWrapper->dequeue_buffer_and_wait(&anb, flags);
        if (ret != 0) {
            ERROR("obtainBuffer failed: (%d)\n", ret);
            EXIT_AND_RETURN(MM_ERROR_NO_MEM);
        }

        DEBUG("%d: anb: %p\n", i, anb);
        // fillSourceBuffer(anb);

        mBufferInfo[i].anb = anb;
        mBufferInfo[i].state = OWNED_BY_US;
        mBufferInfo[i].pts = START_PTS;
        fillSourceBuffer(anb);

    }

    //ret = mm_cancelBuffer(mNativeWindow, mBufferInfo[0].anb, -1);
    //ret |= mm_cancelBuffer(mNativeWindow, mBufferInfo[1].anb, -1);
    ret = mSurfaceWrapper->cancel_buffer(mBufferInfo[0].anb, -1, flags);
    ret |= mSurfaceWrapper->cancel_buffer(mBufferInfo[1].anb, -1, flags);

    if (ret != 0) {
        WARNING("can not return buffer to native window");
        EXIT_AND_RETURN(MM_ERROR_NO_MEM);
    }

    mBufferInfo[0].state = OWNED_BY_NATIVEWINDOW;
    mBufferInfo[1].state = OWNED_BY_NATIVEWINDOW;

    INFO("init input surface finished\n");

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void *AutoFrameGenerator::getAddressFromHandle(MMNativeBuffer *anb)
{
    void *ptr = NULL;

    // get mStrideX and mStrideY
    if (mStrideX == 0 && mStrideY == 0) {
        mStrideX = mWidth;
        mStrideY = mHeight;

        if (mInputYUVFile) {
            WARNING("dones't support input YUV file, use auto YUV");
        }

#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
      #ifdef YUNOS_BOARD_intel
        int ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_X_STRIDE, anb->target, &mStrideX);
        MMASSERT(ret == 0);
        ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_Y_STRIDE, anb->target, &mStrideY);
        MMASSERT(ret == 0);
      #endif
#endif
    DEBUG("mWidth: %d, mStrideX: %d, mHeight: %d, mStrideY: %d\n",
        mWidth, mStrideX, mHeight, mStrideY);

    }


    Rect rect;
    rect.top = 0;
    rect.left = 0;
    rect.right = mStrideX;
    rect.bottom = mStrideY;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    uint32_t usage = ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN);
    yunos::libgui::mapBuffer(anb, usage, rect, &ptr);
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    uint32_t flags = 0;
    mSurfaceWrapper->mapBuffer(anb, 0, 0, mStrideX, mStrideY, &ptr, flags);
#endif

    return ptr;
}

void AutoFrameGenerator::fillSourceBuffer(MMNativeBuffer *anb) {
    ENTER();
    void *ptr = getAddressFromHandle(anb);

    if (mIsFileSource) {
        // yami encoder needs yuv data aligned by w*h, not by strideX*strideY
        loadYUVDataFromFile(ptr, mSourceColorFormat, mWidth, mHeight, mWidth, mHeight);
    } else {
        loadYUVDataAutoGenerator(ptr, mSourceColorFormat, mWidth, mHeight, mStrideX, mStrideY);
    }

#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    yunos::libgui::unmapBuffer(anb);
#endif
    EXIT();
}

bool AutoFrameGenerator::loadYUVDataFromFile(void* pBuf, int format, int width, int height, int x_stride, int y_stride) {
    int plane_num = 2;
    bool ret = false;
    unsigned char* pDst = (unsigned char*)pBuf;

    DEBUG("pBuf: %p, format: 0x%x, width: %d, height: %d, x_stride: %d, y_stride: %d",
        pBuf, format, width, height, x_stride, y_stride);
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    plane_num = 2;
#elif defined(__MM_YUNOS_YUNHAL_BUILD__)
    switch (format) {
    case YUN_HAL_FORMAT_YV12:
        plane_num = 3;
        break;
    case YUN_HAL_FORMAT_NV12:
        plane_num = 2;
        break;
    case YUN_HAL_FORMAT_YCrCb_420_SP:
        plane_num = 2;
        break;
    case YUN_HAL_FORMAT_YCbCr_422_I:
        plane_num = 1;
        break;
    default:
        plane_num = 0;
        break;
    }
#endif

    if(mFile && pDst) {
        int count = 0;

        if (plane_num == 3) {
            for (int i = 0; i < height; i++) {
                count = fread(pDst, width, 1, mFile);
                pDst += x_stride;
                if (count < 0)
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
            }
            pDst = (unsigned char*)pBuf + x_stride * y_stride;
            for (int i = 0; i < height/2; i++) {
                count = fread(pDst, width/2, 1, mFile);
                pDst += (x_stride/2);
                if (count < 0)
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
            }
            pDst = (unsigned char*)pBuf + x_stride * y_stride + x_stride * y_stride / 4;
            for (int i = 0; i < height/2; i++) {
                count = fread(pDst, width/2, 1, mFile);
                pDst += (x_stride/2);
                if (count < 0)
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
            }
        } else if (plane_num == 2) {
            for (int i = 0; i < height; i++) {
                count = fread(pDst, width, 1, mFile);
                pDst += x_stride;
                if (count <= 0) {
                    DEBUG("count %d, read error %d(%s) ", count, errno, strerror(errno));
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
                }
            }
            pDst = (unsigned char*)pBuf + x_stride * y_stride;
            for (int i = 0; i < height/2; i++) {
                count = fread(pDst, width, 1, mFile);
                pDst += x_stride;
                if (count <= 0) {
                    DEBUG("count %d, read error %d(%s) ", count, errno, strerror(errno));
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
                }
            }
        }
        else if (plane_num == 1) {
            for (int i = 0; i < height; i++) {
                count = fread(pDst, width*2, 1, mFile);
                pDst += (x_stride*2);
                if (count < 0)
                    OP_ON_FILE_END(mComponent->mStreamEOS, mFile);
            }
        }
        ret = true;
    }
    return ret;
}
/* static */ int AutoFrameGenerator::loadYUVDataAutoGenerator(void* ptr, int format, int width, int height, int x_stride, int y_stride)
{
    ENTER();
    static int row_shift = 0;
    int block_width = width/256*8; // width/32 /8 * 8; 32 blocks each row, and make 8 alignment.
    int row;
    //int alpha = 0;

    uint8_t *y_start = NULL;
    uint8_t *u_start = NULL;
    uint8_t *v_start = NULL;
    int y_pitch = 0;
    int u_pitch = 0;
    int v_pitch = 0;

    y_start = (uint8_t *)ptr;
    y_pitch = x_stride;

    if (block_width<8)
        block_width = 8;

    /* copy Y plane */
    for (row=0;row<height;row++) {
        uint8_t *Y_row = y_start + row * y_pitch;
        int jj, xpos, ypos;

        ypos = (row / block_width) & 0x1;

        for (jj=0; jj<width; jj++) {
            xpos = ((row_shift + jj) / block_width) & 0x1;
            if (xpos == ypos)
                Y_row[jj] = 0xeb;
            else
                Y_row[jj] = 0x10;
        }
    }

    switch (format) {
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        case FMT_PREF(YV12):
        {
            u_start = (uint8_t *)ptr + y_stride * x_stride;
            v_start = u_start + y_stride * x_stride / 4;
            u_pitch = v_pitch = x_stride/2;
            break;
        }
#endif
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
        case FMT_PREF(NV12):
#elif defined(__PHONE_BOARD_QCOM__)
        case FMT_PREF(IMPLEMENTATION_DEFINED):
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
        case FOURCC('N', 'V', '1', '2'):
#endif
        {
            u_start = v_start = (uint8_t *)ptr + y_stride * x_stride;
            u_pitch = v_pitch = x_stride;
            break;
        }
        default:
            ERROR("unsupported format 0x%x", format);
            break;
    }

    /* copy UV data */
    for( row =0; row < height/2; row++) {
        switch (format) {
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
            case FMT_PREF(YV12):
            {
                uint8_t *U_row = u_start + row * u_pitch;
                uint8_t *V_row = v_start + row * v_pitch;

                memset (U_row,0x80,width/2);
                memset (V_row,0x80,width/2);
                break;
            }
#endif
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
            case FMT_PREF(NV12):
#elif defined(__PHONE_BOARD_QCOM__)
            case FMT_PREF(IMPLEMENTATION_DEFINED):
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
            case FOURCC('N', 'V', '1', '2'):
#endif
            {
                uint8_t *UV_row = u_start + row * u_pitch;
                memset (UV_row,0x80,width);
                break;
            }
            default:
                ERROR("unsupported format 0x%x", format);
                break;
        }
    }

    row_shift += (block_width/8);   // minimual block_width is 8, it is safe for increasement

    /*
    if (getenv("AUTO_UV") == NULL)
        EXIT_AND_RETURN(0);

    if (getenv("AUTO_ALPHA"))
        alpha = 0;
    else
        alpha = 70;

    YUV_blend_with_pic(width,height,
            Y_start, Y_pitch,
            U_start, U_pitch,
            V_start, V_pitch,
            UV_interleave, alpha);
    */
    EXIT_AND_RETURN(0);
}

/* static */ bool AutoFrameGenerator::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    ENTER();
    uint8_t *buf = NULL;
    MediaMetaSP meta;

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&buf, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN(false);
    }

    MM_RELEASE_ARRAY(buf);

    MMNativeBuffer *anb = NULL;
    VideoTestSource *component = NULL;
    int64_t pts;


    meta = mediaBuf->getMediaMeta();
    pts = mediaBuf->pts();

    if (meta) {
        meta->getPointer("buffer", (void *&)anb);
        meta->getPointer("component", (void *&)component);
    }

    // Host surface works on single thread mode
    if (anb && component) {
        INFO("signal buffer return\n");
        component->signalBufferReturned((void*)anb, pts);
     }

    EXIT_AND_RETURN(true);
}

} // YUNOS_MM

extern "C" {

YUNOS_MM::Component *createComponent(const char* mime, bool encode) {
    YUNOS_MM::Component *comp;

    comp = new YUNOS_MM::VideoTestSource();

    INFO("Video Test Source Component is created");

    return comp;
}

void releaseComponent(YUNOS_MM::Component *component) {
    INFO("Video Test Source Component is deleted");
    delete component;
}

}
