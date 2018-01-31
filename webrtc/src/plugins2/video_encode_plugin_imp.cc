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

#include <unistd.h>
#include "video_encode_plugin_imp.h"

#ifdef ADD_RECORD_PREVIEW
#include "native_surface_help.h"
#endif
#include <multimedia/media_attr_str.h>
#include <multimedia/pipeline_basic.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

using namespace YunOSCameraNS;

namespace YUNOS_MM {

DEFINE_LOGTAG(VideoEncodePluginImp);
MM_LOG_DEFINE_MODULE_NAME("VideoEncodePlug");

// #### encode pipeline
class EncodePipe : public PipelineBasic {
  public:
    explicit EncodePipe(MediaMetaSP meta);
    virtual ~EncodePipe();
    virtual mm_status_t reset();
    bool EncodeOneFrame(MediaBufferSP & buffer);

  protected:
    virtual mm_status_t prepareInternal();

  private:
    MediaMetaSP mMeta;
    Component::ReaderSP mSinkReader;
    MM_DISALLOW_COPY(EncodePipe)
    DECLARE_LOGTAG()
};
DEFINE_LOGTAG(EncodePipe);

EncodePipe::EncodePipe(MediaMetaSP meta)
    :  mMeta(meta)
{
    FUNC_TRACK();
    mComponents.reserve(3);
}

EncodePipe::~EncodePipe()
{
    FUNC_TRACK();
}

mm_status_t EncodePipe::reset()
{
    FUNC_TRACK();
    mSinkReader.reset();
    return PipelineBasic::reset();
}

mm_status_t EncodePipe::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    ComponentSP videoSource;
    ComponentSP videoCodec;
    ComponentSP videoSink;

    const char * mime;
    if (!mMeta->getString(MEDIA_ATTR_MIME, mime)) {
        MMLOGE("no mime provided\n");
        return MM_ERROR_FATAL_ERROR;
    }
    MMLOGV("video mime: %s\n", mime);

    // create components
    videoSource = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_CAMERA_SOURCE);
    videoCodec = createComponentHelper(NULL, mime, true);
    videoSink = createComponentHelper(NULL, "media/app-sink");
    if (!videoSource || !videoCodec || !videoSink) {
        ERROR("fail to create components: videoSource:%p, videocodec: %p, videoSink: %p", videoSource.get(), videoCodec.get(), videoSink.get());
        return false;
    }

    // set parameters
    videoSource->setParameter(mMeta);
    videoCodec->setParameter(mMeta);
    videoSink->setParameter(mMeta);

    SourceComponent * source = DYNAMIC_CAST<SourceComponent*>(videoSource.get());
    source->setUri("camera://");
    mSinkReader = videoSink->getReader(Component::kMediaTypeVideo);

    mComponents.push_back(ComponentInfo(videoSource, ComponentInfo::kComponentTypeSource));
    mComponents.push_back(ComponentInfo(videoCodec, ComponentInfo::kComponentTypeFilter));
    mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
    // link components
    bool addCameraRecordPreview = false;
#ifdef ADD_RECORD_PREVIEW
    addCameraRecordPreview = mm_check_env_str("mm.camera.record.preview", "MM_CAMERA_RECORD_PREVIEW", "1", addCameraRecordPreview);
