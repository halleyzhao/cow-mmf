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
#include "media_codec.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_meta.h"
#include "multimedia/mmthread.h"
#include <semaphore.h>
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_errors.h"
#include "media_notify.h"
#include <linux/videodev2.h>
#include <unistd.h>
#include <errno.h>
#include  <sys/mman.h>
// FIXME, move v4l2_codec_device to libmmbase?
#include "v4l2codec_device.h"
#if _ENABLE_X11
#include <X11/Xlib.h>
#endif

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()
MM_LOG_DEFINE_MODULE_NAME("MCV4L2");

#define INT64_TO_TIMEVAL(i64, time_val) do {            \
        time_val.tv_sec = (int32_t)(i64 >> 31);         \
        time_val.tv_usec = (int32_t)(i64 & 0x7fffffff); \
    } while (0)
#define TIMEVAL_TO_INT64(i64, time_val) do {            \
        i64 = time_val.tv_sec;                          \
        i64 = (i64 << 31)  + time_val.tv_usec;  \
    } while(0)
#define CHECK_SURFACE_OPS_RET(ret, funcName) do {                       \
        uint32_t my_errno = errno;                                                                          \
        DEBUG("%s ret: %d", funcName, ret);                                 \
        if (ret) {                                                          \
            ERROR("%s failed: %s (%d)", funcName, strerror(my_errno), my_errno);  \
            return false;                                        \
        } else {                                                            \
            INFO("%s done success\n");                                      \
        }                                                                   \
    } while(0)

using namespace YUNOS_MM;
namespace YunOSMediaCodec {
class Notify;
// FIXME, use SurfaceWrapper next
// class SurfaceWrapper;
// class MediaSurfaceTexture;

class MediaCodecV4l2 : public MediaCodec { //  not necessary to inherit from MMMsgThread
  public:
    MediaCodecV4l2();
    virtual ~MediaCodecV4l2();
    bool init(const char *name, bool nameIsType, bool encoder);
    virtual int configure(const YUNOS_MM::MediaMetaSP& params, WindowSurface* nativeWindow, uint32_t flags);
    virtual int setNativeWindow(WindowSurface *surface);
    virtual int sendCodecData(uint8_t* data, size_t dataSize, uint32_t index);
    virtual int createInputSurface();
    virtual int start();
    virtual int stop();
    virtual int reset();
    virtual int release();
    virtual int flush();
    virtual int getInputBufferCount();
    virtual int getOutputBufferCount();
    virtual int queueInputBuffer(size_t idx, size_t offset, size_t size, int64_t time, uint32_t flags);
    virtual int dequeueInputBuffer(size_t *index, int64_t timeoutUs = 0ll);
    virtual int dequeueOutputBuffer(size_t *index, size_t *offset, size_t *size,
            int64_t *presentationTimeUs, uint32_t *flags, int64_t timeoutUs = 0ll);
    virtual int releaseOutputBuffer(size_t idx, bool render);
    virtual int signalEndOfInputStream();
    virtual MediaMetaSP getOutputFormat();
    virtual MediaMetaSP getInputFormat();
    virtual uint8_t* getOutputBuffer(size_t idx, size_t *out_size);
    virtual MMNativeBuffer* getNativeWindowBuffer(size_t idx);
    virtual MediaMetaSP getOutputFormat(size_t index) const;
    virtual uint8_t* getInputBuffer(size_t idx, size_t *out_size);
    virtual int requestIDRFrame();
    virtual void requestActivityNotification();
    virtual void registerNotify(Notify *notify);
    virtual const char* getName() const;
    virtual int setParameters(MediaMetaSP &params);

  private:
    std::vector<std::vector<uint8_t> > mCodecData; // FIXME, change to std::vector<std::string>
    // bool mUpdateOutputFormat;
    // bool mUseMetadataOnEncoderOutput;
    // uint64_t mRepeatPrevFrameDelayUs;

    // bool mFlushingPending;
    // bool mPortSettingChange; // FIXME, we do not consider dynamic-resolution-change for now, but accept the first frame resolution only
    bool mIsConfigured;
    bool mOutputConfigured; // use a state machine to manage output buffer configuration: uninit/got-new-resolution/configure-surface etc
    // bool mShutDownPending;
    // bool mStartPending;
    enum State {
        UNINITIALIZED,
        INITIALIZED,
        CONFIGURED,
        STARTING,
        STARTED, // STARTD state includes flushing and portSettingChange. input port is started; for output port, additional check for mOutputConfigured
        STOPPING, // --> INITIALIZED
        STOPPED,
    };

    // initial create parameters
    std::string mInitName;
    std::string mMime;
    bool mIsEncoder;

    State mState;
    V4l2CodecDeviceSP mV4l2Codec;
    WindowSurface *mNativeWindow;
    Lock mLock;
#if _ENABLE_X11
    // FIXME, it is caused by intel driver design (it requires X11 display for decoding when the frame shares with egl)
    Display *mX11Display;
#endif

    class DevicePollThread;
    typedef MMSharedPtr <DevicePollThread> DevicePollThreadSP;
    DevicePollThreadSP mDevicePollThread;
    MediaMetaSP mOutputFormat; // FIXME, add it support
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mStride[3];
    uint32_t mFormat;

