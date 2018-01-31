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

#include "vpe_filter.h"

#include <multimedia/mm_debug.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <omap_drm.h>
#include <omap_drmif.h>

#include <libdce.h>

MM_LOG_DEFINE_MODULE_NAME("vpe-filter");

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)

#define ALIGN2(x, n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))
#ifndef PAGE_SHIFT
#  define PAGE_SHIFT 12
#endif
#define FOURCC(ch0, ch1, ch2, ch3) \
        ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
         ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))

namespace YUNOS_MM {

VpeFilter::VpeFilter()
    : mInputConfigured(false),
      mOutputConfigured(false),
      mFd(-1),
      mField(0),
      mDeint(0),
      mTranslen(1),
      mStarted(false),
      mStreamOn(false),
      mCondition(mLock),
      mContinue(false),
      mInputCount(0),
      mOutputCount(0),
      mListener(NULL),
      mDevice(NULL) {
    FUNC_TRACK();

    memset(&mSrc, 0, sizeof(mSrc));
    memset(&mDst, 0, sizeof(mDst));
    memset(&mCrop, 0, sizeof(mCrop));

    for (int i = 0; i < MAX_NUMBUF; i++) {
        mInput_buf_dmafd[i] = -1;
        mInput_buf_dmafd_uv[i] = -1;
        mOutput_buf_dmafd[i] = -1;
        mOutput_buf_dmafd_uv[i] = -1;
        mBo[i] = NULL;
        mBo1[i] = NULL;
    }

}

VpeFilter::~VpeFilter() {
    FUNC_TRACK();

    stop();

    if (mDevice)
        dce_deinit(mDevice);
}

bool VpeFilter::ensureVideoDevice() {
    if (mFd >= 0)
        return true;

    char devname[20] = "/dev/video10";
    mFd =  open(devname, O_RDWR);
    if(mFd < 0) {
        ERROR("Cant open %s\n", devname);
        return false;
    }

    INFO("vpe:%s open success!!! %d", devname, mFd);

    return true;
}

bool VpeFilter::describeFormat (uint32_t fourcc, struct image_params *image) {
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

bool VpeFilter::setListener(Listener* listener) {
    FUNC_TRACK();
    MMAutoLock lock(mLock);

    mListener = listener;
    return true;
}

bool VpeFilter::configureInput(int w, int h, uint32_t fourcc, int bufferNum, int deint) {
    FUNC_TRACK();
    if (!ensureVideoDevice())
        return false;

    MMAutoLock lock(mLock);

    mSrc.width = w;
    mSrc.height = h;
    mSrc.numbuf = bufferNum;
    mDeint = deint;

    // TODO should be other values?
    ERROR("w h fourcc num deint %d %d %x %d %d", w, h, fourcc, bufferNum, deint);
    mTranslen = 3;

    switch (deint) {
    case 1:
        mField = V4L2_FIELD_ALTERNATE;
        break;
    case 2:
        mField = V4L2_FIELD_SEQ_TB;
        break;
    case 0:
    default:
        mField = V4L2_FIELD_ANY;
        break;
    }

    if (!describeFormat(fourcc, &mSrc)) {
        return false;
    }

    mCrop.c.top = 0;
    mCrop.c.left = 0;
    mCrop.c.width = mSrc.width;
    mCrop.c.height = mSrc.height;
    mCrop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    INFO("input configured: %dx%d, fourcc %x, coplanar %d, v4l2 color %d, numbuf %d",
         mSrc.width, mSrc.height, mSrc.fourcc, mSrc.coplanar, mSrc.colorspace, mSrc.numbuf);

    int ret;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers rqbufs;

/*
    struct  v4l2_control ctrl;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_TRANS_NUM_BUFS;
    ctrl.value = mTranslen;
    ret = ioctl(mFd, VIDIOC_S_CTRL, &ctrl);

    if (ret < 0) {
        ERROR("vpe: S_CTRL failed");
        return false;
    }
*/

    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    ret = ioctl(mFd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        ERROR( "vpe i/p: G_FMT_1 failed: %s\n", strerror(errno));
        return false;
    }

    fmt.fmt.pix_mp.width = mSrc.width;
    fmt.fmt.pix_mp.height = mSrc.height;
    fmt.fmt.pix_mp.pixelformat = mSrc.fourcc;
    fmt.fmt.pix_mp.colorspace = mSrc.colorspace;

    switch (mDeint) {
    case 1:
            fmt.fmt.pix_mp.field = V4L2_FIELD_ALTERNATE;
            break;
    case 2:
            fmt.fmt.pix_mp.field = V4L2_FIELD_SEQ_TB;
            break;
    case 0:
    default:
            fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
            break;
    }

    ret = ioctl(mFd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ERROR( "vpe i/p: S_FMT failed: %s\n", strerror(errno));
    } else {
        mSrc.size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
        mSrc.size_uv = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
        INFO("size %d, uv size %d", mSrc.size, mSrc.size_uv);
    }

    ret = ioctl(mFd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        ERROR( "vpe i/p: G_FMT_2 failed: %s\n", strerror(errno));
        return false;
    }

    INFO("vpe i/p: G_FMT: width = %u, height = %u, 4cc = %.4s",
                    fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                    (char*)&fmt.fmt.pix_mp.pixelformat);
    setCrop();

    memset(&rqbufs, 0, sizeof(rqbufs));
    rqbufs.count = mSrc.numbuf;
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    rqbufs.memory = V4L2_MEMORY_DMABUF;

    ret = ioctl(mFd, VIDIOC_REQBUFS, &rqbufs);
    if (ret < 0) {
        ERROR( "vpe i/p: REQBUFS failed: %s\n", strerror(errno));
        return false;
    }

    //mSrc.numbuf = rqbufs.count;
    INFO("vpe i/p: allocated buffers = %d\n", rqbufs.count);

    mInputConfigured = true;

    return true;
}

bool VpeFilter::configureOutput(int w, int h, uint32_t fourcc) {
    FUNC_TRACK();

    if (!ensureVideoDevice())
        return false;

    MMAutoLock lock(mLock);

    if (!describeFormat(fourcc, &mDst)) {
        return false;
    }

    mDst.numbuf = OUTPUT_BUFFER_NUM;
    INFO("output configured: %dx%d, fourcc %x, coplanar %d, v4l2 color %d, numbuf %d",
         mDst.width, mDst.height, mDst.coplanar, mDst.colorspace, mDst.numbuf);

    if(!allocateOutputBuffer()) {
        ERROR("fail to allocate output buffers");
        return false;
    }

    int ret;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers rqbufs;

    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    ret = ioctl(mFd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
            ERROR( "vpe o/p: G_FMT_1 failed: %s", strerror(errno));
        destroyOutputBuffer();
        return false;
    }

    fmt.fmt.pix_mp.width = mDst.width;
    fmt.fmt.pix_mp.height = mDst.height;
    fmt.fmt.pix_mp.pixelformat = mDst.fourcc;
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    fmt.fmt.pix_mp.colorspace = mDst.colorspace;

    ret = ioctl(mFd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ERROR( "vpe o/p: S_FMT failed: %s", strerror(errno));
        destroyOutputBuffer();
        return false;
    }

    ret = ioctl(mFd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        ERROR( "vpe o/p: G_FMT_2 failed: %s", strerror(errno));
        destroyOutputBuffer();
        return false;
    }

    INFO("vpe o/p: G_FMT: width = %u, height = %u, 4cc = %.4s",
                    fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                    (char*)&fmt.fmt.pix_mp.pixelformat);

    memset(&rqbufs, 0, sizeof(rqbufs));
    rqbufs.count = mDst.numbuf;
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    rqbufs.memory = V4L2_MEMORY_DMABUF;

    ret = ioctl(mFd, VIDIOC_REQBUFS, &rqbufs);
    if (ret < 0) {
        ERROR( "vpe o/p: REQBUFS failed: %s", strerror(errno));
        destroyOutputBuffer();
        return false;
    }

    //vpe->dst.numbuf = rqbufs.count;
    INFO("vpe o/p: allocated buffers = %d", rqbufs.count);

    mOutputConfigured = true;
    return true;
}

int VpeFilter::setCrop() {
    FUNC_TRACK();

    if (!ensureVideoDevice())
        return -1;

    MMAutoLock lock(mLock);
    int ret = 0;

    ret = ioctl(mFd, VIDIOC_S_CROP, &mCrop);
    if (ret < 0)
        ERROR("error setting crop\n");

    return 0;
}

int VpeFilter::streamOn() {
    FUNC_TRACK();

    int ret;
    int fd = mFd;
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        ERROR("STREAMON failed,  %d: %s\n", type, strerror(errno));
        return -1;
    }

    INFO("stream ON: done! fd = %d,  type = %d\n", fd, type);

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        ERROR("STREAMON failed,  %d: %s\n", type, strerror(errno));
        return -1;
    }

