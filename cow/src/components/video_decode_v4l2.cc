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

#include "video_decode_v4l2.h"
#include <linux/videodev2.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"
#include "native_surface_help.h"
#include <media_surface_texture.h>
#if defined(YUNOS_BOARD_sprd)
#include "WindowSurfaceTestWindow.h"
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
#include <WindowSurface.h>
#endif
#endif

#if defined(YUNOS_BOARD_intel)
#include <yalloc_drm.h>
#endif
#include <media_surface_utils.h>
#include "cow_util.h"


MM_LOG_DEFINE_MODULE_NAME("VDV4L2");

#define CHECK_V4L2_CMD_RESULT_RET(ret, CMD) do { \
    if (ret == 0 ) {                         \
        INFO("%s success", #CMD);            \
    } else if (ret == -1) {                  \
        ERROR("%s failed", #CMD);            \
        return MM_ERROR_UNKNOWN;             \
    } else {                                 \
        WARNING("%s ret: %d", #CMD, ret);    \
        return MM_ERROR_UNKNOWN;             \
    }                                        \
}while (0)


#define FUNC_TRACK1() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

#define INT64_TO_TIMEVAL(i64, time_val) do {            \
        time_val.tv_sec = (int32_t)(i64 >> 31);         \
        time_val.tv_usec = (int32_t)(i64 & 0x7fffffff); \
    } while (0)
#define TIMEVAL_TO_INT64(i64, time_val) do {            \
        i64 = time_val.tv_sec;                          \
        i64 = (i64 << 31)  + time_val.tv_usec;  \
    } while(0)


using namespace YunOSMediaCodec;
using namespace yunos;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
using namespace libgui;
#endif
namespace YUNOS_MM {
static const char * COMPONENT_NAME = "VideoDecodeV4l2";
static const char * MMSGTHREAD_NAME = "VDV4l2";
static const char * MMTHREAD_NAME = "VDV4l2-Poll";
/* it is used when there is available v4l2 buffer slot, but not any video data.
 *  40ms should be ok, since
 *  - the main driver of data flow is main() after poll() ret
 *  - onHandleInputBuffer() will process all available buffer in one cycle */
static const int32_t kInOutputRetryDelayUs = 40000;       // 40ms
// FIXME, calculate it from video resolution
static const uint32_t kMaxInputBufferSize = 1024*1024;
static const int32_t kInputPlaneCount = 1;
static const int32_t kMaxOutputPlaneCount = 3;
static const int32_t kExtraOutputFrameCount= 2;
static const int kMinUndequeuedBuffs = 3; // hardcode
static const uint32_t kInputBufferCount = 8;

// FIXME, if pipeline always call flush before seek. then decoder does nothing in seek.
#define VDV4L2_MSG_prepare (msg_type)1
#define VDV4L2_MSG_startInput (msg_type)2
#define VDV4L2_MSG_startOutput (msg_type)3
#define VDV4L2_MSG_pause (msg_type)4
#define VDV4L2_MSG_resume (msg_type)5
#define VDV4L2_MSG_stop (msg_type)6
#define VDV4L2_MSG_flush (msg_type)7
#define VDV4L2_MSG_reset (msg_type)8
#define VDV4L2_MSG_handleInputBuffer (msg_type)9
#define VDV4L2_MSG_handleOutputBuffer (msg_type)10
#define VDV4L2_MSG_handleResolutionChange (msg_type)11
#define VDV4L2_MSG_scheduleOutputBuffer (msg_type)12

BEGIN_MSG_LOOP(VideoDecodeV4l2)
    MSG_ITEM(VDV4L2_MSG_prepare, onPrepare)
    MSG_ITEM(VDV4L2_MSG_startInput, onStartInput)
    MSG_ITEM(VDV4L2_MSG_startOutput, onStartOutput)
    MSG_ITEM(VDV4L2_MSG_pause, onPause)
    MSG_ITEM(VDV4L2_MSG_resume, onResume)
    MSG_ITEM(VDV4L2_MSG_stop, onStop)
    MSG_ITEM(VDV4L2_MSG_flush, onFlush)
    MSG_ITEM(VDV4L2_MSG_reset, onReset)
    MSG_ITEM(VDV4L2_MSG_handleInputBuffer, onHandleInputBuffer)
    MSG_ITEM(VDV4L2_MSG_handleOutputBuffer, onHandleOutputBuffer)
    MSG_ITEM(VDV4L2_MSG_handleResolutionChange, onHandleResolutionChange)
    MSG_ITEM(VDV4L2_MSG_scheduleOutputBuffer, onScheduleOutputBuffer)
END_MSG_LOOP()

static inline void handle_fence(int &fencefd)
{
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    if (-1 != fencefd) {
        // sync_wait
        ::close(fencefd);
        fencefd = -1;
    }
#endif
}

class V4l2TexureListener : public YunOSMediaCodec::SurfaceTextureListener {
public:
    V4l2TexureListener(VideoDecodeV4l2 *owner) { mOwner = owner; }
    virtual ~V4l2TexureListener() {}

    virtual void onMessage(int msg, int param1, int param2) {
        if (mOwner) {
            mOwner->notify(Component::kEventUpdateTextureImage, param1, param2, nilParam);
         }
    }

private:
    VideoDecodeV4l2* mOwner;
};

#ifndef N_ELEMENTS
#define N_ELEMENTS(array) (sizeof(array)/sizeof(array[0]))
#endif

struct FormatEntry {
    uint32_t format;
    const char* mime;
};

#define FOURCC(ch0, ch1, ch2, ch3) \
        ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
         ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))

#ifndef V4L2_PIX_FMT_VC1
#define V4L2_PIX_FMT_VC1 FOURCC('V', 'C', '1', '0')
#endif
#ifndef V4L2_PIX_FMT_VP8
#define V4L2_PIX_FMT_VP8 FOURCC('V', 'P', '8', '0')
#endif
static const FormatEntry FormatEntrys[] = {
    {V4L2_PIX_FMT_H264, MEDIA_MIMETYPE_VIDEO_AVC},
    {V4L2_PIX_FMT_H264, "video/h264"},
    {V4L2_PIX_FMT_VP8, MEDIA_MIMETYPE_VIDEO_VP8},
    {V4L2_PIX_FMT_MJPEG, MEDIA_MIMETYPE_IMAGE_JPEG},
    {V4L2_PIX_FMT_MPEG2, MEDIA_MIMETYPE_VIDEO_MPEG2},
    {V4L2_PIX_FMT_MPEG4, MEDIA_MIMETYPE_VIDEO_MPEG4},
    {V4L2_PIX_FMT_H263, MEDIA_MIMETYPE_VIDEO_H263},
    {V4L2_PIX_FMT_VC1, MEDIA_MIMETYPE_VIDEO_VC1},
    {V4L2_PIX_FMT_VC1, MEDIA_MIMETYPE_VIDEO_WMV}
};

uint32_t v4l2PixelFormatFromMime(const char* mime)
{
    uint32_t format = 0;
    for (uint32_t i = 0; i < N_ELEMENTS(FormatEntrys); i++) {
        const FormatEntry* entry = FormatEntrys + i;
        if (strcmp(mime, entry->mime) == 0) {
            format = entry->format;
            break;
        }
    }
    return format;
}

static void mp4toannexb(uint8_t *codecBuf, uint8_t *sourceBuf, const int32_t length) {

// assume length filed has 4 bytes, actually it can be configtured to 1/2/4
// 14496-15 5.4.1.2 lengthSizeMinusOne
#define NAL_LENGTH_OFFSET 4
#define START_CODE_OFFSET 4
#define START_CODE 0x01000000

    uint32_t nalLength;
    uint32_t *ptr;
    int32_t offset = 0;

    while (offset < length) {
        ptr = (uint32_t*)(sourceBuf);

        nalLength = ntohl(*ptr);
        sourceBuf += NAL_LENGTH_OFFSET;

        *((uint32_t *)codecBuf) = START_CODE;
        codecBuf += START_CODE_OFFSET;
        offset += NAL_LENGTH_OFFSET + nalLength;

        if (offset > length || offset < 0) {
            WARNING("got bad buffer, buffer length %d current NAL length %d(0x%x) offset %d\n",
                    length, nalLength, nalLength, offset);
            break;
        }

        memcpy(codecBuf, sourceBuf, nalLength);
        codecBuf += nalLength;
        sourceBuf += nalLength;
    }
}

// ////////////////////// DevicePollThread
class VideoDecodeV4l2::DevicePollThread : public MMThread {
  public:
    DevicePollThread(VideoDecodeV4l2 * decoder)
        : MMThread(MMTHREAD_NAME)
        , mDecoder(decoder)
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
        MMAutoLock locker(mDecoder->mLock);
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
    VideoDecodeV4l2 * mDecoder;
    // FIXME, define a class for sem_t, does init/destroy automatically
    sem_t mSem;         // cork/uncork on pause/resume
    // FIXME, change to something like mExit
    bool mContinue;     // terminate the thread
};

// poll the device for notification of available input/output buffer index
void VideoDecodeV4l2::DevicePollThread::main()
{
    FUNC_TRACK();

    bool event_pending=true; // try to get video resolution.
    while(1) {
        {
            MMAutoLock locker(mDecoder->mLock);
            if (!mContinue) {
                DEBUG("quit DevicePollThread");
                break;
            }
        }
        if (mDecoder->mState != kStatePlaying) {
            INFO("DevicePollThread waitting_sem\n");
            sem_wait(&mSem);
            INFO("DevicePollThread wakeup_sem\n");
            continue;
        }

        // need handle resolution change before take an output frame
        if (event_pending) {
            DEBUG("GOT EVENT");
            mDecoder->postMsg(VDV4L2_MSG_handleResolutionChange, 0, NULL, 0);
            event_pending = false;
        }

        // FIXME, it's better schedule one of the two (or both) depending on ret value
        VERBOSE("post VDV4L2_MSG_handleInputBuffer");
        mDecoder->postMsg(VDV4L2_MSG_handleInputBuffer, 0, NULL, 0);
        mDecoder->postMsg(VDV4L2_MSG_handleOutputBuffer, 0, NULL, 0);

        VERBOSE("poll device for notification\n");
        int ret = mDecoder->mV4l2Decoder->poll(true, &event_pending);
        VERBOSE("poll ret: %d\n", ret);
    #ifdef YUNOS_BOARD_sprd
        usleep(15*1000); // pool is not work, sleep 10ms
    #endif
    }

    INFO("Poll thread exited\n");
}