#endif
    if (addCameraRecordPreview) {
        ComponentSP mediaFission = createComponentHelper("MediaFission", "media/all", mListenerReceive);
        ComponentSP videoPreview = createComponentHelper("VideoSinkSurface", "video/render", mListenerReceive);
        ASSERT(mediaFission && videoPreview);

        mComponents.push_back(ComponentInfo(mediaFission, ComponentInfo::kComponentTypeFilter));
        mComponents.push_back(ComponentInfo(videoPreview, ComponentInfo::kComponentTypeSink));

        videoPreview->setParameter(mMeta);
        // videoSource-->mediaFission-->videoPreview; videoSource-->mediaFission-->videoEncoder-->
        ASSERT(mediaFission && videoPreview);
        status = mediaFission->addSource(videoSource.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

        status = mediaFission->addSink(videoPreview.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

        status = videoCodec->addSource(mediaFission.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoCodec->addSink(videoSink.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
    } else {
        status = videoCodec->addSource(videoSource.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoCodec->addSink(videoSink.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
    }

    return status;
}

bool EncodePipe::EncodeOneFrame(MediaBufferSP & buffer)
{
    FUNC_TRACK();
    mm_status_t ret = mSinkReader->read(buffer);
    if (ret != MM_ERROR_SUCCESS || !buffer) {
        MMLOGI("readsink ret: %d\n", ret);
        return false;
    }
    return true;
}

// #### one instance for webrtc peer connection. in fact: all connection share the same encoding pipeline, but make one copy of the encoded data
class VideoEncoderWrapper : public woogeen::base::VideoEncoderInterface {
  // extends woogeen::base::VideoFrame to share MediaBufferSP with webrtc stack
  class VideoEncodeFrame : public woogeen::base::VideoFrame {
    public:
        VideoEncodeFrame() {}
        virtual ~VideoEncodeFrame() {}
        virtual bool CleanUp() { mMediaBuffer.reset(); return true; }
        bool setMediaBuffer(MediaBufferSP mediaBuffer);

    private:
        MediaBufferSP mMediaBuffer;
  };

  public:
    explicit VideoEncoderWrapper(VideoEncodePluginImp* encoderImp, EncodedBufferQueue* bufferQueue)
        : mEncoderImp(encoderImp)
        , mBufferQueue(bufferQueue)
        {}
    virtual ~VideoEncoderWrapper();

    virtual bool InitEncodeContext(woogeen::base::MediaCodec::VideoCodec video_codec, size_t width, size_t height, uint32_t bitrate, uint32_t framerate) ;
     virtual bool Release() ;
     virtual bool EncodeOneFrame(woogeen::base::VideoFrame* &frame, bool request_key_frame);
     virtual woogeen::base::VideoEncoderInterface* Copy() ;
     virtual bool SetListener(woogeen::base::VideoEncoderInterface::VideoEncoderListener *listener);
  private:
    VideoEncodePluginImp* mEncoderImp;
    EncodedBufferQueue* mBufferQueue;
    DECLARE_LOGTAG()
};
DEFINE_LOGTAG(VideoEncoderWrapper);

bool VideoEncoderWrapper::VideoEncodeFrame::setMediaBuffer(MediaBufferSP mediaBuf)
{
    if (!mediaBuf)
        return false;

    uint8_t * data = NULL;
    int32_t offset = 0;
    int32_t size = 0;
    mediaBuf->getBufferInfo((uintptr_t*)&data, &offset, &size, 1);

    size -= offset;
    data += offset;
    if (size <=0) {
        MMLOGE("invalid size: %d\n", size);
        return false;
    }
    MMLOGV("size: %d, pts: %" PRId64 ", media-buf-age: %dms", size, mediaBuf->pts(), mediaBuf->ageInMs());

    SetFrameInfo(data, size, mediaBuf->pts());
    if (mediaBuf->isFlagSet(MediaBuffer::MBFT_KeyFrame))
        SetFlag(woogeen::base::VideoFrame::VFF_KeyFrame);

    mMediaBuffer = mediaBuf;

    return true;
}

bool VideoEncoderWrapper::InitEncodeContext(woogeen::base::MediaCodec::VideoCodec video_codec, size_t width, size_t height, uint32_t bitrate, uint32_t framerate)
{
    FUNC_TRACK();
    bool ret = false;

#if 0
    width = 640;
    height = 480;
    DEBUG("force video resolution to: %dx%d", width, height);
#else
    if (width == 176 && height == 144)
        return ret;
    DEBUG("video resolution to: %dx%d", width, height);
#endif
    if (mEncoderImp)
        ret = mEncoderImp->InitEncodeContext(video_codec, width, height, bitrate, framerate);

    return ret;
 }

VideoEncoderWrapper::~VideoEncoderWrapper()
{
}

bool VideoEncoderWrapper::Release()
{
    FUNC_TRACK();
    DEBUG("mEncoderImp: %p", mEncoderImp);
    if (mEncoderImp)
        mEncoderImp->RemoveStream(mBufferQueue);

    mEncoderImp = NULL; // indicate VideoEncoderWrapper is release, avoid calls mEncoderImp->RemoveStream() again
    return true;
 }

bool VideoEncoderWrapper::EncodeOneFrame(woogeen::base::VideoFrame* &frame, bool request_key_frame)
{
    FUNC_TRACK();
    frame = NULL;

    while (1) {
        // lock?
        if (mBufferQueue->que.empty())
            mEncoderImp->EncodeOneFrame();
        if (mBufferQueue->que.empty()) {
            WARNING("got no encoded buffer");
            return false;
        }
        MediaBufferSP mediaBuf;
        {
            MMAutoLock locker(mBufferQueue->lock);
            mediaBuf = mBufferQueue->que.front();
            mBufferQueue->que.pop();
        }
        if (!mediaBuf) {
            WARNING("empty MediaBuf");
            continue;
        }

        // check key frame if needed
        if (request_key_frame) {
            if (mediaBuf && !mediaBuf->isFlagSet(MediaBuffer::MBFT_KeyFrame) && !mediaBuf->isFlagSet(MediaBuffer::MBFT_CodecData)) {
                MMLOGI("req key frame, retry\n");
                continue;
            }
        }

        if (!mediaBuf)
            return false;

        VideoEncodeFrame *frm = new VideoEncodeFrame();
        if (frm && frm->setMediaBuffer(mediaBuf))
            frame = frm;

        break; // always break;
    }


    MM_CALCULATE_AVG_FPS("webrtc");
    return true;
}

woogeen::base::VideoEncoderInterface* VideoEncoderWrapper::Copy()
{
    FUNC_TRACK();
    VideoEncoderWrapper *wrapper = NULL;
    if (!mEncoderImp)
        return NULL;

    wrapper = (VideoEncoderWrapper*)mEncoderImp->Copy();
    return wrapper;
}

bool VideoEncoderWrapper::SetListener(woogeen::base::VideoEncoderInterface::VideoEncoderListener* listener)
{
    ERROR("hasn't support SetListener yet");
    return false;
}

// #### VideoEncodePluginImp, sigleton instance of video encoding
static bool releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    DEBUG();

    if (!mediaBuffer) {
        return false;
    }

    uint8_t *buffers = NULL;
    int32_t offsets = 0;
    int32_t strides = 0;
    if ( !mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
        return false;
    }

    if (buffers)
        free(buffers);

    return true;
}

static const int32_t DEFAULT_CAM_ID = 0;
VideoEncodePluginImp::VideoEncodePluginImp(void* camera, void* surface)
    : mCameraId(DEFAULT_CAM_ID)
    , mInited(false)
    , mVideoCodec(woogeen::base::MediaCodec::H264)
    , mWidth(0)
    , mHeight(0)
    , mCopyH264Data(false)
    , mPreviewSurface(NULL)
    , mFrameCount(0)
{
    mCamera = (VideoCapture*)camera;

    mCopyH264Data = mm_check_env_str("mm.webrtc.copy.data", "MM_WEBRTC_COPY_DATA", "1", false);

    FUNC_TRACK();
}

VideoEncodePluginImp::~VideoEncodePluginImp()
{
    FUNC_TRACK();
    mRecorder.reset();
}

bool VideoEncodePluginImp::InitEncodeContext(woogeen::base::MediaCodec::VideoCodec video_codec,
        size_t width, size_t height, uint32_t bitrate, uint32_t framerate)
{
    FUNC_TRACK();
    mm_status_t st = MM_ERROR_SUCCESS;
    const char * codecMime = NULL;
    MMLOGI("video_codec: %d, width: %zu, height: %zu, bitrate: %u, framerate: %d\n",
        video_codec, width, height, bitrate, framerate);
    if (mInited && video_codec == mVideoCodec && width == (size_t)mWidth && height == (size_t)mHeight) {
        MMLOGI("the encoder has been inited before, return");
        return true;
    }
    if (mInited)
        Release();

    switch (video_codec) {
        case woogeen::base::MediaCodec::H264:
            codecMime = MEDIA_MIMETYPE_VIDEO_AVC;
            break;
        default:
            MMLOGE("unsupported codec\n");
            return false;
    }

    MMAutoLock locker(mLock);
    mWidth = width;
    mHeight = height;
    mFramerate = framerate;
    mBitrate = bitrate;

    // prepare camera object
    if (!mCamera) {
        mCamera = createCamera();
        mSelfCamera.reset(mCamera, destroyCamera);
    }
    if (!mCamera) {
        MMLOGE("no camera object to continue");
        return false;
    }

    MediaMetaSP meta = MediaMeta::create();
    meta->setString(MEDIA_ATTR_MIME, codecMime);
    meta->setInt32(MEDIA_ATTR_WIDTH, (int32_t)width);
    meta->setInt32(MEDIA_ATTR_HEIGHT, (int32_t)height);
    meta->setFloat(MEDIA_ATTR_FRAME_RATE, framerate);
    meta->setInt32(MEDIA_ATTR_BIT_RATE, bitrate*1000); // webrtc use kbps, while multimedia uses bps
    meta->setInt32("live-stream-drop-threshhold1", 1);
    meta->setInt32("live-stream-drop-threshhold2", 5);

    RecordingProxy *proxy = mCamera->getRecordingProxy();
    if (!proxy) {
        MMLOGE("Failed to get proxy\n");
        return false;
    }
    meta->setPointer(MEDIA_ATTR_CAMERA_OBJECT, mCamera);
    meta->setPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, proxy);
    meta->setInt32("is-live-stream", 1);

#ifdef ADD_RECORD_PREVIEW
    bool addCameraRecordPreview = false;
    addCameraRecordPreview = mm_check_env_str("mm.camera.record.preview", "MM_CAMERA_RECORD_PREVIEW", "1", addCameraRecordPreview);
    if (addCameraRecordPreview) {
        mPreviewSurface = createSimpleSurface(width, height);
        void * bq= getBQProducer((WindowSurface*)mPreviewSurface).get();
        meta->setPointer(MEDIA_ATTR_VIDEO_BQ_PRODUCER, bq);
    }
#endif

    // create recorder
    mRecorder = CowAppBasic::create(new CowAppBasic());
    if (!mRecorder)
        return false;

    // create my pipeline
    mPipeline = Pipeline::create(new EncodePipe(meta));
    if (!mPipeline)
        return false;

    // prepare & start
    st = mRecorder->setPipeline(mPipeline);
    MMASSERT(st == MM_ERROR_SUCCESS);
    st = mRecorder->prepare();
    MMASSERT(st == MM_ERROR_SUCCESS);
    st = mRecorder->start();
    MMASSERT(st == MM_ERROR_SUCCESS || st == MM_ERROR_ASYNC);

    mInited = true;
    return true;
}

bool VideoEncodePluginImp::EncodeOneFrame()
{
    FUNC_TRACK();
    MediaBufferSP mediaBuf;
    MMAutoLock locker(mLock);
    EncodePipe *pipe = DYNAMIC_CAST<EncodePipe*>(mPipeline.get());
    if (!pipe) {
        MMLOGE("not inited\n");
        return false;
    }

    while (1) {
        // FIXME, make it interruptable
        if (!pipe->EncodeOneFrame(mediaBuf)) {
            usleep(5000);
            continue;
        }
        break;
    }

    // make a copy of MediaBuffer, since webrtc requires a big jitter buffer while hw codec usually has few #
    MediaBufferSP newBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);

    if (mCopyH264Data) {
        DEBUG("make a copy of encoded video frame data");
        do {
            uintptr_t *buffers = NULL, *newBuffers = NULL;
            int32_t offsets = 0, newOffsets = 0;
            int32_t strides = 0, newStrides = 0;

            mediaBuf->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1);
            if (!buffers || offsets > mediaBuf->size())
                break;
            newBuffers = (uintptr_t*)malloc(mediaBuf->size());
            if (!newBuffers)
                break;
            memcpy(newBuffers, buffers+offsets, mediaBuf->size()-offsets);
            newOffsets = 0;
            newStrides = mediaBuf->size()-offsets;
            newBuf->setBufferInfo((uintptr_t*)&newBuffers, &newOffsets, &newStrides, 1);

            newBuf->setPts(mediaBuf->pts());
            newBuf->setDts(mediaBuf->dts());
            if (mediaBuf->isFlagSet(MediaBuffer::MBFT_KeyFrame))
                newBuf->setFlag(MediaBuffer::MBFT_KeyFrame);

            newBuf->addReleaseBufferFunc(releaseOutputBuffer);
        }while (0);
    } else
        newBuf = mediaBuf;

    std::list<EncodedBufferQueue*>::iterator it = mBufferQueues.begin();
    while (it != mBufferQueues.end()) {
        MMAutoLock locker((*it)->lock);
        (*it)->que.push(mediaBuf);

        while ((*it)->que.size() > 3) {
            (*it)->que.pop();
        }
        it++;
    }

    mFrameCount++;
    MMLOGD("mFrameCount: %d, dts: %" PRId64 "pts: %" PRId64 ", media-buf-age: %dms", mFrameCount, mediaBuf->dts(), mediaBuf->pts(), mediaBuf->ageInMs());

    return true;
}

bool VideoEncodePluginImp::Release()
{
    FUNC_TRACK();
    // MMAutoLock locker(mLock);  // it is called by RemoveStream() only, with lock already on
    if (!mRecorder) {
        MMLOGE("not inited\n");
        return true;
    }

#ifdef ADD_RECORD_PREVIEW
    destroySimpleSurface((WindowSurface*)mPreviewSurface);
#endif
    MMLOGI("destroy recording pipeline");
    mRecorder.reset();
    MMLOGI("destroy camera\n");
    mSelfCamera.reset();

    MMLOGI("-\n");
    return true;
}

woogeen::base::VideoEncoderInterface* VideoEncodePluginImp::Copy()
{
    FUNC_TRACK();
    EncodedBufferQueue* queue = new EncodedBufferQueue();
    {
        MMAutoLock locker(mLock);
        mBufferQueues.push_back(queue);
    }
    VideoEncoderWrapper* wrapper = new  VideoEncoderWrapper(this, queue);
    return wrapper;
}

// remove one encoder instance, in fact, remove corresponding buffer queue
bool VideoEncodePluginImp::RemoveStream(EncodedBufferQueue * queue)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    std::list<EncodedBufferQueue*>::iterator it = mBufferQueues.begin();
    while(*it != queue)
        it++;

    if (it == mBufferQueues.end()) {
        ERROR("fail to find buffer queue: %p in the list", queue);
        return true;
    }

    mBufferQueues.erase(it);
    while (!queue->que.empty())
        queue->que.pop();
    delete queue;

    // when there is not any VideoEncoderWrapper, release the sigleton VideoEncodePluginImp
    if (mBufferQueues.empty()) {
        Release();
        delete this;
    }

    return true;
}

