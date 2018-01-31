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

#ifndef __video_source_H
#define __video_source_H

#include "video_source_base.h"
#include "mm_surface_compat.h"

#define  FRAMEGENERATOR_DUMP_FILE     0
namespace YUNOS_MM {

class VideoSource;

class FrameGenerator {
public:
    FrameGenerator(SourceComponent *comp);
    virtual ~FrameGenerator();
    virtual mm_status_t configure(SourceType type,  const char *fileName,
                                  int width, int height,
                                  float fps, uint32_t format = VIDEO_SOURCE_DEFAULT_CFORMAT);
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t flush();
    virtual mm_status_t reset();
#if defined (__MM_YUNOS_CNTRHAL_BUILD__)
    void signalBufferReturned(void*, int64_t);
#endif
    MediaBufferSP getMediaBuffer();
    mm_status_t setParameters(const MediaMetaSP & meta) {return MM_ERROR_SUCCESS;}

private:
    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

#define MAX_BUFFER 32
#define START_PTS 0
#define MAX_PTS 0xffffffff

private:
    /*
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
    */
    int32_t mTag;
    FILE *mYUVFile;
#if FRAMEGENERATOR_DUMP_FILE
    FILE *mDumpYUVFile;
#endif
    SourceType mType;
    std::string mUrl;
    /*
    int32_t mInputBufferCount;
    */
    int32_t mWidth;
    int32_t mHeight;
    float mFrameRate;
    uint32_t mFourcc;

    // deduce from width/height/format
    uint32_t mStrides[3];
    uint32_t mFrameSize;
    uint32_t mPlaneCount;
    uint32_t mPlaneSize[3];

    uint32_t mTargetFrameCount; // trigger EOS after # of frames

    /*
    BufferInfo mBufferInfo[MAX_BUFFER];
    */

    uint32_t mFrameNum;
    int64_t mFramePts;

    VideoSource *mComponent;

    Lock mFrameLock;
    Condition mFrameCondition;
};

class VideoSource : public VideoSourceBase <FrameGenerator> {

public:

    VideoSource();
    virtual ~VideoSource();

friend class FrameGenerator;

private:

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    //DECLARE_MSG_HANDLER2(onSetParameter)
    //DECLARE_MSG_HANDLER(onStop)
    //DECLARE_MSG_HANDLER(onStart)
    //DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoSource)
};

} // YUNOS_MM

#endif // __video_source