// /////////////////////////////////////
VideoDecodeV4l2::VideoDecodeV4l2(const char* mimeType, bool isEncoder)
    : MMMsgThread(MMSGTHREAD_NAME)
    , mMime(mimeType)
    , mComponentName(COMPONENT_NAME)
    , mState(kStateNull)
    , mEosState(kNoneEOS)
    , mSource(NULL)
    , mSink(NULL)
    , mWidth(-1)
    , mHeight(-1)
    , mFormat(0)
    , mRotationDegrees(0)
    , mIsAVCcType(false)
    , mCSDBufferIndex(0)
    , mInputMemoryType(V4L2_MEMORY_MMAP)
    , mOutputMemoryType(V4L2_MEMORY_MMAP)
    , mInputQueueCapacity(0)
    , mLastTimeInputRetry(0)
    , mOutputQueueCapacity(0)
    , mOutputPlaneCount(0)
    , mDumpInput(false)
    , mDumpOutput(false)
    , mSurfaceWrapper(NULL)
    , mSurfaceTexture(NULL)
    , mSurfaceOwnedByUs(false)
    , mSurface(NULL)
    , mConfigured(false)
    , mGeneration(1)
    , mInputBufferCount(0)
    , mOutputBufferCount(0)
    , mOutputBufferCountRender(0)
    , mOutputDequeueRetryCount(0)
    , mInputDataDump("/tmp/input.data")
    , mOutputDataDump(NULL)
{
    FUNC_TRACK();

    ASSERT(!isEncoder);
    mCodecFormat = v4l2PixelFormatFromMime(mimeType);
    DEBUG("mCodecFormat %d", mCodecFormat);
    if (mCodecFormat == 0) {
        ERROR("%s hasn't supported yet\n", mimeType);
        ASSERT(0 && "mimetype hasn't supported");
    }

    if (mm_check_env_str("mm.vdv4l2.memory.userptr","MM_VDV4L2_MEMORY_USERPTR", "1"))
        mInputMemoryType = V4L2_MEMORY_USERPTR;

    mDumpOutput = mm_check_env_str("mm.vdv4l2.dump.output","MM_VDV4L2_DUMP_OUTPUT", "1", 0);
    mDumpInput = mm_check_env_str("mm.vdv4l2.dump.input","MM_VDV4L2_DUMP_INPUT", "1", 0);

    mOutputFormat = MediaMeta::create();

#if defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    mPrefixCSD = true;
    mMediaBufferType = MediaBuffer::MBT_DrmBufName;
#else
    mMediaBufferType = MediaBuffer::MBT_BufferIndexV4L2;
#endif
#if defined(__MM_YUNOS_LINUX_BSP_BUILD__) || defined(YUNOS_BOARD_sprd)
    mForceByteStream = true;
#endif
}

VideoDecodeV4l2::~VideoDecodeV4l2()
{
    FUNC_TRACK();
    // in case reset is not called, make sure members are deconstruction in order
    // FIXME, it is not good to call onReset() here, since onReset() wille notify(kEventResetComplete)
    // then PipelinePlayer will visit current Component during Component's decostruction func.
    // it lead to undefined behavior.
    // onReset(0, NULL, 0);

    if (mSurfaceWrapper)
        delete mSurfaceWrapper;

#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    if (mYalloc) {
        int ret = yalloc_close(mYalloc);
        MMASSERT(ret == 0);
    }
#endif

    if (mOutputDataDump)
        delete mOutputDataDump;
}

void VideoDecodeV4l2::updateBufferGeneration()
{
    FUNC_TRACK();

    // 0 & UINT32_MAX are invalid generation
    mGeneration++;
    if (mGeneration == UINT32_MAX)
        mGeneration = 1;

    DEBUG("update mGeneration: %d", mGeneration);

    mInputBufferCount = 0;
    mOutputBufferCount = 0;
    mOutputBufferCountRender = 0;
    mOutputDequeueRetryCount= 0;
}

mm_status_t VideoDecodeV4l2::prepare()
{
    FUNC_TRACK();
    postMsg(VDV4L2_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeV4l2::start()
{
    FUNC_TRACK();
    if (mConfigured) {
        postMsg(VDV4L2_MSG_resume, 0, NULL);
    } else {
        postMsg(VDV4L2_MSG_startInput, 0, NULL);
    }

    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeV4l2::stop()
{
    FUNC_TRACK();
    postMsg(VDV4L2_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoDecodeV4l2::pause()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(VDV4L2_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeV4l2::resume()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(VDV4L2_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoDecodeV4l2::seek(int msec, int seekSequence)
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}
mm_status_t VideoDecodeV4l2::reset()
{
    FUNC_TRACK();
    postMsg(VDV4L2_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeV4l2::flush()
{
    FUNC_TRACK();
    postMsg(VDV4L2_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoDecodeV4l2::addSource(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            mInputFormat = mReader->getMetaData();
            if (mInputFormat) {
                if (mInputFormat->getInt32(MEDIA_ATTR_WIDTH, mWidth)) {
                    DEBUG("get meta data, width is %d\n", mWidth);
                    mOutputFormat->setInt32(MEDIA_ATTR_WIDTH, mWidth);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, mHeight)) {
                    DEBUG("get meta data, height is %d\n", mHeight);
                    mOutputFormat->setInt32(MEDIA_ATTR_HEIGHT, mHeight);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, mFp)) {
                    DEBUG("get meta data, mFp is %d\n", mFp);
                    mOutputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, mFp);
                }

                const char* mime = NULL;
                if (mInputFormat->getString(MEDIA_ATTR_MIME, mime)) {
                    DEBUG("get meta data, mime is %s\n", mime);
                }

                int32_t codecId = -1;
                if (mInputFormat->getInt32(MEDIA_ATTR_CODECID, codecId)) {
                    DEBUG("get meta data, codec ID is %d\n", codecId);
                }

                // FIXME, yami doesn't requires extracted SPS/PPS, but the complete codecdata
                uint8_t *data = NULL;
                int32_t size = 0;
                if (mInputFormat->getByteBuffer(MEDIA_ATTR_EXTRADATA0, data, size)) {
                    DEBUG("get meta data, csd0 size is %d", size);
                }

                if (mInputFormat->getByteBuffer(MEDIA_ATTR_EXTRADATA1, data, size)) {
                    DEBUG("get meta data, csd1 size is %d", size);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_ROTATION, mRotationDegrees)) {
                    DEBUG("get meta data, rotation degrees is %d\n", mRotationDegrees);
                }
            #if defined(YUNOS_BOARD_sprd) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
                mStreamCSDMaker = createStreamCSDMaker(mInputFormat);
            #endif
            }
            return MM_ERROR_SUCCESS;
        }
    }

    return MM_ERROR_OP_FAILED;
}

mm_status_t VideoDecodeV4l2::addSink(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);

        if (mWriter && mOutputFormat) {
            mWriter->setMetaData(mOutputFormat);
        }
    }
    return MM_ERROR_SUCCESS;
}

void VideoDecodeV4l2::setState(StateType state)
{
    // FIXME, seem Lock isn't necessary
    // caller thread update state to kStatePaused,  other state are updated by MMMsgThread
    // MMAutoLock locker(mLock);
    mState = state;
}

void VideoDecodeV4l2::setEosState(EosStateType state)
{
    // FIXME, seem Lock isn't necessary
    mEosState = state;
}

StreamMakeCSDSP VideoDecodeV4l2::createStreamCSDMaker(MediaMetaSP &meta) {
    StreamMakeCSDSP csd;
    int32_t codec_id = 0;
    uint8_t *data = NULL;
    int32_t size = 0;

    FUNC_TRACK();
    if ((!meta->getInt32(MEDIA_ATTR_CODECID, codec_id)) ||
        ((codec_id != kCodecIDAAC) &&
        (!meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) )){
        INFO("codecid %d, data %p\n", codec_id, data);
        return csd;
    }

    if (codec_id == kCodecIDAAC) {
        csd.reset(new AACCSDMaker(meta));
    } else if (codec_id == kCodecIDH264) {
        DEBUG();
        AVCCSDMaker *maker = new AVCCSDMaker(meta);
        if (maker == NULL) {
            ERROR("no memory\n");
            return csd;
        }
        csd.reset(maker);
        maker->getCSDCount();//make isAVCc valid
        mIsAVCcType = maker->isAVCc();
        DEBUG("mIsAVCcType %d\n", mIsAVCcType);
    } else {
        csd.reset(new StreamCSDMaker(meta));
    }

    INFO("codec 0x%0x", codec_id);

    return csd;
}

void VideoDecodeV4l2::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock); // big lock
    int ioctlRet = -1;

    mState = kStatePreparing;
    ASSERT(mInputFormat && mMime.c_str());

#ifdef __MM_BUILD_DRM_SURFACE__
        // create Wayland Drm Surface in advance to make sure drm auth
        if (!mSurfaceWrapper) {
            mSurfaceWrapper = new YunOSMediaCodec::WlDrmSurfaceWrapper((omap_device *)NULL);
        }
#elif defined(__MM_BUILD_VPU_SURFACE__)
        if (!mSurfaceWrapper) {
            mSurfaceWrapper = new YunOSMediaCodec::VpuSurfaceWrapper();
        }
#endif


    mV4l2Decoder = V4l2CodecDevice::create("decoder", 0);
    ASSERT(mV4l2Decoder);

    // set output frame memory type
#if defined (__MM_YUNOS_CNTRHAL_BUILD__)
    mV4l2Decoder->setParameter("frame-memory-type", "plugin-buffer-handle");
    mOutputMemoryType = (enum v4l2_memory)V4L2_MEMORY_PLUGIN_BUFFER_HANDLE;
#elif defined(__MM_YUNOS_YUNHAL_BUILD__)
    mV4l2Decoder->setParameter("frame-memory-type", "yunos-native-target");
    mOutputMemoryType = (enum v4l2_memory)V4L2_MEMORY_MMAP;
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    if (mDecodeThumbNail) {
        mV4l2Decoder->setParameter(MEDIA_ATTR_DECODE_MODE, MEDIA_ATTR_DECODE_THUMBNAIL);
    }
#endif

    // query hw capability
    struct v4l2_capability caps;
    memset(&caps, 0, sizeof(caps));
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QUERYCAP, &caps);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYCAP);
    uint32_t capabilitiesMask = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_STREAMING;
    if ((caps.capabilities & capabilitiesMask) != capabilitiesMask) {
        ERROR("unsupport capablities 0x%0x\n", capabilitiesMask);
        ioctlRet = -1;
    }

    // set input data format
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.pixelformat = mCodecFormat;
    format.fmt.pix_mp.num_planes = 1;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = kMaxInputBufferSize;
    format.fmt.pix_mp.width = mWidth;
    format.fmt.pix_mp.height = mHeight;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);

    // set preferred output format
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    // send codecdata before deque/enque
    uint8_t* data = NULL;
    int32_t size = 0;

    bool ret = mInputFormat->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size);
    if (ret) {
        hexDump(data, size, 16);

#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        int32_t csdOffset = 0;
        uint8_t codecData[256] = {0};

        if (mIsAVCcType && mForceByteStream) {
            getCSDInfo(codecData, csdOffset);
            data = &codecData[0];
            size = csdOffset;
        }
        hexDump(data, size, 16);
#endif

        //save codecdata, size+data, the type of format.fmt.raw_data is __u8[200]
        //we must make sure enough space (>=sizeof(uint32_t) + size) to store codecdata
        memcpy(format.fmt.raw_data, &size, sizeof(uint32_t));

        uint32_t copySize = size;
        if(sizeof(format.fmt.raw_data) < size + sizeof(uint32_t)) {
            copySize = sizeof(format.fmt.raw_data) - sizeof(uint32_t);
            ERROR("No enough space to store codec data, truncate it to continue");
        }
        memcpy(format.fmt.raw_data + sizeof(uint32_t), data, copySize);

    } else {
        INFO("no codecdata\n");
        format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
    }

    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);

    mState = kStatePrepared;
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
}