    // V4L2Codec assume input buffer init status is on client side, while MediaCodec assume it is owned by codec
    // so, we needn't deque the first mInputBufferSize buffer from V4L2 for MediaCodec
    uint32_t mDqInputBufferCount;
    // uint32_t mPortBufferCount[2]; // mInputBufferCount/mOutputBufferCount
    // size_t mPortBufferSize[2];       //kMaxInputBufferSize, not support for output buffer size
    uint32_t mInputBufferSize;
    // bool mPortEOS[2];                  // it is not necessary for MediaCodec. if do need, we can use a state like NoneEOS/InputEOS/OutputEOS
    // bool mPortFlushComplete[2];  // simple STREAM_OFF to flush the input/output port?

    // std::list<size_t> mAvailPortBuffers[2];      // simple translate to V4L2 cmd, do not cache the buffers
    // std::vector<BufferInfo> mPortBuffers[2];     // do not cache buffers, not necesary to track buffer status

    // uint32_t mDequeueInputRspId;
    // uint32_t mDequeueOutputRspId;

    // buffers deal with V4l2Codec
    enum v4l2_memory mInputMemoryType;      // MediaCodec supports MMAP(V4L2_MEMORY_MMAP) only
    enum v4l2_memory mOutputMemoryType;     // MediaCodec supports MMAP(V4L2_MEMORY_MMAP) only
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;

    // output frame buffer
    uint32_t mOutputPlaneCount;
    std::vector<uint8_t*>   mInputBuffers;
#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
        // SurfaceWrapper *mSurface; // FIXME
        bool configureOutputSurface();
        std::vector<MMNativeBuffer*> mOutputFrames;
#endif
    // output raw data if NativeWindow is not provided
    struct RawFrameData {
        uint32_t width;
        uint32_t height;
        uint32_t pitch[3];
        uint32_t offset[3];
        uint32_t fourcc;            //NV12
        uint8_t *data;
    };
    std::vector<struct RawFrameData> mOutputFramesRaw;
    bool configureOutputBufferRaw();

    uint32_t mEnqueInputBufferCount;             // debug use only for enqued input buffers;
    uint32_t mDequeOutputBufferCount;            // debug use only for dequed output buffers;

    // uint32_t mCheckInputGeneration;
    // uint32_t mCheckOutputGeneration;

    Notify *mNotify;

    bool mForceRender;
    bool mForceDumpInput;
    bool mForceDumpOutput;
    int32_t mRotationDegrees;

    // DataDump mInputDataDump;
    // DataDump mOutputDataDump;
    // size_t mNumUndequeuedBuffers;

  private:
    bool notifyAvailableBuffers(int32_t inputCount, int32_t outputCount);
    bool initInputBuffers();
    bool initOutputBuffers();
    bool renderOutputBuffer(uint32_t &index, bool renderIt);
    int enqueV4l2OutputBuffer(uint32_t index);
    MM_DISALLOW_COPY(MediaCodecV4l2);
};

// FIXME, optimize it for memory consumption
static const uint32_t kMaxInputBufferSize = 1024*1024;
static const int32_t kInputPlaneCount = 1;
static const int32_t kMaxOutputPlaneCount = 3;
static const int32_t kExtraOutputFrameCount= 2;

// ////////////////////// DevicePollThread
class MediaCodecV4l2::DevicePollThread : public MMThread {
  public:
    DevicePollThread(MediaCodecV4l2 * codec)
        : MMThread("MCV4l2-Poll")
        , mMediaCodec(codec)
        , mContinue(true)
    {
        FUNC_TRACK();
        sem_init(&mSem, 0, 0);
    }

    ~DevicePollThread()
    {
        FUNC_TRACK();
        sem_destroy(&mSem);
    }

    // FIXME, since we have signalExit, MMThread::destroy()/pthread_join() is not necessary for us.
    // FIXME, uses pthrad_detach
    void signalExit()
    {
        FUNC_TRACK();
        MMAutoLock locker(mMediaCodec->mLock);
        mContinue = false;
        sem_post(&mSem);
    }

    void signalContinue()
    {
        FUNC_TRACK();
        sem_post(&mSem);
    }

  protected:
    virtual void main();

