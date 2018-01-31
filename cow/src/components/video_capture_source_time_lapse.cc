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
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>
#include <video_capture_source_time_lapse.h>

#if MM_USE_CAMERA_VERSION>=20
    #include <yunhal/VCMSHMem.h>
    #if MM_USE_CAMERA_VERSION>=30
        #include <yunhal/VideoCaptureParam.h>
    #else
        #include <yunhal/VideoCaptureProperties.h>
    #endif
    #include <yunhal/VideoCaptureDevice.h>
#else
    #include <videocapture/VCMSHMem.h>
    #include <videocapture/hal/VideoCaptureProperties.h>
    #include <videocapture/hal/VideoCaptureDevice.h>
#endif


// Note: This file only can be compiled in YUNOS_BUILD OR YUNOS_HOST_ONLY_BUILD platform

namespace YUNOS_MM {

using namespace YunOSCameraNS;

MM_LOG_DEFINE_MODULE_NAME("VideoCaptureSourceTimeLapse")


////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*static*/VideoCaptureSourceTimeLapse* VideoCaptureSourceTimeLapse::create(VideoCapture *camera,
                                                        RecordingProxy *recordingProxy,
                                                        void *surface,
                                                        Size *videoSize,
                                                        int32_t frameRate,
                                                        int64_t timeBetweenFrameCaptureUs,
                                                        bool storeMetaDataInVideoBuffers)
{
    VideoCaptureSourceTimeLapse* cameraSource = new VideoCaptureSourceTimeLapse(-1, camera, recordingProxy, surface, videoSize, frameRate, timeBetweenFrameCaptureUs, storeMetaDataInVideoBuffers);
    if (!cameraSource->initCheck(-1, camera, recordingProxy, surface)) {
        delete cameraSource;
        return nullptr;
    }
    return cameraSource;
}

VideoCaptureSourceTimeLapse::VideoCaptureSourceTimeLapse(int32_t cameraId,
                     VideoCapture *camera,
                     RecordingProxy *recordingProxy,
                     void *surface,
                     Size *videoSize, int32_t frameRate,
                     int64_t captureFrameDurationUs,
                     bool storeMetaDataInVideoBuffers)
                    : VideoCaptureSource(-1, camera, recordingProxy, surface, videoSize, frameRate, storeMetaDataInVideoBuffers)
                    , mLastFrameRealTimestampUs(0)
                    , mSkipCurrentFrame(false)
{
    if (frameRate > 0) {
        mVideoFrameDurationUs = 1E6 / frameRate;
    }

    mCaptureFrameDurationUs = captureFrameDurationUs;
    DEBUG("mVideoFrameDurationUs: %" PRId64 ", mCaptureFrameDurationUs: %" PRId64 "", mVideoFrameDurationUs, captureFrameDurationUs);
}


VideoCaptureSourceTimeLapse::~VideoCaptureSourceTimeLapse()
{
}

bool VideoCaptureSourceTimeLapse::skipCurrentFrame(int64_t *timestampUs) {
    if (mLastFrameRealTimestampUs == 0) {
        DEBUG("dataCallbackTimestamp timelapse: initial frame");
        mLastFrameRealTimestampUs = *timestampUs;
        return false;
    }

    // Keep decoder info and first key frame
    if (mNumFramesEncoded >= 1 &&
            *timestampUs + mCaptureFrameDurationUs / 2 <
            (mLastFrameRealTimestampUs + mCaptureFrameDurationUs) &&
            mCaptureFrameDurationUs > mVideoFrameDurationUs) {
        // Do not cast any frame in slow motion mode
        DEBUG("dataCallbackTimestamp timelapse: skipping intermediate frame");
        return true;
    } else {
        DEBUG("dataCallbackTimestamp timelapse: got timelapse frame");

        mLastFrameRealTimestampUs = *timestampUs;
        *timestampUs = mLastFrameTimestampUs + mVideoFrameDurationUs;
        return false;
    }
    return false;
}

}; // namespace YunOSCameraNS

