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

#include <inttypes.h>



#include <multimedia/component.h>
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>
#include "video_source_camera.h"

// Note: This file should not compile in ubuntu platform

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    #include <OMX_IVCommon.h>
#endif

#include "video_source_base_impl.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("VideoSourceCamera");

#define CAMERA_ID 0

#undef DEBUG_PERF

#ifdef DEBUG_PERF
#define TIME_TAG(str) printTimeMs(str)
void printTimeMs(const char *str) {

    uint64_t nowMs;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    nowMs = t.tv_sec * 1000LL + t.tv_nsec / 1000000LL;

    INFO("%s at %" PRId64 "ms\n", str, (nowMs % 10000));
}
#else
#define TIME_TAG(str)
#endif

static const char * COMPONENT_NAME = "VideoSourceCamera";

BEGIN_MSG_LOOP(VideoSourceCamera)
    MSG_ITEM(CAMERA_MSG_prepare, onPrepare)
    MSG_ITEM(CAMERA_MSG_stop, onStop)
END_MSG_LOOP()

VideoSourceCamera::VideoSourceCamera() : VideoSourceBase(COMPONENT_NAME, true, true)
{
    ENTER();

    mSendEmptyEosBuffer = mm_check_env_str("mm.camera.empty.eos.buffer","MM_CAMERA_EMPTY_EOS_BUFFER","1", true);
    mSentEosBufferCount = 0;
    mComponentName = COMPONENT_NAME;
    EXIT();
}

VideoSourceCamera::~VideoSourceCamera() {
    ENTER();

    EXIT();
}

void VideoSourceCamera::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO add specific prepare

    VideoSourceBase<CameraSourceGenerator>::onPrepare(param1, param2, rspId);

    EXIT();
}

//////////////////////////////////////////////////////////////////////////////////
//CameraSourceGenerator
CameraSourceGenerator::CameraSourceGenerator(SourceComponent *comp) :
        mVideoWidth(0)
      , mVideoHeight(0)
      , mFrameCount(0)
      , mFramePts(0)
      , mFrameRate(30.0f)
      , mStartMediaTime(-1ll)
      , mDuration(0)
      , mFirstFrameTimeUs(-1)
      , mCamera(NULL)
      , mRecordingProxy(NULL)
      , mCameraId(-1)
      , mIsMetaDataStoredInVideoBuffers(true)
      , mIsContinue(true)
      , mCaptureFpsEnable(false)
      , mCaptureFps(0)
      , mCaptureFrameDurationUs(0)
      , mStartTimeUs(-1ll)
{
    ENTER();

    mComponent = DYNAMIC_CAST<VideoSourceCamera *>(comp);

    EXIT();
}

CameraSourceGenerator::~CameraSourceGenerator() {
    ENTER();

    MMAutoLock locker(mFrameLock);
    mIsContinue = false;

    EXIT();
}

mm_status_t CameraSourceGenerator::configure(SourceType type,  const char *fileName,
                                          int width, int height,
                                          float fps, uint32_t fourcc) {
    ENTER();
    //mm_status_t status = MM_ERROR_SUCCESS;

    ASSERT(fourcc == 'NV12');

    MMAutoLock locker(mFrameLock);


    // Video frame rate default is 30.0fps
    mFrameRate = fps;
    mVideoWidth = width;
    mVideoHeight = height;

    INFO("frame generator config: %dx%d %0.2f fps",
         mVideoWidth, mVideoHeight, mFrameRate);

    Size videoSize(mVideoWidth, mVideoHeight);

    int32_t cameraId = -1;
    if (mCamera != NULL && mRecordingProxy != NULL) {
        // mFrameRate = 15.0f; //only support 15.0 and 30.0 for front camera

        if (!mCaptureFpsEnable) {
            mCameraSource.reset(VideoCaptureSource::create(mCamera, mRecordingProxy, NULL, &videoSize, mFrameRate, true));
        } else {
            mCameraSource.reset(VideoCaptureSourceTimeLapse::create(mCamera, mRecordingProxy, NULL, &videoSize,
                mFrameRate, mCaptureFrameDurationUs, true));
        }
    } else {
        cameraId = (type == CAMERA_BACK) ? 0 : 1;
        // mFrameRate = (type == CAMERA_BACK) ? mFrameRate : 15.0f; //only support 15.0 and 30.0 for front camera
        if (mSurface.empty()) {
            ERROR("mSurface is null\n");
            EXIT_AND_RETURN1(MM_ERROR_INVALID_PARAM);
        }
        mCameraSource.reset(VideoCaptureSource::create(cameraId, (void*)mSurface.c_str(), &videoSize, mFrameRate, true));
    }

    if (mCameraSource == NULL) {
        ERROR("camera source create failed\n");
        EXIT_AND_RETURN1(MM_ERROR_SOURCE);
    }

#if 0
    const Properties &values = mCameraSource->getFormat();
    colorFormat = getColorFormat(values.get(Properties::KEY_VIDEO_FRAME_FORMAT).c_str());
    if (colorFormat == -1) {
        EXIT_AND_RETURN1(MM_ERROR_INVALID_PARAM);
    }

    INFO("get color format %x from camera", colorFormat);
    mComponent->mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat);