mm_status_t VideoDecodeV4l2::handlePortSettingChanged()
{
    FUNC_TRACK();
    int ioctlRet = 0;

    // 3. free all output buffers: reqbufs.count = 0
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT_RET(ioctlRet, VIDIOC_REQBUFS);

    // 4. streamoff output port
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT_RET(ioctlRet, VIDIOC_STREAMOFF);

    updateBufferGeneration();

    // 5. return all buffers to Buffer Queue
    dbgSurfaceBufferStatus();
    for (uint32_t i = 0; i < mOutputQueueCapacity; i++) {
        if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_V4L2CCODEC) {
            // stream off is set, set buffer owner to US
            mOutputFrames[i].mOwner = BufferInfo::OWNED_BY_US;
        } else if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_NATIVE_WINDOW) {
            continue;
        }
        cancelBufferToNativeWindow(&mOutputFrames[i]);
    }

    // 6. reallocate buffers
    mm_status_t status = allocateOutputBuffersFromNativeWindow();
    if (status != MM_ERROR_SUCCESS) {
        notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
        return status;
    }

    processDeferMessage();

    return status;
}

void VideoDecodeV4l2::onHandleResolutionChange(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    bool resolutionChanged = false;
    struct v4l2_event ev;
    memset(&ev, 0, sizeof(ev));

    while (mV4l2Decoder->ioctl(VIDIOC_DQEVENT, &ev) == 0) {
        if (ev.type == V4L2_EVENT_RESOLUTION_CHANGE) {
            resolutionChanged = true;
            break;
        }
    }

    if (!resolutionChanged) {
        WARNING("no resolution chaged");
        return;
    }
    DEBUG("format changed");

#if defined(YUNOS_BOARD_intel) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    int ioctlRet = 0;

    // 2. get new format
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (mV4l2Decoder->ioctl( VIDIOC_G_FMT, &format) == -1) {
        return false;
    }
    ASSERT(ioctlRet != -1);
    // resolution and pixelformat got here
    mOutputPlaneCount = format.fmt.pix_mp.num_planes;
    ASSERT(mOutputPlaneCount == 2);
    int w = format.fmt.pix_mp.width;
    int h = format.fmt.pix_mp.height;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_G_CTRL, &ctrl);
    ASSERT(ioctlRet != -1);
    int dpbSize = ctrl.value;
    if(mWidth == w && mHeight == h && mVideoDpbSize == dpbSize) {
        ERROR("port foramt change, but actually no! w = %d, h = %d, dpbsize = %d\n", w, h, dpbSize);
        return false;
    }
    DEBUG("old %dx%d, dpb %d, new %dx%d, dpb %d", mWidth, mHeight, mVideoDpbSize, w, h, dpbSize);
    mWidth = w;
    mHeight = h;
    mVideoDpbSize = dpbSize;

    mPortSettingChange = true;

    int32_t count = 0;
    for (size_t index = 0; index < mOutputFrames.size(); index++) {
        if (mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_V4L2CCODEC ||
            mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_CLIENT)
            count++;
    }
    DEBUG("there are %d output buffers owned by codec or client", count);

#else
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (mV4l2Decoder->ioctl(VIDIOC_G_FMT, &format) == -1) {
        ERROR("fail to get output format\n");
        return;
    }

    // resolution and pixelformat got here
    mOutputPlaneCount = format.fmt.pix_mp.num_planes;
    ASSERT(mOutputPlaneCount == 2);
    mWidth = format.fmt.pix_mp.width;
    mHeight = format.fmt.pix_mp.height;
    notify(kEventGotVideoFormat, mWidth, mHeight, nilParam);
    DEBUG("mWidth %d, mHeight %d, mOutputPlaneCount %d\n", mWidth, mHeight, mOutputPlaneCount);
#endif
    return;
}

//only can be called in MMMsgThread
void VideoDecodeV4l2::deferMessage(msg_type what, param1_type param1,
    param2_type param2, uint32_t rspId)
{
    INFO("defer message, what %d, param1 %d, param2 %p, rspId %u",
        what, param1, param2, rspId);
    mDeferMessages.push_back(Message(what, param1, param2, rspId, 0));
}

void VideoDecodeV4l2::processDeferMessage() {
    if (mDeferMessages.empty()) {
        return;
    }
    std::list< Message >::iterator it;
    INFO("mDeferMessages size %d", mDeferMessages.size());
    for(it = mDeferMessages.begin();
        it != mDeferMessages.end(); ){
        auto ite = it++;
        param1_type param1 = ite->param1();
        param2_type param2 = ite->param2();
        uint32_t rspId = ite->respId();
        uint32_t what = ite->what();
        mDeferMessages.erase(ite); //it will be invalid after erase
        switch (what){
            case VDV4L2_MSG_flush:
                onFlush(param1, param2, rspId);
                break;
            case VDV4L2_MSG_stop:
                onStop(param1, param2, rspId);
                break;
            default:
                ASSERT(0);
                break;
        }
    }
}

mm_status_t VideoDecodeV4l2::allocateOutputBuffers()
{
    int ioctlRet = -1;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_G_CTRL, &ctrl);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_G_CTRL);
    mOutputQueueCapacity = ctrl.value ? ctrl.value : mOutputQueueCapacity;

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    // Check mOutputQueueCapacity here, otherwise output buffer count is 2, cause to block here.
    mOutputQueueCapacity = (mOutputQueueCapacity == 0) ? 3 : mOutputQueueCapacity;
    reqbufs.count = mOutputQueueCapacity + kExtraOutputFrameCount;
    DEBUG("reqbufs.count %d", reqbufs.count);
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
    ASSERT(reqbufs.count>0);
    mOutputQueueCapacity = reqbufs.count;
    DEBUG("reqbufs.count %d", reqbufs.count);

    mOutputFrames.resize(mOutputQueueCapacity);
    uint32_t i=0;
    for (i=0; i<mOutputQueueCapacity; i++) {
        struct v4l2_plane planes[kMaxOutputPlaneCount];
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        memset(planes, 0, sizeof(planes));
        buffer.index = i;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = mOutputMemoryType;
        buffer.m.planes = planes;
        buffer.length = mOutputPlaneCount;
        ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QUERYBUF, &buffer);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYBUF);

        mOutputFrames[i].width = mWidth;
        mOutputFrames[i].height = mHeight;
        mOutputFrames[i].fourcc = mFormat;

        if (mOutputMemoryType == V4L2_MEMORY_MMAP) {
            for (uint32_t j=0; j<mOutputPlaneCount; j++) {
                // length and mem_offset are filled by VIDIOC_QUERYBUF above
                void* address = mV4l2Decoder->mmap(NULL,
                                              buffer.m.planes[j].length,
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED,
                                              buffer.m.planes[j].m.mem_offset);
                ASSERT(address);
                if (j == 0) {
                    mOutputFrames[i].data = static_cast<uint8_t*>(address);
                    mOutputFrames[i].offset[0] = 0;
                } else {
                    mOutputFrames[i].offset[j] = static_cast<uint8_t*>(address) - mOutputFrames[i].data;
                }

                //mOutputFrames[i].pitch[j] = format.fmt.pix_mp.plane_fmt[j].bytesperline;
                DEBUG("mOutputFrames[%d][%d] = %p\n", i, j, address);
            }
        }

        // feed the output frame to v4l2codec
        // FIXME, multi-plane frame are enqued multi-times
        int ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QBUF, &buffer);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QBUF);
    }

     // start output port
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    return MM_ERROR_SUCCESS;
}

int VideoDecodeV4l2::getCSDInfo(uint8_t *data, int32_t &size)
{
    ASSERT(data);
    if (!mStreamCSDMaker) {
        return -1;
    }
    int32_t i = 0;
    uintptr_t csdBuffers[1];
    int32_t csdOffsets[1], csdStrides[1];
    int32_t offset = 0;
    DEBUG("mStreamCSDMaker->getCSDCount() : %d", mStreamCSDMaker->getCSDCount());
    for (i=0; i< mStreamCSDMaker->getCSDCount(); i++) {
        MediaBufferSP csdBuf = mStreamCSDMaker->getMediaBufferFromCSD(i);
        csdBuf->getBufferInfo((uintptr_t *)csdBuffers, csdOffsets, csdStrides, 1);
        DEBUG("buffer[%d]: %p, offset: %d, stride: %d, size: %d",
            i, csdBuffers[0], csdOffsets[0], csdStrides[0], csdBuf->size());
        memcpy(data+offset, (void*)csdBuffers[0], csdBuf->size());
        offset += csdBuf->size();
    }
    size = offset;
    DEBUG("data %p, size %d", data, size);

    mp4toannexb((uint8_t*)data, data, size);
    return 0;
}

