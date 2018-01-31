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
#include "image_source_camera.h"

#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#if MM_USE_CAMERA_VERSION>=30
    #include <yunhal/VideoCaptureParam.h>
#else
    #include <yunhal/VideoCaptureProperties.h>
#endif
using namespace YunOSCameraNS;
#endif

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("ImageSourceCamera")

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define CAMERA_ID 0
#define THREAD_NAME "CallbackThread"

#define SET_PARAMETER_INT32(key, from, to) do { \
    int32_t i = 0;                            \
    if (from->getInt32(key, i)) {             \
        DEBUG("set %s, value %d\n", #key, i); \
        to->setInt32(key, i);                 \
    }                                           \
}while(0)

#define SET_PARAMETER_STRING(key, from, to) do { \
    const char * str = NULL;                     \
    if (from->getString(key, str)) {             \
        DEBUG("set %s, value %s\n", #key, str);  \
        to->setString(key, str);                  \
    }                                            \
}while(0)

#define SET_PARAMETER_POINTER(key, from, to) do { \
    void *p = NULL;                     \
    if (from->getPointer(key, p)) {             \
        DEBUG("set %s, value %p\n", #key, p);  \
        to->setPointer(key, p);                  \
    }                                            \
}while(0)


static MMParamSP nilMeta;

#define FUNCTION_REPORT() DEBUG("%s: line %d\n", __FUNCTION__, __LINE__);

static bool releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

#if defined (__MM_YUNOS_CNTRHAL_BUILD__)
class VideoCaptureCallbackListener: public virtual VideoCaptureCallback {
public:
    VideoCaptureCallbackListener(ImageSourceCamera *wrapper) { mWrapper = wrapper; }
    virtual ~VideoCaptureCallbackListener() {}
    virtual void OnAutoFocus(VideoCapture* vc, bool success) {}
    virtual void OnAutoFocusMoving(VideoCapture* vc, bool start) {}
    virtual void OnShutter() {}
    virtual void OnZoomChange(VideoCapture* vc, int zoomValue, bool stopped) {}
    virtual bool OnFaceDetection(VideoCapture* vc, FaceInfo* faces, size_t num) {return false;}
    virtual void OnError(VideoCapture* vc, int error) {}
    virtual bool OnPreviewTaken(VideoCapture* vc, int64_t timestamp, VCMSHMem* data) {return false;}
    virtual bool OnPreviewTaken(VideoCapture* vc, char* data, size_t size){return false;}
    virtual bool OnCaptureTaken(VideoCapture* vc, char* data, size_t size);
    virtual bool OnVideoTaken(VideoCapture* vc, int64_t timestamp, VCMSHMem* data) {return false;}

private:
    ImageSourceCamera *mWrapper;
    MM_DISALLOW_COPY(VideoCaptureCallbackListener);
};

bool VideoCaptureCallbackListener::OnCaptureTaken(VideoCapture* vc, char* data, size_t size) {
    DEBUG("OnCaptureTaken() called, data %p, size %d\n", data, size);
    if (!data || size == 0){
        ERROR("invalid param, data %p, size %d\n", data, size);
        return false;
    }

    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer) {
        ERROR("no mem\n");
        return false;
    }

    uint8_t *buf = new uint8_t[size];
    memcpy(buf, data, size);

    int32_t tmp = (int32_t)size;
    buffer->setBufferInfo((uintptr_t *)&buf, NULL, &tmp, 1);
    buffer->setSize((int64_t)size);
    buffer->addReleaseBufferFunc(releaseOutputBuffer);

    {
        MMAutoLock locker(mWrapper->mLock);
        mWrapper->enqueueBuffer(buffer);
        mWrapper->mCallBackThread->signal();
    }
    return false;
}

#endif


///////////////////////////////////////////////////////////////////////////////////////////////////
//CallBackThread
ImageSourceCamera::CallBackThread::CallBackThread(ImageSourceCamera* cameraWrapper)
    : MMThread(THREAD_NAME)
    , mCameraWrapper(cameraWrapper)
    , mContinue(true) {
    ENTER();
    sem_init(&mSem, 0, 0);
    EXIT();
}

ImageSourceCamera::CallBackThread::~CallBackThread() {
    ENTER();
    sem_destroy(&mSem);
    EXIT();
}

