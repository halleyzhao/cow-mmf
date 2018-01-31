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
#include <algorithm>
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>
#include <video_capture_source.h>

#if defined(YUNOS_BOARD_intel)
#include <yalloc_drm.h>
#endif

// Note: This file only can be compiled in YUNOS_BUILD OR YUNOS_HOST_ONLY_BUILD platform

namespace YUNOS_MM {

using namespace YunOSCameraNS;

#if defined(__SOURCE_BUFFER_DUMP__)
DataDump VideoCaptureSource::rawDataDump("/tmp/raw.yuv");
#endif

MM_LOG_DEFINE_MODULE_NAME("VideoCaptureSource")

static const int64_t CAMERA_SOURCE_TIMEOUT = 3000000LL;
#define ENTER() DEBUG(">>>\n")
#define EXIT() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)


class VideoCaptureSourceListener : public VideoCaptureCallback {
public:
    VideoCaptureSourceListener(VideoCaptureSource *source);
    virtual ~VideoCaptureSourceListener() {}
    virtual void OnAutoFocus(VideoCapture* vc, bool success) {}
    virtual void OnAutoFocusMoving(VideoCapture* vc, bool start) {}
    virtual void OnShutter() {}
    virtual void OnZoomChange(VideoCapture* vc, int zoomValue, bool stopped) {}
    virtual bool OnFaceDetection(VideoCapture* vc, FaceInfo* faces, size_t num) {return false;}
    virtual void OnError(VideoCapture* vc, int error) {}
    virtual bool OnPreviewTaken(VideoCapture* vc, int64_t timestamp, VCMSHMem* data) {return false;}
    virtual bool OnPreviewTaken(VideoCapture* vc, char* data, size_t size){return false;}
    virtual bool OnCaptureTaken(VideoCapture* vc, char* data, size_t size){return false;}
    virtual bool OnVideoTaken(VideoCapture* vc, int64_t timestamp, VCMSHMem* data);
#if MM_USE_CAMERA_VERSION>=20
    virtual bool OnPreviewTaken(VideoCapture* vc, StreamData* data, ImageParam* param){return false;}
    virtual bool OnCaptureTaken(VideoCapture* vc, StreamData* data, ImageParam* param){return false;}
    virtual bool OnReprocessTaken(VideoCapture* vc, StreamData* data, ImageParam* param){return false;}
    virtual bool OnVideoTaken(VideoCapture* vc, StreamData* data, ImageParam* param);
#endif
private:
    VideoCaptureSource* mSource;
    MM_DISALLOW_COPY(VideoCaptureSourceListener);
};

VideoCaptureSourceListener::VideoCaptureSourceListener(VideoCaptureSource* source)
    : mSource(source) {
}

bool VideoCaptureSourceListener::OnVideoTaken(
        VideoCapture* vc, int64_t timestamp, VCMSHMem* data) {

    if (mSource) {
        mSource->OnVideoTaken(vc, timestamp/1000ll, data);
    } else {
        WARNING("source is NULL");
    }

    return true;
}

#if MM_USE_CAMERA_VERSION>=20
bool VideoCaptureSourceListener::OnVideoTaken(VideoCapture* vc, StreamData* data, ImageParam* param) {

    if (mSource) {
        ASSERT(data);
        int64_t timestamp = data->timestamp;
        if (!mSource->mCallbackFlagSet) { //get mHasCallbackInfos and mCbStreamType once
            mSource->mHasCallbackInfos = data->info.hasCallbackInfos;
            mSource->mCbStreamType = (int)data->memType;
            mSource->mCallbackFlagSet = true;
            DEBUG("mCallbackFlagSet %d, mCbStreamType %d", mSource->mHasCallbackInfos, mSource->mCbStreamType);
        }
        mSource->OnVideoTaken(vc, timestamp/1000ll, data->mem);
    } else {
        WARNING("source is NULL");
    }

    return true;
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/VideoCaptureSource* VideoCaptureSource::create(int32_t videocaptureId,
                                                        void *surface,
                                                        Size *videoSize,
                                                        int32_t frameRate,
                                                        bool storeMetaDataInVideoBuffers)
{
    VideoCaptureSource* cameraSource = new VideoCaptureSource(videocaptureId, nullptr, nullptr, surface, videoSize, frameRate, storeMetaDataInVideoBuffers);
    if (!cameraSource->initCheck(videocaptureId, nullptr, nullptr, surface)) {
        delete cameraSource;
        return nullptr;
    }
    return cameraSource;
}

/*static*/VideoCaptureSource* VideoCaptureSource::create(VideoCapture *camera,
                                                        RecordingProxy *recordingProxy,
                                                        void *surface,
                                                        Size *videoSize,
                                                        int32_t frameRate,
                                                        bool storeMetaDataInVideoBuffers)
{
    VideoCaptureSource* cameraSource = new VideoCaptureSource(-1, camera, recordingProxy, surface, videoSize, frameRate, storeMetaDataInVideoBuffers);
    if (!cameraSource->initCheck(-1, camera, recordingProxy, surface)) {
        delete cameraSource;
        return nullptr;
    }
    return cameraSource;
}

VideoCaptureSource::VideoCaptureSource(int32_t cameraId,
                     VideoCapture *camera,
                     RecordingProxy *recordingProxy,
                     void *surface,
                     Size *videoSize, int32_t frameRate,
                     bool storeMetaDataInVideoBuffers):
                      mCaptureFrameDurationUs(0)
                    , mLastFrameTimestampUs(0)
                    , mNumFramesEncoded(0)
                    , mCameraId(cameraId)
                    , mCamera(nullptr)
                    , mRecordingProxy(nullptr)
                    , mIsHotCamera(false)
                    , mStoreMetaDataInVideoBuffers(storeMetaDataInVideoBuffers)
                    , mInitCheck(true)
                    , mFrameDuration(30)
                    , mVideoHeight(-1)
                    , mVideoWidth(-1)
                    , mFrameRate(frameRate)
                    , mColorFormat(-1)
                    , mStartTimeUs(-1ll)
                    , mFirstFrameTimeUs(-1ll)
                    , mNumFramesReceived(0)
                    , mStarted(false)
                    , mNumFramesDropped(0)
                    , mFrameAvailableCondition(mLock)
                    , mFrameCompleteCondition(mLock)
                    , mCameraApi2(true)
                    , mForceReturn(false)
                #ifdef HAVE_EIS_AUDIO_DELAY
                    , mIsVideoDrop(false)
                #endif
{
    if (videoSize) {
        mVideoWidth = videoSize->width;
        mVideoHeight = videoSize->height;
    }
    mFrameDuration = (mFrameRate != 0) ? (1000 / mFrameRate) : mFrameDuration;
    //initCheck(cameraId, camera, recordingProxy, surface);
#ifdef __USING_CAMERA_API2__
    mCameraApi2 = mm_check_env_str("mm.camera.api2.enable", "MM_CAMERA_API2_ENABLE", "1", true);
#else
    mCameraApi2 = false;
#endif
    DEBUG("mCameraApi2 %d", mCameraApi2);

    mForceReturn = mm_check_env_str("mm.recorder.force.return", "MM_RECORDER_FORCE_RETURN", "1", false);
    DEBUG("mForceReturn %d", mForceReturn);

#if defined(__SOURCE_BUFFER_DUMP__)
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    if (mModule == NULL) {
        int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mModule);
        ASSERT(err == 0);
        ASSERT(mModule);
        mAllocMod = (gralloc_module_t*)mModule;
        ASSERT(mAllocMod);
        gralloc_open(mModule, &mAllocDev);
        ASSERT(mAllocDev);
    }
#endif

#ifdef __MM_YUNOS_YUNHAL_BUILD__
#if defined(__USING_YUNOS_MODULE_LOAD_FW__)
    VendorModule* module =
                (VendorModule*) LOAD_VENDOR_MODULE(YALLOC_VENDOR_MODULE_ID);
    yalloc_open(module,&mYalloc);
#else
    int ret = yalloc_open(&mYalloc);
#endif
#endif

#endif //__SOURCE_BUFFER_DUMP__

#ifdef __USING_USB_CAMERA__
    mStoreMetaDataInVideoBuffers = false; // using raw data
#endif
}


MediaMetaSP VideoCaptureSource::getMetadata()
{
    if(!mMeta) {
        mMeta = MediaMeta::create();
        mMeta->setInt32(MEDIA_ATTR_COLOR_FOURCC, mColorFormat);
    }

    return mMeta;

}

bool VideoCaptureSource::initCheck(int32_t cameraId, VideoCapture *camera,
                                    RecordingProxy *recordingProxy, void *surface)
{
    int32_t numOfCamera = VideoCapture::GetNumberOfVideoCapture();
    if ((cameraId < numOfCamera && cameraId >= 0) &&
        camera != nullptr) {
        mInitCheck = false;
        ERROR("invalid param, cameraId %d, camera %p", cameraId, camera);
        return false;
    }

    if ((cameraId >= numOfCamera || cameraId < 0) &&
        camera == nullptr) {
        mInitCheck = false;
        ERROR("invalid param, cameraId %d, camera %p", cameraId, camera);
        return false;
    }

    if (surface == nullptr && camera == nullptr) {
        ERROR("invalid surface");
        return false;
    }
    if (surface) {
        mSurface = (char*)surface;
        DEBUG("mSurface %s", surface);
    }
    mCameraListener.reset(new VideoCaptureSourceListener(this));

    if (camera != nullptr) {
        mCamera = camera;
        mIsHotCamera = true;
        mRecordingProxy = recordingProxy;
        DEBUG("camera %p, recording %p\n", mCamera, mRecordingProxy);
    } else {
        mCamera = VideoCapture::Create(cameraId);
        if (mCamera == 0) return false;
        mIsHotCamera  = false;
        mCamera->SetVideoCaptureCallback(mCameraListener.get());
    }

    DEBUG("hotCamera %d, mCameraListener %p", mIsHotCamera, mCameraListener.get());

    Size size(mVideoWidth, mVideoHeight);
    checkVideoSize(size);
    // Unnecessary to check video frame rate
    // checkPreviewFrameRate(mFrameRate);
    checkColorFormat();
    if (!mSurface.empty()){
        if (!mCameraApi2) {
            if (!mCamera->StartStream(STREAM_PREVIEW, nullptr, mSurface.c_str())) {
                ERROR("start preview failed\n");
                return MM_ERROR_UNKNOWN;
            }
        }
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
        else {

        }
#endif
    }


    return true;
}


VideoCaptureSource::~VideoCaptureSource()
{
    if (mStarted) {
        DEBUG("stop after source is started");
        stop();
    }

    reset();

    if (!mIsHotCamera) {
        delete mCamera;
        mCamera = nullptr;
    }

#if defined(__SOURCE_BUFFER_DUMP__)
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    int ret = yalloc_close(mYalloc);
    MMASSERT(ret == 0);
#else
    if (mAllocDev != NULL) {
        gralloc_close(mAllocDev);
        mAllocDev = NULL;
    }
#endif
#endif
}

mm_status_t VideoCaptureSource::start(int64_t startTimeUs)
{
    ENTER();
    if (!mInitCheck) {
        ERROR("error occur");
        return MM_ERROR_UNKNOWN;
    }
    if (mStarted) {
        DEBUG("already started");
        return MM_ERROR_SUCCESS;
    }

#ifdef HAVE_EIS_AUDIO_DELAY
    mIsVideoDrop = mm_check_env_str("persist.camera.morphoeis.en", "PERSIST_CAMERA_MORPHOEIS_EN", "1", false);
#endif
    mStartTimeUs = startTimeUs;
    DEBUG("mStartTimeUs %lld", mStartTimeUs);

    mm_status_t err = MM_ERROR_SUCCESS;

    if (mIsHotCamera && mRecordingProxy) {
        mRecordingProxy->setRecordingCallback(mCameraListener.get());
    }

    mCamera->SetRecordingFlag(true, true);

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    if (mCameraApi2) {
        if (mCamera->StartCustomStreamWithImageParam(STREAM_RECORD, nullptr, nullptr, true, true)) {
            ERROR("Failed to start recording");
            // return MM_ERROR_UNKNOWN;
        }
    } else
#endif
    {
        if (!mCamera->StartStream(STREAM_RECORD, nullptr, nullptr)) {
            ERROR("Failed to start recording");
            return MM_ERROR_UNKNOWN;
        }
    }

    mStarted = true;
    EXIT_AND_RETURN(err);
}

mm_status_t VideoCaptureSource::read(MediaBufferSP &buffer)
{
    VERBOSE("read");

    {
        MMAutoLock autoLock(mLock);
        while (mStarted && mFramesReceived.empty()) {

#ifdef __TABLET_BOARD_INTEL__
            int64_t waitTimeUs = 2 * mFrameDuration * 1000;
#else
            int64_t waitTimeUs = 2 * mFrameDuration * 1000 + CAMERA_SOURCE_TIMEOUT;
#endif
            int ret = mFrameAvailableCondition.timedWait(waitTimeUs);
            if (ret) {
                WARNING("wait for frame %d(%s)\n", ret, strerror(ret));
                return MM_ERROR_AGAIN;
            }
        }
        if (!mStarted) {
            WARNING("VideoCaptureSource is stopped");
            return MM_ERROR_SUCCESS;
        }

        buffer = *mFramesReceived.begin();
        MediaMetaSP meta = buffer->getMediaMeta();
        if (!meta) {
            ERROR("no meta data");
            return MM_ERROR_INVALID_PARAM;
        }

        meta->setInt32("encoding", 1);
        mFramesReceived.erase(mFramesReceived.begin());
        mFramesBeingEncoded.push_back(buffer.get());
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoCaptureSource::stop()
{
    ENTER();

    {
        MMAutoLock autoLock(mLock);
        if (!mStarted) {
            DEBUG("source is stopped already\n");
            EXIT_AND_RETURN(MM_ERROR_SUCCESS);
        }
        mStarted = false;
        mFrameAvailableCondition.signal();

        // Discard frames not being encode
        releaseReceivedBuffersInternal();

        DEBUG("release %d encoding frames\n", mFramesBeingEncoded.size());
        // Wait all the being encoding frames return
        while (!mFramesBeingEncoded.empty()) {
            int ret = mFrameCompleteCondition.timedWait(CAMERA_SOURCE_TIMEOUT);
            if (ret) {
                WARNING("wait for frame , error: %d(%s)\n", ret, strerror(ret));
            }
        }

        mCamera->StopStream(STREAM_RECORD);

        if (mNumFramesReceived != mNumFramesEncoded + mNumFramesDropped) {
            WARNING("mNumFramesReceived %d, mNumFramesEncoded %d, mNumFramesDropped %d\n",
                mNumFramesReceived, mNumFramesEncoded, mNumFramesDropped);
        }
    }


    if (mRecordingProxy) {
        mRecordingProxy->setRecordingCallback(nullptr);
    }

    mCallbackFlagSet = false;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

bool VideoCaptureSource::isMetaDataStoredInVideoBuffers() const
{
    return mStoreMetaDataInVideoBuffers;
}

void VideoCaptureSource::reset()
{
    ENTER();

    if (!mIsHotCamera) {
        VERBOSE("Camera was cold when we started, stopping preview");
        mCamera->StopStream(STREAM_PREVIEW);
        mCamera->SetVideoCaptureCallback(nullptr);
        delete mCamera;
        mCamera = nullptr;
    }

    EXIT();
}

void VideoCaptureSource::checkColorFormat()
{
    ImageParam *param = mCamera->GetStreamImageParam(stream_video_capture_type_e::STREAM_RECORD);
    if (param) {
        uint32_t format = param->getVideoFrameFormat();
        switch (format ) {
            case FMT_PREF(RGBA_8888):
                mColorFormat = 'RGBX';
                break;
            case FMT_PREF(RGB_565):
                mColorFormat = 'RGBX'; // todo: check
                break;
            case FMT_PREF(YV12):
                mColorFormat = 'YV12';
                break;
            case FMT_PREF(YCbCr_422_SP):
                mColorFormat = 'NV16';
                break;
            case FMT_PREF(YCrCb_420_SP):
                mColorFormat = 'NV21';
                break;
            case FMT_PREF(YCbCr_422_I):
                mColorFormat = 'YUY2';
                break;
        }
        INFO("mColorFormat 0x%0x", mColorFormat);
    } else {
        WARNING("invalid param");
    }
}

void VideoCaptureSource::checkVideoSize(Size &size)
{
    std::vector<Size> sizes;
    mCamera->GetStreamSizesSupported(STREAM_RECORD, sizes);
    DEBUG("support sizes: ");
    for (uint32_t i = 0; i < sizes.size(); i++) {
        DEBUG("%dx%d ", sizes[i].width, sizes[i].height);
    }

    bool found = false;
    if (size.width > 0 && size.height > 0) {
        for (uint32_t i = 0; i < sizes.size(); i++) {
            if (sizes[i].width == size.width && sizes[i].height == size.height) {
                found = true;
                DEBUG("set video size %dX%d\n", size.width, size.height);
                break;
            }
        }
    }

    if (!found) {
        Size dSize;
        mCamera->GetStreamSize(STREAM_RECORD, dSize);
        INFO("%dX%d is not supported, using default size %dX%d\n",
            size.width, size.height, dSize.width, dSize.height);
        mVideoWidth = dSize.width;
        mVideoHeight = dSize.height;
    }

    Size pSize;
    mCamera->GetStreamSize(STREAM_PREVIEW, pSize);
    DEBUG("default preview size %dX%d\n", pSize.width, pSize.height);

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    if (!mCameraApi2)
#endif
    {
        mCamera->SetStreamSize(STREAM_RECORD, mVideoWidth, mVideoHeight);
    }
}


void VideoCaptureSource::checkPreviewFrameRate(int32_t rate)
{
    std::vector<int> rates;
    mCamera->GetPreviewFrameRatesSupported(rates);

    bool found = false;
    if (rate > 0) {
        for (uint32_t i = 0; i < rates.size(); i++) {
            if (rates[i] == rate) {
                found = true;
                DEBUG("set preview frame rate %d\n", rate);
                break;
            }
        }
    }

    if (!found) {
        int32_t dRate = mCamera->GetPreviewFrameRate();
        INFO("%d is not supported, using default preview rate %d\n",
            rate, dRate);
        mFrameRate = dRate;
    }

    mFrameDuration = (mFrameRate != 0) ? (1000 / mFrameRate) : mFrameDuration;
    DEBUG("frame duration %d", mFrameDuration);
    mCamera->SetPreviewFrameRate(mFrameRate, true);
}

void VideoCaptureSource::releaseReceivedBuffersInternal()
{
    std::list<MediaBufferSP >::iterator it;
    DEBUG("release %d received frame", mFramesReceived.size());
    while (!mFramesReceived.empty()) {
        it = mFramesReceived.begin();
        mFramesReceived.erase(it);
        ++mNumFramesDropped;
    }
}

static bool releaseEncodingBuffer(MediaBuffer* mediaBuffer)
{
    void *ptr = nullptr;
    if (!mediaBuffer) {
        DEBUG();
        return false;
    }

#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    uint8_t *buf = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buf, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN(false);
    }

    MM_RELEASE_ARRAY(buf);
#endif

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    if (!meta || !meta->getPointer("this", ptr)) {
        DEBUG();
        return false;
    }

    VideoCaptureSource *source = static_cast<VideoCaptureSource*>(ptr);
    return source->releaseEncodingBuffer(mediaBuffer);

}

bool VideoCaptureSource::releaseEncodingBuffer(MediaBuffer* mediaBuffer)
{
    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    int32_t encoding = 0;
    void *ptr = nullptr;
    // Buffers are not read by client
    if (!meta->getInt32("encoding", encoding) || encoding == 0) {
        if (!meta->getPointer("SharedBuffer", ptr)) {
            return false;
        }

        releaseRecordingFrame((VCMSHMem*)ptr);
        return true;
    }


    // Buffer are read by client.
    // 1: remove from mFramesBeingEncoded vector and siganl to Stop Method
    // 2: release this frame to camera hal to fulfill source again
    MMAutoLock autoLock(mLock);
    bool found = false;
    for (std::list<MediaBuffer*>::iterator it = mFramesBeingEncoded.begin();
         it != mFramesBeingEncoded.end(); ++it) {
        if (*it ==  mediaBuffer) {
            mFramesBeingEncoded.erase(it);
            ++mNumFramesEncoded;

            MediaMetaSP meta = mediaBuffer->getMediaMeta();
            if (!meta || !meta->getPointer("SharedBuffer", ptr)) {
                return false;
            }

            releaseRecordingFrame((VCMSHMem*)ptr);
            mFrameCompleteCondition.signal();
            found = true;
            break;
        }
    }

    if (!found) {
        WARNING("buffer is not owned by us");
        return false;
    }

    return true;
}

void VideoCaptureSource::OnVideoTaken(VideoCapture* vc, int64_t timestampUs, VCMSHMem* shmem)
{
    if (shmem == nullptr || shmem->GetSize() < 16) {
        return;
    }

    DEBUG("dataCallbackTimestamp: timestamp %" PRId64 " ms, size %d", timestampUs/1000LL, shmem->GetSize());
    MMAutoLock autoLock(mLock);
    if (!mStarted) {
        INFO("Source is not started, release this frame");
        releaseRecordingFrame(shmem);
        return;
    }

#ifdef HAVE_EIS_AUDIO_DELAY
    static int32_t dropCount = 0;
    if (mIsVideoDrop && dropCount < 30) {
        // when EIS is enable, don't drop frame for timestamp
        DEBUG("drop frame in EIS mode, count %d", dropCount);

        dropCount++;
        releaseRecordingFrame(shmem);
        return;
    }
#endif

    if (mNumFramesReceived == 0 && timestampUs < mStartTimeUs) {
        DEBUG("drop video frame, %" PRId64 " < %" PRId64 "", timestampUs, mStartTimeUs);
        releaseRecordingFrame(shmem);
        return;
    }

    if (skipCurrentFrame(&timestampUs)) {
        releaseRecordingFrame(shmem);
        return;
    }

    if (mNumFramesReceived == 0) {
        mFirstFrameTimeUs = timestampUs;
    }
    ++mNumFramesReceived;
    mLastFrameTimestampUs = timestampUs;

    int bufferType = MediaBuffer::MBT_CameraSourceMetaData;
#ifdef __MM_YUNOS_YUNHAL_BUILD__
    bufferType = mHasCallbackInfos ? MediaBuffer::MBT_RawVideo : MediaBuffer::MBT_GraphicBufferHandle;
#endif
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MediaBufferType(bufferType));
    if (!buffer) {
        ERROR("no mem\n");
        return;
    }

    MediaMetaSP meta = buffer->getMediaMeta();
    if (!meta) {
        ERROR("no meta shmem");
        return;
    }

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    if (mHasCallbackInfos) { // no shmem header
        if (mCbStreamType == STREAM_MEM_TYPE_DATA) {
            uint8_t *pData = (uint8_t*)(shmem->GetBase());
            // int size = shmem->GetSize();
            mInputBuffersStaging.push_back(pData);
            #if 0
            auto it = mInputBuffersMap.find(shmem->GetKey());
            if (it != mInputBuffersMap.end()) {
                INFO("find no key %d", shmem->GetKey());
            }
            #endif
            uint8_t *buf = new uint8_t[sizeof(uintptr_t)];
            *(uintptr_t *)buf = (uintptr_t)pData;
            DEBUG("shmem key %d, buf %p, size %d\n", shmem->GetKey(), pData, shmem->GetSize());


            int32_t offset = 0;
            int32_t size = sizeof(uintptr_t);//set 4 bytes
            buffer->setBufferInfo((uintptr_t *)&buf, &offset, &size, 1);
            buffer->setSize((int64_t)size);

            // fake index, for videoSourceCamera will check the index
            meta->setInt32("index", shmem->GetKey());
        }
    } else { //no callback header
        int headerLength = sizeof(data_callback_header_t);
        data_callback_header_t* header = (data_callback_header_t*)(shmem->GetBase());

        uint8_t *src = (uint8_t*)shmem->GetBase() + headerLength;
        uint32_t i = 0;

        for (i = 0; i < mInputIndexes.size(); i++) {
            if ((int32_t)mInputIndexes[i] == header->index) {
                memcpy(mInputBuffers[i].get(), src, sizeof(MMNativeHandleT));
                break;
            }
        }
        if (i == mInputIndexes.size()) {
            DEBUG("get new slot buffer, i %d", i);

            mInputBuffers.push_back(MMSharedPtr<MMNativeHandleT>(new MMNativeHandleT));
            memcpy(mInputBuffers[i].get(), src, sizeof(MMNativeHandleT));
            mInputIndexes.push_back(header->index);
        }


        int32_t offset = 0;
        int32_t size = sizeof(MMBufferHandleT);//set 4 bytes
        uint8_t *buf = new uint8_t[size];
        MMBufferHandleT *handle = (MMBufferHandleT *)buf;
        *handle = mInputBuffers[i].get();
        buffer->setBufferInfo((uintptr_t *)&buf, &offset, &size, 1);
        buffer->setSize((int64_t)size);
        meta->setInt32("index", header->index);
    }
#else //__MM_YUNOS_CNTRHAL_BUILD__
    int headerLength = sizeof(data_callback_header_t);
    ASSERT(headerLength == 12);
    data_callback_header_t* header = (data_callback_header_t*)(shmem->GetBase());

    uint8_t *src = (uint8_t*)shmem->GetBase() + headerLength;
    MMBufferHandleT h = (MMBufferHandleT)src;
    int32_t nativeHandleSize = sizeof(MMNativeHandleT) + sizeof(int) * (h->numFds + h->numInts);

    uint32_t i = 0;
    for (i = 0; i < mInputIndexes.size(); i++) {
        if ((int32_t)mInputIndexes[i] == header->index) {
            memcpy(&(mInputBuffers[i][0]), h, nativeHandleSize);
            break;
        }
    }
    if (i == mInputIndexes.size()) {
        DEBUG("get new slot buffer, i %d", i);
        std::vector<uint8_t> v;
        v.resize(nativeHandleSize);
        memcpy(&(v[0]), h, nativeHandleSize);

        mInputBuffers.push_back(v);
        mInputIndexes.push_back(header->index);
    }


    int32_t offset = 0;
    int32_t size = 4 + sizeof(MMBufferHandleT);
    uint8_t *buf = new uint8_t[size];
    memcpy(buf, &(header->type), 4);
    MMBufferHandleT *handle = (MMBufferHandleT *)(buf + 4);
    *handle = (MMBufferHandleT)(&mInputBuffers[i][0]);
    buffer->setBufferInfo((uintptr_t *)&buf, &offset, &size, 1);
    buffer->setSize((int64_t)size);
#endif //defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)

#if defined(__SOURCE_BUFFER_DUMP__)
    dumpInputBuffer((unsigned long)*handle);
#endif
    buffer->addReleaseBufferFunc(YUNOS_MM::releaseEncodingBuffer);
    meta->setPointer("SharedBuffer", shmem);
    meta->setPointer("this", this);

    int64_t timeUs = (timestampUs - mFirstFrameTimeUs);
    buffer->setPts(timeUs);
    buffer->setDts(timeUs);

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
  #ifdef __USING_USB_CAMERA__
  #else
    DEBUG("OnVideoTaken: index %d, native_target_t: %p, target->handle: %p, fd.num %d, fd.shmem %d, current time stamp: %0.3f\n",
        header->index,
        *handle,
        (*handle)->handle,
        (*handle)->fds.num,
        (*handle)->fds.shmem[0],
        timeUs/1000000.0f);
  #endif
#else
    DEBUG("OnVideoTaken: index %d, type %d, address %p, fd.num %d, current time stamp: %0.3f\n",
        header->index,
        header->type,
        &(mInputBuffers[i][0]),
        (*handle)->numFds,
        timeUs/1000000.0f);
#endif

    // Debug for 120fps, slow motion
    if (mForceReturn) {
        return;
    }
    mFramesReceived.push_back(buffer);
    mFrameAvailableCondition.signal();
}

#if defined(__SOURCE_BUFFER_DUMP__)
void VideoCaptureSource::dumpInputBuffer(unsigned long target) {

    Size size;
    mCamera->GetStreamSize(STREAM_RECORD, size);
    int w = size.width;
    int h = size.height;
    DEBUG("width %d, height %d\n", size.width, size.height);

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    void *vaddr = NULL;
    static YunAllocator &allocator(YunAllocator::get());
    MMBufferHandleT handle = (MMBufferHandleT)target;

    allocator.authorizeBuffer(handle); //register first, otherwise map failed

    uint32_t x_stride = 0;
    uint32_t y_stride = 0;
#ifdef YUNOS_BOARD_intel
    int ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_X_STRIDE, target, &x_stride);
    MMASSERT(ret == 0);
    ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_Y_STRIDE, target, &y_stride);
    MMASSERT(ret == 0);
#else
    x_stride = w;
    y_stride = h;
#endif

