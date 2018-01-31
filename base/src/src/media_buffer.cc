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
#include <stdint.h>
#include <string.h>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/mm_debug.h"

MM_LOG_DEFINE_MODULE_NAME("Cow-MediaBuffer");

// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

namespace YUNOS_MM {

const int64_t MediaBuffer::MediaBufferSizeUndefined = -1;
const int64_t MediaBuffer::MediaBufferTimeInvalid = -1;

MediaBuffer::~MediaBuffer()
{
    FUNC_TRACK();
    int i=0;
    for (i=0; i<mReleaseFuncCount; i++) {
        mReleaseFunc[i](this);
    }
}

MediaBuffer::MediaBuffer(MediaBufferType type)
    : mType(type)
    , mSize(MediaBufferSizeUndefined)
    , mDts(MediaBufferTimeInvalid)
    , mPts(MediaBufferTimeInvalid)
    , mDuration(MediaBufferTimeInvalid)
    , mReleaseFuncCount(0)
{
    FUNC_TRACK();
    mFlags = 0;
    memset(mBuffers, 0, sizeof(mBuffers));
    memset(mOffsets, 0, sizeof(mOffsets));
    memset(mStrides, 0, sizeof(mStrides));
    memset(mReleaseFunc, 0, sizeof(mReleaseFunc));
    mBirthTime = getTimeUs();

    mMeta = MediaMeta::create();
}

MediaBufferSP MediaBuffer::createMediaBuffer(MediaBufferType type)
{
    FUNC_TRACK();
    MediaBufferSP buffer;

    buffer.reset(new MediaBuffer(type));

    return buffer;
}

bool MediaBuffer::setBufferInfo(uintptr_t* buffers, int32_t* offsets, int32_t* strides, int dimension)
{
    FUNC_TRACK();
    int i=0;

    MMASSERT(dimension <= MediaBufferMaxDataPlane);
    if (dimension>MediaBufferMaxDataPlane)
        return false;

    if (isFlagSet(MBFT_BufferInited)) {
        /*
        * we don't support change buffer data since there seems to be no such usage.
        * - besides buffer/data, mMeta decides how to interpret it. it is useless to update data only
        * - usually media buffer is create once and used some places, it is not good to change the data on the fly (it's hard to sync)
        * - when you are thinking about setBufferInfo for another time, usually you'd create another MediaBuffer
        * - if we DO meet case to update MediaBuffer data, we'd call mReleaseFunc[] first
        */
        ERROR("wrong usage, buffer has been initialized before");
        return false;
    }

    for(i=0; i<dimension; i++) {
        if (buffers)
            mBuffers[i] = buffers[i];
        if (offsets)
            mOffsets[i] = offsets[i];
        if (strides)
            mStrides[i] = strides[i];
    }

    setFlag(MBFT_BufferInited);
    return true;
}

bool MediaBuffer::getBufferInfo(uintptr_t* buffers, int32_t* offsets, int32_t* strides, int dimension) const
{
    FUNC_TRACK();
    int i=0;

    MMASSERT(dimension <= MediaBufferMaxDataPlane);
    if (dimension>MediaBufferMaxDataPlane)
        return false;

    if (!isFlagSet(MBFT_BufferInited))
        return false;

    for(i=0; i<dimension; i++) {
        if (buffers)
            buffers[i] = mBuffers[i];
        if (offsets)
            offsets[i] = mOffsets[i];
        if (strides)
            strides[i] = mStrides[i];
    }

    return true;
}

bool MediaBuffer::updateBufferOffset(int32_t* offsets, int dimension)
{
    FUNC_TRACK();
    int i=0;

    MMASSERT(dimension <= MediaBufferMaxDataPlane);
    if (dimension>MediaBufferMaxDataPlane)
        return false;

    for(i=0; i<dimension; i++) {
        if (offsets)
            mOffsets[i] = offsets[i];
    }

    return true;
}

bool MediaBuffer::addReleaseBufferFunc(releaseBufferFunc releaseFunc)
{
    FUNC_TRACK();
    // not thread safe; assume curren filter owns the buffer
    MMASSERT(mReleaseFuncCount+1<=MediaBufferMaxReleaseFunc);
    if (mReleaseFuncCount+1>MediaBufferMaxReleaseFunc)
        return false;
    mReleaseFuncCount++;

    mReleaseFunc[mReleaseFuncCount-1] = releaseFunc;

    return true;
}

int32_t MediaBuffer::ageInMs() const
{
    int64_t age = (getTimeUs() - mBirthTime)/1000;
    return int32_t(age);
}

} // end of namespace YUNOS_MM