  private:
    Lock mLock;
    MediaCodecV4l2 * mMediaCodec;
    // FIXME, define a class for sem_t, does init/destroy automatically
    sem_t mSem;         // cork/uncork on pause/resume
    // FIXME, change to something like mExit
    bool mContinue;     // terminate the thread
};

// poll the device for notification of available input/output buffer index
void MediaCodecV4l2::DevicePollThread::main()
{
    FUNC_TRACK();
    bool event_pending=true; // try to get video resolution changes.
    while(1) {
        {
            MMAutoLock locker(mMediaCodec->mLock);
            if (!mContinue) {
                break;
            }
        }
        if (mMediaCodec->mState != STARTED) {
            INFO("DevicePollThread waitting_sem\n");
            sem_wait(&mSem);
            INFO("DevicePollThread wakeup_sem\n");
            continue;
        }
        // need handle resolution change before configure output buffers
        if (event_pending) {
            bool resolutionChanged = false;
            struct v4l2_event ev;
            memset(&ev, 0, sizeof(ev));
            while (mMediaCodec->mV4l2Codec->ioctl(VIDIOC_DQEVENT, &ev) == 0) {
                if (ev.type == V4L2_EVENT_RESOLUTION_CHANGE) {
                    resolutionChanged = true;
                    break;
                }
            }

            if (resolutionChanged) {
                // FIXME, is it ok to query output format in this thread
                // FIXME, handle dynamic resolution change
                DEBUG("resolution changed, try to init output buffers");
                mMediaCodec->initOutputBuffers();
                break;
            }
        }

        // FIXME, it's better schedule one of the two (or both) depending on ret value
        // FIXME, lock mMediaCodec->mNotify
        if (!mMediaCodec->notifyAvailableBuffers(1, 1)) {
            WARNING("fail to notify client on available buffers");
        }

        DEBUG("poll device for notification\n");
        int ret = mMediaCodec->mV4l2Codec->poll(true, &event_pending);
        DEBUG("poll ret: %d\n", ret);
    }

    INFO("Poll thread exited\n");
}

///////////////////////////////////// MediaCodecV4l2
/* static */ MediaCodec* MediaCodec::CreateByType(const char *mime, bool encoder, mm_status_t *err)
{
    FUNC_TRACK();
    // FIXME, add other codec type support
    if (strcmp(mime, MEDIA_MIMETYPE_VIDEO_AVC)) {
        ERROR("%s hasn't supported yet\n", mime);
        return NULL;
    }

    MediaCodecV4l2* codec = new MediaCodecV4l2();

    INFO("MediaCodec::CreateByType mime:%s,encoder:%d\n", mime,encoder);
    ASSERT(!encoder); // FIXME, add encoder support
    bool ret = codec->init(mime, true /* nameIsType */, encoder);
    INFO("MediaCodec::CreateByType:%d\n",ret);

    if (!ret) {
        delete codec;
        return NULL;
    }

    return codec;
}

/* static */ MediaCodec* MediaCodec::CreateByComponentName(const char *name, mm_status_t *err)
{
    FUNC_TRACK();
    WARNING("Not Implemented");
    return NULL;
}

MediaCodecV4l2::MediaCodecV4l2()
{
    FUNC_TRACK();
    mIsConfigured = false;
    mOutputConfigured = false;
    mIsEncoder = false;
    mState = UNINITIALIZED;
#if _ENABLE_X11
    mX11Display = NULL;
#endif

    mWidth = 0;
    mHeight = 0;
    mStride[0] = mStride[1] = mStride[2] = 0;
    mFormat = 0;

    mDqInputBufferCount = 0;
    mInputBufferSize = 0;
    mInputMemoryType = V4L2_MEMORY_MMAP;
    mOutputMemoryType = V4L2_MEMORY_MMAP;
    mInputBufferCount = 0;
    mOutputBufferCount = 0;

    mOutputPlaneCount = 0;
#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
    mNativeWindow = 0;
    // SurfaceWrapper *mSurface; // FIXME
#endif
    mEnqueInputBufferCount = 0;
    mDequeOutputBufferCount = 0;

    mNotify = NULL;

    mForceRender = false;
    mForceDumpInput = false;
    mForceDumpOutput = false;
    mRotationDegrees = 0;
}

bool MediaCodecV4l2::init(const char *name, bool nameIsType, bool encoder)
{
    FUNC_TRACK();
    int ioctlRet = -1;
    mMime = name;

    // FIXME, add encoder next
    ASSERT(!encoder);
    mIsEncoder = encoder;

    if (encoder)
        mV4l2Codec = V4l2CodecDevice::create("encoder", 0);
    else
        mV4l2Codec = V4l2CodecDevice::create("decoder", 0);
    ASSERT(mV4l2Codec);

#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
    mV4l2Codec->setParameter("frame-memory-type", "plugin-buffer-handle");
    mOutputMemoryType = (enum v4l2_memory)V4L2_MEMORY_PLUGIN_BUFFER_HANDLE;
#else
  #if _ENABLE_X11
    if (!mX11Display) {
        XInitThreads();
        mX11Display = XOpenDisplay(NULL);
    }

    DEBUG("x11display: %p", mX11Display );
    ASSERT(mX11Display );

    char displayStr[32];
    sprintf(displayStr, "%" PRIu64 "", (uint64_t)mX11Display );
    DEBUG("displayStr: %s", displayStr);
    mV4l2Codec->setParameter("frame-memory-type", "drm-name");
    ioctlRet = mV4l2Codec->setParameter("x11-display", displayStr);
  #else
    mV4l2Codec->setParameter("frame-memory-type", "raw-copy");
  #endif
#endif

    // query hw capability
    struct v4l2_capability caps;
    memset(&caps, 0, sizeof(caps));
    caps.capabilities = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_STREAMING;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_QUERYCAP, &caps);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYCAP);

    // set input data format
    // FIXME, add more format support
    uint32_t codecFormat = V4L2_PIX_FMT_H264;

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.pixelformat = codecFormat;
    format.fmt.pix_mp.num_planes = 1;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = kMaxInputBufferSize;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);

    mState= INITIALIZED;

    return true;
}

