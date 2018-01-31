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

#include "csc_filter.h"

#include <multimedia/mm_debug.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <omap_drm.h>
#include <omap_drmif.h>

#include <libdce.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifdef __MM_NATIVE_BUILD__
#include <libavutil/time.h>
#else
#include <libavutil/avtime.h>
#endif
#include <libavutil/opt.h>
#include <libavutil/common.h>
#include <libswscale/swscale.h>
#include <libavcodec/h264_parse.h>

#ifdef __cplusplus
}
#endif

MM_LOG_DEFINE_MODULE_NAME("csc-filter");

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)

#define ALIGN2(x, n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))
#ifndef PAGE_SHIFT
#  define PAGE_SHIFT 12
#endif
#define FOURCC(ch0, ch1, ch2, ch3) \
        ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
         ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))

namespace YUNOS_MM {

CscFilter::CscFilter()
    : mInputConfigured(false),
      mOutputConfigured(false),
      mDeint(0),
      mStarted(false),
      mCondition(mLock),
      mContinue(false),
      mInputCount(0),
      mOutputCount(0),
      mListener(NULL),
      mDevice(NULL) {
    FUNC_TRACK();

    memset(&mSrc, 0, sizeof(mSrc));
    memset(&mDst, 0, sizeof(mDst));

    memset(mInputBuffers, 0, sizeof(mInputBuffers));
    memset(mOutputBuffers, 0, sizeof(mOutputBuffers));
}

CscFilter::~CscFilter() {
    FUNC_TRACK();

    stop();
    destroyOutputBuffer();

    if (mDevice)
        dce_deinit(mDevice);
}

bool CscFilter::describeFormat (uint32_t fourcc, struct image_params *image) {
    FUNC_TRACK();
    image->size   = -1;
    image->fourcc = -1;
    if (fourcc == V4L2_PIX_FMT_RGB32) {
            image->fourcc = V4L2_PIX_FMT_RGB32;
            image->size = image->height * image->width * 4;
            image->coplanar = 0;
            image->colorspace = V4L2_COLORSPACE_SRGB;

    } else if (fourcc == V4L2_PIX_FMT_BGR32) {
            image->fourcc = V4L2_PIX_FMT_BGR32;
            image->size = image->height * image->width * 4;
            image->coplanar = 0;
            image->colorspace = V4L2_COLORSPACE_SRGB;

    } else if (fourcc == V4L2_PIX_FMT_NV12) {
            image->fourcc = V4L2_PIX_FMT_NV12;
            image->size = image->height * image->width * 1.5;
            //image->coplanar = 1;
            image->coplanar = 0;
            image->colorspace = V4L2_COLORSPACE_SMPTE170M;

    } else if (fourcc == V4L2_PIX_FMT_NV21) {
            image->fourcc = V4L2_PIX_FMT_NV21;
            image->size = image->height * image->width * 1.5;
            //image->coplanar = 1;
            image->coplanar = 0;
            image->colorspace = V4L2_COLORSPACE_SMPTE170M;

    } else {
            ERROR("invalid format %x", fourcc);
            return false;
    }

    return true;
}

bool CscFilter::setListener(Listener* listener) {
    FUNC_TRACK();
    MMAutoLock lock(mLock);

    mListener = listener;
    return true;
}

bool CscFilter::configureInput(int w, int h, uint32_t fourcc, int bufferNum, int deint) {
    FUNC_TRACK();
    MMAutoLock lock(mLock);

    mSrc.width = w;
    mSrc.height = h;
    mSrc.numbuf = bufferNum;
    mDeint = deint;

    if (!fourcc)
        fourcc = V4L2_PIX_FMT_RGB32;

    INFO("w h fourcc num deint %d %d %x %d %d", w, h, fourcc, bufferNum, deint);
    // TODO should be other values?

    if (!describeFormat(fourcc, &mSrc)) {
        return false;
    }

    INFO("input configured: %dx%d, fourcc %x, coplanar %d, v4l2 color %d, numbuf %d",
         mSrc.width, mSrc.height, mSrc.fourcc, mSrc.coplanar, mSrc.colorspace, mSrc.numbuf);

    mInputConfigured = true;

    return true;
}

bool CscFilter::configureOutput(int w, int h, uint32_t fourcc, int devFd) {
    FUNC_TRACK();
    MMAutoLock lock(mLock);

    mDst.width = w;
    mDst.height = h;

    if (!fourcc)
        fourcc = V4L2_PIX_FMT_NV12;

    if (!describeFormat(fourcc, &mDst)) {
        return false;
    }

    //mDst.numbuf = OUTPUT_BUFFER_NUM;
    // outupt buffer count is equal to input buffer count, VideoSourceSurface assumes mInputBufferCount
    mDst.numbuf = mSrc.numbuf;
    INFO("output configured: %dx%d, fourcc %x, coplanar %d, v4l2 color %d, numbuf %d",
         mDst.width, mDst.height, mDst.coplanar, mDst.colorspace, mDst.numbuf);

    if(!allocateOutputBuffer(devFd)) {
        ERROR("fail to allocate output buffers");
        return false;
    }

    mOutputConfigured = true;
    return true;
}

void* csc_filter_process(void *arg) {
    FUNC_TRACK();

    CscFilter *csc = (CscFilter*)arg;
    if (!csc) {
        ERROR("invalid arg");
        return NULL;
    }

    csc->filterProcess();

    return NULL;
}

void CscFilter::filterProcess() {
    FUNC_TRACK();
    int index, index1;

    // fill all output buffer
    for (int i = 0; i < mDst.numbuf; i++) {
        fillBuffer(mOutputBuffers[i].buf);
    }

    while(mContinue) {

        {
            MMAutoLock lock(mLock);
            
            index = -1;
            index1 = -1;
            int64_t pts;
            for (int i = 0; i < mInputBufferNum; i++) {
                if (mInputBuffers[i].ownByUs &&
                    (index == -1 || pts < mInputBuffers[i].pts)) {
                    index = i;
                    pts = mInputBuffers[i].pts;
                }
            }

            for (int i = 0; i < mDst.numbuf; i++) {
                if (mOutputBuffers[i].ownByUs) {
                    index1 = i;
                    break;
                }
            }

            if (index < 0 || index1 < 0) {
                INFO("input queue empty %d, output queue empty %d",
                     index == -1, index1 == -1);
                mCondition.wait();
                continue;
            }

            INFO("input index %d, output index %d, pts is %.3f",
                 index, index1, mInputBuffers[index].pts/1000000.0f);

            mInputBuffers[index].ownByUs = false;
            mOutputBuffers[index1].ownByUs = false;
        }

        cscConvert(mInputBuffers[index].buf->bo[0],
                   mOutputBuffers[index1].buf->bo[0]);

        if (mListener) {
            mListener->onBufferEmptied(mInputBuffers[index].buf);
            mListener->onBufferFilled(mOutputBuffers[index1].buf, mInputBuffers[index].pts);
        }
    }

    mInputIndexMap.clear();
    INFO("csc filter process is going to exit");
}

bool CscFilter::start() {
    FUNC_TRACK();
    int ret;
    MMAutoLock lock(mLock);

    if (!mInputConfigured || !mOutputConfigured) {
        ERROR("input configured %d, output configured %d",
              mInputConfigured, mOutputConfigured);
        return false;
    }

    if (mStarted) {
        INFO("already started");
        return true;
    }

    mInputBufferNum = 0;
    mContinue = true;
    ret = pthread_create(&mThreadID, NULL, csc_filter_process, this);
    if(ret) {
        INFO("could not create task for csc filter processing");
        return false;
    }

    mStarted = true;
    return true;
}

void CscFilter::stop() {
    FUNC_TRACK();
    {
        MMAutoLock lock(mLock);

        if (!mStarted) {
            INFO("not started");
            return true;
        }

        mContinue = false;
        mCondition.broadcast();
    }

    void * aaa = NULL;
    pthread_join(mThreadID, &aaa);

    INFO("csc process joined");

    {
        MMAutoLock lock(mLock);

        mStarted = false;
    }

    return true;
}

bool CscFilter::emptyBuffer(MMNativeBuffer* buffer, int64_t pts) {
    FUNC_TRACK();
    int index;
    int freeIdx[MAX_NUMBUF];
    int freeCnt = 0;

    {
        MMAutoLock lock(mLock);
        if (!buffer || !mStarted) {
            ERROR("buffer %p, start %d", buffer, mStarted);
            return false;
        }

        std::map<MMNativeBuffer*, int>::iterator it = mInputIndexMap.find(buffer);
        if (it == mInputIndexMap.end()) {
            DEBUG("get new input entry %p with index %d", buffer, mInputBufferNum);
            if (mInputBufferNum >= mSrc.numbuf) {
                WARNING("invalid input buffer %p", buffer);
                return false;
            }
            index = mInputBufferNum;
            mInputBuffers[index].buf = buffer;
            mInputIndexMap[buffer] = index;
            mInputBufferNum++;
        } else {
            index = it->second;
            DEBUG("get input buffer %d", index);
        }

        mInputBuffers[index].pts = pts;
        mInputBuffers[index].ownByUs = true;

        //int num = 0;
        for (int i = 0; i < mInputBufferNum; i++) {
           if (index != i && mInputBuffers[i].ownByUs) {
               freeIdx[freeCnt] = i;
               freeCnt++;
               mInputBuffers[i].ownByUs = false;
           }
        }

       // if (num == 1)
        mCondition.signal();
    }

    for (int i = 0; i < freeCnt; i++) {
        if (mListener)
            mListener->onBufferEmptied(mInputBuffers[freeIdx[i]].buf);
    }

    return true;
}

bool CscFilter::fillBuffer(MMNativeBuffer* buffer) {
    FUNC_TRACK();

    if (!buffer || !mStarted) {
        ERROR("buffer %p, start %d", buffer, mStarted);
        return false;
    }

    int index;

    {
        MMAutoLock lock(mLock);
        std::map<MMNativeBuffer*, int>::iterator it = mOutputIndexMap.find(buffer);
        if (it == mOutputIndexMap.end()) {
            ERROR("invalid buffer");
            return false;
        }
        index = it->second;
    }

    mOutputBuffers[index].ownByUs = true;

    int num = 0;
    for (int i = 0; i < mDst.numbuf; i++) {
       if (mOutputBuffers[i].ownByUs) {
           num++;
           if (num > 1)
               break;
       }
    }

    if (num == 1)
        mCondition.signal();

    return true;
}

bool CscFilter::allocateOutputBuffer(int devFd) {
    FUNC_TRACK();

    //if (!mDevice)
       //mDevice = (struct omap_device*)dce_init();
    if (!mDevice) {
        int fd = dce_get_fd();
        INFO("dce drm fd %d", fd);
        if (fd < 0) {
            fd = devFd;
            INFO("get auth drm fd %d", fd);
            dce_set_fd(fd);
        }

        mDevice = (struct omap_device*)dce_init();

        fd = dce_get_fd();
        INFO("create omap device on our own with drm fd %d", fd);
    }

    if (!mDevice) {
        ERROR("cannot get omap device");
        return false;
    }

    int number = mDst.numbuf;
    bool multiBo = !!mDst.coplanar;
    int i;

    for (i = 0; i < number; i++) {
        if (!allocBuffer(i, mDst.fourcc, mDst.width, mDst.height, multiBo))
            break;
    }

    if (i < number)
        goto alloc_fail;

    return true;

alloc_fail:
    destroyOutputBuffer();
    return false;
}

static struct omap_bo *
allocBo(struct omap_device* device, uint32_t bpp, uint32_t width, uint32_t height,
		uint32_t *bo_handle, uint32_t *pitch)
{
    FUNC_TRACK();
    struct omap_bo *bo;
    uint32_t bo_flags = OMAP_BO_SCANOUT | OMAP_BO_WC;

    // the width/height has been padded already
    bo = omap_bo_new(device, width * height * bpp / 8, bo_flags);

    if (bo) {
    	*bo_handle = omap_bo_handle(bo);
    	*pitch = width * bpp / 8;
    	if (bo_flags & OMAP_BO_TILED)
    		*pitch = ALIGN2(*pitch, PAGE_SHIFT);
    }

    return bo;
}

bool CscFilter::allocBuffer(int index, uint32_t fourcc, uint32_t w, uint32_t h, bool multiBo) {
    FUNC_TRACK();

    uint32_t bo_handles[4] = {0}/*, offsets[4] = {0}*/;
    uint32_t pitches[4] = {0};;

    mOutputBuffers[index].buf = new MMNativeBuffer;
    MMNativeBuffer* buffer = mOutputBuffers[index].buf;
    memset(buffer, 0, sizeof(MMNativeBuffer));

    switch(fourcc) {
    case FOURCC('N','V','1','2'):
        if (multiBo) {
                buffer->bo[0] = allocBo(mDevice, 8, w, h,
                                &bo_handles[0], &(buffer->pitches[0]));

                buffer->fd[0] = omap_bo_dmabuf(buffer->bo[0]);

                buffer->bo[1] = allocBo(mDevice, 16, w/2, h/2,
                                &bo_handles[1], &(buffer->pitches[1]));

                buffer->fd[1] = omap_bo_dmabuf(buffer->bo[1]);
        } else {
                buffer->bo[0] = allocBo(mDevice, 8, w, h * 3 / 2,
                                &bo_handles[0], &(buffer->pitches[0]));
                buffer->fd[0] = omap_bo_dmabuf(buffer->bo[0]);
                buffer->pitches[1] = buffer->pitches[0];
        }
        mOutputIndexMap[(mOutputBuffers[index].buf)] = index;
        break;
    case FOURCC('R','G','B','4'): // V4L2_PIX_FMT_RGB32 argb8888 argb4
            buffer->bo[0] = allocBo(mDevice, 8*4, w, h,
                            &bo_handles[0], &(buffer->pitches[0]));
            buffer->fd[0] = omap_bo_dmabuf(buffer->bo[0]);
            bo_handles[1] = bo_handles[0];
            buffer->pitches[1] = buffer->pitches[0];
        break;
    default:
        ERROR("invalid format: 0x%08x", mDst.fourcc);
        goto fail;
    }

    return true;

fail:
    return false;
}

void CscFilter::destroyOutputBuffer() {
    FUNC_TRACK();

    int i;

    for (i = 0; i < MAX_NUMBUF; i++) {
        MMNativeBuffer *buffer = mOutputBuffers[i].buf;

        if (!buffer || buffer->bo[0] == NULL)
            break;

        close(buffer->fd[0]);
        omap_bo_del(buffer->bo[0]);

        if (buffer->bo[1] == NULL) {
            INFO("");
            delete buffer;
            mOutputBuffers[i].buf = NULL;
            continue;
        }

        close(buffer->fd[1]);
        omap_bo_del(buffer->bo[1]);

        delete buffer;
        mOutputBuffers[i].buf = NULL;
    }

    mOutputIndexMap.clear();
}

void CscFilter::cscConvert(struct omap_bo *input, struct omap_bo *output) {
    FUNC_TRACK();
    void *in, *out;

/*
    if (mSrc.width != mDst.width || mSrc.height != mDst.height) {
        ERROR("scale is not supported");
        return;
    }
*/

    if (!input || !output) {
        ERROR("input is %p, output is %p", input, output);
        return;
    }

    in = (void*)omap_bo_map(input);
    out = (void*)omap_bo_map(output);

    if (!in || !out) {
        ERROR("map buffer null pointer %p %p", in, out);
        return;
    }

    /* src is WL_DRM_FORMAT_ARGB8888 */
    struct SwsContext * sws = sws_getContext(
                        mSrc.width,
                        mSrc.height,
                        //AV_PIX_FMT_ARGB,
                        //AV_PIX_FMT_ABGR,
                        //AV_PIX_FMT_RGBA,
                        AV_PIX_FMT_BGRA,
                        mDst.width,
                        mDst.height,
                        AV_PIX_FMT_NV12,
                        SWS_FAST_BILINEAR,
                        NULL,
                        NULL,
                        NULL);
    if (!sws) {
        ERROR("new sws is failed\n");
        return;
    }

    int size = avpicture_get_size(AV_PIX_FMT_RGBA, mSrc.width, mSrc.height);
    INFO("rgba size is %d for %dx%d, bo size is %d",
         size, mSrc.width, mSrc.height, omap_bo_size(input));

    size = avpicture_get_size(AV_PIX_FMT_NV12, mDst.width, mDst.height);
    INFO("nv12 size is %d for %dx%d, bo size is %d",
         size, mDst.width, mDst.height, omap_bo_size(output));

    AVFrame * frameIn = av_frame_alloc();
    AVFrame * frameOut = av_frame_alloc();

    avpicture_fill((AVPicture*)frameOut, (uint8_t*)out, AV_PIX_FMT_NV12, mDst.width, mDst.height);
    avpicture_fill((AVPicture*)frameIn, (uint8_t*)in, AV_PIX_FMT_RGBA, mSrc.width, mSrc.height);

    INFO("in: data0 %p data1 %p, linesize0 %d linesize1 %d",
         frameIn->data[0], frameIn->data[1], frameIn->linesize[0], frameIn->linesize[1]);
    INFO("out: data0 %p data1 %p, linesize0 %d linesize1 %d",
         frameOut->data[0], frameOut->data[1], frameOut->linesize[0], frameOut->linesize[1]);

#if 0
            static FILE *fp = NULL;
            if (fp == NULL) {
                char name[128];
                sprintf(name, "/data/%dx%d.argb8888", mSrc.width, mSrc.height);
                fp = fopen(name, "wb");
            }
            fwrite(frameIn->data[0], frameOut->linesize[0], mSrc.height, fp);
#endif

    int ret = sws_scale(sws,
        (const uint8_t* const*)frameIn->data,
        frameIn->linesize,
        0,
        mSrc.height,
        frameOut->data,
        frameOut->linesize);

#if 0
            static FILE *fp1 = NULL;
            if (fp1 == NULL) {
                char name[128];
                sprintf(name, "/data/%dx%d.nv12", mSrc.width, mSrc.height);
                fp1 = fopen(name, "wb");
            }
            fwrite(frameOut->data[0], frameOut->linesize[0], mDst.height*3/2, fp1);
#endif

    DEBUG("scal ret: %d\n", ret);

    av_frame_free(&frameOut);
    av_frame_free(&frameIn);
    sws_freeContext(sws);
}

struct omap_bo* CscFilter::getMapBoFromName(uint32_t handle) {
    if (!mDevice) {
        ERROR("no device");
        return NULL;
    }

    return omap_bo_from_name(mDevice, handle);
}

};