    INFO("stream ON: done! fd = %d,  type = %d\n", fd, type);

    mStreamOn = true;
    return 0;
}

int VpeFilter::streamOff() {
    FUNC_TRACK();

    int ret;
    int fd = mFd;
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        ERROR("STREAMON failed,  %d: %s\n", type, strerror(errno));
        return -1;
    }

    INFO("stream OFF: done! fd = %d,  type = %d\n", fd, type);

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        ERROR("STREAMON failed,  %d: %s\n", type, strerror(errno));
        return -1;
    }

    INFO("stream OFF: done! fd = %d,  type = %d\n", fd, type);

    mStreamOn = false;
    return 0;

}

void* vpe_filter_process(void *arg)
{
    FUNC_TRACK();
    int ret;
    int index;
    int fd[2];
    bool run = false;
    VpeFilter *vpe = (VpeFilter*)arg;
    if (!vpe) {
        ERROR("invalid arg");
        return NULL;
    }

    for (int i = 0; i < vpe->mDst.numbuf; i++) {
        vpe->queueOutputBuffer(i);
    }

    vpe->mInputBufferNum = 0;

    while(vpe->mContinue) {

        {
            MMAutoLock lock(vpe->mLock);
            if (vpe->mInputFd.empty()) {
                vpe->mCondition.wait();
                continue;
            }
        }

        {
            MMAutoLock lock(vpe->mLock);
            fd[0] = vpe->mInputFd.front();
            vpe->mInputFd.pop();
            if (vpe->mSrc.coplanar) {
                fd[1] = vpe->mInputFd1.front();
                vpe->mInputFd1.pop();
            }

            std::map<int, int>::iterator it = vpe->mInputIndexMap.find(fd[0]);
            if (it == vpe->mInputIndexMap.end()) {
                vpe->mInputIndexMap[fd[0]] = vpe->mInputBufferNum;
                index = vpe->mInputBufferNum;
                vpe->mInputBufferNum++;
                INFO("fd %d, index %d", fd[0], index);
                vpe->mInput_buf_dmafd[index] = fd[0];
                if (vpe->mSrc.coplanar)
                    vpe->mInput_buf_dmafd_uv[index] = fd[1];
            } else
                index = it->second;
        }

        ret = vpe->queueInputBuffer(index);
        if (!ret)
            return NULL;

        vpe->mInputCount++;
        if (vpe->mInputCount < 3)
            continue;

        if (!run) {
            vpe->streamOn();
            run = true;
        }

        index = vpe->dequeueInputBuffer();
        if (index >= 0 && vpe->mListener) {
            fd[0] = vpe->mInput_buf_dmafd[index];
            if (vpe->mSrc.coplanar)
                fd[1] = vpe->mInput_buf_dmafd_uv[index];
            // emptied rgb buffer is sending to Westow
            vpe->mListener->onBufferEmptied(fd, vpe->mSrc.coplanar);
        }

        index = vpe->dequeueOutputBuffer();
        if (index >= 0 && vpe->mListener) {
            fd[0] = vpe->mOutput_buf_dmafd[index];
            if (vpe->mDst.coplanar)
                fd[1] = vpe->mOutput_buf_dmafd_uv[index];
            int64_t pts;
            if (vpe->mInputQPts.empty()) {
                WARNING("should not be here...");
                pts = 0;
            } else
                pts = vpe->mInputQPts.front();
            vpe->mInputQPts.pop();
            // Filled nv12 buffer is to be encoding
            vpe->mListener->onBufferFilled(fd, vpe->mDst.coplanar, pts);
        }
    }

    vpe->mInputIndexMap.clear();
    vpe->mInputBufferNum = 0;
    INFO("vpe filter process is going to exit");
    return NULL;
}