void VideoDecodeV4l2::onHandleInputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    if (mPortSettingChange) {
        INFO("port setting changed, return directly");
        return;
    }

    struct v4l2_buffer buf;
    const uint32_t kInputPlaneCount = 1;
    struct v4l2_plane planes[kInputPlaneCount];
    int ioctlRet = -1;

    if (mState != kStatePlaying && mState != kStatePlay)
        return;

    // dequeue input buffers from V4l2Codec device
    do {
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; // it indicates input buffer(raw frame) type
        buf.memory = mInputMemoryType;
        buf.m.planes = planes;
        buf.length = kInputPlaneCount;


        ioctlRet = mV4l2Decoder->ioctl(VIDIOC_DQBUF, &buf);
        if (ioctlRet == 0) {
            mInputIndces.push(buf.index);
            if (mInputMemoryType == V4L2_MEMORY_USERPTR && !mInputBufferStaging.empty()) {
                // verify the buffer is match, release input MediaBuffer
                MediaBufferSP stagingBuf = mInputBufferStaging.front();
                uintptr_t buffers[1];
                int32_t offsets[1], strides[1];
                stagingBuf->getBufferInfo((uintptr_t *)buffers, offsets, strides, 1);

                void *ptr = buf.m.planes[0].reserved;
                uintptr_t value = *((uintptr_t*) ptr);

                DEBUG("deque buffer with user_ptr: %p, staging buffer head user_ptr: %p", (void*) value, (void*)buffers[0]);
                ASSERT (value == buffers[0]);
                mInputBufferStaging.pop();
            }
        }
    } while (ioctlRet== 0);

    if (mInputIndces.empty()) {
        VERBOSE("no input buffer from v4l2codec device is available\n");
        return;
    }

    // enque input buffers
    do {
        // FIXME, not necessary to lock
        if (mEosState != kNoneEOS)
            break;

        MediaBufferSP sourceBuf;
        MediaBufferSP csdBuffer;

#if defined(__MM_YUNOS_LINUX_BSP_BUILD__) //ti no need to push sps/pps to codec, pretend to IDR frame
        if (mStreamCSDMaker) {
            mCSDBufferIndex = mStreamCSDMaker->getCSDCount();
        }
#endif
        // FIXME, sprd codec doesn't support codec_data sent in onPrepare, send sps/pps from codecdata if any
        if ( mStreamCSDMaker && MM_UNLIKELY(mCSDBufferIndex < mStreamCSDMaker->getCSDCount()) ) {
            DEBUG("feed sps/pps from codecdata with index: %d", mCSDBufferIndex);
            sourceBuf = mStreamCSDMaker->getMediaBufferFromCSD(mCSDBufferIndex);
            // FIXME, may fail to enque the buffer
            mCSDBufferIndex++;
        } else {

            mm_status_t status = mReader->read(sourceBuf);
            if(status != MM_ERROR_SUCCESS || !sourceBuf) {
                DEBUG("fail to get input data");
                if (mState == kStatePlay) {
                    // wait and retry at startup time
                    usleep(10000);
                    continue;
                } else {
                    bool scheduleRetry = true;
                    if (mLastTimeInputRetry) {
                        if (getTimeUs() - mLastTimeInputRetry < kInOutputRetryDelayUs - 500)
                            scheduleRetry = false;
                    }

                    if (scheduleRetry) {
                        DEBUG("post VDV4L2_MSG_handleInputBuffer");
                        mLastTimeInputRetry = getTimeUs();
                        postMsg(VDV4L2_MSG_handleInputBuffer, 0, NULL, kInOutputRetryDelayUs);
                    }
                    return;
                }
            }

        }

       int64_t targetTime = -1LL;
       if (sourceBuf->getMediaMeta()->getInt64(MEDIA_ATTR_TARGET_TIME, targetTime)) {
           mTargetTimeUs = targetTime;
           INFO("mTargetTimeUs %0.3f\n", mTargetTimeUs/1000000.0f);
       }

        uintptr_t buffers[1];
        int32_t offsets[1], strides[1];
        sourceBuf->getBufferInfo((uintptr_t *)buffers, offsets, strides, 1);
        uint32_t index = mInputIndces.front();

        int32_t csdOffset = 0;
        if (mPrefixCSD) {
            mPrefixCSD = false;

            if (mIsAVCcType && mForceByteStream) {
                getCSDInfo((uint8_t*)mInputBuffers[buf.index], csdOffset);
            }
        }

        if (sourceBuf->size()+csdOffset >= kMaxInputBufferSize) {
            ERROR("too big input buffer size, ignore it: %zu", sourceBuf->size()+csdOffset);
            postMsg(VDV4L2_MSG_handleInputBuffer, 0, NULL, kInOutputRetryDelayUs);
            return;
        }

        buf.index = index;
        buf.flags = 0;
        if (sourceBuf->isFlagSet(MediaBuffer::MBFT_EOS)) {
            INFO("VDV4L2-EOS, input read EOS\n");
            setEosState(kInputEOS);
            // FIXME, EOS, set data to NULL but that buffer is mapped from v4l2codec device
            buf.m.planes[0].bytesused = 0;
            buf.flags |= V4L2_BUF_FLAG_EOS;
        } else {
            if (sourceBuf->isFlagSet(MediaBuffer::MBFT_KeyFrame)) {
                buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
            }
            if (mInputMemoryType == V4L2_MEMORY_MMAP) {
                if (mIsAVCcType && mForceByteStream) {
                    mp4toannexb((uint8_t*)mInputBuffers[buf.index]+csdOffset, (uint8_t*)buffers[0], sourceBuf->size());
                } else {
                    memcpy(mInputBuffers[buf.index]+csdOffset, (void*)buffers[0], sourceBuf->size());
                }
                if (mDumpInput) {
                    mInputDataDump.dump(mInputBuffers[buf.index], sourceBuf->size()+csdOffset);
                    hexDump((uint8_t*)mInputBuffers[buf.index],
                        sourceBuf->size()+csdOffset > 64 ? 64 : sourceBuf->size()+csdOffset, 16);
                }

            } else if (mInputMemoryType == V4L2_MEMORY_USERPTR) {
                // FIXME, for mIsAVCcType, mp4toannexb
                uintptr_t *ptr = (uintptr_t*)(buf.m.planes[0].reserved);
                *ptr = buffers[0];
                mInputBufferStaging.push(sourceBuf);
            } else {
                ERROR("not supported input buffer memory type\n");
            }
            buf.m.planes[0].bytesused = sourceBuf->size()+csdOffset;
            buf.m.planes[0].m.mem_offset = 0;
            int64_t pts = sourceBuf->pts();
            INT64_TO_TIMEVAL(pts, buf.timestamp);
        }

        int32_t retryCount = 10;
        int32_t retry = 0;
        // try more times if pps and sps are not queue to codec
        if (mStreamCSDMaker && MM_UNLIKELY(mCSDBufferIndex < mStreamCSDMaker->getCSDCount())) {
            retryCount = 100;
        }

        do {
            ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QBUF, &buf);
            ASSERT(ioctlRet == 0 || ioctlRet == EAGAIN);
            if (ioctlRet == EAGAIN) {
                usleep(10*1000);
            }

            if (retry++ >= retryCount) {
                break;
            }
        } while(ioctlRet != 0);
        ASSERT(ioctlRet == 0);

        mInputBufferCount++;
        DEBUG("enqueue one input buffer[%d], mInputBufferCount=%d, size %d, dts: %0.3f, pts: %0.3f, timeval (%ld, %ld)\n",
            buf.index, mInputBufferCount, buf.m.planes[0].bytesused,
            sourceBuf->dts()/1000000.0f, sourceBuf->pts()/1000000.0f, buf.timestamp.tv_sec, buf.timestamp.tv_usec);

        mInputIndces.pop();
    }while (!mInputIndces.empty());

}

#define CHECK_SURFACE_OPS_RET(ret, funcName) do {                       \
        uint32_t my_errno = errno;                                                                          \
        VERBOSE("%s ret: %d", funcName, ret);                                 \
        if (ret) {                                                          \
            ERROR("%s failed: %s (%d)", funcName, strerror(my_errno), my_errno);  \
            return MM_ERROR_UNKNOWN;                                        \
        } else {                                                            \
            VERBOSE("%s done success\n");                                      \
        }                                                                   \
    } while(0)


const char* VideoDecodeV4l2::bufferStatusToString(BufferInfo::BufferStatus s)
{
#define MSG_NAME(status) case (status): return #status;
    switch (s) {
        MSG_NAME(BufferInfo::OWNED_BY_US);
        MSG_NAME(BufferInfo::OWNED_BY_V4L2CCODEC);
        MSG_NAME(BufferInfo::OWNED_BY_NATIVE_WINDOW);
        MSG_NAME(BufferInfo::OWNED_BY_CLIENT);
        default: return "unknown status";
    }
    return "unknown status";
}

void VideoDecodeV4l2::dbgSurfaceBufferStatus(bool gotBufferCount)
{
    if (!gotBufferCount) {
        for (size_t index = 0; index < mOutputFrames.size(); index++) {
            DEBUG("bufferInfo[%d]: mANB %x, %s\n",
                 index, mOutputFrames[index].mANB,
                 bufferStatusToString(mOutputFrames[index].mOwner));
        }
    } else {
        int nativeWindowCount = 0;
        int clientCount = 0;
        int usCount = 0;
        int codecCount = 0;
        for (size_t index = 0; index < mOutputFrames.size(); index++) {
            if (mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_NATIVE_WINDOW) {
                nativeWindowCount++;
            } else if (mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_US) {
                usCount++;
            } else if (mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_V4L2CCODEC) {
                codecCount++;
            } else if (mOutputFrames[index].mOwner == BufferInfo::OWNED_BY_CLIENT) {
                clientCount++;
            }
        }
        DEBUG("nativeWindowCount %d, usCount %d, codecCount %d, clientCount %d",
            nativeWindowCount, usCount, codecCount, clientCount);
    }

}

void VideoDecodeV4l2::dbgSurfaceBufferStatus(int line, bool forceErrorDebug)
{
    uint32_t i = 0;
    char bufferStatus[128];
    uint32_t surfaceHoldBufferCount = 0;
    static uint32_t preCount = 3; // assume surface hold 3 buffer
    for (i=0; i<mOutputFrames.size(); i++) {
        sprintf(&bufferStatus[i*3], "%d, ", mOutputFrames[i].mOwner);
        if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_NATIVE_WINDOW)
            surfaceHoldBufferCount++;
    }
    if (surfaceHoldBufferCount > preCount || forceErrorDebug) {
        ERROR("9848149: mOutputBufferCountRender: %d, current buffer status %d: %s", mOutputBufferCountRender, line, bufferStatus);
        preCount++;
    } else
        DEBUG("9848149: mOutputBufferCountRender: %d, current buffer status %d: %s", mOutputBufferCountRender, line, bufferStatus);
}

void VideoDecodeV4l2::onScheduleOutputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    bool isRender = (param1 > 0);
    struct v4l2_buffer *buf = (struct v4l2_buffer*)param2;

    if (mState == kStateStopped || mState == kStateStopping){
        DEBUG("ignore buffer[%d] in stop state.", buf->index);
        free(buf);
        return;
    }
    int ret = renderOutputBuffer(buf, isRender);
    if (ret != 0) {
        setState(kStatePaused);
        notify(kEventError, ret, 0, nilParam);
    }

    return;
}