    VERBOSE("x_stride %d, y_stride %d", x_stride, y_stride);
    allocator.map(handle, YALLOC_FLAG_SW_READ_OFTEN|YALLOC_FLAG_SW_WRITE_OFTEN,
                  0, 0, w, h, &vaddr);

    if (!vaddr) {
        ERROR("map fail, abort dump");
        return;
    }

    // DEBUG("ptr: %p, mWidth: %d, h_stride: %d, mHeight: %d\n", vaddr, mWidths[0], x_stride, mHeights[0]);

    uint8_t *buffer = (uint8_t *)vaddr;
    for (int32_t i = 0; i < h; i++) {
        rawDataDump.dump(buffer, w);
        buffer += x_stride;
    }

    buffer = (uint8_t *)vaddr + y_stride * x_stride;
    for (int32_t i = 0; i < h / 2; i++) {
        rawDataDump.dump(buffer, w);
        buffer += x_stride;
    }

    allocator.unmap(handle);
#else // __MM_YUNOS_CNTRHAL_BUILD__
    // mAllocMod->registerBuffer(mAllocMod, buffer_handle_t(src));
    void* vaddr;

    mAllocMod->lock(mAllocMod, (buffer_handle_t)target,
        BUFFER_ALLOC_USAGE_SW_WRITE_OFTEN|BUFFER_ALLOC_USAGE_SW_READ_OFTEN,
        0, 0, size.width, size.height, &vaddr);
    if (vaddr) {
        rawDataDump.dump(vaddr, size.width*size.height*3/2);
        mAllocMod->unlock(mAllocMod, (buffer_handle_t)target);
    }
#endif
}
#endif