bool VpeFilter::start() {
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

    ret = pthread_create(&mThreadID, NULL, vpe_filter_process, this);
    if(ret) {
        INFO("could not create task for vpe filter processing");
        return false;
    }

    mStarted = true;
    return true;
}

void VpeFilter::stop() {
    FUNC_TRACK();
    {
        MMAutoLock lock(mLock);

        if (!mStarted) {
            INFO("not started");
            return true;
        }

        if (mStreamOn)
            streamOff();

        mContinue = false;
        mCondition.broadcast();
    }

    void * aaa = NULL;
    pthread_join(mThreadID, &aaa);

    INFO("vpe process joined");

    {
        MMAutoLock lock(mLock);
        destroyOutputBuffer();
        close(mFd);

        for (int i = 0; i < MAX_NUMBUF; i++) {
            mInput_buf_dmafd[i] = -1;
            mInput_buf_dmafd_uv[i] = -1;
        }

        mStarted = false;
    }

    return true;
}

bool VpeFilter::emptyBuffer(int fd[], bool multiBo, int64_t pts) {
    FUNC_TRACK();
    MMAutoLock lock(mLock);

    if (!fd)
        return false;

    mInputQPts.push(pts);
    mInputFd.push(fd[0]);

    bool tmp = !!mSrc.coplanar;
    if (tmp != multiBo)
        WARNING("invalid format");

    //if (mSrc.coplanar)
    if (multiBo)
        mInputFd1.push(fd[1]);

    if (mInputFd.size() == 1)
        mCondition.signal();

    return true;
}