bool VideoDecodeV4l2::allBuffersBelongToUs()
{
    dbgSurfaceBufferStatus(true);
    bool ret = true;
    for (uint32_t i = 0; i < mOutputFrames.size(); i++) {
        if (mOutputFrames[i].mOwner != BufferInfo::OWNED_BY_US &&
            mOutputFrames[i].mOwner != BufferInfo::OWNED_BY_NATIVE_WINDOW) {
            ret = false;
            break;
        }
    }

    return ret;
}

// FIXME, renderOutputBuffer doesn't run in the thread to configure WindowSurface
mm_status_t VideoDecodeV4l2::renderOutputBuffer(struct v4l2_buffer *buf, bool renderIt)
{
    int ret = 0;
    if (!buf) {
        ERROR("invalid v4l2_buffer");
        return -1;
    }

    // after stop or flush, mGeneration is changed, skip to render
    if (buf->sequence != mGeneration) {
        DEBUG("ignore v4l2_buffer: %p, generation (current : buffer), (%d : %d)", buf, mGeneration, buf->sequence);
        free(buf);
        return 0;
    }

    DEBUG("one output mOutputBufferCountRender: %d, generation: %d, v4l2_buffer[%d]: %p, render is %d, status %d",
        mOutputBufferCountRender, buf->sequence, buf->index, buf, renderIt, mOutputFrames[buf->index].mOwner);
    mOutputBufferCountRender++;
    ASSERT(mOutputFrames[buf->index].mOwner == BufferInfo::OWNED_BY_CLIENT ||
        mOutputFrames[buf->index].mOwner == BufferInfo::OWNED_BY_US); // may not write to sink component
    mOutputFrames[buf->index].mOwner = BufferInfo::OWNED_BY_US;

    dbgSurfaceBufferStatus(true);

    int fencefd = -1;
    uint32_t flags = 0;

    if(renderIt) {
        //FIXME, surface_v2 hasn't support submitBuffer well
        ret = mSurfaceWrapper->queueBuffer(mOutputFrames[buf->index].mANB, -1, flags);
        if (ret != 0) {
            ERROR("queueBuffer failed, %s(%d)", strerror(ret), ret);
            free(buf);
            return -1;
        }
        CHECK_SURFACE_OPS_RET(ret, "submitBuffer");
        mOutputFrames[buf->index].mOwner = BufferInfo::OWNED_BY_NATIVE_WINDOW;
        VERBOSE("submitBuffer done");


        MMNativeBuffer *nwb = NULL;
        int tryTimes = 0;
        do {
            ret = mSurfaceWrapper->dequeue_buffer_and_wait((&nwb), flags);
            if ( ++tryTimes == 100) {
                ERROR("obtain buffer failed after 100 times attempt, %s(%d)", strerror(ret), ret);
                free(buf);
                return -1;
            }
            if (ret == 0 && nwb != NULL)break;
            usleep(10*1000);
        } while (1);
        if (ret != 0) {
            ERROR("queueBuffer failed, %s(%d)", strerror(ret), ret);
            free(buf);
            return -1;
        }
        handle_fence(fencefd);
        CHECK_SURFACE_OPS_RET(ret, "obtainBuffer");

        uint32_t i = 0;
        bool found = false;
        for (i=0; i<mOutputFrames.size(); i++) {
            if (nwb == mOutputFrames[i].mANB) {
                buf->index = i;
                if (mOutputFrames[i].mOwner != BufferInfo::OWNED_BY_NATIVE_WINDOW) {
                    ERROR("one output 9848149: mOutputBufferCountRender: %d, buffer[%d]: %p is dequed from surface but not owned by surface. (actual owner: %d)",
                        mOutputBufferCountRender, i, nwb, mOutputFrames[i].mOwner);
                }
                mOutputFrames[i].mOwner = BufferInfo::OWNED_BY_US;
                found = true;
                break;
            }
        }
        if (!found) {
            ERROR("got an unknown buffer from surface : %p", nwb);
        }
        VERBOSE("update buffer index from dequeue of WindowSurface: %d", buf->index);
    }


    // DO NOT queue output buffer to codec if format changed
    if (mState < kStateStopping && !mPortSettingChange) {
        buf->m.userptr = (unsigned long)mm_getBufferHandle(mOutputFrames[buf->index].mANB);
        int ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QBUF, buf);
        DEBUG("enqueue one output buffer[%d]: %p, handle %p to codec",
            buf->index, mOutputFrames[buf->index].mANB, (void*)buf->m.userptr);
        mOutputFrames[buf->index].mOwner = BufferInfo::OWNED_BY_V4L2CCODEC;
        ASSERT(ioctlRet == 0 || ioctlRet == EAGAIN);
        // FIXME, handle EAGAIN
    }
    free(buf);

    if (mPortSettingChange && allBuffersBelongToUs()) {
        mPortSettingChange = false;
        INFO("all output buffers belong to us");
        return handlePortSettingChanged();
    }

    return ret;
}

/* static */ bool VideoDecodeV4l2::releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    if (!mediaBuffer)
        return false;

    bool ret = false;
    void *ptr = NULL;

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    ASSERT_RET(meta, false);
    ret = meta->getPointer("v4l2-decoder", ptr);
    ASSERT_RET(ret && ptr, false);
    VideoDecodeV4l2 *decoder = (VideoDecodeV4l2*) ptr;

    ret = meta->getPointer("v4l2-buffer", ptr);
    ASSERT_RET(ret && ptr, false);
    struct v4l2_buffer *buf = (struct v4l2_buffer*)ptr;
    ASSERT_RET(buf, false);

    // ASSERT(mediaBuffer->type() == decoder->mMediaBufferType);

    if (mediaBuffer->type() == MediaBuffer::MBT_RawVideo) {
        uint8_t *sourceBuf = NULL;
        int32_t offset = 0;
        int32_t size = 0;
        mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1);
        if (!sourceBuf)
            return false;
        DEBUG("buffer %p released", sourceBuf);
        delete []sourceBuf;
        // return true;
    }


    int32_t isRender = 0; // set to 0 means not to render as default
    ret = meta->getInt32(MEDIA_ATTR_IS_VIDEO_RENDER, isRender);
    if (decoder->mSurfaceOwnedByUs || decoder->mDecodeThumbNail) {
        isRender = false;
    }

    //ret = decoder->renderOutputBuffer(buf, isRender);
    //ASSERT(ret == 0);
    decoder->postMsg(VDV4L2_MSG_scheduleOutputBuffer, (int)isRender, buf, 0);

    return true;
}

void VideoDecodeV4l2::onHandleOutputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    if (mState != kStatePlaying && mState != kStatePlay)
        return;

    int ioctlRet = -1;
    int64_t pts = 0;

    do {
        struct v4l2_buffer *buf = (struct v4l2_buffer*)calloc(1, sizeof(struct v4l2_buffer));

        buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //decode output
        buf->memory = mOutputMemoryType;
        buf->m.planes = NULL;
        buf->length = mOutputPlaneCount;

        ioctlRet = mV4l2Decoder->ioctl(VIDIOC_DQBUF, buf);
        /* there are two scenario to reach output EOS state
         * 1. decent way: V4l2Device return us output buffer with EOS flag V4L2_BUF_FLAG_EOS
         * 2. work around: there is no more output buffer after continually tried 30 times (after input port signal EOS).
         *    we should have a way for ending anyway
         */
        // DEBUG("ioctlRet %d, errno %d(%s), mEosState %d, buf->flags 0x%0x", ioctlRet, errno, strerror(errno), mEosState, buf->flags);
        if (ioctlRet == -1) {
            #ifndef __MM_YUNOS_LINUX_BSP_BUILD__
            mOutputDequeueRetryCount++;
            #endif
            MMAutoLock locker(mLock);
            if (mEosState == kInputEOS) {
                if (mOutputDequeueRetryCount > 30 ||
                    (buf->flags & V4L2_BUF_FLAG_EOS)) {
                    DEBUG("VDV4L2-EOS, reach output EOS");
                    mEosState = kOutputEOS;
                    MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_BufferIndexV4L2);
                    mediaOutputBuffer->setFlag(MediaBuffer::MBFT_EOS);
                    mediaOutputBuffer->setSize(0);

                    mm_status_t ret = mWriter->write(mediaOutputBuffer);
                    if ((ret != MM_ERROR_SUCCESS) && (ret != MM_ERROR_EOS)) {
                        WARNING("decoder fail to write Sink");
                    }
                    // FIXME, ok?
                    DEBUG("VDV4L2-EOS, setDevicePollInterrupt");
                    setState(kStatePaused);
                    mV4l2Decoder->setDevicePollInterrupt();
                    if (buf->flags & V4L2_BUF_FLAG_ERROR) {
                        INFO("output eos buffer is invalid");
                        buf->sequence = -1;
                    }
                    free(buf);
                    return;
                } else {
                    free(buf);
                    /* - timed msg doesn't work well in retry scenario, because when there are already more than one VDV4L2_MSG_handleOutputBuffer
                     *   in MMMsgThread. the actual wait time will be divided by the # of existing VDV4L2_MSG_handleOutputBuffer
                     * - input port has already been in EOS state when running here, simple sleep doesn't impact much
                     */
                    usleep(5000);
                    postMsg(VDV4L2_MSG_handleOutputBuffer, 0, NULL, 0);
                }
            }

            break;
        } else {
            mOutputDequeueRetryCount= 0;
        }

        mOutputFrames[buf->index].mOwner = BufferInfo::OWNED_BY_US;
    #ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        if (mPortSettingChange) {
            INFO("keep this output buffer in port setting changed, index %d", buf->index);
            if (allBuffersBelongToUs()) {
                INFO("all output buffers belong to us");
                handlePortSettingChanged();
                mPortSettingChange = false;
            }
            break;
        }
    #endif

    #ifdef __MM_YUNOS_YUNHAL_BUILD__
        if (mDumpOutput)
            dumpBuffer((MMBufferHandleT)mm_getBufferHandle(mOutputFrames[buf->index].mANB),
                       mOutputFrames[buf->index].mANB->stride,
                       mOutputFrames[buf->index].mANB->height);
    #endif
        MediaBufferSP mediaBuffer;
        if (mSurfaceOwnedByUs || mDecodeThumbNail) {
            mediaBuffer = createRawMediaBuffer(buf);
        } else {
            mediaBuffer = createHwMediaBuffer(buf);
        }

        mm_status_t status = MM_ERROR_SUCCESS;
        TIMEVAL_TO_INT64(pts, buf->timestamp);

        INFO("dequeue one output frame[%d], mOutputBufferCount: %d, generation: %d, v4l2_buffer: %p, wl_buffer: %p, pts: %0.3f, timestamp: (%ld, %ld)\n",
            buf->index, mOutputBufferCount, mGeneration, buf, (void*)buf->m.userptr, pts/1000000.0f, buf->timestamp.tv_sec, buf->timestamp.tv_usec);
        mOutputBufferCount++;

        if (pts < mTargetTimeUs) {
            DEBUG("ignore buffer[%d]: %0.3f, mTargetTimeUs %0.3f",
                buf->index, pts/1000000.0f, mTargetTimeUs/1000000.0f);
            break; //skip write to sink, which means not to render
        } else {
            mTargetTimeUs = -1ll;
        }

        do {
            status = mWriter->write(mediaBuffer);
        } while (status == MM_ERROR_AGAIN);
        // FIXME, why MM_ERROR_ASYNC?
        ASSERT(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC);
        mOutputFrames[buf->index].mOwner = BufferInfo::OWNED_BY_CLIENT;

    }    while (ioctlRet== 0);
}

