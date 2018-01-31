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

#ifndef __video_source_surface_H
#define __video_source_surface_H

#include "video_source_base.h"
#include "video_consumer_base.h"
#include "stdint.h"

namespace YUNOS_MM {

class VideoSourceSurface;

class SurfaceFrameGenerator : public VideoConsumerListener {
public:
    SurfaceFrameGenerator(SourceComponent *comp);
    virtual ~SurfaceFrameGenerator();
    virtual mm_status_t configure(SourceType type,  const char *fileName,
                                  int width, int height,
                                  float fps, uint32_t fourcc = VIDEO_SOURCE_DEFAULT_CFORMAT);
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t flush();
    virtual mm_status_t reset();

    void signalBufferReturned(void*, int64_t);
    MediaBufferSP getMediaBuffer();

    mm_status_t setParameters(const MediaMetaSP & meta) {return MM_ERROR_SUCCESS;}

    /* on VideoConsumerListener */
    virtual void bufferAvailable(void* buffer, int64_t pts = -1ll);

private:
    mm_status_t prepareInputBuffer();

    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);
    static const char* getOwnerName(int state);

private:

#define MAX_BUFFER 32
#define START_PTS 0
#define MAX_PTS 0xffffffff
#define MIN_PTS -100000000

    typedef MMSharedPtr<void*> NativeBufferSP;

private:
    enum BufferState {
        OWNED_BY_US_EMPTY,
        OWNED_BY_US_FILLED,
        OWNED_BY_READER,
        OWNED_BY_WESTON,
    };

    struct BufferInfo {
        void *bnb;
        BufferState state;
        int64_t pts; // pts of video frame
        int64_t pts1; // pts when video frame get to this component
        NativeBufferSP yuvBuffer;
        bool empty;
    };

private:
    void dumpBufferInfo();
    const char* bufferStateToString(BufferState state);

private:
    int32_t mInputBufferCount;
    //WindowSurface *mNativeWindow;

    int32_t mWidth;
    int32_t mHeight;
    uint32_t mSourceColorFormat;
    uint32_t mEncoderColorFormat;
    BufferInfo mBufferInfo[MAX_BUFFER];

    uint32_t mFrameNum;
    int64_t mFramePts;
    float mFrameRate;

    int64_t mStartMediaTime;
    int64_t mNextMediaTime;
    int32_t mDuration;
    int mEncoderLastIdx;
    bool mNeedRepeat;

    VideoSourceSurface *mComponent;
    VideoConsumerCore *mConsumer;

    Lock mFrameLock; // protect mBufferInfo[]
    Lock mLock; // protect mIsContinue and VirtualDisplay

    Condition mFillFrameCondition;
    Condition mEmptyFrameCondition;
    bool mIsContinue;
    int mRepeatCount;
};

class VideoSourceSurface : public VideoSourceBase <SurfaceFrameGenerator> {

public:

    VideoSourceSurface(const char* mime);
    virtual ~VideoSourceSurface();

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    //virtual mm_status_t stop();
    virtual mm_status_t prepare();

friend class SurfaceFrameGenerator;

private:
    std::string mMime;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    //DECLARE_MSG_HANDLER2(onSetParameter)
    //DECLARE_MSG_HANDLER(onStop)
    //DECLARE_MSG_HANDLER(onStart)
    //DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoSourceSurface)
};

} // YUNOS_MM

#endif // __video_source_surface
