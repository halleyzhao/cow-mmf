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

#ifndef VIDEO_SOURCE_CAMERA_H_

#define VIDEO_SOURCE_CAMERA_H_
#include "stdint.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

#include <video_capture_source.h>
#include <video_capture_source_time_lapse.h>
#include "multimedia/mm_camera_compat.h"

#include "video_source_base.h"

namespace YUNOS_MM {
class VideoSourceCamera;

class CameraSourceGenerator {
public:
    CameraSourceGenerator(SourceComponent *comp);
    virtual ~CameraSourceGenerator();
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
    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

#define MAX_BUFFER 32
#define START_PTS 0
#define MAX_PTS 0xffffffff
#define MIN_PTS -100000000

private:
    int32_t mVideoWidth;
    int32_t mVideoHeight;

    uint32_t mFrameCount;
    int64_t mFramePts;
    float mFrameRate;

    int64_t mStartMediaTime;
    int32_t mDuration;

    VideoSourceCamera *mComponent;
    int64_t mFirstFrameTimeUs;

    std::string mSurface;
    MMSharedPtr<VideoCaptureSource> mCameraSource;
    #if MM_USE_CAMERA_VERSION>=30
        typedef VideoCaptureParam ImageParameters;
    #else
        typedef Properties ImageParameters;
    #endif
    VideoCapture *mCamera;
    RecordingProxy *mRecordingProxy;
    int32_t mCameraId;

    bool mIsMetaDataStoredInVideoBuffers;

    Lock mFrameLock;
    bool mIsContinue;

    bool mCaptureFpsEnable;
    float mCaptureFps;
    float mCaptureFrameDurationUs;
    int64_t mStartTimeUs;
};

class VideoSourceCamera : public VideoSourceBase <CameraSourceGenerator> {

public:

    VideoSourceCamera();
    virtual ~VideoSourceCamera();

friend class CameraSourceGenerator;

private:
    bool mSendEmptyEosBuffer; // send EOS buffer with empty data or not
    uint32_t mSentEosBufferCount; // debug use
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    MM_DISALLOW_COPY(VideoSourceCamera)
};

} // YUNOS_MM

#endif // VIDEO_SOURCE_CAMERA_H_