MediaBufferSP VideoDecodeV4l2::createHwMediaBuffer(v4l2_buffer *buf)
{
    int64_t pts = 0;
    MediaBufferSP mediaBuffer = MediaBuffer::createMediaBuffer(mMediaBufferType);

    if (buf->flags & V4L2_BUF_FLAG_EOS) {
        mediaBuffer->setFlag(MediaBuffer::MBFT_EOS);
        mEosState = kOutputEOS;
        setState(kStatePaused);
        DEBUG("VDV4L2-EOS, setDevicePollInterrupt");
        mV4l2Decoder->setDevicePollInterrupt();
        // update state after render this frame
    }

    MediaMetaSP meta = MediaMeta::create();

    meta->setPointer("v4l2-buffer", buf);
    meta->setPointer("v4l2-decoder", this);

    buf->sequence = mGeneration;

    TIMEVAL_TO_INT64(pts, buf->timestamp);
    mediaBuffer->setPts(pts);
    mediaBuffer->setMediaMeta(meta);
    mediaBuffer->addReleaseBufferFunc(releaseOutputBuffer);

    return mediaBuffer;
}

MediaBufferSP VideoDecodeV4l2::createRawMediaBuffer(v4l2_buffer *buf)
{
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    uint32_t flags = 0;
    int x_stride = 0;
    int y_stride = 0;

    MMBufferHandleT target = (MMBufferHandleT)mm_getBufferHandle(mOutputFrames[buf->index].mANB);

#ifdef YUNOS_BOARD_intel
#if defined(__USING_YUNOS_MODULE_LOAD_FW__)
    if (!mYalloc) {
        mModule = (struct __vendor_module_t*) LOAD_VENDOR_MODULE(YALLOC_VENDOR_MODULE_ID);
        yalloc_open(mModule,&mYalloc);
    }
#else
    if (!mYalloc) {
        yalloc_open(&mYalloc);
    }
#endif
    ASSERT(mYalloc);
    int ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_X_STRIDE, target, &x_stride);
    MMASSERT(ret == 0);
    ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_Y_STRIDE, target, &y_stride);
    MMASSERT(ret == 0);
#elif defined(__MM_BUILD_DRM_SURFACE__)
    mSurfaceWrapper->query(QUERY_GET_X_STRIDE, &x_stride, flags);
    mSurfaceWrapper->query(QUERY_GET_Y_STRIDE, &y_stride, flags);
#else
    x_stride = mWidth;
    y_stride = mHeight;
#endif
    DEBUG("x_stride %d, y_stride %d, mWidth %d, mHeight %d", x_stride, y_stride, mWidth, mHeight);
    int32_t offset = 0;
    int32_t size = mWidth*mHeight*3/2;
    uint8_t *data = new uint8_t[size];
    if (!data) {
        ERROR("out of memory");
        return buffer;
    }

    void *vaddr = NULL;
#ifdef __MM_YUNOS_YUNHAL_BUILD__
    static YunAllocator &yunAllocator(YunAllocator::get());
    yunAllocator.map(target, ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
                  0, 0, x_stride, y_stride, &vaddr);
    memcpy(data, vaddr, size);
#elif __MM_YUNOS_LINUX_BSP_BUILD__
    mSurfaceWrapper->mapBuffer(target, 0, 0, mWidth, mHeight, &vaddr, flags);
    DEBUG("vaddr %p", vaddr);
    uint8_t *addr = (uint8_t*)vaddr;
    if (mCodecFormat == V4L2_PIX_FMT_H264) {
        uint32_t dataOffset = 24*x_stride + 32;
        for (int i = 0; i < mHeight; i++) {
            memcpy(data+mWidth*i, addr+dataOffset+x_stride*i, mWidth);
        }

        dataOffset = 12*x_stride + 32 + x_stride*y_stride;
        for (int i = 0; i < mHeight/2; i++) {
            memcpy(data+mWidth*(mHeight+i), addr+dataOffset+x_stride*i, mWidth);
        }
    } else {
        WARNING("not support for other codec");
    }

#endif

    #if 0
    static int count = 0;
    if (count == 0) {
        DataDump dumper("/data/v4l2_raw.yuv");
        dumper.dump(data, size);
        count++;
    }
    #endif

#ifdef __MM_YUNOS_YUNHAL_BUILD__
    yunAllocator.unmap(target);
#else
    mSurfaceWrapper->unmapBuffer(target, flags);
#endif
    buffer->setBufferInfo((uintptr_t *)&data, &offset, &size, 1);
    buffer->setSize(size);
    buffer->addReleaseBufferFunc(releaseOutputBuffer);
    buf->sequence = mGeneration;


    MediaMetaSP outMeta = buffer->getMediaMeta();
    outMeta->setInt32(MEDIA_ATTR_WIDTH, mWidth);
    outMeta->setInt32(MEDIA_ATTR_HEIGHT, mHeight);

    outMeta->setPointer("v4l2-buffer", buf);
    outMeta->setPointer("v4l2-decoder", this);

    //if (mFormat == YUN_HAL_FORMAT_NV12) {
    outMeta->setInt32(MEDIA_ATTR_COLOR_FOURCC, 'NV12');
    outMeta->setInt32(MEDIA_ATTR_STRIDE, x_stride);
    outMeta->setInt32(MEDIA_ATTR_STRIDE_X, x_stride);
    outMeta->setInt32(MEDIA_ATTR_STRIDE_Y, y_stride);
#endif
    return buffer;
}
void VideoDecodeV4l2::onStartInput(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();
    uint32_t i = 0;
    MMAutoLock locker(mLock); // big locker
    int ioctlRet = -1;

    INFO("mDeferMessage.size %d", mDeferMessages.size());
    mDeferMessages.clear();

    DEBUG("mConfigured:%d\n", mConfigured);
    if (!mConfigured) {
        mState = kStatePlay; // setState(kStatePlay);

        // start input port
        DEBUG("input port stream on\n");
        __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMON, &type);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

        // setup input buffers
        struct v4l2_requestbuffers reqbufs;
        memset(&reqbufs, 0, sizeof(reqbufs));
        reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        reqbufs.memory = mInputMemoryType;
        reqbufs.count = kInputBufferCount;
        ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
        ASSERT(reqbufs.count>0);
        mInputQueueCapacity = reqbufs.count;
        INFO("mInputQueueCapacity: %d", mInputQueueCapacity);

        if (mInputMemoryType == V4L2_MEMORY_MMAP) {
            mInputBuffers.resize(mInputQueueCapacity);
            for (i=0; i<mInputQueueCapacity; i++) {
                struct v4l2_plane planes[kInputPlaneCount];
                struct v4l2_buffer buffer;
                memset(&buffer, 0, sizeof(buffer));
                memset(planes, 0, sizeof(planes));
                buffer.index = i;
                buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                buffer.memory = mInputMemoryType;
                buffer.m.planes = planes;
                buffer.length = kInputPlaneCount;
                ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QUERYBUF, &buffer);
                CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYBUF);

                // length and mem_offset should be filled by VIDIOC_QUERYBUF above
                void* address = mV4l2Decoder->mmap(NULL,
                                              buffer.m.planes[0].length,
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED,
                                              buffer.m.planes[0].m.mem_offset);
                ASSERT(address);
                mInputBuffers[i] = static_cast<uint8_t*>(address);
                DEBUG("mInputBuffers[%d] = %p", i, mInputBuffers[i]);
            }
        }

        // feed input frames first
        for (i=0; i<mInputQueueCapacity; i++) {
            mInputIndces.push(i);
        }
        postMsg(VDV4L2_MSG_handleInputBuffer, 0, NULL, 0);
    }
    mEosState = kNoneEOS;
    mTargetTimeUs = -1ll;

    postMsg(VDV4L2_MSG_startOutput, 0, NULL, 0);

}

/*static*/int VideoDecodeV4l2::getTransform(int degree) {
    int transform = 0;
    #ifndef __MM_YUNOS_LINUX_BSP_BUILD__
        #if defined(YUNOS_ENABLE_UNIFIED_SURFACE) || defined(__MM_YUNOS_YUNHAL_BUILD__)
        switch (degree) {
            case 0: transform = 0; break;
            case 90: transform = YUN_HAL_TRANSFORM_ROT_90; break;
            case 180: transform = YUN_HAL_TRANSFORM_ROT_180; break;
            case 270: transform = YUN_HAL_TRANSFORM_ROT_270; break;
            default: transform = 0; break;
        }
        #else
        switch (degree) {
            case 0: transform = 0; break;
            case 90: transform = WS_OUTPUT_TRANSFORM_90; break;
            case 180: transform = WS_OUTPUT_TRANSFORM_180; break;
            case 270: transform = WS_OUTPUT_TRANSFORM_270; break;
            default: transform = 0; break;
        }
        #endif
    #endif

    return transform;
}

mm_status_t VideoDecodeV4l2::allocateOutputBuffersFromNativeWindow()
{
    int ioctlRet = -1;
    // FIXME, should we query output buffer count after configure windowsurface?
    // Number of output buffers we need.
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_G_CTRL, &ctrl);
    CHECK_V4L2_CMD_RESULT_RET(ioctlRet, VIDIOC_G_CTRL);
    mOutputQueueCapacity = ctrl.value ? ctrl.value : mOutputQueueCapacity;
    mVideoDpbSize = ctrl.value;

    // setup output buffers
    mm_status_t ret = configOutputSurface();
    if (ret != MM_ERROR_SUCCESS) {
        return ret;
    }
     // start output port
    __u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoDecodeV4l2::cancelBufferToNativeWindow(BufferInfo *info)
{
    ASSERT_EQ((int)info->mOwner, BufferInfo::OWNED_BY_US);
    uint32_t flags = 0;

    int err;
    DEBUG("cancel buffer %p", info->mANB);
    err = mSurfaceWrapper->cancel_buffer(info->mANB, -1, flags);
    CHECK_SURFACE_OPS_RET(err, "native_cancel_buffer");
    info->mOwner = BufferInfo::OWNED_BY_NATIVE_WINDOW;

    return err ? MM_ERROR_OP_FAILED : MM_ERROR_SUCCESS;

}