int MediaCodecV4l2::configure(const YUNOS_MM::MediaMetaSP& params, WindowSurface* nativeWindow, uint32_t flags)
{
    FUNC_TRACK();
    int ioctlRet = -1;
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    if (!nativeWindow)
        return MM_ERROR_INVALID_PARAM;
#endif

    mNativeWindow = static_cast<WindowSurface*>(nativeWindow);

    uint8_t *buf = NULL;
    int32_t bufSize;
    if (params->getByteBuffer(MEDIA_ATTR_EXTRADATA0, buf, bufSize)) {
        DEBUG("onConfigure get CSD0 data, size is %d", bufSize);
        std::vector<uint8_t> d;
        d.resize(bufSize);
        memcpy(&d[0], buf, bufSize);
        mCodecData.push_back(d);
    }

    if (params->getByteBuffer(MEDIA_ATTR_EXTRADATA1, buf, bufSize)) {
        DEBUG("onConfigure get CSD0 data, size is %d", bufSize);
        std::vector<uint8_t> d;
        d.resize(bufSize);
        memcpy(&d[0], buf, bufSize);
        mCodecData.push_back(d);
    }

#if 0
    if (nativeWindow) {
        INFO("nativeWindow %p, flag is %x", nativeWindow, flags);

        if (flags & CONFIGURE_FLAG_SURFACE_TEXTURE)
            err = (mm_status_t)setSurfaceTexture((MediaSurfaceTexture *)nativeWindow);
        else
            err = (mm_status_t)setNativeWindow((WindowSurface *)nativeWindow);

        ASSERT_ERROR(err == MM_ERROR_SUCCESS, "setNativeWindow failed");
    }
#endif

    int32_t tmpInt;
    params->getInt32(MEDIA_ATTR_WIDTH, tmpInt);
    mWidth = (uint32_t)tmpInt;

    params->getInt32(MEDIA_ATTR_HEIGHT, tmpInt);
    mHeight = (uint32_t)tmpInt;

    // kParamMaxWidth/kParamMaxHeight

    params->getInt32(MEDIA_ATTR_ROTATION, mRotationDegrees);
    DEBUG("mRotationDegrees %d\n", mRotationDegrees);

    /*
    param = findParameterByKey (params, kParamInputBufferSize);
    if (param) {
        mInputBufferSize = param->value.ii;
        INFO("client suggest input buffer size %d", mInputBufferSize);
    }
    */

    // set preferred output format
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    // send codecdata before deque/enque
    if (mCodecData.size()) {
        //save codecdata, size+data, the type of format.fmt.raw_data is __u8[200]
        //we must make sure enough space (>=sizeof(uint32_t) + size) to store codecdata
        uint32_t codecdata_size  = 0;
        for (uint32_t i=0; i<mCodecData.size(); i++) {
            codecdata_size += mCodecData[i].size();
        }
        if(sizeof(format.fmt.raw_data) >= codecdata_size + sizeof(uint32_t)) {
            uint8_t *ptr = format.fmt.raw_data;
            memcpy(ptr, &codecdata_size, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            for (uint32_t i=0; i<mCodecData.size(); i++) {
                memcpy(ptr, &mCodecData[0][0], mCodecData[0].size());
                ptr += mCodecData[0].size();
            }
            DEBUG("dump codecdata in MediaCodecV4l2::configure");
            hexDump(format.fmt.raw_data, sizeof(uint32_t)+codecdata_size, 16);
            mCodecData.clear();
        } else {
            ERROR("No enough space to store codec data");
            return MEDIACODEC_ERROR;
        }
    } else {
        INFO("no codecdata\n");
        format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M; //  V4L2_PIX_FMT_RGB32;
    }

    ioctlRet = mV4l2Codec->ioctl(VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);
    mIsConfigured = true;

    // initOutputBuffers() are called after got video resolution from poll thread
    mState = CONFIGURED;

    return MEDIACODEC_OK;
}

int MediaCodecV4l2::setNativeWindow(WindowSurface *surface)
{
    FUNC_TRACK();
    if (mNativeWindow) {
        WARNING("native window %p is set before", mNativeWindow);
    }

    mNativeWindow = surface;
    return MEDIACODEC_OK;
}

int MediaCodecV4l2::sendCodecData(uint8_t* data, size_t dataSize, uint32_t index)
{
    FUNC_TRACK();
    ASSERT(mState == INITIALIZED);

    if (index == 0)
        mCodecData.clear();

    // FIXME, use std::string, then we can use assign()
    std::vector<uint8_t> d;
    d.assign(data, data+dataSize);
    mCodecData.push_back(d);
    return MEDIACODEC_OK;
}


int MediaCodecV4l2::createInputSurface()
{
    FUNC_TRACK();
    WARNING("Not Implemented");
    return MEDIACODEC_ERROR;
}

bool MediaCodecV4l2::notifyAvailableBuffers(int32_t inputCount, int32_t outputCount)
{
    FUNC_TRACK();
    if (!mNotify) {
        DEBUG("no registered mNotify");
        return true;
    }

    mNotify->setInt32("num-input-buffers", inputCount);
    mNotify->setInt32("num-output-buffers", outputCount);
    mNotify->postAsync();
    mNotify = NULL;

    return true;
}

int MediaCodecV4l2::start()
{
    FUNC_TRACK();
    uint32_t i = 0;
    MMAutoLock locker(mLock); // big locker
    int ioctlRet = -1;

    DEBUG("mIsConfigured:%d\n", mIsConfigured);
    if (!mIsConfigured) {
        WARNING("configure() should be called before startInputPort");
        return MEDIACODEC_ERROR;
    }
    mState = STARTING;

    // start input port
    DEBUG("input port stream on\n");
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    // setup input buffers
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInputMemoryType;
    reqbufs.count = 2;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
    ASSERT(reqbufs.count>0);
    mInputBufferCount = reqbufs.count;

    if (mInputMemoryType == V4L2_MEMORY_MMAP) {
        mInputBuffers.resize(mInputBufferCount);
        for (i=0; i<mInputBufferCount; i++) {
            struct v4l2_plane planes[kInputPlaneCount];
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(buffer));
            memset(planes, 0, sizeof(planes));
            buffer.index = i;
            buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            buffer.memory = mInputMemoryType;
            buffer.m.planes = planes;
            buffer.length = kInputPlaneCount;
            ioctlRet = mV4l2Codec->ioctl(VIDIOC_QUERYBUF, &buffer);
            CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYBUF);

            // length and mem_offset should be filled by VIDIOC_QUERYBUF above
            void* address = mV4l2Codec->mmap(NULL,
                                          buffer.m.planes[0].length,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED,
                                          buffer.m.planes[0].m.mem_offset);
            ASSERT(address);
            mInputBuffers[i] = static_cast<uint8_t*>(address);
            DEBUG("mInputBuffers[%d] = %p", i, mInputBuffers[i]);
        }
    }
    if (notifyAvailableBuffers(mInputBufferCount, 0)) {
        WARNING("fail to notify client on available buffers");
    }

    mV4l2Codec->clearDevicePollInterrupt();
    mDevicePollThread.reset(new DevicePollThread(this), MMThread::releaseHelper);
    // FIXME, add SP to handle create and destroy
    mDevicePollThread->create();
    // FIXME, should we notify input buffers are available?
    mDevicePollThread->signalContinue();

    mState = STARTED;
    return MEDIACODEC_OK;
}