#endif

    mIsMetaDataStoredInVideoBuffers =
        mCameraSource->isMetaDataStoredInVideoBuffers();

#ifdef __MM_YUNOS_YUNHAL_BUILD__
    int32_t bufferType = mIsMetaDataStoredInVideoBuffers ? MediaBuffer::MBT_GraphicBufferHandle : MediaBuffer::MBT_RawVideo;
    mComponent->mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, bufferType);
#else
    mComponent->mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, MediaBuffer::MBT_CameraSourceMetaData);
#endif


    MediaMetaSP meta = mCameraSource->getMetadata();
    int32_t color = 0;
    if (meta->getInt32(MEDIA_ATTR_COLOR_FOURCC, color) && color != 0) {
        mComponent->mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FOURCC, color);
        DEBUG_FOURCC(NULL, color);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t CameraSourceGenerator::start() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    mIsContinue = true;

    mStartMediaTime = mComponent->getTimeUs();
    mDuration = (1000.0f / mFrameRate) * 1000;

    int ret = mCameraSource->start(mStartTimeUs);
    if (ret) {
        ERROR("camera source start fail!");
        EXIT_AND_RETURN(MM_ERROR_SOURCE);
    }

    INFO("frame source starts, fps(%0.2f)", mFrameRate);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t CameraSourceGenerator::stop() {
    ENTER();
    {
        MMAutoLock locker(mFrameLock);
        mIsContinue = false;
    }

    int ret = mCameraSource->stop();
    if (ret) {
        ERROR("camera source stop fail!");
        EXIT_AND_RETURN(MM_ERROR_SOURCE);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t CameraSourceGenerator::flush() {
    ENTER();


    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t CameraSourceGenerator::reset() {
    ENTER();
    {
        MMAutoLock locker(mFrameLock);

        mFrameCount = 0;
        mFramePts = 0;

        mIsContinue = false;
    }
    mCameraSource.reset();


    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t CameraSourceGenerator::setParameters(const MediaMetaSP & meta) {
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    void *surface = NULL;
    if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE, surface)) {
        DEBUG("surface %p\n", surface);
        mSurface = static_cast<char*>(surface);
    }

    void *p = NULL;
    if (meta->getPointer(MEDIA_ATTR_CAMERA_OBJECT, p)) {
        mCamera = static_cast<VideoCapture*>(p);

        DEBUG("camera %p\n", mCamera);
    }
    if (meta->getPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, p)) {
         mRecordingProxy = static_cast<RecordingProxy*>(p);
         DEBUG("mRecordingProxy %p\n", mRecordingProxy);
    }

    int32_t timeLapseEnable = 0;
    if (meta->getInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, timeLapseEnable)) {
        mCaptureFpsEnable = timeLapseEnable;
        DEBUG("mCaptureFpsEnable: %d", mCaptureFpsEnable);
    }

    float fps = 0;
    if (meta->getFloat(MEDIA_ATTR_TIME_LAPSE_FPS, fps)) {
        mCaptureFps = fps;
        if (mCaptureFps > 0.0001f) {
            mCaptureFrameDurationUs = 1E6 / mCaptureFps;
        } else {
            status = MM_ERROR_INVALID_PARAM;
            mCaptureFrameDurationUs = mCaptureFps = 0;
            ERROR("invalid capture fps %0.3f", mCaptureFps);
        }
        DEBUG("mCaptureFps: %0.3f, mCaptureFrameDurationUs %0.3f",
            mCaptureFps, mCaptureFrameDurationUs);
    }

    int64_t startTime = 0;
    if (meta->getInt64(MEDIA_ATTR_START_TIME, startTime)) {
        mStartTimeUs = startTime;
        DEBUG("mStartTimeUs %" PRId64 "", mStartTimeUs);
    }

    EXIT_AND_RETURN(status);
}