bool VideoDecodeV4l2::setSurface()
{
    if (mSurfaceTexture) {
        // SurfaceTexture is set by client
        mSurfaceWrapper = new SurfaceTextureWrapper(mSurfaceTexture);
        if (!(mSurfaceTexture->listenerExisted())) {
            mSurfaceTextureListener.reset(new V4l2TexureListener(this));
            mSurfaceTexture->setListener(mSurfaceTextureListener.get());
        }
    } else if (mSurface) {
        // Surface is set by client
    #if !defined(__MM_YUNOS_LINUX_BSP_BUILD__)
        mSurfaceWrapper = new WindowSurfaceWrapper(mSurface);
    #endif
    } else {
#   if (defined(__MM_YUNOS_YUNHAL_BUILD__))
#       if defined(YUNOS_BOARD_sprd)
        mSurface = createWaylandWindow2(mWidth, mHeight);
        ASSERT(mSurface);
        mSurfaceWrapper = new WindowSurfaceWrapper(mSurface);
        mSurfaceOwnedByUs = true;
#       else
        // mSurfaceTexture and mSurface is NULL
        mSurfaceTexture = new YunOSMediaCodec::MediaSurfaceTexture();
        mSurfaceWrapper = new SurfaceTextureWrapper(mSurfaceTexture);
        mSurfaceOwnedByUs = true;
        DEBUG("create mSurfaceTexture");
#       endif
#   endif
    }

#ifdef __MM_BUILD_DRM_SURFACE__
        if (!mSurfaceWrapper) {
            mSurfaceWrapper = new YunOSMediaCodec::WlDrmSurfaceWrapper((omap_device *)NULL);
        }
#elif defined(__MM_BUILD_VPU_SURFACE__)
    if (!mSurfaceWrapper) {
        mSurfaceWrapper = new YunOSMediaCodec::VpuSurfaceWrapper();
    }
#endif
    return true;
}

mm_status_t VideoDecodeV4l2::configOutputSurface()
{
    int ret = 0;
    uint32_t minUndequeuedBuffers = kMinUndequeuedBuffs;
    struct v4l2_buffer buffer;
    MMNativeBuffer *nwb = NULL;
    uint32_t flags = 0;
    // FIXME, add support for dynamic resolution change, assume we are in the process of starting;
    ASSERT(mState == kStatePlay || mState == kStatePlaying);

    // 1. config WindowSurface
    INFO("config mSurface with size %dx%d\n", mWidth, mHeight);
    // FIXME, is Surface & NativeWindow same for libgui_2?
    ret = mSurfaceWrapper->set_buffers_dimensions(mWidth, mHeight, flags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_dimensions");

    if (mRotationDegrees != 0) {
        uint32_t transform = getTransform(mRotationDegrees);
        if (transform > 0) {
            ret = mSurfaceWrapper->set_buffers_transform(
                    transform, flags);
            CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_transform");
        }
    }

    // FIXME, hardcode mFormat to NV12
#if  defined(__MM_YUNOS_YUNHAL_BUILD__)
    mFormat = YUN_HAL_FORMAT_NV12;
#endif
    INFO("native_set_buffers_format 0x%x", mFormat);
    ret = mSurfaceWrapper->set_buffers_format(mFormat, flags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffers_format");

    // FIXME, add crop support, getGraphicBufferUsage() from v4l2codec
#if  defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    uint32_t bufferUsage = YALLOC_FLAG_HW_VIDEO_DECODER | YALLOC_FLAG_HW_TEXTURE | YALLOC_FLAG_HW_RENDER;
    #ifdef YUNOS_BOARD_sprd
        bufferUsage = bufferUsage | ALLOC_USAGE_PREF(SW_READ_MASK);
    #endif

#elif defined(__MM_YUNOS_CNTRHAL_BUILD__)
    uint32_t bufferUsage = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP;
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    uint32_t bufferUsage = 0;
    if (mCodecFormat == V4L2_PIX_FMT_H264) {
        bufferUsage |= DRM_NAME_USAGE_HW_DECODE_PAD_H264;
    } else if (mCodecFormat == V4L2_PIX_FMT_MPEG4 || mCodecFormat == V4L2_PIX_FMT_H263) {
        bufferUsage |= DRM_NAME_USAGE_HW_DECODE_PAD_MPEG4;
    } else if (mCodecFormat == V4L2_PIX_FMT_MPEG2) {
        bufferUsage |= DRM_NAME_USAGE_HW_DECODE_PAD_MPEG2;
    } else if (mCodecFormat == V4L2_PIX_FMT_VC1) {
        bufferUsage |= DRM_NAME_USAGE_HW_DECODE_PAD_VC1;
    }
#endif
    ret = mSurfaceWrapper->set_usage(bufferUsage, flags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_usage");

    // FIXME, query minUndequeuedBuffers from surface
    INFO("minUndequeuedBuffers=%d", minUndequeuedBuffers);
    // enlarge output frame queue size considering surface's minUndequeuedBuffers and extra buffer
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;

    for (uint32_t extraBuffers = kExtraOutputFrameCount; /*condition inside*/; extraBuffers--) {
        DEBUG("mOutputQueueCapacity =%u, minUndequeuedBuffers=%u, extraBuffers=%u",
            mOutputQueueCapacity, minUndequeuedBuffers, extraBuffers);
        uint32_t newBufferCount = mOutputQueueCapacity;
        if (!mDecodeThumbNail) {
            newBufferCount += minUndequeuedBuffers + extraBuffers;
        }
        reqbufs.count = newBufferCount;
        DEBUG("try to set output buffer count=%u", newBufferCount);
        int ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
        if (ioctlRet == 0) {
            mOutputQueueCapacity = reqbufs.count;
            DEBUG("final buffer count: mOutputQueueCapacity: %d, (suggest) newBufferCount: %d",
                mOutputQueueCapacity, newBufferCount);
            break;
        }

        /* exit condition */
        if (extraBuffers == 0) {
            ERROR("fail to set output frame queue size\n");
            return MM_ERROR_UNKNOWN;
        }
    }


    // report memory
    // hard code, calculate according to color format in future
    int32_t outputSize = mWidth * mHeight * 3 / 2;
    int32_t memorySize = mInputQueueCapacity * kMaxInputBufferSize +
        mOutputQueueCapacity * outputSize;
    DEBUG("memory size %d report", memorySize);
    notify(kEventInfo, kEventCostMemorySize, memorySize, nilParam);
    if (mMemorySize > 0 && memorySize > mMemorySize) {
        DEBUG("meomrySize %d, mMemorySize %d", memorySize, mMemorySize);
        return MM_ERROR_NO_MEM;
    }


    // set buffer count to 0 to release previous buffers
    ret = mSurfaceWrapper->set_buffer_count(0, flags);
    ret = mSurfaceWrapper->set_buffer_count(mOutputQueueCapacity, flags);
    CHECK_SURFACE_OPS_RET(ret, "native_set_buffer_count");
    INFO("final output buffer count=%lu", mOutputQueueCapacity);

    // 2. alloc output buffer
    // Dequeue surface buffers and send them to V4l2codec
    mOutputFrames.clear();

    // 3. feed output buffer to v4l2codec
    // FIXME, the canceled buffer to windowsurface is required to send to v4l2codec at the begining as well
    for (uint32_t i = 0; i < mOutputQueueCapacity; i++) {
        ret = mSurfaceWrapper->dequeue_buffer_and_wait(&nwb, flags);
        //handle_fence(fencefd);
        if (ret) {
            ERROR("dequeue buffer failed, %s(%d)", strerror(ret), ret);
            return MM_ERROR_UNKNOWN;
        }

        memset(&buffer, 0, sizeof(buffer));
        buffer.m.userptr = (unsigned long)mm_getBufferHandle(nwb);
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.index = i;
        buffer.memory = mOutputMemoryType;
        buffer.length = mOutputPlaneCount;
        DEBUG("obtainBuffer: %p, handle %p", nwb, (void*)buffer.m.userptr);

        ret = mV4l2Decoder->ioctl(VIDIOC_QBUF, &buffer);
        CHECK_V4L2_CMD_RESULT(ret, VIDIOC_QBUF);

        // FIXME(seem not necessary), track buffer ownership of ANativeWindowBuffer
        mOutputFrames.push_back(nwb);
        mOutputFrames[i].mOwner = BufferInfo::OWNED_BY_V4L2CCODEC;
    }

    // 4. FIXME, deque some buffer and cancelBuffer them to WindowSurface
    for (uint32_t i = 0; !mDecodeThumbNail && i < minUndequeuedBuffers; i++) {
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = mOutputMemoryType;
        buffer.length = mOutputPlaneCount;

        ret = mV4l2Decoder->ioctl(VIDIOC_DQBUF, &buffer);
        ASSERT(ret == 0);

        DEBUG("Calling dropBuffer on buffer %p", mOutputFrames[i].mANB);
        ret = mSurfaceWrapper->cancel_buffer(mOutputFrames[buffer.index].mANB, -1, flags);
        mOutputFrames[buffer.index].mOwner = BufferInfo::OWNED_BY_NATIVE_WINDOW;
        CHECK_SURFACE_OPS_RET(ret, "native_cancel_buffer");
    }

    return MM_ERROR_SUCCESS;
}

void VideoDecodeV4l2::onStartOutput(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();
    int ioctlRet = -1;
    MMAutoLock locker(mLock); // big locker

    if (!mConfigured) {
        // query (output) video resolution
        struct v4l2_format format;
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        // we give the hint to v4l2codec, while they decide the final output format
        format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        int32_t count = 50;
        int32_t tryCount = 0;
        do {
            ioctlRet = mV4l2Decoder->ioctl(VIDIOC_G_FMT, &format);
            int32_t my_errno = errno;
            INFO("ioctlRet: %d", ioctlRet);
            if (ioctlRet == EAGAIN) {
                INFO("ioctl() failed: VIDIOC_G_FMT, haven't get video resolution during start yet, waiting");
                usleep(3000);
            } else if (ioctlRet) {
                if (tryCount++ < count) {
                    if (my_errno == EINVAL) {
                        usleep(10000);
                        WARNING("not got video resolution yet, try again ...");
                        continue;
                    }
                }
                ERROR("got unexpected ret: %d with errno: %s(%d) when waiting for output video format, tryCount %d\n",
                    ioctlRet, strerror(my_errno), my_errno, tryCount);
                notify(kEventStartResult, MM_ERROR_UNKNOWN, 0, nilParam);
                return;
            }
        } while (ioctlRet);

        mOutputPlaneCount = format.fmt.pix_mp.num_planes;
        ASSERT(mOutputPlaneCount == 2);
        mWidth = format.fmt.pix_mp.width;
        mHeight = format.fmt.pix_mp.height;
        mFormat = format.fmt.pix_mp.pixelformat;
        ASSERT(mWidth && mHeight);
        DEBUG("got video property: mWidth: %d, mHeight: %d, mFormat: 0x%x, mOutputPlaneCount: %d", mWidth, mHeight, mFormat, mOutputPlaneCount);
        // FIXME, should we query output buffer count after configure windowsurface?
        // Number of output buffers we need.

        // setup output buffers
        setSurface();
        mm_status_t status = allocateOutputBuffersFromNativeWindow();
        if (status != MM_ERROR_SUCCESS) {
            DEBUG("allocate outout buffer failed, status %d", status);
            notify(kEventStartResult, status, 0, nilParam);
            return;
        }

        mDevicePollThread.reset(new DevicePollThread(this), MMThread::releaseHelper);
        // FIXME, add SP to handle create and destroy
        mDevicePollThread->create();
        mConfigured = true;
    }

    mState = kStatePlaying;
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mV4l2Decoder->clearDevicePollInterrupt();
    mDevicePollThread->signalContinue();
}


void VideoDecodeV4l2::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();

    if (mState != kStatePlaying) {
        WARNING("already paused, just return");
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    }

    setState(kStatePaused);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoDecodeV4l2::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();

    if (mState != kStatePaused) {
        ERROR("invalid resume command, not in kStatePaused: mState: %d", mState);
        // notify(kEventResumed, MM_ERROR_OP_FAILED, 0, nilParam);
    }

    setState(kStatePlaying);
    mEosState = kNoneEOS;
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    mV4l2Decoder->clearDevicePollInterrupt();
    postMsg(VDV4L2_MSG_handleInputBuffer, 0, NULL, 0);

    mDevicePollThread->signalContinue();
}

void VideoDecodeV4l2::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();
    // MMAutoLock locker(mLock);
    if (mPortSettingChange) {
        INFO("defer stop in port settint changed");
        deferMessage(VDV4L2_MSG_stop, 0, NULL, rspId);
        return;
    }
    int ioctlRet = -1;

    DEBUG("mConfigured=%d, mState=%d\n", mConfigured, mState);
    if (!mConfigured || mState == kStateStopped || mState == kStateStopping){
        mReader.reset();
        mWriter.reset();
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    setState(kStateStopping);

    mV4l2Decoder->setDevicePollInterrupt();
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
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    // stop input port
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // stop output port
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // skip output buffers in sink component to render
    updateBufferGeneration();

    mReader.reset();
    mWriter.reset();
    // FIXME, lock?
    while(!mInputBufferStaging.empty())
        mInputBufferStaging.pop();

    dbgSurfaceBufferStatus();
    for (uint32_t i = 0; i < mOutputQueueCapacity; i++) {
        if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_V4L2CCODEC ||
            mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_CLIENT) {
            // stream off is set, set buffer owner to US
            mOutputFrames[i].mOwner = BufferInfo::OWNED_BY_US;
        } else if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_NATIVE_WINDOW) {
            continue;
        }
        cancelBufferToNativeWindow(&mOutputFrames[i]);
    }


    setState(kStateStopped);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    mConfigured = false;

    if (mSurfaceTexture) {
        mSurfaceTexture->setListener(NULL);
        mSurfaceTextureListener.reset();
    #ifdef YUNOS_BOARD_intel
        if (mSurfaceOwnedByUs) {
            delete mSurfaceTexture;
            mSurfaceTexture = NULL;
        }
    #endif
    }
#if (defined(__MM_YUNOS_YUNHAL_BUILD__) && defined(YUNOS_BOARD_sprd))
    if (mSurfaceOwnedByUs && mSurface) {
        destroyWaylandWindow2(mSurface);
        mSurfaceOwnedByUs = false;
        mSurface = NULL;
    }
#endif
    mTargetTimeUs = -1ll;
}