bool VpeFilter::fillBuffer(int fd[], bool multiBo) {
    FUNC_TRACK();

    if (!fd)
        return false;

    int index;

    {
        MMAutoLock lock(mLock);
        std::map<int, int>::iterator it = mOutputIndexMap.find(fd[0]);
        if (it == mOutputIndexMap.end()) {
            ERROR("invalid buffer");
            return false;
        }
        index = it->second;
    }

    int ret = queueOutputBuffer(index);
    if (ret < 0)
        return false;

    return true;
}

bool VpeFilter::allocateOutputBuffer() {
    FUNC_TRACK();

    mDevice = (struct omap_device*)dce_init();

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

bool VpeFilter::allocBuffer(int index, uint32_t fourcc, uint32_t w, uint32_t h, bool multiBo) {
    FUNC_TRACK();

    uint32_t bo_handles[4] = {0}/*, offsets[4] = {0}*/;
    uint32_t pitches[4] = {0};;


    switch(fourcc) {
    case FOURCC('N','V','1','2'):
        if (multiBo) {
                mBo[index] = allocBo(mDevice, 8, w, h,
                                &bo_handles[0], &pitches[0]);

                mOutput_buf_dmafd[index] = omap_bo_dmabuf(mBo[index]);

                mBo1[index] = allocBo(mDevice, 16, w/2, h/2,
                                &bo_handles[1], &pitches[1]);

                mOutput_buf_dmafd_uv[index] = omap_bo_dmabuf(mBo1[index]);
        } else {
                mBo[index] = allocBo(mDevice, 8, w, h * 3 / 2,
                                &bo_handles[0], &pitches[0]);
                mOutput_buf_dmafd[index] = omap_bo_dmabuf(mBo[index]);
                bo_handles[1] = bo_handles[0];
                pitches[1] = pitches[0];
                //offsets[1] = w * h;
        }
        mOutputIndexMap[mOutput_buf_dmafd[index]] = index;
        break;
    case FOURCC('R','G','B','4'): // V4L2_PIX_FMT_RGB32 argb8888 argb4
            mBo[index] = allocBo(mDevice, 8*4, w, h,
                            &bo_handles[0], &pitches[0]);
            mOutput_buf_dmafd[index] = omap_bo_dmabuf(mBo[index]);
            bo_handles[1] = bo_handles[0];
            pitches[1] = pitches[0];
        break;
    default:
        ERROR("invalid format: 0x%08x", mDst.fourcc);
        goto fail;
    }

    return true;

fail:
    return false;
}

void VpeFilter::destroyOutputBuffer() {
    FUNC_TRACK();

    int i;

    for (i = 0; i < MAX_NUMBUF; i++) {
        if (mBo[i] == NULL)
            break;

        omap_bo_del(mBo[i]);
        close(mOutput_buf_dmafd[i]);
    }

    for (i = 0; i < MAX_NUMBUF; i++) {
        if (mBo1[i] == NULL)
            break;

        omap_bo_del(mBo1[i]);
        close(mOutput_buf_dmafd_uv[i]);
    }

    for (int i = 0; i < MAX_NUMBUF; i++) {
        mOutput_buf_dmafd[i] = -1;
        mOutput_buf_dmafd_uv[i] = -1;
        mBo[i] = NULL;
        mBo1[i] = NULL;
    }

    mOutputIndexMap.clear();

    if (mDevice) {
        dce_deinit(mDevice);
        mDevice = NULL;
    }
}

int VpeFilter::queueOutputBuffer(int index) {
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];

    if (!mStarted) {
        ERROR("not started");
        return -1;
    }

    DEBUG("queue output buffer %d\n", index);

    memset(&buf, 0, sizeof buf);
    memset(&planes, 0, sizeof planes);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index = index;
    buf.m.planes = planes;
    if(mDst.coplanar)
            buf.length = 2;
    else
            buf.length = 1;

    buf.m.planes[0].m.fd = mOutput_buf_dmafd[index];

    if(mDst.coplanar)
            buf.m.planes[1].m.fd = mOutput_buf_dmafd_uv[index];

    ret = ioctl(mFd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        ERROR( "vpe o/p: QBUF failed: %s, index = %d\n",
                    strerror(errno), index);
        return ret;
    }

    return 0;
}

