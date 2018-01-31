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

#ifndef csc_filter
#define csc_filter

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

#include <mm_surface_compat.h>

#define MAX_NUMBUF 12
#define OUTPUT_BUFFER_NUM 6

struct omap_device;
struct omap_bo;

namespace YUNOS_MM {

class CscFilter {
public:
    class Listener {
    public:
        Listener() {}
        virtual ~Listener() {}
        virtual void onBufferEmptied(MMNativeBuffer* buffer) = 0;
        virtual void onBufferFilled(MMNativeBuffer* buffer, int64_t pts) = 0;
    };

    CscFilter();
    virtual ~CscFilter();

    virtual bool setListener(Listener *listener);

    virtual bool configureInput(int w, int h, uint32_t fourcc, int bufferNum, int deint = 0);
    virtual bool configureOutput(int w, int h, uint32_t fourcc, int devFd);

    virtual bool start();
    virtual void stop();

    virtual bool emptyBuffer(MMNativeBuffer* buffer, int64_t pts);
    virtual bool fillBuffer(MMNativeBuffer* buffer);

    struct omap_bo* getMapBoFromName(uint32_t handle);

friend void* csc_filter_process(void *arg);

protected:

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

    bool describeFormat(uint32_t fourcc, struct image_params *image);
    bool allocateOutputBuffer(int devFd);
    void destroyOutputBuffer();
    bool allocBuffer(int index, uint32_t fourcc, uint32_t w, uint32_t h, bool multiBo);

    void filterProcess();
    void cscConvert(struct omap_bo *in, struct omap_bo *out);

    bool mInputConfigured;
    bool mOutputConfigured;

    int mField;
    int mDeint; // input de-interface

    struct image_params mSrc;
    struct image_params mDst;

    bool mStarted;

    Lock mLock;
    Condition mCondition;
    bool mContinue;
    pthread_t mThreadID;

    uint32_t mInputCount;
    uint32_t mOutputCount;

    struct BufferInfo {
        MMNativeBuffer *buf;
        bool ownByUs;
        int64_t pts;
    };

    BufferInfo mInputBuffers[MAX_NUMBUF];
    BufferInfo mOutputBuffers[MAX_NUMBUF];

    int mInput_buf_dmafd[MAX_NUMBUF];
    int mInput_buf_dmafd_uv[MAX_NUMBUF];
    int mOutput_buf_dmafd[MAX_NUMBUF];
    int mOutput_buf_dmafd_uv[MAX_NUMBUF];

    // output buffer object
    struct omap_bo* mBo[MAX_NUMBUF];
    struct omap_bo* mBo1[MAX_NUMBUF];


    bool mOutputState[MAX_NUMBUF]; // 0: own by us; 1: own by codec

    std::queue<int64_t> mInputQPts;
    std::queue<int> mInputFd;
    std::queue<int> mInputFd1;
    std::queue<struct omap_bo*> mInputBo;
    std::map<MMNativeBuffer*, int> mInputIndexMap;
    int mInputBufferNum;

    std::map<MMNativeBuffer*, int> mOutputIndexMap;

    std::queue<int> mOutputFd;

    Listener *mListener;
    struct omap_device *mDevice;
};

};
#endif //_csc_filter_
