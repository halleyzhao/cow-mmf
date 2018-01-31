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

#ifndef __YUNOS_VIDEO_CAPTURE_SOURCE_TIME_LAPSE_H_
#define __YUNOS_VIDEO_CAPTURE_SOURCE_TIME_LAPSE_H_

#include <stdint.h>
#include <string>
#include <list>

#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/media_buffer.h>
#include "video_capture_source.h"
#ifdef __MM_YUNOS_YUNHAL_BUILD__
#include "multimedia/mm_surface_compat.h"
#endif
#include "multimedia/mm_camera_compat.h"

namespace YunOSCameraNS {
    class VideoCaptureCallback;
    class VideoCapture;
    class VCMSHMem;
    class RecordingProxy;
    class Size;
}
using YunOSCameraNS::VideoCaptureCallback;
using YunOSCameraNS::VideoCapture;
using YunOSCameraNS::VCMSHMem;
using YunOSCameraNS::RecordingProxy;
using YunOSCameraNS::Size;

#if MM_USE_CAMERA_VERSION>=30
    namespace YunOSCameraNS {
        class VideoCaptureParam;
    }
    using YunOSCameraNS::VideoCaptureParam;
#else
    namespace YunOSCameraNS {
        class Properties;
    }
    using YunOSCameraNS::Properties;
#endif


namespace YUNOS_MM {

/**
 *  @breif Interface for VideoCapture V1.
 *  @since 4.0
 */
class VideoCaptureSourceListener;
class VideoCaptureSourceTimeLapse : public VideoCaptureSource {
public:
	virtual ~VideoCaptureSourceTimeLapse();

    static VideoCaptureSourceTimeLapse* create(VideoCapture *camera, RecordingProxy *recordingProxy,
                                       void *surface,
                                       Size *videoSize, int32_t frameRate,
                                       int64_t captureFrameDurationUs,
                                       bool storeMetaDataInVideoBuffers);
protected:
    virtual bool skipCurrentFrame(int64_t *timestampUs);

private:
    VideoCaptureSourceTimeLapse(int32_t cameraId, VideoCapture *camera, RecordingProxy *recordingProxy,
                 void *surface,
                 Size *videoSize, int32_t frameRate,
                 int64_t captureFrameDurationUs,
                 bool storeMetaDataInVideoBuffers);

private:

    // Time between two frames in final video (1/frameRate)
    int64_t mVideoFrameDurationUs;

    // Real timestamp of the last encoded time lapse frame
    int64_t mLastFrameRealTimestampUs;

    bool mSkipCurrentFrame;
    MM_DISALLOW_COPY(VideoCaptureSourceTimeLapse);
};


}

#endif //__YUNOS_VIDEO_CAPTURE_SOURCE_TIME_LAPSE_H_