mm_status_t ImageSourceCamera::CallBackThread::start() {
    ENTER();
    mContinue = true;
    if ( create() ) {
        ERROR("failed to create thread\n");
        return MM_ERROR_NO_MEM;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void ImageSourceCamera::CallBackThread::stop() {
    ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    EXIT();
}

void ImageSourceCamera::CallBackThread::signal() {
    ENTER();
    sem_post(&mSem);
    EXIT();
}

void ImageSourceCamera::CallBackThread::main() {
    ENTER();

    while (1) {
        if (!mContinue) {
            break;
        }
        if (mCameraWrapper->mIsPaused ||
            mCameraWrapper->mBufferQueue.empty()) {
            sem_wait(&mSem);
            continue;
        }

        MediaBufferSP buffer;
        while(!mCameraWrapper->mBufferQueue.empty()) {
            buffer = mCameraWrapper->dequeueBuffer();
            if (buffer && (mCameraWrapper->mWriter->write(buffer) != MM_ERROR_SUCCESS)) {
                ERROR("failed to write to jpeg\n");
                EXIT();
            }
        }

        //start preview after takepicture
        if (mCameraWrapper->startPreview() != MM_ERROR_SUCCESS) {
            ERROR("failed to write to jpeg\n");
            EXIT();
        }

    }

    INFO("callback thread exited\n");
    EXIT();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//ImageSourceCamera
ImageSourceCamera::ImageSourceCamera(const char *mimeType, bool isEncoder) :
      mCondition(mLock)
    , mIsPaused(true)
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    , mCameraId(CaptureInfo::CAMERA_FACING_BACK) 
#endif
    , mSink(NULL)
    , mPreviewWidth(-1)
    , mPreviewHeight(-1)
    , mImageWidth(-1)
    , mImageHeight(-1)
    , mPhotoCount(-1)
    , mJpegQuality(-1)
    , mDegrees(-1)
{
    mInputMetaData = MediaMeta::create();
    //mOutputMetaData = MediaMeta::create();

    //default value
    mInputMetaData->setInt32(MEDIA_ATTR_PREVIEW_WIDTH, 1280);
    mInputMetaData->setInt32(MEDIA_ATTR_PREVIEW_HEIGHT, 720);
    mInputMetaData->setInt32(MEDIA_ATTR_PHOTO_COUNT, 1);
    mInputMetaData->setInt32(MEDIA_ATTR_JPEG_QUALITY, 100);
    mInputMetaData->setInt32(MEDIA_ATTR_IMAGE_WIDTH, 1920);
    mInputMetaData->setInt32(MEDIA_ATTR_IMAGE_HEIGHT, 1088);
}

ImageSourceCamera::~ImageSourceCamera() {
    ENTER();
    EXIT();
}

const char * ImageSourceCamera::name() const {
    return "ImageSourceCamera";
}

mm_status_t ImageSourceCamera::addSink(Component * component, MediaType mediaType) {
    ENTER1();
    if (component && mediaType == kMediaTypeImage) {
        mWriter = component->getWriter(kMediaTypeImage);
        if (mWriter) {
            mOutputMetaData = mInputMetaData->copy();
            mWriter->setMetaData(mOutputMetaData);
        }

        EXIT_AND_RETURN1(MM_ERROR_SUCCESS);
    } else {
        EXIT_AND_RETURN1(MM_ERROR_INVALID_PARAM);
    }
}

mm_status_t ImageSourceCamera::setParameter(const MediaMetaSP & meta) {
    ENTER();

    SET_PARAMETER_INT32(MEDIA_ATTR_PHOTO_COUNT, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_JPEG_QUALITY, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_PREVIEW_WIDTH, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_PREVIEW_HEIGHT, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_IMAGE_WIDTH, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_IMAGE_HEIGHT, meta, mInputMetaData);
    SET_PARAMETER_INT32(MEDIA_ATTR_ROTATION, meta, mInputMetaData);

    void *surface = NULL;
    if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE, surface)) {
        DEBUG("surface %p\n", surface);
#if __MM_YUNOS_CNTRHAL_BUILD__
        mSurface = static_cast<char*>(surface);
#endif

    }
    return setCameraParameters();
}

mm_status_t ImageSourceCamera::setUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    ENTER();
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    if (!strcmp(uri, "camera://0")) {
        mCameraId = CaptureInfo::CAMERA_FACING_BACK;
    } else if (!strcmp(uri, "camera://1")) {
        mCameraId = CaptureInfo::CAMERA_FACING_FRONT;
    }
#endif

    DEBUG("setUri uri %s, %d\n", uri, mCameraId);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t ImageSourceCamera::getParameter(MediaMetaSP & meta) const {
    ENTER();
    meta = mOutputMetaData->copy();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t ImageSourceCamera::prepare() {
    ENTER1();
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock locker(mLock);

#if __MM_YUNOS_CNTRHAL_BUILD__
    mListener.reset(new VideoCaptureCallbackListener(this));

    VideoCapture* camera = VideoCapture::Create(mCameraId);
    mCamera.reset(camera);
    if (mCamera == NULL) {
        ERROR("error to open camera id %d\n", mCameraId);
        return MM_ERROR_UNKNOWN;
    }

    DEBUG("numbers of camera is %d\n", VideoCapture::GetNumberOfVideoCapture());

    CaptureInfo info;
    VideoCapture::GetVideoCaptureInfo(mCameraId, &info);
    DEBUG("id %d, info->facing %d, info->orientation %d\n", mCameraId, info.facing, info.orientation);

    mCamera->SetVideoCaptureCallback(mListener.get());

#endif
    mOutputMetaData->setPointer(MEDIA_ATTR_CAMERA_OBJECT, mCamera.get());
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    mOutputMetaData->setPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, mCamera->getRecordingProxy());
#endif
    setCameraParameters();


    startPreview();

    //create capture thread
    mIsPaused = false;

    if (!mCallBackThread) {
        mCallBackThread.reset(new CallBackThread(this));
        if (!mCallBackThread) {
            EXIT_AND_RETURN1(status);
        }

        mm_status_t status = mCallBackThread->start();
        if (status != MM_ERROR_SUCCESS) {
            EXIT_AND_RETURN1(status);
        }
    }

    EXIT_AND_RETURN1(status);
}

mm_status_t ImageSourceCamera::start() {
    ENTER1();
    MMAutoLock locker(mLock);
    mIsPaused = false;
    return takePicture();
}

mm_status_t ImageSourceCamera::reset() {
    ENTER1();
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock locker(mLock);
    if (mCamera == NULL) {
        DEBUG("camera is released\n");
        return status;
    }

    stopPreview();

    mIsPaused = true;
    if (mCallBackThread) {
        mCallBackThread->stop();
        mCallBackThread.reset();
    }

#if __MM_YUNOS_CNTRHAL_BUILD__
    mCamera.reset();
#endif
    mWriter.reset();
    clearBuffers();

    EXIT_AND_RETURN1(status);
}

void ImageSourceCamera::enqueueBuffer(MediaBufferSP buffer) {
    mBufferQueue.push(buffer);
    DEBUG("bufferQueue size %d\n", mBufferQueue.size());

}

MediaBufferSP ImageSourceCamera::dequeueBuffer() {
    MediaBufferSP buffer;
    if (!mBufferQueue.empty()) {
        buffer = mBufferQueue.front();
        mBufferQueue.pop();
    }

    return buffer;
}

void ImageSourceCamera::clearBuffers()
{
    while(!mBufferQueue.empty()) {
        mBufferQueue.pop();
    }
}

mm_status_t ImageSourceCamera::setCameraParameters()
{
    ENTER1();

    int32_t previewWidth;
    int32_t previewHeight;
    int32_t imageWidth;
    int32_t imageHeight;
    mInputMetaData->getInt32(MEDIA_ATTR_PREVIEW_WIDTH, previewWidth);
    mInputMetaData->getInt32(MEDIA_ATTR_PREVIEW_HEIGHT, previewHeight);
    mInputMetaData->getInt32(MEDIA_ATTR_IMAGE_WIDTH, imageWidth);
    mInputMetaData->getInt32(MEDIA_ATTR_IMAGE_WIDTH, imageHeight);

    if (mCamera != NULL) {
#if __MM_YUNOS_CNTRHAL_BUILD__
        mCamera->SetStreamSize(STREAM_CAPTURE, imageWidth, imageHeight);
        mCamera->SetStreamSize(STREAM_PREVIEW, previewWidth, previewHeight);
        Size size;
        mCamera->GetStreamSize(STREAM_CAPTURE, size);
        DEBUG("picture size %dX%d\n", size.width, size.height);
        mCamera->GetStreamSize(STREAM_PREVIEW, size);
        DEBUG("preview size %dX%d\n", size.width, size.height);
#endif
    }
    EXIT_AND_RETURN1(MM_ERROR_SUCCESS);

}

mm_status_t ImageSourceCamera::startPreview() {
    ENTER();
#if __MM_YUNOS_CNTRHAL_BUILD__
    if (!mCamera->IsPreviewEnabled()) {
        bool ret = mCamera->StartStream(STREAM_PREVIEW, NULL, (SURFACE_TOKEN_TYPE)mSurface.c_str());
        if (!ret) {
            ERROR("start preview stream failed\n");
            EXIT_AND_RETURN1(MM_ERROR_UNKNOWN);
        }
    }

    Size size;
    mCamera->GetStreamSize(STREAM_PREVIEW, size);
    DEBUG("preview size %dX%d\n", size.width, size.height);
#endif
    EXIT_AND_RETURN1(MM_ERROR_SUCCESS);
}

mm_status_t ImageSourceCamera::takePicture() {
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;
#if __MM_YUNOS_CNTRHAL_BUILD__
    Size size;
    mCamera->GetStreamSize(STREAM_CAPTURE, size);
    DEBUG("picture size %dX%d\n", size.width, size.height);
    if (!mCamera->StartStream(STREAM_CAPTURE)) {
        ERROR("start capture stream failed\n");
        status = MM_ERROR_UNKNOWN;
    }
#endif

    EXIT_AND_RETURN1(status);
}


void ImageSourceCamera::stopPreview() {
    ENTER();

#if __MM_YUNOS_CNTRHAL_BUILD__
    if (mCamera->IsPreviewEnabled()) {
        mCamera->StopStream(STREAM_PREVIEW);
        DEBUG("preview enable %d\n", mCamera->IsPreviewEnabled());
    }
#endif
}

}  // namespace YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::ImageSourceCamera *component = new YUNOS_MM::ImageSourceCamera(mimeType, isEncoder);
    if (component == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(component);
}


void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}

}
