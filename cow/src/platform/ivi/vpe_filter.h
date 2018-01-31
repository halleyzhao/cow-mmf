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

#ifndef vpe_ti_h
#define vpe_ti_h

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "multimedia/mm_cpp_utils.h"

#include <queue>
#include <map>

#define V4L2_CID_TRANS_NUM_BUFS         (V4L2_CID_PRIVATE_BASE)

#define MAX_NUMBUF 12
#define OUTPUT_BUFFER_NUM 6

struct omap_device;
struct omap_bo;

namespace YUNOS_MM {

class VpeFilter {
public:
    class Listener {
    public:
        Listener() {}
        virtual ~Listener() {}
        virtual void onBufferEmptied(int fd[], bool multiBo) = 0;
        virtual void onBufferFilled(int fd[], bool multiBo, int64_t pts) = 0;
    };

    VpeFilter();
    virtual ~VpeFilter();

    bool setListener(Listener *listener);

    bool configureInput(int w, int h, uint32_t fourcc, int bufferNum, int deint = 0);
    bool configureOutput(int w, int h, uint32_t fourcc);

    bool start();
    void stop();

    bool emptyBuffer(int fd[], bool multiBo, int64_t pts);
    bool fillBuffer(int fd[], bool multiBo);

friend void* vpe_filter_process(void *arg);

private:

    struct image_params {
        int width;
        int height;
        int fourcc;
        int size;
        int size_uv;
        int coplanar;
        enum v4l2_colorspace colorspace;
        int numbuf;
    };

    bool ensureVideoDevice();
    bool describeFormat(uint32_t fourcc, struct image_params *image);
    int streamOn();
    int streamOff();
    int setCrop();
    bool allocateOutputBuffer();
    void destroyOutputBuffer();
    bool allocBuffer(int index, uint32_t fourcc, uint32_t w, uint32_t h, bool multiBo);

    int queueOutputBuffer(int index);
    int dequeueOutputBuffer();

    int queueInputBuffer(int index);
    int dequeueInputBuffer();

    bool mInputConfigured;
    bool mOutputConfigured;

    int mFd;
    int mField;
    int mDeint; // input de-interface
    int mTranslen; // num of buffers to process in one transaction

    struct image_params mSrc;
    struct image_params mDst;

    struct  v4l2_crop mCrop;

    int mInput_buf_dmafd[MAX_NUMBUF];
    int mInput_buf_dmafd_uv[MAX_NUMBUF];
    int mOutput_buf_dmafd[MAX_NUMBUF];
    int mOutput_buf_dmafd_uv[MAX_NUMBUF];

    struct omap_bo* mBo[MAX_NUMBUF];
    struct omap_bo* mBo1[MAX_NUMBUF];

    bool mStarted;
    bool mStreamOn;

    Lock mLock;
    Condition mCondition;
    bool mContinue;
    pthread_t mThreadID;

    uint32_t mInputCount;
    uint32_t mOutputCount;

    std::queue<int64_t> mInputQPts;
    std::queue<int> mInputFd;
    std::queue<int> mInputFd1;
    std::map<int, int> mInputIndexMap; // <fd, index of input queue>
    int mInputBufferNum;

    std::map<int, int> mOutputIndexMap; // <fd, index of output queue>

    Listener *mListener;
    struct omap_device *mDevice;
};

};
#endif //_vpe_ti_h_
