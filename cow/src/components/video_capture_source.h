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

#ifndef __YUNOS_VIDEO_CAPTURE_SOURCE_H_
#define __YUNOS_VIDEO_CAPTURE_SOURCE_H_

#include <stdint.h>
#include <string>
#include <list>
#include <map>

#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/media_buffer.h>
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "multimedia/mm_surface_compat.h"
#endif

// #define __SOURCE_BUFFER_DUMP__
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) && defined(__SOURCE_BUFFER_DUMP__)
#include <hardware/gralloc.h>
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
class VideoCaptureSource {
public:
	virtual ~VideoCaptureSource();

    //Client needs to delete object after used
    static VideoCaptureSource* create(int32_t videocaptureId, void *surface,
                                       Size *videoSize, int32_t frameRate,
                                       bool storeMetaDataInVideoBuffers);
    static VideoCaptureSource* create(VideoCapture *camera, RecordingProxy *recordingProxy,
                                       void *surface,
                                       Size *videoSize, int32_t frameRate,
                                       bool storeMetaDataInVideoBuffers);
    mm_status_t start(int64_t startTimeUs = 0);
    mm_status_t stop();
    mm_status_t read(MediaBufferSP &buffer);
    bool isMetaDataStoredInVideoBuffers() const;
    bool releaseEncodingBuffer(MediaBuffer* mediaBuffer);
    MediaMetaSP getMetadata();
protected:
    VideoCaptureSource(int32_t cameraId, VideoCapture *camera, RecordingProxy *recordingProxy,
                 void *surface,
                 Size *videoSize, int32_t frameRate,
                 bool storeMetaDataInVideoBuffers);

    // Returns true if need to skip the current frame.
    virtual bool skipCurrentFrame(int64_t *timestampUs) {return false;}
    virtual bool initCheck(int32_t cameraId, VideoCapture *camera, RecordingProxy *recordingProxy, void *surface);
    virtual void OnVideoTaken(VideoCapture* vc, int64_t timestampUs, VCMSHMem* data);

    int64_t mCaptureFrameDurationUs;
    int64_t mLastFrameTimestampUs;
    int32_t mNumFramesEncoded;

private:
    void reset();
    void releaseCamera();
    void checkColorFormat();
    void checkVideoSize(Size &size);
    void checkPreviewFrameRate(int32_t rate);
    void releaseReceivedBuffersInternal();
    void releaseRecordingFrame(VCMSHMem* data);
#if defined(__SOURCE_BUFFER_DUMP__)
    void dumpInputBuffer(unsigned long target);
#endif

private:
    friend class VideoCaptureSourceListener;
    int32_t mCameraId;
    VideoCapture *mCamera;
    MMSharedPtr<VideoCaptureCallback> mCameraListener;
    RecordingProxy *mRecordingProxy;
    std::string  mSurface;
    bool mIsHotCamera;
    bool mStoreMetaDataInVideoBuffers;
    bool mInitCheck;
    int32_t mFrameDuration; //ms

    int32_t mVideoHeight;
    int32_t mVideoWidth;
    int32_t mFrameRate;
    uint32_t mColorFormat; //fourcc

    int64_t mStartTimeUs;
    int64_t mFirstFrameTimeUs;
    int32_t mNumFramesReceived;
    bool mStarted;
    int32_t mNumFramesDropped;

    Condition mFrameAvailableCondition;
    Condition mFrameCompleteCondition;
    Lock mLock;
    std::list<MediaBufferSP> mFramesReceived;
    //Note:Do not keep strong reference of received frames, otherwise YUNOS_MM::releaseRecordingFrame won't be call
    std::list<MediaBuffer*> mFramesBeingEncoded;

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    std::vector<MMSharedPtr<MMNativeHandleT> > mInputBuffers;
#else
    std::vector<std::vector<uint8_t> > mInputBuffers;
#endif
    std::vector<uint32_t> mInputIndexes;
    std::map<int, uint8_t*> mInputBuffersMap; // for raw video
    std::list<uint8_t*> mInputBuffersStaging;


#if defined(__SOURCE_BUFFER_DUMP__)
    static DataDump rawDataDump;
    #if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
        yalloc_device_t* mYalloc = NULL;
    #else
        const hw_module_t *mModule = NULL;
        gralloc_module_t *mAllocMod = NULL;
        alloc_device_t  *mAllocDev = NULL;
    #endif
#endif

    bool mCameraApi2;
    bool mForceReturn;
#ifdef HAVE_EIS_AUDIO_DELAY
    bool mIsVideoDrop;
#endif

    bool mHasCallbackInfos = false;
    int mCbStreamType = -1;
    bool mCallbackFlagSet = false;

    MediaMetaSP mMeta;
    MM_DISALLOW_COPY(VideoCaptureSource);
};


}

#endif //__YUNOS_VIDEO_CAPTURE_SOURCE_H_
