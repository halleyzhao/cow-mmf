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

#ifndef __video_test_source_H
#define __video_test_source_H

#include "video_source_base.h"
#include "stdint.h"
#include "multimedia/mm_surface_compat.h"
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
#include <cutils/yalloc.h>
#endif

namespace YunOSMediaCodec {
class SurfaceWrapper;
}


namespace YUNOS_MM {

class VideoTestSource;
class AutoFrameGenerator {
public:
    AutoFrameGenerator(SourceComponent *comp);
    virtual ~AutoFrameGenerator();
    virtual mm_status_t configure(SourceType type,  const char *fileName,
                                  int width, int height,
                                  float fps, uint32_t fourcc = VIDEO_SOURCE_DEFAULT_CFORMAT);
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t flush();
    virtual mm_status_t reset();

    void signalBufferReturned(void*, int64_t);
    MediaBufferSP getMediaBuffer();
    mm_status_t setParameters(const MediaMetaSP & meta);

private:
    mm_status_t prepareInputBuffer();
    void fillSourceBuffer(MMNativeBuffer *anb);
    void *getAddressFromHandle(MMNativeBuffer *anb);

    static int loadYUVDataAutoGenerator(void* ptr, int format, int width, int height, int x_stride, int y_stride);
    bool loadYUVDataFromFile(void* pBuf, int format, int width, int height, int x_stride, int y_stride);

    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

#define MAX_BUFFER 32
#define START_PTS 0
#define MAX_PTS 0xffffffff

private:
    enum BufferState {
        OWNED_BY_US,
        OWNED_BY_COMPONENT,
        OWNED_BY_NATIVEWINDOW,
    };

    struct BufferInfo {
        MMNativeBuffer *anb;
        BufferState state;
        int64_t pts;
    };

    FILE *mInputYUVFile;
    int32_t mInputBufferCount;
    uint32_t mMaxFrameCount;
    WindowSurface *mNativeWindow;
    YunOSMediaCodec::SurfaceWrapper *mSurfaceWrapper;
    int mSourceBufferType = 0;

    int32_t mWidth;
    int32_t mHeight;
    uint32_t mStrideX = 0;
    uint32_t mStrideY = 0;
    BufferInfo mBufferInfo[MAX_BUFFER];

    uint32_t mFrameNum;
    int64_t mFramePts;
    float mDefaultFrameRate;

    int64_t mStartTimeUs;
    int64_t mStartMediaTime;
    int64_t mNextMediaTime;
    int32_t mDefaultDuration;
    bool mIsRawData = 0;

    VideoTestSource *mComponent;

    Lock mFrameLock;
    Condition mFrameCondition;
    bool mIsContinue;

    bool mIsFileSource;
    FILE *mFile;
    uint32_t mSourceColorFormat;
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    yalloc_device_t* mYalloc;
#endif
    std::list<int32_t> mQueuedIndex;
    bool mSlowMotionEnable;
    float mFrameCaptureRate;
    int32_t mCaptureFrameDurationUs;
};

class VideoTestSource : public VideoSourceBase <AutoFrameGenerator> {

public:

    VideoTestSource();
    virtual ~VideoTestSource();

friend class AutoFrameGenerator;

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    //DECLARE_MSG_HANDLER2(onSetParameter)
    //DECLARE_MSG_HANDLER(onStop)
    //DECLARE_MSG_HANDLER(onStart)
    //DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoTestSource)
};

} // YUNOS_MM

#endif // __video_test_source