#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
bool MediaCodecV4l2::configureOutputSurface()
{
    FUNC_TRACK();
    int ret = 0;
    int32_t minUndequeuedBuffers = 0;
    uint32_t i = 0;

    if (!mNativeWindow)
        return false;

    // FIXME, add support for dynamic resolution change next. now,  assume we are in the process of starting;
    ASSERT(mState == STARTED);

    // 1. config WindowSurface
    INFO("config mSurface with size %dx%d\n", mWidth, mHeight);
    ASSERT(mWidth && mHeight);
    ret = WINDOW_API(set_buffers_dimensions)(GET_ANATIVEWINDOW(mNativeWindow), mWidth, mHeight);
    // ret = WINDOW_API(set_buffers_geometry)(mSurface, mWidth, mHeight, HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL);
    CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(set_buffers_dimensions)");

    // FIXME, the mFormat is got from v4l2codec, and set to WindowSurface;
    // from v4l2/videodev2.h: it is fourcc/uint32_t, but surface uses enum from OMX definition
    INFO("native window set buffers format %x", mFormat);
    ret = WINDOW_API(set_buffers_format)(GET_ANATIVEWINDOW(mNativeWindow), mFormat);
    CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(set_buffers_format)");

    // FIXME, add crop support, getGraphicBufferUsage() from v4l2codec
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
    ret = mm_setSurfaceUsage (mNativeWindow, GRALLOC_USAGE_HW_TEXTURE  |GRALLOC_USAGE_EXTERNAL_DISP);
#else
    ret = mm_setSurfaceUsage (mNativeWindow, YALLOC_FLAG_HW_TEXTURE | YALLOC_FLAG_HW_RENDER);
#endif
    CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(set_usage)");

#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
    ret = GET_ANATIVEWINDOW(mNativeWindow)->query(GET_ANATIVEWINDOW(mNativeWindow), NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &minUndequeuedBuffers);
#else
    minUndequeuedBuffers = 1;
#endif
    INFO("minUndequeuedBuffers=%d", minUndequeuedBuffers);
    CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(query)");

    // enlarge output frame queue size considering surface's minUndequeuedBuffers and extra buffer
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;

    for (uint32_t extraBuffers = kExtraOutputFrameCount; /* condition inside loop */; extraBuffers--) {
        DEBUG("mOutputBufferCount =%u, minUndequeuedBuffers=%u, extraBuffers=%u",
            mOutputBufferCount, minUndequeuedBuffers, extraBuffers);
        uint32_t newBufferCount = mOutputBufferCount + minUndequeuedBuffers + extraBuffers;
        reqbufs.count = newBufferCount;
        DEBUG("try to set output buffer count=%u", newBufferCount);
        int ioctlRet = mV4l2Codec->ioctl(VIDIOC_REQBUFS, &reqbufs);
        if (ioctlRet == 0) {
            mOutputBufferCount = reqbufs.count;
            DEBUG("final buffer count: mOutputBufferCount: %d, (suggest) newBufferCount: %d",
                mOutputBufferCount, newBufferCount);
            break;
        }

        /* exit condition */
        if (extraBuffers == 0) {
            ERROR("fail to set output frame queue size\n");
            return false;
        }
    }

    ret = WINDOW_API(set_buffer_count)(GET_ANATIVEWINDOW(mNativeWindow), mOutputBufferCount);
    CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(set_buffer_count)");
    INFO("final output buffer count=%lu", mOutputBufferCount);

    // 2. alloc output buffer
    // Dequeue surface buffers and send them to V4l2codec
    mOutputFrames.clear();

    // 3. feed output buffer to v4l2codec
    // FIXME, the canceled buffer to windowsurface is required to send to v4l2codec at the begining as well
    for (i = 0; i < mOutputBufferCount; i++) {
        MMNativeBuffer *anb = NULL;
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);

        ret = mm_dequeueBufferWait(mNativeWindow, &anb);
        CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(dequeue_buffer_and_wait)");

#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
        buf.m.userptr = (unsigned long)anb->handle;
#else
        buf.m.userptr = (unsigned long)anb->target;
#endif
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.index = i;
        buf.memory = mOutputMemoryType;
        ret = mV4l2Codec->ioctl(VIDIOC_QBUF, &buf);
        if (ret <0) {
            ERROR("VIDIOC_QBUF for buffer %d failed: %s (anb %p)\n", buf.index, strerror(errno), anb);
            return false;
        }

        // FIXME(seem not necessary), track buffer ownership of ANativeWindowBuffer
        mOutputFrames.push_back(anb);
    }

    // 4. FIXME, deque some buffer and cancelBuffer them to WindowSurface
    for (i = 0; (int32_t)i < minUndequeuedBuffers; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[kMaxOutputPlaneCount];
        int32_t ioctlRet = 0;

        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
        buf.memory = mOutputMemoryType;
        buf.m.planes = planes;
        buf.length = mOutputPlaneCount;
        ioctlRet = mV4l2Codec->ioctl(VIDIOC_DQBUF, &buf);
        ASSERT(ioctlRet == 0);

        DEBUG("Calling cancelBuffer on buffer %p", mOutputFrames[i]);
        ret = mm_cancelBuffer(mNativeWindow,  mOutputFrames[buf.index], -1);
        CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(cancel_buffer)");
    }

    return true;
}
#endif