void VideoEncodePluginImp::destroyCamera(VideoCapture* camera)
{
    FUNC_TRACK();
    if (camera) {
        camera->StopStream(STREAM_PREVIEW);
        delete camera;
        camera = NULL;
    }
}

VideoCapture*  VideoEncodePluginImp::createCamera()
{
    FUNC_TRACK();
    VideoCapture* camera = NULL;
#if MM_USE_CAMERA_VERSION>=20
    video_capture_device_version_e gDevVersion = DEVICE_ENHANCED;
    video_capture_api_version_e    gApiVersion = API_2_0;
    video_capture_version_t version = {gDevVersion, gApiVersion};
    #if MM_USE_CAMERA_VERSION>=30
        camera = VideoCapture::Create(mCameraId, gApiVersion);
    #else
        camera = VideoCapture::Create(mCameraId, &version);
    #endif
#else
    camera = VideoCapture::Create(mCameraId);
#endif


    if (!camera) {
        MMLOGE("failed to create camera\n");
        return NULL;
    }
    VideoCaptureInfo info;
    VideoCapture::GetVideoCaptureInfo(mCameraId, &info);
    MMLOGI("id %d, facing %d, orientation %d\n", mCameraId, info.facing, info.orientation);
    camera->SetDisplayRotation(info.orientation);
    camera->SetStreamSize(STREAM_PREVIEW, mWidth/4, mHeight/4);
    camera->SetStreamSize(STREAM_RECORD, mWidth, mHeight);

    bool ret = false;
#if MM_USE_CAMERA_VERSION>=20
    int status = STATUS_OK;
    #if MM_USE_CAMERA_VERSION>=30
        int format = VIDEO_CAPTURE_FMT_YUV;
    #else
        int format = VIDEO_CAPTURE_FMT_YUV_NV21_PLANE;
    #endif
    StreamConfig previewconfig = {STREAM_PREVIEW, mWidth, mHeight,
            format, 90, "camera"};
    StreamConfig recordConfig = {STREAM_RECORD, mWidth, mHeight,
            format, 0, NULL};
    std::vector<StreamConfig> configs;
    configs.push_back(previewconfig);
    configs.push_back(recordConfig);
    camera->CreateVideoCaptureTaskWithConfigs(configs);

    status = camera->StartCustomStreamWithImageParam(STREAM_PREVIEW, NULL, NULL, true, true);
    DEBUG("status: %d", status);
    ret = (status ==0);
#else
    static const char* name = "camera";
    ret = camera->StartStream(STREAM_PREVIEW, NULL, name);
#endif
    if (!ret) {
        MMLOGE("failed to StartStream\n");
        delete camera;
        camera = NULL;
        return NULL;
    }

    return camera;
}

}