void VideoDecodeV4l2::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();
    if (mPortSettingChange) {
        INFO("defer stop in port settint changed");
        deferMessage(VDV4L2_MSG_flush, 0, NULL, rspId);
        return;
    }
    int ioctlRet = -1;
    uint32_t i = 0;

    if (!mConfigured) {
        notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    DEBUG("do_real_flush");
    // #### flush buffers in V4l2CodecDevice
    // stop input port
    DEBUG("flush: input port stream off");
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // stop output port
    DEBUG("flush: output port stream off");
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // re-start input port
    DEBUG("flush: input port stream on");
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    // re-start output port
    DEBUG("flush: output port stream on");
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Decoder->ioctl(VIDIOC_STREAMON, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

    // skip output buffers in sink component to render
    updateBufferGeneration();

    // #### reset buffer status
    // clear client side buffer
    while(!mInputBufferStaging.empty())
        mInputBufferStaging.pop();
    while(!mInputIndces.empty())
        mInputIndces.pop();

    // the ownership of input buffer is client
    for (i=0; i<mInputQueueCapacity; i++) {
        mInputIndces.push(i);
    }

    // feed output frame to V4L2Codec
    for (i=0; i<mOutputQueueCapacity; i++) {
        struct v4l2_buffer buffer;

        if (mOutputFrames[i].mOwner == BufferInfo::OWNED_BY_NATIVE_WINDOW)
            continue;

        memset(&buffer, 0, sizeof(buffer));
        buffer.index = i;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = mOutputMemoryType;
        buffer.m.userptr = (unsigned long)mm_getBufferHandle(mOutputFrames[i].mANB);
        buffer.length = mOutputPlaneCount;

        int ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QBUF, &buffer);
        mOutputFrames[i].mOwner = BufferInfo::OWNED_BY_V4L2CCODEC;
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QBUF);
    }

    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoDecodeV4l2::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK1();

    // FIXME, onStop wait until the pthread_join of mDevicePollThread
    onStop(param1, param2, rspId);

    // close device
    mV4l2Decoder.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

// boilplate for MMMsgThread and Component
mm_status_t VideoDecodeV4l2::init()
{
    FUNC_TRACK1();
    int ret = MMMsgThread::run();
    if (ret)
        return MM_ERROR_OP_FAILED;

    return MM_ERROR_SUCCESS;
}

void VideoDecodeV4l2::uninit()
{
    FUNC_TRACK1();
    MMMsgThread::exit();
}

const char * VideoDecodeV4l2::name() const
{
    return mComponentName.c_str();
}

mm_status_t VideoDecodeV4l2::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;

        if ( !strcmp(item.mName, MEDIA_ATTR_VIDEO_SURFACE) ) {
            if ( item.mType != MediaMeta::MT_Pointer ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mSurface = (WindowSurface*)item.mValue.ptr;
            // FIXME, is lock required?
            mOutputFormat->setPointer(item.mName, item.mValue.ptr);
            INFO("key: %s, value: %p\n", item.mName, item.mValue.ptr);
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_VIDEO_SURFACE_TEXTURE) ) {
            if ( item.mType != MediaMeta::MT_Pointer ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mSurfaceTexture = (MediaSurfaceTexture*)item.mValue.ptr;
            INFO("key: %s, value: %p\n", item.mName, item.mValue.ptr);
        }
        else if ( !strcmp(item.mName, "memory-size") ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }

            mMemorySize = item.mValue.ii;
            INFO("key: %s, value: %d\n", item.mName, mMemorySize);
            continue;
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_DECODE_MODE) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mDecodeThumbNail = !strcmp(item.mValue.str, MEDIA_ATTR_DECODE_THUMBNAIL) ? true : false;
            INFO("key: %s, value: %d\n", item.mName, mDecodeThumbNail);
            if (mCodecFormat == V4L2_PIX_FMT_MPEG2) {
                mDecodeThumbNail = false;
                DEBUG("disable thumbnail mode in mpeg2");
            }
            continue;
        }
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoDecodeV4l2::getParameter(MediaMetaSP & meta) const
{
    FUNC_TRACK();
    WARNING("setParameter isn't supported yet\n");
    return MM_ERROR_SUCCESS;
}

void VideoDecodeV4l2::dumpBuffer(MMBufferHandleT target, int w, int h) {
    void *vaddr = NULL;
#ifdef __MM_YUNOS_YUNHAL_BUILD__
    static YunAllocator &allocator(YunAllocator::get());

    allocator.map(target, ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
                  0, 0, w, h, &vaddr);
#endif
    if (!vaddr) {
        ERROR("map fail, abort dump");
        return;
    }

    // debug the buffer handle/address of surface. for example buffer handle changes after resize
    static uint32_t frameCount = 0;
    typedef struct {
        void* target;
        void * addr;
     } TargetAddrMap;
    static std::vector<TargetAddrMap> bufferMaps;
    uint32_t i=0;

    frameCount++;
    bool targetMatched = false, addrMatched = false;
    for (i=0; i<bufferMaps.size(); i++) {
        if (bufferMaps[i].target == target) {
            targetMatched = true;
            if (bufferMaps[i].addr == vaddr)
                addrMatched = true;
        }
    }
    if (!targetMatched || ! addrMatched) {
        TargetAddrMap map;
        map.target = (void*)target;
        map.addr = vaddr;
        bufferMaps.push_back(map);
        DEBUG("frameCount: %d, index: %d, target: %p, vaddr: %p, (%d, %d)", frameCount, bufferMaps.size(), target, vaddr, targetMatched, addrMatched);
    }

    if (!mOutputDataDump) {
        std::string filename;
        std::stringstream ss;

        ss << "/data/v4l2dec_" << w << "x" << h << "_NV12" << std::endl;
        ss >> filename;
        mOutputDataDump = new DataDump(filename.c_str());
    }

    int size = w * h * 3 / 2;

    mOutputDataDump->dump(vaddr, size);
#ifdef __MM_YUNOS_YUNHAL_BUILD__
    allocator.unmap(target);
#endif
}

// //////// for component factory
extern "C" {
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    FUNC_TRACK();
    YUNOS_MM::VideoDecodeV4l2 *VideoDecodeV4l2Component = new YUNOS_MM::VideoDecodeV4l2(mimeType, isEncoder);
    if (VideoDecodeV4l2Component == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(VideoDecodeV4l2Component);
}


void releaseComponent(YUNOS_MM::Component *component) {
    FUNC_TRACK();
    delete component;
}
} // extern "C"

} // YUNOS_MM