// V4L2Codec alloc output buffer when NativeWinodw is not set
bool MediaCodecV4l2::configureOutputBufferRaw()
{
    FUNC_TRACK();
    int ioctlRet = -1;

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    reqbufs.count = mOutputBufferCount + kExtraOutputFrameCount;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
    ASSERT(reqbufs.count>0);
    mOutputBufferCount = reqbufs.count;

    DEBUG();
    mOutputFramesRaw.clear();
    mOutputFramesRaw.resize(mOutputBufferCount);
    uint32_t i=0;
    for (i=0; i<mOutputBufferCount; i++) {
        DEBUG();
        struct v4l2_plane planes[kMaxOutputPlaneCount];
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        memset(planes, 0, sizeof(planes));
        buffer.index = i;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = mOutputMemoryType;
        buffer.m.planes = planes;
        buffer.length = mOutputPlaneCount;
        ioctlRet = mV4l2Codec->ioctl(VIDIOC_QUERYBUF, &buffer);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYBUF);

        mOutputFramesRaw[i].width = mWidth;
        mOutputFramesRaw[i].height = mHeight;
        mOutputFramesRaw[i].fourcc = mFormat;

        for (uint32_t j=0; j<mOutputPlaneCount; j++) {
            // length and mem_offset are filled by VIDIOC_QUERYBUF above
            void* address = mV4l2Codec->mmap(NULL,
                                          buffer.m.planes[j].length,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED,
                                          buffer.m.planes[j].m.mem_offset);
            ASSERT(address);
            if (j == 0) {
                mOutputFramesRaw[i].data = static_cast<uint8_t*>(address);
                mOutputFramesRaw[i].offset[0] = 0;
            } else {
                mOutputFramesRaw[i].offset[j] = static_cast<uint8_t*>(address) - mOutputFramesRaw[i].data;
            }

            mOutputFramesRaw[i].pitch[j] = mStride[j];
            DEBUG("mOutputFramesRaw[%d][%d] = %p\n", i, j, address);

            // feed the output frame to v4l2codec
            int ioctlRet = mV4l2Codec->ioctl(VIDIOC_QBUF, &buffer);
            CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QBUF);
        }
    }

    DEBUG();
    return true;
}

bool MediaCodecV4l2::initOutputBuffers()
{
    FUNC_TRACK();
    int ioctlRet = -1;
    MMAutoLock locker(mLock); // big locker

    // mV4l2Codec->setParameter("tiling-mode", "Y_TILED_INTEL");
    if (mState != STARTED)
        return false;

    DEBUG();
    // query (output) video resolution
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    // we give the hint to v4l2codec, while they decide the final output format
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_G_FMT, &format);
    if (ioctlRet) {
        WARNING("fail to get output video format, ret: %d", ioctlRet);
        return false;
    }

    DEBUG();
    mOutputPlaneCount = format.fmt.pix_mp.num_planes;
    ASSERT(mOutputPlaneCount == 2);
    mWidth = format.fmt.pix_mp.width;
    mHeight = format.fmt.pix_mp.height;
    mFormat = format.fmt.pix_mp.pixelformat;
    for (uint32_t i=0; i<mOutputPlaneCount; i++)
        mStride[i] = format.fmt.pix_mp.plane_fmt[i].bytesperline;
    ASSERT(mWidth && mHeight);

    // FIXME, should we query output buffer count after configure windowsurface?
    // Number of output buffers we need.
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_G_CTRL, &ctrl);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_G_CTRL);
    mOutputBufferCount = ctrl.value;
    DEBUG("mOutputBufferCount: %d", mOutputBufferCount);

    // setup output buffers
#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
    if (mNativeWindow)
        configureOutputSurface();
    else
#endif
        configureOutputBufferRaw();

    // start output port
    DEBUG("output port stream on\n");
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    mOutputConfigured = true;

    return true;
}

int MediaCodecV4l2::stop()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    int ioctlRet = -1;

    DEBUG("mIsConfigured=%d, mState=%d\n", mIsConfigured, mState);
    if (!mIsConfigured || mState < STARTED || mState == STOPPED){
        return MEDIACODEC_OK;
    }

    mState = STOPPING;

    mV4l2Codec->setDevicePollInterrupt();
    if (mDevicePollThread) {
        mDevicePollThread->signalExit();
        mDevicePollThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mDevicePollThread
    }

    // release queued input/output buffer
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    // stop input port
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // stop output port
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    mState = STOPPED;
    mIsConfigured = false;

    return MEDIACODEC_OK;
}

int MediaCodecV4l2::reset()
{
    FUNC_TRACK();
    return stop();
}