int VpeFilter::dequeueOutputBuffer() {
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];

    if (!mStarted) {
        ERROR("not started");
        return -1;
    }

    VERBOSE("vpe output dequeue buffer");

    memset(&buf, 0, sizeof buf);
    memset(&planes, 0, sizeof planes);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.m.planes = planes;
    if(mDst.coplanar)
        buf.length = 2;
    else
        buf.length = 1;
    ret = ioctl(mFd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        ERROR("vpe o/p: DQBUF failed: %s\n", strerror(errno));
        return -1;
    }

    DEBUG("dequeue output buffer %d", buf.index);

    return buf.index;
}

int VpeFilter::queueInputBuffer(int index) {
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];

    if (!mStarted) {
        ERROR("not started");
        return -1;
    }

    DEBUG("vpe: src QBUF (%d):%s field, index %d", mField,
            mField==V4L2_FIELD_TOP?"top":"bottom", index);

    memset(&buf, 0, sizeof buf);
    memset(&planes, 0, sizeof planes);

    planes[0].length = planes[0].bytesused = mSrc.size;
    if(mSrc.coplanar)
            planes[1].length = planes[1].bytesused = mSrc.size_uv;

    planes[0].data_offset = planes[1].data_offset = 0;

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index = index;
    buf.m.planes = planes;
    buf.field = mField;
    if(mSrc.coplanar)
            buf.length = 2;
    else
            buf.length = 1;

    buf.m.planes[0].m.fd = mInput_buf_dmafd[index];
    if(mSrc.coplanar)
            buf.m.planes[1].m.fd = mInput_buf_dmafd_uv[index];

    ret = ioctl(mFd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
            ERROR( "vpe i/p: QBUF failed: %s, index = %d\n",
                    strerror(errno), index);
        return ret;
    }

    return 0;
}

int VpeFilter::dequeueInputBuffer() {
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];

    if (!mStarted) {
        ERROR("not started");
        return -1;
    }

    VERBOSE("vpe input dequeue buffer\n");

    memset(&buf, 0, sizeof buf);
    memset(&planes, 0, sizeof planes);

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.m.planes = planes;
    if(mSrc.coplanar)
            buf.length = 2;
    else
            buf.length = 1;
    ret = ioctl(mFd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        ERROR("vpe i/p: DQBUF failed: %s\n", strerror(errno));
        return -1;
    }

    DEBUG("dequeue input buffer %d\n", buf.index);

    return buf.index;
}

};
