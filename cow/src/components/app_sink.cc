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
#include <math.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_sink.h"
#include <unistd.h>
#include <multimedia/media_attr_str.h>
#include <multimedia/mm_types.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(APPSink)
DEFINE_LOGTAG(APPSink::APPSinkWriter)
DEFINE_LOGTAG(APPSink::APPSinkReader)

APPSink::APPSinkReader::APPSinkReader(APPSink * component)
                    : mComponent(component)
{
    MMLOGI("+\n");
}

APPSink::APPSinkReader::~APPSinkReader()
{
    MMLOGI("+\n");
}

mm_status_t APPSink::APPSinkReader::read(MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    return mComponent->read(buffer);
}

MediaMetaSP APPSink::APPSinkReader::getMetaData()
{
    MMLOGI("+\n");
    return mComponent->getMeta();
}

APPSink::APPSinkWriter::APPSinkWriter(APPSink * component) : mComponent(component)
{
    MMLOGI("+\n");
}

APPSink::APPSinkWriter::~APPSinkWriter()
{
    MMLOGI("+\n");
}

mm_status_t APPSink::APPSinkWriter::write(const MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    return mComponent->write(buffer);
}

mm_status_t APPSink::APPSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    MMLOGI("+\n");
    return mComponent->setMeta(metaData);
}

APPSink::APPSink()
    : mIsLive(false)
    , mDropFrameThreshHold1(1)
    , mDropFrameThreshHold2(5)
    , mInputBufferCount(0)
    , mOutputBufferCount(0)
    , mDropedBufferCount(0)
{
    MMLOGI("+\n");
}

APPSink::~APPSink()
{
    MMLOGI("+\n");
    reset();
}

Component::WriterSP APPSink::getWriter(MediaType mediaType)
{
    MMLOGV("mediaType: %d\n", mediaType);
    MMAutoLock lock(mLock);
    return WriterSP(new APPSinkWriter(this));
}

Component::ReaderSP APPSink::getReader(MediaType mediaType)
{
    MMLOGI("mediaType: %d\n", mediaType);
    MMAutoLock lock(mLock);

    return ReaderSP(new APPSinkReader(this));
}

mm_status_t APPSink::setMeta(const MediaMetaSP & metaData)
{
    MMLOGI("+\n");
    MMAutoLock lock(mLock);
    mMeta = metaData;
    return MM_ERROR_SUCCESS;
}

MediaMetaSP APPSink::getMeta()
{
    MMLOGI("+\n");
    MMAutoLock lock(mLock);
    MediaMetaSP meta(mMeta);
    return meta;
}

mm_status_t APPSink::reset()
{
    MMLOGI("+\n");
    flush();
    return MM_ERROR_SUCCESS;
}

mm_status_t APPSink::flush()
{
    MMLOGI("+\n");
    MMAutoLock lock(mLock);
    while (!mBuffers.empty()) {
        mBuffers.pop();
    }
    return MM_ERROR_SUCCESS;
}

// live stream: when buffer queue size is greater than maxQSize, drop non-key frame on head
void APPSink::dropNonRefFrame_l(uint32_t maxQSize)
{
    if (!mIsLive || mBuffers.size() <= maxQSize)
        return;

    while(!mBuffers.empty()) {
        MediaBufferSP bufFront = mBuffers.front();
         if (!bufFront->isFlagSet(MediaBuffer::MBFT_NonRef))
            break;

         mBuffers.pop();
         mDropedBufferCount++;
         DEBUG("drop aged buffer (%d, %d)", mDropedBufferCount, mInputBufferCount);
    }
}

mm_status_t APPSink::write(const MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    if (!buffer)
        return MM_ERROR_SUCCESS;

    MMAutoLock lock(mLock);

    // 0. when cached buffer count is greater than threashhold1, try to drop non-ref frame before reach threshhold2.
    // 1. when read() is not called in time, try to drop non-ref key frame on queue head directly.
    dropNonRefFrame_l(mDropFrameThreshHold1-1);

    // 2. it means the non-ref compressed video frame cached in libyami_v4l2 can be dropped  before input buffer (camera frame) of VEV4L2
    if (mIsLive && mBuffers.size() > mDropFrameThreshHold1 && buffer->isFlagSet(MediaBuffer::MBFT_NonRef)) {
        mDropedBufferCount++;
        DEBUG("drop aged buffer (%d, %d)", mDropedBufferCount, mInputBufferCount);
    } else {
        mBuffers.push(buffer);
        mInputBufferCount++;
    }

    MM_CALCULATE_AVG_FPS("APPSink-write");
    return MM_ERROR_SUCCESS;
}

mm_status_t APPSink::read(MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    MMAutoLock lock(mLock);
    if (mBuffers.empty()) {
        MMLOGV("empty\n");
        return MM_ERROR_NO_MORE;
    }

#if 0
    // drop ref frames help to reduce latency, but lead to artifact
    while (mIsLive && mBuffers.size() >mDropFrameThreshHold2-2) {
        DEBUG("drop aged buffers");
        mBuffers.pop();
        mDropedBufferCount++;
    }
#endif

    dropNonRefFrame_l(mDropFrameThreshHold1);
    MMASSERT(!mBuffers.empty());
    buffer = mBuffers.front();
    mBuffers.pop();
    mOutputBufferCount++;
    MMLOGD("mBuffers.size(): %zu, mInputBufferCount: %d, mOutputBufferCount: %d, mDropedBufferCount: %d, buffer pts: %" PRId64 ", age: %dms",
        mBuffers.size(), mInputBufferCount, mOutputBufferCount, mDropedBufferCount, buffer->pts(), buffer->ageInMs());

    return MM_ERROR_SUCCESS;
}

mm_status_t APPSink::setParameter(const MediaMetaSP & meta)
{
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, "is-live-stream") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s", item.mName);
                continue;
            }
            mIsLive = (bool)item.mValue.ii;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        }
        if ( !strcmp(item.mName, "live-stream-drop-threshhold1") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mDropFrameThreshHold1 = item.mValue.ii;
            if (mDropFrameThreshHold1<1)
                mDropFrameThreshHold1 = 1;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        }
        if ( !strcmp(item.mName, "live-stream-drop-threshhold2") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mDropFrameThreshHold2 = item.mValue.ii;
            if (mDropFrameThreshHold2<3)
                mDropFrameThreshHold2 = 3;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        }
        else {
            INFO("unknown parameter %s\n", item.mName);
        }
    }
    return MM_ERROR_SUCCESS;
}

extern "C" {

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("APPSinkCreater");

Component * createComponent(const char* mimeType, bool isEncoder)
{
    MMLOGI("+\n");
    APPSink * com = new APPSink();
    if ( !com ) {
        MMLOGE("no mem\n");
        return NULL;
    }

    MMLOGI("ret: %p\n", com);
    return com;
}

void releaseComponent(Component * component)
{
    MMLOGI("%p\n", component);
    if ( component ) {
        APPSink * com = DYNAMIC_CAST<APPSink*>(component);
        MM_RELEASE(com);
    }
}
}

}