int MediaCodecV4l2::release()
{
    FUNC_TRACK();
    mV4l2Codec.reset();
#if _ENABLE_X11
    if (mX11Display)
        XCloseDisplay(mX11Display);
#endif
    return MEDIACODEC_OK;
}

int MediaCodecV4l2::flush()
{
    FUNC_TRACK();
    // FIXME, maybe VIDIOC_REQBUFS to flush buffers
    return MEDIACODEC_OK;
}

int MediaCodecV4l2::getInputBufferCount()
{
    FUNC_TRACK();
    if (mState < STARTED)
        return -1;

    return mInputBufferCount;
}

int MediaCodecV4l2::getOutputBufferCount()
{
    FUNC_TRACK();
    if (mState <STARTED || !mOutputConfigured)
        return -1;

    return mOutputBufferCount;
}

int MediaCodecV4l2::queueInputBuffer(size_t idx, size_t offset, size_t size, int64_t time, uint32_t flags)
{
    FUNC_TRACK();
    struct v4l2_buffer buf;
    const uint32_t kInputPlaneCount = 1;
    struct v4l2_plane planes[kInputPlaneCount];
    int ioctlRet = -1;

    if (mState < STARTED || mState >= STOPPING)
        return MEDIACODEC_ERROR;

    // v4l2codec supports usr_ptr mode buffer input, while MediaCodec not
    buf.index = idx;
    buf.flags = flags;
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory = mInputMemoryType;

    // data has already been copied to the buffer (corresponding to index) by client
    buf.length = kInputPlaneCount;
    buf.m.planes = planes;
    buf.m.planes[0].bytesused = size;
    buf.m.planes[0].m.mem_offset = offset;
    INT64_TO_TIMEVAL(time, buf.timestamp);

    ioctlRet = mV4l2Codec->ioctl(VIDIOC_QBUF, &buf);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QBUF);


    if (ioctlRet == EAGAIN)
        return MEDIACODEC_INFO_TRY_AGAIN_LATER;
    else if (ioctlRet) {
        WARNING("fail to deque input buffer");
        return MEDIACODEC_ERROR;
    }

    mEnqueInputBufferCount++;
    DEBUG("enqueue one input buffer, mEnqueInputBufferCount=%d, idx: %d", mEnqueInputBufferCount, idx);

    return MM_ERROR_SUCCESS;
}

int MediaCodecV4l2::dequeueInputBuffer(size_t *index, int64_t timeoutUs)
{
    FUNC_TRACK();
    struct v4l2_buffer buf;
    const uint32_t kInputPlaneCount = 1;
    struct v4l2_plane planes[kInputPlaneCount];
    int ioctlRet = -1;

    if (mState < STARTED || mState >= STOPPING)
        return MEDIACODEC_ERROR;

    // V4L2Codec assumes input buffer initial status is on client side
    if (mDqInputBufferCount < mInputBufferCount) {
        *index = mDqInputBufferCount++;
        return MEDIACODEC_OK;
    }

    // dequeue input buffers from V4l2Codec device
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(planes));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; // it indicates input buffer(raw frame) type
    buf.memory = mInputMemoryType;
    buf.m.planes = planes;
    buf.length = kInputPlaneCount;

    ioctlRet = mV4l2Codec->ioctl(VIDIOC_DQBUF, &buf);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_DQBUF);

    if (ioctlRet == EAGAIN)
        return MEDIACODEC_INFO_TRY_AGAIN_LATER;
    else if (ioctlRet) {
        WARNING("fail to deque input buffer");
        return MEDIACODEC_ERROR;
    }

    *index = buf.index;

    return MEDIACODEC_OK;
}

int MediaCodecV4l2::dequeueOutputBuffer(size_t *index, size_t *offset, size_t *size,
       int64_t *presentationTimeUs, uint32_t *flags, int64_t timeoutUs)
{
    FUNC_TRACK();
    DEBUG("mState: %d, STARTED: %d, STOPPING: %d", mState, STARTED, STOPPING);
    if (mState < STARTED || mState >= STOPPING)
        return MEDIACODEC_ERROR;

    if (!mOutputConfigured) {
        #if 0
        if (!initOutputBuffers()) {
            DEBUG("not able to initOutputBuffers() yet");
            return MEDIACODEC_ERROR;
        }
        #else
            DEBUG("output buffers not inited yet, return");
            return MEDIACODEC_ERROR;
        #endif
    }
    DEBUG();
    int ioctlRet = -1;
    struct v4l2_buffer *buf = (struct v4l2_buffer*)calloc(1, sizeof(struct v4l2_buffer));
    struct v4l2_plane *planes = (struct v4l2_plane*)calloc(kMaxOutputPlaneCount, sizeof(struct v4l2_plane)); // YUV output, in fact, we use NV12 of 2 planes

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
    buf->memory = mOutputMemoryType;
    buf->m.planes = planes;
    buf->length = mOutputPlaneCount;

    DEBUG();
    ioctlRet = mV4l2Codec->ioctl(VIDIOC_DQBUF, buf);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_DQBUF);

    if (ioctlRet == EAGAIN) {
        DEBUG();
        return MEDIACODEC_INFO_TRY_AGAIN_LATER;
    } else if (ioctlRet) {
        WARNING("fail to deque output buffer");
        return MEDIACODEC_ERROR;
    }

    *index = buf->index;
    *offset = buf->m.offset;
    *flags = buf->flags; // FIXME, add EOS flag
    int64_t pts;
    TIMEVAL_TO_INT64(pts, buf->timestamp);
    *presentationTimeUs = pts;

    mDequeOutputBufferCount++;
    INFO("dequeue one output frame, mDequeOutputBufferCount: %d, timestamp: (%ld, %ld)\n", mDequeOutputBufferCount, buf->timestamp.tv_sec, buf->timestamp.tv_usec);

    return MEDIACODEC_OK;
}

