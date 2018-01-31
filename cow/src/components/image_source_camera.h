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

#ifndef CAMERA_WRAPPER_H_
#define CAMERA_WRAPPER_H_

#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <queue>

#include "multimedia/mmthread.h"
#include "multimedia/mm_cpp_utils.h"

#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#include "multimedia/mm_camera_compat.h"

namespace YunOSCameraNS {
    class VideoCaptureCallback;
    class VideoCapture;
    class VCMSHMem;
    class RecordingProxy;
}
using YunOSCameraNS::VideoCaptureCallback;
using YunOSCameraNS::VideoCapture;
using YunOSCameraNS::VCMSHMem;
using YunOSCameraNS::RecordingProxy;
#endif

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
#if __MM_YUNOS_CNTRHAL_BUILD__
    class VideoCaptureCallbackListener;
#endif

class ImageSourceCamera : public SourceComponent {
public:
    ImageSourceCamera(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~ImageSourceCamera();

    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_SUCCESS;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType);

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) {return MM_ERROR_UNSUPPORTED;}

    const char * name() const;

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop() {return MM_ERROR_SUCCESS;}
    virtual mm_status_t reset();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }

protected:
    class CallBackThread : public MMThread {
    public:
        CallBackThread(ImageSourceCamera *wrapper);
        ~CallBackThread();

        mm_status_t start();
        void stop();
        void signal();

    protected:
        virtual void main();

    private:
        ImageSourceCamera *mCameraWrapper;
        bool mContinue;
        sem_t mSem;
    };
    typedef MMSharedPtr <CallBackThread> CallBackThreadSP;

protected:
    Component * mSink;

    Condition mCondition;
    Lock mLock;

    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;

    WriterSP mWriter;
    bool mIsPaused;
    CallBackThreadSP mCallBackThread;

    std::queue<MediaBufferSP> mBufferQueue;

    int32_t mPreviewWidth;
    int32_t mPreviewHeight;
    int32_t mImageWidth;
    int32_t mImageHeight;
    int32_t mPhotoCount;
    int32_t mJpegQuality;
    int32_t mDegrees;

private:
    void enqueueBuffer(MediaBufferSP buffer);
    MediaBufferSP dequeueBuffer();
    void clearBuffers();
    mm_status_t setCameraParameters();
    mm_status_t startPreview();
    mm_status_t takePicture();
    void stopPreview();

private:
    int mCameraId;

#if __MM_YUNOS_CNTRHAL_BUILD__
    friend class VideoCaptureCallbackListener;
    friend class VideoRecordingProxy;

    #if MM_USE_CAMERA_VERSION>=30
        typedef VideoCaptureParam ImageParameters;
    #else
        typedef Properties ImageParameters;
    #endif
    typedef VideoCaptureInfo CaptureInfo;

    MMSharedPtr<VideoCapture> mCamera;
    MMSharedPtr<VideoCaptureCallback> mListener;
    std::string mSurface;
#endif

    MM_DISALLOW_COPY(ImageSourceCamera);
};

}  // namespace YUNOS_MM

#endif  // CAMERA_WRAPPER_H_