MediaBufferSP CameraSourceGenerator::getMediaBuffer() {
    ENTER();

    uint8_t *buf;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;
    MediaMetaSP meta;

    TIME_TAG("\ngetBuffer start");

    {
        MMAutoLock lock(mFrameLock);

        if (!mIsContinue)
            return mediaBuf;

        if (mComponent->mStreamEOS && mComponent->mSendEmptyEosBuffer) {
            WARNING("Exiting, mIsContinue %d, mStreamEOS %d",
                mIsContinue, mComponent->mStreamEOS);
            mediaBuf = MediaBuffer::createMediaBuffer(
                                MediaBuffer::MBT_GraphicBufferHandle);

            mediaBuf->setSize(0);
            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
            DEBUG("VideoSourceCamera send empty EOS buffer: %d", ++mComponent->mSentEosBufferCount);
            return mediaBuf;
        }
    }

    int ret = 0;

    ret = mCameraSource->read(mediaBuf);
    if (ret != MM_ERROR_SUCCESS) {
        ERROR("camera source read failed, ret = %d, return NULL MediaBuffer", ret);
        return mediaBuf;
    }
    if (!mediaBuf || !mediaBuf->getBufferInfo((uintptr_t *)&buf, &bufOffset, &bufStride, 1) ||
        buf == NULL || bufStride <= 0) {
        mediaBuf.reset();
        DEBUG("invalid buffer\n");
        return mediaBuf;
    }

#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    meta = mediaBuf->getMediaMeta();
    if (!meta) {
        ERROR("no meta data");
        return mediaBuf;
    }
    int32_t index = -1;
    bool ret_ = meta->getInt32("index", index);
    ASSERT(ret_);
#endif

    // send EOS flag with empty buffer
    if (mComponent->mStreamEOS && !mComponent->mSendEmptyEosBuffer) {
        INFO("send EOS buffer with valid camera data: %d", ++mComponent->mSentEosBufferCount);
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    }

    #ifdef _MM_YUNOS_BUILD__
    VERBOSE("getMediaBuffer: type %d, address %p, pts %0.3f, mFrameCount: %d\n",
        *(int32_t*)buf, (void*)(*(int32_t*)(buf+4)), mediaBuf->dts()/1000000.0f, mFrameCount);
    #elif defined(__MM_YUNOS_YUNHAL_BUILD__)
    DEBUG("getMediaBuffer: index %d, target %p, pts %0.3f s, mFrameCount: %d\n",
       index, buf, mediaBuf->dts()/1000000.0f, mFrameCount);
    #endif
    mFrameCount++;

    MM_CALCULATE_AVG_FPS("CameraSource");
    return mediaBuf;
}

} // YUNOS_MM

extern "C" {

YUNOS_MM::Component *createComponent(const char* mime, bool encode) {
    YUNOS_MM::Component *comp;

    comp = new YUNOS_MM::VideoSourceCamera();

    INFO("VideoSourceCamera Component is created");

    return comp;
}

void releaseComponent(YUNOS_MM::Component *component) {
    INFO("VideoSourceCamera Component is deleted");
    delete component;
}

}