bool MediaCodecV4l2::renderOutputBuffer(uint32_t &index, bool renderIt)
{
    FUNC_TRACK();
    int ret = 0;
#if (defined (__MM_YUNOS_CNTRHAL_BUILD__) && defined(_USE_PLUGIN_BUFFER_HANDLE))
    if(renderIt) {
        DEBUG("enque buffer index to WindowSurface: %d", index);
        int ret = mm_queueBuffer(mNativeWindow, mOutputFrames[index], -1);
        CHECK_SURFACE_OPS_RET(ret, "queueBuffer");
        mNativeWindow->finishSwap();

        MMNativeBuffer* pbuf = NULL;
        ret = mm_dequeueBufferWait(mNativeWindow, &pbuf);
        CHECK_SURFACE_OPS_RET(ret, "WINDOW_API(dequeue_buffer_and_wait)");

        uint32_t i = 0;
        for (i=0; i<mOutputFrames.size(); i++) {
            if (pbuf == mOutputFrames[i]) {
                index = i;
                break;
            }
        }
        DEBUG("update buffer index from dequeue of WindowSurface: %d", index);
    }
#endif
    return ret;
}

int MediaCodecV4l2::enqueV4l2OutputBuffer(uint32_t index)
{
    FUNC_TRACK();
    struct v4l2_buffer *buf = (struct v4l2_buffer*)calloc(1, sizeof(struct v4l2_buffer));
    struct v4l2_plane *planes = (struct v4l2_plane*)calloc(kMaxOutputPlaneCount, sizeof(struct v4l2_plane)); // YUV output, in fact, we use NV12 of 2 planes

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
    buf->memory = mOutputMemoryType;
    buf->m.planes = planes;
    buf->length = mOutputPlaneCount;

    buf->index = index;
    int ioctlRet = mV4l2Codec->ioctl(VIDIOC_QBUF, buf);

    if (ioctlRet == EAGAIN)
        return MEDIACODEC_INFO_TRY_AGAIN_LATER;
    else if (ioctlRet) {
        WARNING("fail to deque input buffer");
        return MEDIACODEC_ERROR;
    }

    return MEDIACODEC_OK;
}

int MediaCodecV4l2::releaseOutputBuffer(size_t idx, bool render)
{
    FUNC_TRACK();
    int status = MEDIACODEC_ERROR;
    uint32_t index = idx;
    status = renderOutputBuffer(index, render);
    status = enqueV4l2OutputBuffer(index);

    return status;
}

int MediaCodecV4l2::signalEndOfInputStream()
{
    FUNC_TRACK();
    WARNING("Not Implemented yet");
    // FIXME, fill an empty buffer to V4l2Codec?
    return MEDIACODEC_ERROR_UNSUPPORTED;
}

MediaMetaSP MediaCodecV4l2::getOutputFormat()
{
    FUNC_TRACK();
    MediaMetaSP vec;
    // FIXME, add output format support
    WARNING("Not Implemented\n");
    return vec;
}

MediaMetaSP MediaCodecV4l2::getInputFormat()
{
    FUNC_TRACK();
    MediaMetaSP vec;
    WARNING("Not Implemented\n");
    return vec;
}

uint8_t* MediaCodecV4l2::getOutputBuffer(size_t idx, size_t *out_size)
{
    FUNC_TRACK();
    if (mNativeWindow) {
        // FIXME, do we support map the surface buffer?
        return NULL;
    } else
        return mOutputFramesRaw[idx].data;
}

MMNativeBuffer* MediaCodecV4l2::getNativeWindowBuffer(size_t idx)
{
    FUNC_TRACK();
    WARNING("Not Implemented");
    return NULL;
}

MediaMetaSP MediaCodecV4l2::getOutputFormat(size_t index) const
{
    FUNC_TRACK();
    MediaMetaSP vec;
    WARNING("Not Implemented\n", __FUNCTION__);
    return vec;
}

uint8_t* MediaCodecV4l2::getInputBuffer(size_t idx, size_t *out_size)
{
    FUNC_TRACK();
    if (mState < STARTED)
        return NULL;

    *out_size = kMaxInputBufferSize;
    return mInputBuffers[idx];

}

int MediaCodecV4l2::requestIDRFrame()
{
    FUNC_TRACK();
    WARNING("Not Implemented yet");
    return MEDIACODEC_ERROR_UNSUPPORTED;
}

void MediaCodecV4l2::requestActivityNotification()
{
    FUNC_TRACK();
    // FIXME, always enable it?
}

void MediaCodecV4l2::registerNotify(Notify *notify)
{
    FUNC_TRACK();
    // FIXME, discard previous notify?
    if (notify)
        mNotify = notify;
}


const char* MediaCodecV4l2::getName() const
{
    FUNC_TRACK();
    WARNING("Not Implemented");
    return NULL;
}


int MediaCodecV4l2::setParameters(MediaMetaSP &params)
{
    FUNC_TRACK();
    WARNING("Not Implemented");
    return MEDIACODEC_ERROR_UNSUPPORTED;
}

MediaCodecV4l2::~MediaCodecV4l2()
{
    FUNC_TRACK();
    release();
}

}// end of YunOSMediaCodec