void VideoCaptureSource::releaseRecordingFrame(VCMSHMem* shmem)
{
    if (mCamera != nullptr) {
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
        if(mHasCallbackInfos) {
            if (mCbStreamType == STREAM_MEM_TYPE_DATA) {
                uint8_t *pData = (uint8_t*)(shmem->GetBase());
                auto it = std::find(mInputBuffersStaging.begin(), mInputBuffersStaging.end(), pData);
                if (it == mInputBuffersStaging.end()) {
                    ERROR("not found pData %p", pData);
                    return;
                } else {
                    mInputBuffersStaging.erase(it);
                }
                DEBUG("releaseRecordingFrame: key %d, pData %p\n",
                        shmem->GetKey(), pData);
            }

        } else {
            int headerLength = sizeof(data_callback_header_t);
            gb_target_t handle = NULL;
            data_callback_header_t* header = (data_callback_header_t*)(shmem->GetBase());
            handle = (gb_target_t)((uint8_t*)shmem->GetBase() + headerLength);
            DEBUG("releaseRecordingFrame: index %d, target: %p\n",
                    header->index,
                    handle);
        }

#else
        int headerLength = sizeof(data_callback_header_t);
        data_callback_header_t* header = (data_callback_header_t*)(shmem->GetBase());
        MMBufferHandleT handle = NULL;
        handle = (MMBufferHandleT)((uint8_t*)shmem->GetBase() + headerLength);
        DEBUG("releaseRecordingFrame: index %d, handle: %p\n",
            header->index,
            handle);

#endif
        mCamera->ReleaseStreamFrame(STREAM_RECORD, shmem);
    }
}

}; // namespace YunOSCameraNS

