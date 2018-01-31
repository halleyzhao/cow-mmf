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

#include "video_encode_v4l2.h"
#include <linux/videodev2.h>
#include <unistd.h>
#include <errno.h>
#include  <sys/mman.h>
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include <system/window.h>
#elif defined(__MM_YUNOS_YUNHAL_BUILD__)
#include <YunAllocator.h>
#ifdef YUNOS_BOARD_intel
#include <yalloc_drm.h>
#endif
#endif

#if defined(__MM_YUNOS_LINUX_BSP_BUILD__)
#include <xf86drm.h>
#include <omap_drm.h>
#include <omap_drmif.h>
#endif
#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"
#include "cow_util.h"

MM_LOG_DEFINE_MODULE_NAME("VEV4L2");
// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

static const char* encodedDataDumpFile = "/tmp/encoded.h264";
static DataDump encodedDataDump(encodedDataDumpFile);
static const char* yuvDataDumpFile = "/tmp/raw.yuv";
static DataDump rawDataDump(yuvDataDumpFile);
#if defined(__MM_YUNOS_LINUX_BSP_BUILD__)
const uint32_t kOutputBufferCount = 3;
#else
const uint32_t kOutputBufferCount = 8;
#endif
// #define I_FRAME_SKIP_SPS_PPS

namespace YUNOS_MM {
static const char * COMPONENT_NAME = "VideoEncodeV4l2";
static const char * MMTHREAD_NAME = "VideoEncodeV4l2::DevicePollThread";
/* when there is no buffer to process, schedule next run with duration comparable to that of video frame.
 * when there is some buffer to process, schedule next process soon: 1/4T.
 * we do not process all available buffer in one run, in case the other port can't be scheduled in time.
 */
static const int32_t kInOutputRetryDelayUs = 40000;       // 40ms
// static const uint32_t VideoEncodeV4l2::kMaxInputPlaneCount = 3;

// FIXME, if pipeline always call flush before seek. then encoder does nothing in seek.
#define VEV4L2_MSG_prepare (msg_type)1
#define VEV4L2_MSG_start (msg_type)2
#define VEV4L2_MSG_pause (msg_type)4
#define VEV4L2_MSG_resume (msg_type)5
#define VEV4L2_MSG_stop (msg_type)6
#define VEV4L2_MSG_flush (msg_type)7
#define VEV4L2_MSG_reset (msg_type)8
#define VEV4L2_MSG_handleInputBuffer (msg_type)9
#define VEV4L2_MSG_handleOutputBuffer (msg_type)10
#define VEV4L2_MSG_handleEncoderEvent (msg_type)11

BEGIN_MSG_LOOP(VideoEncodeV4l2)
    MSG_ITEM(VEV4L2_MSG_prepare, onPrepare)
    MSG_ITEM(VEV4L2_MSG_start, onStart)
    MSG_ITEM(VEV4L2_MSG_stop, onStop)
    MSG_ITEM(VEV4L2_MSG_flush, onFlush)
    MSG_ITEM(VEV4L2_MSG_reset, onReset)
    MSG_ITEM(VEV4L2_MSG_handleInputBuffer, onHandleInputBuffer)
    MSG_ITEM(VEV4L2_MSG_handleOutputBuffer, onHandleOutputBuffer)
    MSG_ITEM(VEV4L2_MSG_handleEncoderEvent, onHandleEncoderEvent)
END_MSG_LOOP()

#define INT64_TO_TIMEVAL(i64, time_val) do {            \
        time_val.tv_sec = (int32_t)(i64 >> 31);         \
        time_val.tv_usec = (int32_t)(i64 & 0x7fffffff); \
    } while (0)
#define TIMEVAL_TO_INT64(i64, time_val) do {            \
        i64 = time_val.tv_sec;                          \
        i64 = (i64 << 31)  + time_val.tv_usec;  \
    } while(0)

// ////////////////////// DevicePollThread
class VideoEncodeV4l2::DevicePollThread : public MMThread {
  public:
    DevicePollThread(VideoEncodeV4l2 * encoder)
        : MMThread(MMTHREAD_NAME, true)
        , mEncoder(encoder)
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
        MMAutoLock locker(mEncoder->mLock);
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
    VideoEncodeV4l2 * mEncoder;
    // FIXME, define a class for sem_t, does init/destroy automatically
    sem_t mSem;         // cork/uncork on pause/resume
    // FIXME, change to something like mExit
    bool mContinue;     // terminate the thread
};

// poll the device for notification of available input/output buffer index
void VideoEncodeV4l2::DevicePollThread::main()
{
    FUNC_TRACK();

    bool event_pending=false;
    while(1) {
        {
            MMAutoLock locker(mEncoder->mLock);
            if (!mContinue) {
                break;
            }
        }
        if (mEncoder->mState != kStatePlaying) {
            INFO("DevicePollThread waitting_sem\n");
            sem_wait(&mSem);
            INFO("DevicePollThread wakeup_sem\n");
            continue;
        }

        if (event_pending) {
            mEncoder->postMsg(VEV4L2_MSG_handleEncoderEvent, 0, NULL, 0);
            event_pending = false;
        }

        // FIXME, it's better schedule one of the two (or both) depending on ret value
        if (mEncoder->mEosState < kInputEOS) {
            // IF got input eos, no need to schedule again.
            mEncoder->dequeueInputBuffer();
        }
        mEncoder->dequeueOutputBuffer();

        VERBOSE("poll device for notification\n");
        int ret = mEncoder->mV4l2Encoder->poll(true, &event_pending);
        VERBOSE("poll ret: %d\n", ret);
    #ifdef YUNOS_BOARD_sprd
        // remove if poll method is implement
        usleep(15*1000);
    #endif
    }

    INFO("Poll thread exited\n");
}

// /////////////////////////////////////
VideoEncodeV4l2::VideoEncodeV4l2(const char* mimeType, bool isEncoder)
    : MMMsgThread(COMPONENT_NAME)
    , mMime(mimeType)
    , mComponentName(COMPONENT_NAME)
    , mState(kStateNull)
    , mEosState(kNoneEOS)
    , mIsLive(false)
    , mSource(NULL)
    , mSink(NULL)
    , mInputBufferType(MediaBuffer::MBT_RawVideo)
    , mInputPlaneCount(-1)
    , mFormat(V4L2_PIX_FMT_YUV420)
    , mFramerateNum(30)
    , mFramerateDenom(1)
    , mBitrate(0)
    , mInputMemoryType(V4L2_MEMORY_USERPTR)
    , mOutputMemoryType(V4L2_MEMORY_MMAP)
    , mInputQueueCapacity(5) //FIXME: it's better to set encoder input buffer count less than surface input buffer count, so we no need to waste time to wait to read
    , mOutputQueueCapacity(kOutputBufferCount)
    , mConfigured(false)
    , mDropFrameThreshHold2(5)
    , mInputBufferCount(1)
    , mInputBufferDropedCount(0)
    , mOutputBufferCount(0)
    , mOutputBufferRecycleCount(0)
    , mRepeated(false)
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    , mYalloc(NULL)
#endif
    , mIsCodecDataSet(false)
{
    FUNC_TRACK();
    std::string env_str;

    memset(mWidths, 0, sizeof(mWidths));
    memset(mHeights, 0, sizeof(mHeights));
    ASSERT(isEncoder);
    // FIXME, add other codec type support
    if (strcmp(mimeType, MEDIA_MIMETYPE_VIDEO_AVC)) {
        ERROR("%s hasn't supported yet\n", mimeType);
        ASSERT(0 && "mimetype hasn't supported");
    }

    // check debug flag with default value
    mResetOutputBuffer = mm_check_env_str("v4l2.encoder.reset.output", "VL42_ENCODER_RESET_OUTPUT", "1", false);
    mDumpOutputBuffer = mm_check_env_str("v4l2.encoder.dump.output", "VL42_ENCODER_DUMP_OUTPUT", "1", false);

    mResetInputBuffer = mm_check_env_str("v4l2.encoder.reset.input", "VL42_ENCODER_RESET_INPUT", "1", false);
    mDumpInputBuffer = mm_check_env_str("v4l2.encoder.dump.input", "VL42_ENCODER_DUMP_INPUT", "1", false);

#ifdef __MM_YUNOS_YUNHAL_BUILD__
#if defined(__USING_YUNOS_MODULE_LOAD_FW__)
    VendorModule* module =
                (VendorModule*) LOAD_VENDOR_MODULE(YALLOC_VENDOR_MODULE_ID);
    int ret = yalloc_open(module,&mYalloc);
#else
    int ret = yalloc_open(&mYalloc);
#endif
    DEBUG("yalloc open: yalloc=%p, ret=%d", mYalloc, ret);
    MMASSERT(ret == 0);
#endif

#if defined (__MM_YUNOS_CNTRHAL_BUILD__)
    mInputMemoryType = (v4l2_memory)V4L2_MEMORY_PLUGIN_BUFFER_HANDLE;
#elif defined(__MM_YUNOS_YUNHAL_BUILD__)
    mInputMemoryType = (v4l2_memory)V4L2_MEMORY_YUNOS_NATIVE_TARGET;
#elif defined(__MM_BUILD_DRM_SURFACE__)
    mInputMemoryType = (v4l2_memory)V4L2_MEMORY_TYPE_DRM_NAME;
#endif

}

VideoEncodeV4l2::~VideoEncodeV4l2()
{
    FUNC_TRACK();
    // in case reset is not called, make sure members are deconstruction in order
    // FIXME, it is not good to call onReset() here, since onReset() wille notify(kEventResetComplete)
    // then PipelinePlayer will visit current Component during Component's decostruction func.
    // it lead to undefined behavior.
    // onReset(0, NULL, 0);
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    int ret = yalloc_close(mYalloc);
    MMASSERT(ret == 0);
#endif
}

mm_status_t VideoEncodeV4l2::prepare()
{
    FUNC_TRACK();
    postMsg(VEV4L2_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoEncodeV4l2::start()
{
    FUNC_TRACK();
    postMsg(VEV4L2_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoEncodeV4l2::stop()
{
    FUNC_TRACK();
    postMsg(VEV4L2_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoEncodeV4l2::reset()
{
    FUNC_TRACK();
    postMsg(VEV4L2_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoEncodeV4l2::flush()
{
    FUNC_TRACK();
    postMsg(VEV4L2_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}


uint32_t VideoEncodeV4l2::fourccConvert(uint32_t fourcc) {

    switch(fourcc) {
        case 'YUYV':
        case 'YUY2':
            return V4L2_PIX_FMT_YUYV;
        case 'NV12':
            return V4L2_PIX_FMT_NV12;
        case 'NV21':
            return V4L2_PIX_FMT_NV21;
        case 'YU12':
            return V4L2_PIX_FMT_YUV420;
        case 'YV12':
        case 'I420':
            return V4L2_PIX_FMT_YVU420;
        case 'YVYU':
            return V4L2_PIX_FMT_YVYU;
        case 'RGBX':
            return V4L2_PIX_FMT_RGB32;

        default:
            ASSERT(0 && "SHOULD NOT BE HERE");

    }
    return V4L2_PIX_FMT_YUYV;

}

mm_status_t VideoEncodeV4l2::addSource(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            mInputFormat = mReader->getMetaData();
            mOutputFormat = MediaMeta::create();
            if (mInputFormat) {
                if (mInputFormat->getInt32(MEDIA_ATTR_WIDTH, mWidths[0])) {
                    DEBUG("get meta data, width is %d\n", mWidths[0]);
                    mOutputFormat->setInt32(MEDIA_ATTR_WIDTH, mWidths[0]);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, mHeights[0])) {
                    DEBUG("get meta data, height is %d\n", mHeights[0]);
                    mOutputFormat->setInt32(MEDIA_ATTR_HEIGHT, mHeights[0]);
                }

                if (mInputFormat->getInt32("repeat", mRepeated)) {
                    DEBUG("get meta data, mRepeated is %d\n", mRepeated);
                }

                // FIXME,  some extra meta info is required to pass through to sink component
                // FIXME, let source component uses v4l2 pixel format?
                int32_t fourcc = 0;
                if (mInputFormat->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc)) {
                    DEBUG("get meta data, input fourcc is %08x\n", fourcc);
                    DEBUG_FOURCC("fourcc: ", fourcc);
                }
                mFormat = fourccConvert((uint32_t)fourcc);
                DEBUG_FOURCC("mFormat: ", mFormat);

                ASSERT(mFormat == V4L2_PIX_FMT_NV12 ||
                    mFormat == V4L2_PIX_FMT_NV21 ||
                    mFormat == V4L2_PIX_FMT_YUV420 ||
                    mFormat == V4L2_PIX_FMT_YVU420 ||
                    mFormat == V4L2_PIX_FMT_YUYV ||
                    mFormat == V4L2_PIX_FMT_YVYU ||
                    mFormat == V4L2_PIX_FMT_RGB32);

                int32_t tempInt = 0;
                if (mInputFormat->getInt32(MEDIA_META_BUFFER_TYPE, tempInt)) {
                    mInputBufferType = (MediaBuffer::MediaBufferType)tempInt;
                    DEBUG("get meta data, input buffer type is %d\n", mInputBufferType);
                }

            }
            return MM_ERROR_SUCCESS;
        }
    }

    return MM_ERROR_OP_FAILED;
}

mm_status_t VideoEncodeV4l2::addSink(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);
        // FIXME, use setParameter to support more encoder format
        mInputFormat->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_VIDEO_AVC);
        CowCodecID codecId= mime2CodecId(MEDIA_MIMETYPE_VIDEO_AVC);
        mOutputFormat->setInt32(MEDIA_ATTR_CODECID, codecId);
        mOutputFormat->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
        if (mWriter && mOutputFormat) {
            mWriter->setMetaData(mOutputFormat);
        }
    }
    return MM_ERROR_SUCCESS;
}

void VideoEncodeV4l2::setState(StateType state)
{
    // FIXME, seem Lock isn't necessary
    // caller thread update state to kStatePaused
    // other state are updated by MMMsgThread
    // MMAutoLock locker(mLock);
    mState = state;
}

void VideoEncodeV4l2::setEosState(EosStateType state)
{
    // FIXME, seem Lock isn't necessary
    mEosState = state;
}

void VideoEncodeV4l2::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock); // big lock
    int ioctlRet = -1;

    // mState = kStatePreparing;
    setState(kStatePreparing);
    ASSERT(mInputFormat && mMime.c_str());

    mV4l2Encoder = V4l2CodecDevice::create("encoder", 0);
    ASSERT(mV4l2Encoder);

    // query hw capability
    struct v4l2_capability caps;
    memset(&caps, 0, sizeof(caps));
    ioctlRet = mV4l2Encoder->ioctl(VIDIOC_QUERYCAP, &caps);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYCAP);
    uint32_t capabilitiesMask = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_STREAMING;
    if ((caps.capabilities & capabilitiesMask) != capabilitiesMask) {
        ERROR("unsupport capablities 0x%0x\n", capabilitiesMask);
        ioctlRet = -1;
    }

    struct v4l2_format format;
    // set compressed (output) format, output first to decide encoder type
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);

    // set raw data (input) format
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.pixelformat = mFormat;
    format.fmt.pix_mp.width = mWidths[0];
    format.fmt.pix_mp.height = mHeights[0];
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_FMT, &format);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_FMT);

    DEBUG("mInputBufferType: %d", mInputBufferType);
    char *memoryTypeStr = NULL;
    if (mInputBufferType == MediaBuffer::MBT_RawVideo) {
        mInputMemoryType = V4L2_MEMORY_USERPTR;
    }
    // set input buffer type
    // mInputMemoryType is set in addSource, memory type is checked by video source
    if (mInputMemoryType == V4L2_MEMORY_PLUGIN_BUFFER_HANDLE) {
        memoryTypeStr = (char *)"plugin-buffer-handle";
    } else if (mInputMemoryType == V4L2_MEMORY_USERPTR) {
        memoryTypeStr = (char *)"raw-data";
    } else if (mInputMemoryType == V4L2_MEMORY_YUNOS_NATIVE_TARGET) {
        memoryTypeStr = (char *)"yunos-native-target";
    }
    #if defined(__MM_BUILD_DRM_SURFACE__)
    else if (mInputMemoryType == V4L2_MEMORY_TYPE_DRM_NAME) {
        memoryTypeStr = (char *)"drm-buf";
    }
    #endif

#ifndef __MM_NATIVE_BUILD__
    mV4l2Encoder->setParameter("frame-memory-type", memoryTypeStr);

    DEBUG("set input memory type to %s, mInputMemoryType %d", memoryTypeStr, mInputMemoryType);
#endif
    if (mIsLive) {
        mV4l2Encoder->setParameter("encode-mode", "svct");
    }

    // set framerate
    struct v4l2_streamparm parms;
    memset(&parms, 0, sizeof(parms));
    parms.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    parms.parm.output.timeperframe.denominator = mFramerateDenom;
    parms.parm.output.timeperframe.numerator = mFramerateNum;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_PARM, &parms);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_PARM);

    struct v4l2_ext_control ctrls[1];
    struct v4l2_ext_controls control;

    // set bitrate
    if (!mBitrate) {
        mBitrate = mWidths[0]*mHeights[0]*(mFramerateNum/mFramerateDenom)*8/30;
        DEBUG("set default bitrate %d", mBitrate);
    }
    memset(&ctrls, 0, sizeof(ctrls));
    memset(&control, 0, sizeof(control));
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_BITRATE;
    ctrls[0].value = mBitrate;
    control.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    control.count = 1;
    control.controls = ctrls;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_EXT_CTRLS, &control);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_EXT_CTRLS);

    // other controls
    memset(&ctrls, 0, sizeof(ctrls));
    memset(&control, 0, sizeof(control));
    // No B-frames, for lowest decoding latency.
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_B_FRAMES;
    ctrls[0].value = 0;
    control.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    control.count = 1;
    control.controls = ctrls;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_EXT_CTRLS, &control);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_EXT_CTRLS);

    // Set base profile
    memset(&ctrls, 0, sizeof(ctrls));
    memset(&control, 0, sizeof(control));
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
    ctrls[0].value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    control.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    control.count = 1;
    control.controls = ctrls;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_EXT_CTRLS, &control);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_EXT_CTRLS);

    // set 41 level
    memset(&ctrls, 0, sizeof(ctrls));
    memset(&control, 0, sizeof(control));
    ctrls[0].id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
    ctrls[0].value = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
    control.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    control.count = 1;
    control.controls = ctrls;
    ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_EXT_CTRLS, &control);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_EXT_CTRLS);


    // setup some internal variables
    switch(mFormat) {
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
        mInputPlaneCount = 1;
        break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        mInputPlaneCount = 2;
        mWidths[1] = (mWidths[0] + 1)/2*2;
        mHeights[1] = (mHeights[0] + 1)/2;
        break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YUV420M:
        mInputPlaneCount = 3;
        mWidths[1] = (mWidths[0] + 1)/2;
        mHeights[1] = (mHeights[0] + 1)/2;
        mWidths[2] = mWidths[1];
        mHeights[2] = mHeights[2];
    case V4L2_PIX_FMT_RGB32:
        mInputPlaneCount = 1;
        break;
    default:
        ASSERT(0 && "unsupport input format");
        break;
    }

    if (ioctlRet != 0) {
        notify(kEventPrepareResult, MM_ERROR_INVALID_PARAM, 0, nilParam);
    } else {
        // mState = kStatePrepared;
        setState(kStatePrepared);
        notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    }
}

mm_status_t VideoEncodeV4l2::requestIDR()
{
    if (!mV4l2Encoder)
        return MM_ERROR_SUCCESS;

    struct v4l2_ext_control ctrls[1];
    struct v4l2_ext_controls control;

    memset(&ctrls, 0, sizeof(ctrls));
    memset(&control, 0, sizeof(control));
    ctrls[0].id = V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE;
    ctrls[0].value = V4L2_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE_I_FRAME;
    control.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    control.count = 1;
    control.controls = ctrls;
    int ioctlRet = mV4l2Encoder->ioctl( VIDIOC_S_EXT_CTRLS, &control);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_S_EXT_CTRLS);

    return MM_ERROR_SUCCESS;
}

void VideoEncodeV4l2::onHandleEncoderEvent(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
}

void VideoEncodeV4l2::dumpResetInputBuffer(unsigned long target, int w, int h, bool isDump)
{
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    void *vaddr = NULL;
    bool isMapped = false;
    static YunAllocator &allocator(YunAllocator::get());
    MMBufferHandleT handle  = NULL;

    if (mInputBufferType == MediaBuffer::MBT_RawVideo) {
        vaddr = (void*)target;
        if (mStrideX == 0 && mStrideY == 0) {
            mStrideX = mWidths[0];
            mStrideY = mHeights[0];
        }
    } else {
        if (mStrideX == 0 && mStrideY == 0) {
            MMBufferHandleT handle = (MMBufferHandleT)target;

            allocator.authorizeBuffer(handle); //register first, otherwise map failed

          #ifdef YUNOS_BOARD_intel
            int ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_X_STRIDE, target, &mStrideX);
            MMASSERT(ret == 0);
            ret = mYalloc->dispose(mYalloc, YALLOC_DISPOSE_GET_Y_STRIDE, target, &mStrideY);
            MMASSERT(ret == 0);
          #else
            mStrideX = mWidths[0];
            mStrideY = mHeights[0];
          #endif

            allocator.map(handle, YALLOC_FLAG_SW_READ_OFTEN|YALLOC_FLAG_SW_WRITE_OFTEN,
                          0, 0, w, h, &vaddr);
            isMapped = true;
        }
        // DEBUG("ptr: %p, mWidth: %d, h_stride: %d, mHeight: %d\n", vaddr, mWidths[0], mStrideX, mHeights[0]);

#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
        MMNativeBuffer *handle = (MMNativeBuffer*)target;
        void *vaddr = omap_bo_map(handle->bo[0]);
        mStrideX = mWidths[0];
        mStrideY = mHeights[0];
#endif
    }
    DEBUG("mStrideX %d, mStrideY %d", mStrideX, mStrideY);

    if (!vaddr) {
        ERROR("map fail, abort dump");
        return;
    }

    uint8_t *buffer = (uint8_t *)vaddr;
    if (isDump) {
        if (mFormat == V4L2_PIX_FMT_RGB32) {
            int size = w * h * 4;
            rawDataDump.dump(buffer, size);
        } else if (mFormat == V4L2_PIX_FMT_NV12) {
            for (int32_t i = 0; i < h; i++) {
                rawDataDump.dump(buffer, w);
                buffer += mStrideX;
            }

            buffer = (uint8_t *)vaddr + mStrideY * mStrideX;
            for (int32_t i = 0; i < h / 2; i++) {
                rawDataDump.dump(buffer, w);
                buffer += mStrideX;
            }
        } else if (mFormat == V4L2_PIX_FMT_YUYV) {
            rawDataDump.dump(buffer, w*h*2);
        }
    } else {
        if (mFormat == V4L2_PIX_FMT_RGB32) {
            int size = w * h * 4;
            memset(buffer, 0, size);
        } else if (mFormat == V4L2_PIX_FMT_NV12) {
            for (int32_t i = 0; i < h; i++) {
                memset(buffer, 0, w);
                buffer += mStrideX;
            }

            buffer = (uint8_t *)vaddr + mStrideX * mStrideY;
            for (int32_t i = 0; i < h / 2; i++) {
                memset(buffer, 0, w);
                buffer += mStrideX;
            }
        } else if (mFormat == V4L2_PIX_FMT_YUYV) {
            memset(buffer, 0, w*h*2);
        }
    }
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    if (isMapped) {
        isMapped =  false;
        allocator.unmap(handle);
    }
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
#endif
}

uint32_t VideoEncodeV4l2::returnInputBuffer(unsigned long address)
{
    // V4L2_MEMORY_YUNOS_NATIVE_TARGET type
    if (address == 0) {
        WARNING("null address");
        ASSERT(mEosState > kNoneEOS);
        return -1;
    }

    if (mInputMemoryType == V4L2_MEMORY_USERPTR) {
        auto it = std::find(mInputBufferStaging.begin(), mInputBufferStaging.end(), (uintptr_t)address);
        if (it != mInputBufferStaging.end()) {
            mInputBufferStaging.erase(it);
            DEBUG("buffer: %p is returned by codec", address);
        } else {
            ERROR("not found address %p", (uintptr_t)address);
            return -1;
        }

        auto it1 = mInputMediaBufferStaging.find((uintptr_t)address);
        if (it1 != mInputMediaBufferStaging.end()) {
            //release this media buffer, invoke MediaBuffer:releaseBuffer and return this input buffer to source
            mInputMediaBufferStaging.erase(it1);
        } else {
            ERROR("address %p not in mInputMediaBufferStaging", (uintptr_t)address);
            return -1;
        }
        return 0;
    }

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    bool found = false;
    uint32_t i = -1;
    // release the ref buffer to Reader
    for (i = 0; i < mInputFrames.size(); i++) {
        if (mInputFrames[i].mHandle == (MMBufferHandleT)address) {
            if (mResetInputBuffer) {
                dumpResetInputBuffer(address, mWidths[0], mHeights[0], false);
            }
            mInputFrames[i].mBuffer.reset(); //reset cause to MediaBuffer:releaseBuffer
            found = true;
            DEBUG("release one input buffer, inputFrames[%d]: native_target_t: %p\n",
                i, (void *)mInputFrames[i].mHandle);

            break;
        }
    }
    ASSERT(found);
    return i;
#else
    return -1;
#endif
}


uint32_t VideoEncodeV4l2::checkInputBuffer(unsigned long target)
{
#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    ASSERT(target != 0);
    uint32_t i = -1;
    // release the ref buffer to Reader
    for (i = 0; i < mInputFrames.size(); i++) {
        if (mInputFrames[i].mHandle == (MMBufferHandleT)target) {
            break;
        }
    }

    if (i == mInputFrames.size()) {
        BufferInfo info;
        mInputFrames.push_back(info);

        mInputFrames[i].mHandle = (MMBufferHandleT)target;
        mInputFrames[i].mBuffer.reset();
        DEBUG("add target %p", (MMBufferHandleT)target);
    }
    return i;
#else
    return -1;
#endif
}

void VideoEncodeV4l2::onHandleInputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    struct v4l2_buffer buf;
    struct v4l2_plane planes[kMaxInputPlaneCount];
    uint32_t i = 0;
    int ioctlRet = -1;

    if (mState != kStatePlaying && mState != kStatePlay)
        return;

    // for live stream, let's drop input frames when output frame are not consumed in time
    if (mIsLive) {
        // FIXME, video_encode_plugin.cc holds 2 MediaBuffer
        if (mMonitorWrite && mMonitorWrite->aliveCount() >= mDropFrameThreshHold2) {
            MediaBufferSP sourceBuf;
            mm_status_t status = mReader->read(sourceBuf);
            if (status == MM_ERROR_SUCCESS) {
                mInputBufferDropedCount++;
            }
            postMsg(VEV4L2_MSG_handleInputBuffer, 0, NULL, kInOutputRetryDelayUs/2);
            DEBUG("%d output buffers cached, drop input buffer and continue (%d, %d)", mMonitorWrite->aliveCount(), mInputBufferDropedCount, mInputBufferCount);
            return;
        }
    }

    // enque input buffers
    do {
        uint32_t encoderIndex = -1;

        {
            MMAutoLock locker(mLock);
            if (mEosState != kNoneEOS) {
                DEBUG("mEosState %d", mEosState);
                return;
            }

            // FIXME, deque the input buffer on stop/release or EOS
            if (mInputIndces.empty()) {
                DEBUG("no input buffer from v4l2codec device is available\n");
                break;
            }
            encoderIndex = mInputIndces.front();
        }


        MediaBufferSP sourceBuf;

        // try read in loop may block this thread
        mm_status_t status = mReader->read(sourceBuf);
        if (status == MM_ERROR_AGAIN) {
            VERBOSE("read again");
            break;
        } else if (status != MM_ERROR_SUCCESS || !sourceBuf) {
            VERBOSE("read error");
            break;
        }

        uintptr_t buffers[kMaxInputPlaneCount] = {0};
        int32_t offsets[kMaxInputPlaneCount] = {0};
        int32_t strides[kMaxInputPlaneCount] = {0};
        sourceBuf->getBufferInfo((uintptr_t *)buffers, offsets, strides, kMaxInputPlaneCount);

        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; // it indicates input buffer(raw frame) type
        buf.m.planes = planes;
        buf.length = mInputPlaneCount;

        buf.flags = 0;
        buf.memory = mInputMemoryType;
        do { // for block control
            if (sourceBuf->isFlagSet(MediaBuffer::MBFT_EOS)) {
                INFO("input read EOS, %lld\n", sourceBuf->pts());
                setEosState(kInputEOS);
                buf.flags |= V4L2_BUF_FLAG_EOS;
                // send EOS to device
                // Maybe eos buffer has valid data
                if (!buffers[0] || sourceBuf->size() == 0) {
                    buf.m.planes[0].m.userptr = buf.m.planes[1].m.userptr = buf.m.planes[2].m.userptr = 0;
                    buf.m.planes[0].bytesused = buf.m.planes[1].bytesused = buf.m.planes[2].bytesused = 0;
                    DEBUG("EOS buffer is invalid");
                    break;
                }
            }
            // Skip invalid eos timestamp
            mTimeStampMap[sourceBuf->pts()] = sourceBuf->birthTimeInMs();
            mEncodingDTSQueue.push(sourceBuf->pts());

        #if defined(__MM_YUNOS_CNTRHAL_BUILD__)
            if (sourceBuf->type() == MediaBuffer::MBT_GraphicBufferHandle) {
                buffer_handle_t *handle_ptr = (buffer_handle_t*)buffers[0];
                DEBUG("buffers[0]: %p, handle: %p\n", (void*)buffers[0], *handle_ptr);
                buf.m.planes[0].m.userptr = (unsigned long)(*handle_ptr);
                DEBUG("buffer_handle_t %p, buffers[0]: %p\n", (void*)buf.m.planes[0].m.userptr, (void*)buffers[0]);
            } else if (sourceBuf->type() == MediaBuffer::MBT_CameraSourceMetaData) {
                uint32_t *ptr = (uint32_t*)buffers[0];
                DEBUG("camera_data_dump in video_encode_v4l2");
                hexDump(ptr, sourceBuf->size(), 32);
                if (*ptr == 1) { //kMetadataBufferTypeGrallocSource
                    buffer_handle_t *handle_ptr = (buffer_handle_t*)(ptr + 1);
                    buffer_handle_t handle = *handle_ptr;
                    buf.m.planes[0].m.userptr = reinterpret_cast<long>(handle);
                    DEBUG("buffer_handle_t %p", (void*)buf.m.planes[0].m.userptr);
                } else {
                    ERROR("not supported camera source data type\n");
                }
            } else
        #elif defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
            if (sourceBuf->type() == MediaBuffer::MBT_GraphicBufferHandle ||
                sourceBuf->type() == MediaBuffer::MBT_DrmBufName) {
                MediaMetaSP meta = sourceBuf->getMediaMeta();
                if (!meta) {
                    ERROR("no meta data");
                    ASSERT(0);
                }

                // ASSERT(meta->getInt32("index", srcIndex));
                // ASSERT(srcIndex >= 0 && (uint32_t)srcIndex < mInputFrames.size());

                // Hold the ref until the input buffer return to component
                MMBufferHandleT target_t = *(MMBufferHandleT*)buffers[0];
                ASSERT(target_t);

                // We can ensure srcIndex slot is available
                uint32_t slot = checkInputBuffer((unsigned long)target_t);
                if (!mRepeated) {
                    // If frame is repeated, no need to check
                    ASSERT(mInputFrames[slot].mBuffer == nullptr);
                }
                mInputFrames[slot].mBuffer = sourceBuf;

                buf.m.planes[0].m.userptr = (unsigned long)target_t;
                buf.m.planes[0].bytesused = sizeof(buf.m.planes[0].m.userptr);
                int64_t pts = sourceBuf->pts();
                INT64_TO_TIMEVAL(pts, buf.timestamp);
                VERBOSE("pts: %" PRId64 ", buf.timestamp: (%d, %d)", pts, buf.timestamp.tv_sec, buf.timestamp.tv_usec);

                if (mDumpInputBuffer) {
                    dumpResetInputBuffer((unsigned long)target_t, mWidths[0], mHeights[0]);
                }

                DEBUG("enqueue one input buffer, mInputBufferCount: %d,  inputFrames[%d]: native_target_t: %p, with timestamp: %" PRId64 ", age: %dms",
                    mInputBufferCount, slot, (void *)mInputFrames[slot].mHandle, sourceBuf->pts(), sourceBuf->ageInMs());
            } else
        #endif
            if (sourceBuf->type() == MediaBuffer::MBT_RawVideo) {
                if (mInputMemoryType == V4L2_MEMORY_USERPTR) {
                    // todo: check color format
                    uintptr_t data = *(uintptr_t*)buffers[0];
                    DEBUG("enqueue one input buffer, mInputBufferCount: %d,  mInputPlaneCount: %d, data: %p, offsets[0]: %d\n",
                        mInputBufferCount, mInputPlaneCount, data, offsets[0]);

                    switch (mFormat) {
                        case V4L2_PIX_FMT_YUYV:
                            buf.m.planes[0].bytesused = mWidths[0] * mHeights[0] * 2;
                            buf.m.planes[0].m.userptr = data + offsets[0];
                            DEBUG("data %p, size %d", buf.m.planes[0].m.userptr, buf.m.planes[0].bytesused);
                            break;
                        case V4L2_PIX_FMT_NV12:
                        case V4L2_PIX_FMT_NV21:
                            buf.m.planes[0].bytesused = mWidths[0] * mHeights[0];
                            buf.m.planes[0].m.userptr = data + offsets[0];
                            buf.m.planes[1].bytesused = mWidths[0] * mHeights[0] / 2;
                            buf.m.planes[1].m.userptr = data + mWidths[0] * mHeights[0];
                            break;
                        case V4L2_PIX_FMT_YUV420:
                            break;
                        case V4L2_PIX_FMT_YVU420:
                            break;
                        case V4L2_PIX_FMT_RGB32:
                            break;
                        default:
                            ASSERT("Should not be here!");
                            break;
                    }


                    if (mDumpInputBuffer) {
                        dumpResetInputBuffer((unsigned long)data, mWidths[0], mHeights[0]);
                    }


                    mInputBufferStaging.push_back(data);
                    mInputMediaBufferStaging[data] = sourceBuf;
                }
            }
        } while(0);

        mInputBufferCount++;

        buf.index = encoderIndex;
        const int32_t retryCount = 10;
        int32_t retry = 0;
        do {
            ioctlRet = mV4l2Encoder->ioctl(VIDIOC_QBUF, &buf);
            ASSERT(ioctlRet == 0 || ioctlRet == EAGAIN);
            if (ioctlRet == EAGAIN) {
                usleep(10*1000);
            }
        } while(ioctlRet != 0 && retry++ < retryCount);
        ASSERT(ioctlRet == 0);

        {
            MMAutoLock locker(mLock);
            mInputIndces.pop();
        }

    }while (0);

    MM_CALCULATE_AVG_FPS("VEV4L2-input");
    int32_t scheduleDelay = kInOutputRetryDelayUs;
    if (!mInputIndces.empty())
        scheduleDelay /= 4;

    if (mEosState == kNoneEOS)
        postMsg(VEV4L2_MSG_handleInputBuffer, 0, NULL, scheduleDelay);

}

/* static */ bool VideoEncodeV4l2::releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    // FUNC_TRACK();
    bool ret = false;
    void *ptr = NULL;
    int32_t buffer_index = -1;

    if (!mediaBuffer) {
        return false;
    }

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    ASSERT_RET(meta, false);
    ret = meta->getPointer("v4l2-encoder", ptr);
    ASSERT_RET(ret && ptr, false);
    VideoEncodeV4l2 *encoder = (VideoEncodeV4l2*) ptr;

    ret = meta->getInt32("v4l2-buffer-index", buffer_index);
    ASSERT_RET(ret , false);

    {
        // MMAutoLock locker(encoder->mLock);

        // encoder->mOutputIndces.push(buffer_index);
        // DEBUG("release one output buffer[%d]", buffer_index);

        // debugging
        if (encoder->mResetOutputBuffer) {
            uint8_t *buffers;
            int32_t offsets;
            int32_t strides;
            if ( !mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
                return false;
            }

            memset(buffers, 0, strides);
        }
        encoder->queueOutputBuffer(buffer_index);
        encoder->mOutputBufferRecycleCount++;
    }

    return true;
}

int32_t VideoEncodeV4l2::getCodecDataSize(uint8_t *data, int32_t size)
{
    int32_t codecDataSize = 0;
#ifdef YUNOS_BOARD_sprd
    codecDataSize = 32;
#else
    if (isAnnexBByteStream(data, size)) {
        uint8_t *nalStart = nullptr;
        size_t nalSize = 0;
        const uint8_t *srcBuffer = data;
        size_t srcSize = size;
        int loop = 0;
        while (getNextNALUnit(&srcBuffer, &srcSize, (const uint8_t**)&nalStart, &nalSize, true) == MM_ERROR_SUCCESS ) {
            if (nalSize > 0) {
                //nalSize not include header size
                loop++;
                codecDataSize += (nalSize + 4);
                if (loop == 2)
                    break;
            }
        }
        ASSERT(codecDataSize > 0);
        DEBUG("codec data size %d", codecDataSize);
        hexDump(data, 64, 16);
    }
#endif
    return codecDataSize;
}

void VideoEncodeV4l2::onHandleOutputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    if (mState != kStatePlaying && mState != kStatePlay)
        return;

    // get output buffers
    do {
        uint32_t index = -1;
        {
            MMAutoLock locker(mLock);
            if (mEosState == kOutputEOS)
                return;

            if (mOutputIndces.empty()) {
                DEBUG("no output buffer slot from v4l2codec device is available, mInputBufferCount: %d, mOutputBufferCount: %d, mOutputBufferRecycleCount: %d",
                    mInputBufferCount, mOutputBufferCount, mOutputBufferRecycleCount);
                break;
            }
            index = mOutputIndces.front();
        }

        uint64_t timeStamp = 0;
        if(!mEncodingDTSQueue.empty()){
            timeStamp = mEncodingDTSQueue.front();
            mEncodingDTSQueue.pop();
        }

        MediaBufferSP mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
        uintptr_t buffers[1];
        int32_t offsets[1];
        int32_t strides[1];
        // buffers[0] = (uintptr_t)mOutputBuffers[buffer.index];
        offsets[0] = 0;
        strides[0] = mOutputBuffers[index].size;


        MediaMetaSP meta = MediaMeta::create();
        meta->setInt32("v4l2-buffer-index", index);
        meta->setPointer("v4l2-encoder", this);
        mediaBuffer->setMediaMeta(meta);
        mediaBuffer->addReleaseBufferFunc(releaseOutputBuffer);
        mediaBuffer->setDuration(0);
        mediaBuffer->setDts(timeStamp);
        if (mOutputBuffers[index].pts == 0 && timeStamp > 0) {
            mOutputBuffers[index].pts = timeStamp;
        }
        mediaBuffer->setPts(mOutputBuffers[index].pts);
        if (mOutputBuffers[index].flags & V4L2_BUF_FLAG_NON_REF) {
            DEBUG("non-ref frame");
            mediaBuffer->setFlag(MediaBuffer::MBFT_NonRef);
        }

        // debug use only
        std::map<int64_t, int64_t>::iterator it = mTimeStampMap.find(mediaBuffer->pts());
        if (it != mTimeStampMap.end()) {
            mediaBuffer->setBirthTimeInMs(it->second);
            mTimeStampMap.erase(it);
        }
        if (mTimeStampMap.size() > 50) {
            WARNING("something wrong, mTimeStampMap grows and leak memory");
        }

        DEBUG("output buffer size %d, dts: %" PRId64 ", pts: %" PRId64 ", age: %dms, mInputBufferCount: %d, mOutputBufferCount: %d, mOutputBufferRecycleCount: %d, readyOutputBuffer: %zu",
            strides[0], timeStamp, mOutputBuffers[index].pts, mediaBuffer->ageInMs(), mInputBufferCount, mOutputBufferCount, mOutputBufferRecycleCount, mOutputIndces.size());

        if (mOutputBuffers[index].flags & V4L2_BUF_FLAG_KEYFRAME &&
            mOutputBuffers[index].size != 0 &&
            mOutputBuffers[index].data != NULL) {
            DEBUG("encoder output key frame");
            mediaBuffer->setFlag(MediaBuffer::MBFT_KeyFrame);
            if (!mIsCodecDataSet) {
                int32_t codecDataSize = getCodecDataSize((uint8_t*)mOutputBuffers[index].data, mOutputBuffers[index].size);
                if (codecDataSize != 0) {
                    mIsCodecDataSet = true;

                    meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, (uint8_t*)mOutputBuffers[index].data, codecDataSize);
                    hexDump((uint8_t*)mOutputBuffers[index].data, codecDataSize, 16);
                } else {
                    ERROR("get no codec data");
                    return;
                }
            }
#if defined(I_FRAME_SKIP_SPS_PPS)
            offsets[0] += 0x20; //just prefix header and sps/pps
#endif
        }
        // May be the last frame is key frame or a valid frame
        // add V4L2_BUF_FLAG_ERROR flag to check whether the eos frame is invalid
        if (mOutputBuffers[index].flags & V4L2_BUF_FLAG_EOS) {
            ASSERT(mEosState == kInputEOS);
            MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_BufferIndexV4L2);
            mediaOutputBuffer->setFlag(MediaBuffer::MBFT_EOS);
            mediaOutputBuffer->setSize(0);
            mm_status_t ret = mWriter->write(mediaOutputBuffer);
            if ((ret != MM_ERROR_SUCCESS) && (ret != MM_ERROR_EOS)) {
                WARNING("decoder fail to write Sink");
            }
            DEBUG("output buffer eos");
            setState(kStatePaused);
            mV4l2Encoder->setDevicePollInterrupt();
            return;
        }

        strides[0] -= offsets[0];
        buffers[0] = (uintptr_t)mOutputBuffers[index].data;
        mediaBuffer->setBufferInfo(buffers, offsets, strides, 1);
        mediaBuffer->setSize(strides[0]);


        if (mDumpOutputBuffer) {
            uint8_t *buffers;
            int32_t offsets;
            int32_t strides;
            mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1);
            strides = mediaBuffer->size();

            encodedDataDump.dump(buffers+offsets, strides);
        }

        mm_status_t status = MM_ERROR_SUCCESS;
        int32_t retry = 0;
        const int32_t retryCount = 100;
        mediaBuffer->setMonitor(mMonitorWrite);
        do {
            status = mWriter->write(mediaBuffer);
        } while (status == MM_ERROR_AGAIN && retry++ < retryCount);

        if (status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC) {
            ERROR("write failed, err %d\n", status);
            if (mediaBuffer) {
                // end record and set eos flag if write error occur
                mediaBuffer->setFlag(MediaBuffer::MBFT_EOS);
                return;
            }
        }
        {
            MMAutoLock locker(mLock);
            mOutputIndces.pop();
        }
    }    while (0);

    int32_t scheduleDelay = kInOutputRetryDelayUs;
    if (!mOutputIndces.empty())
        scheduleDelay /= 4;
    postMsg(VEV4L2_MSG_handleOutputBuffer, 0, NULL, scheduleDelay);
}

void VideoEncodeV4l2::queueOutputBuffer(uint32_t index)
{
    int ioctlRet = -1;
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[1];

    {
        // MMAutoLock locker(mLock);
        if (mEosState == kOutputEOS) {
            DEBUG("output is already eos");
            return;
        }

        if (mState != kStatePlay && mState != kStatePlaying && mState != kStatePaused) {
            DEBUG("index: %d, defunc state: %d", index, mState);
            return;
        }
    }

    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
    buffer.memory = mOutputMemoryType;
    buffer.m.planes = planes;
    buffer.length = 1;

    // MMAutoLock locker(mLock);

    buffer.index = index;
    const int32_t retryCount = 10;
    int32_t retry = 0;
    do {
        ioctlRet = mV4l2Encoder->ioctl(VIDIOC_QBUF, &buffer);
        ASSERT(ioctlRet == 0 || ioctlRet == EAGAIN);
        if (ioctlRet == EAGAIN) {
            usleep(10*1000);
        }
    } while(ioctlRet != 0 && retry++ < retryCount);
    ASSERT(ioctlRet == 0);

    DEBUG("enqueue one output frame, index: %d\n", index);
    // mOutputIndces.pop();
}

void VideoEncodeV4l2::dequeueInputBuffer()
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[kMaxInputPlaneCount];
    int ioctlRet = -1;

    do {
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; // it indicates input buffer(raw frame) type
        buf.memory = mInputMemoryType;
        buf.m.planes = planes;
        buf.length = mInputPlaneCount;

        ioctlRet = mV4l2Encoder->ioctl(VIDIOC_DQBUF, &buf);
        if (ioctlRet == 0) {
            DEBUG("dequeue one input buffer, buf.index %d, reserved %p\n",
                buf.index, (void *)buf.m.planes[0].reserved[0]);
            /*uint32_t index = */returnInputBuffer(buf.m.planes[0].reserved[0]);

            // ASSERT(buf.index == index);
            {
                MMAutoLock locker(mLock);
                mInputIndces.push(buf.index);
            }
        }
    } while (ioctlRet == 0);
}

void VideoEncodeV4l2::dequeueOutputBuffer()
{
    int ioctlRet = -1;
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[1];

    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
    buffer.memory = mOutputMemoryType;
    buffer.m.planes = planes;
    buffer.length = 1;

    // get output buffers
    do {
        ioctlRet = mV4l2Encoder->ioctl(VIDIOC_DQBUF, &buffer);

        if (ioctlRet == -1) {
            VERBOSE("fail to get output buffer from codec, mInputBufferCount: %d, mOutputBufferCount: %d, mOutputBufferRecycleCount: %d", mInputBufferCount, mOutputBufferCount, mOutputBufferRecycleCount);
            break;
        }

        {
            MMAutoLock locker(mLock);
            mOutputBufferCount++;
            mOutputIndces.push(buffer.index);
            mOutputBuffers[buffer.index].size = buffer.bytesused;
            mOutputBuffers[buffer.index].flags = buffer.flags;
            VERBOSE("buffer->timestamp: (%ld, %ld)", buffer.timestamp.tv_sec, buffer.timestamp.tv_usec);
            TIMEVAL_TO_INT64(mOutputBuffers[buffer.index].pts, buffer.timestamp);

            DEBUG("dequeue one output frame, mInputBufferCount: %d, mOutputBufferCount: %d, mOutputBufferRecycleCount: %d, buffer[%d]: %p, size %d, flags 0x%0x\n",
                mInputBufferCount, mOutputBufferCount, mOutputBufferRecycleCount, buffer.index,
                mOutputBuffers[buffer.index].data,
                mOutputBuffers[buffer.index].size,
                mOutputBuffers[buffer.index].flags);

        }
    }while (ioctlRet == 0);
}

void VideoEncodeV4l2::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    uint32_t i = 0;
    MMAutoLock locker(mLock); // big locker
    int ioctlRet = -1;

    if (!mDevicePollThread) {
        mDevicePollThread.reset (new DevicePollThread(this), MMThread::releaseHelper);
        mDevicePollThread->create();
    }

    DEBUG("mConfigured:%d\n", mConfigured);
    if (!mConfigured) {
        setState(kStatePlay); // mState = kStatePlay;

        // start
        __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ioctlRet = mV4l2Encoder->ioctl( VIDIOC_STREAMON, &type);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctlRet = mV4l2Encoder->ioctl( VIDIOC_STREAMON, &type);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMON);

        // setup input buffers
        // we needn't malloc the buffer for frames, but ref the data from MediaBuffer
        struct v4l2_requestbuffers reqbufs;
        memset(&reqbufs, 0, sizeof(reqbufs));
        reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        reqbufs.memory = mInputMemoryType;
        reqbufs.count = mInputQueueCapacity;
        ioctlRet = mV4l2Encoder->ioctl( VIDIOC_REQBUFS, &reqbufs);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
        ASSERT(reqbufs.count);
        mInputQueueCapacity = reqbufs.count;
        // mInputFrames.resize(mInputQueueCapacity);
#if 0
        for (auto &it : mInputFrames) {
            it.mHandle = nullptr; //make sure init correctly
            DEBUG("allocate input buffer %p", it.mHandle);
        }
#endif
        DEBUG("mInputQueueCapacity: %d", mInputQueueCapacity);

        // setup output buffers
        memset(&reqbufs, 0, sizeof(reqbufs));
        reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        reqbufs.memory = mOutputMemoryType;
        reqbufs.count = mOutputQueueCapacity;
        ioctlRet = mV4l2Encoder->ioctl( VIDIOC_REQBUFS, &reqbufs);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);
        ASSERT(reqbufs.count);
        mOutputQueueCapacity = reqbufs.count;
        mOutputBuffers.resize(mOutputQueueCapacity);
        DEBUG("mOutputQueueCapacity: %d", mOutputQueueCapacity);

        for (i=0; i<mOutputQueueCapacity; i++) {
            struct v4l2_plane planes[1];
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(buffer));
            memset(planes, 0, sizeof(planes));
            buffer.index = i;
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buffer.memory = mOutputMemoryType;
            buffer.m.planes = planes;
            buffer.length = 1;
            ioctlRet = mV4l2Encoder->ioctl( VIDIOC_QUERYBUF, &buffer);
            CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QUERYBUF);
            DEBUG("output buffer[%d] size %d, offset %d",
                i, buffer.m.planes[0].length, buffer.m.planes[0].m.mem_offset);
            // length and mem_offset should be filled by VIDIOC_QUERYBUF above
            void* address = mV4l2Encoder->mmap(NULL, buffer.m.planes[0].length,
                                PROT_READ | PROT_WRITE, MAP_SHARED, buffer.m.planes[0].m.mem_offset);
            ASSERT(address);
            mOutputBuffers[i].data = static_cast<uint8_t*>(address);
            // ignore length here
            DEBUG("allocate mOutputBuffers[%d].data %p\n", i, address);
        }

        mMonitorWrite.reset(new Monitor("VEV4L2OutBuffer"));
        // feed input frames first
        for (i=0; i<mInputQueueCapacity; i++) {
            mInputIndces.push(i);
        }
        postMsg(VEV4L2_MSG_handleInputBuffer, 0, NULL, 0);

        for (i=0; i<mOutputQueueCapacity; i++) {
            // mOutputIndces.push(i);
            queueOutputBuffer(i);
        }
        postMsg(VEV4L2_MSG_handleOutputBuffer, 0, NULL, 0);


        // mState = kStatePlaying;
        setState(kStatePlaying);
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        mDevicePollThread->signalContinue();
        mConfigured = true;
        mIsCodecDataSet = false;

    }

}

void VideoEncodeV4l2::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    // MMAutoLock locker(mLock);
    int ioctlRet = -1;

    DEBUG("mConfigured=%d, mState=%d\n", mConfigured, mState);
    if (!mConfigured || mState == kStateStopped || mState == kStateStopping){
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    setState(kStateStopping);
    mV4l2Encoder->setDevicePollInterrupt();

    if (mDevicePollThread) {
        mDevicePollThread->signalExit();
        mDevicePollThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mDevicePollThread
    }

    // Return the buffer to source component
    for (auto &it : mInputFrames) {
#ifndef __MM_NATIVE_BUILD__
        it.mHandle = nullptr;
#endif
        it.mBuffer.reset();
    }
    mInputBufferStaging.clear();
    mInputMediaBufferStaging.clear();

    // release queued input/output buffer
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Encoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutputMemoryType;
    reqbufs.count = 0;
    ioctlRet = mV4l2Encoder->ioctl(VIDIOC_REQBUFS, &reqbufs);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_REQBUFS);

    // stop input port
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctlRet = mV4l2Encoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    // stop output port
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctlRet = mV4l2Encoder->ioctl(VIDIOC_STREAMOFF, &type);
    CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_STREAMOFF);

    mReader.reset();
    mWriter.reset();

    mConfigured = false;
    setState(kStateNull);
    mEosState = kNoneEOS;
    mInputBufferCount = 0;
    mInputBufferDropedCount = 0;
    mOutputBufferCount = 0;
    mOutputBufferRecycleCount = 0;
    mIsCodecDataSet = false;

    mStrideX = 0;
    mStrideY = 0;

    setState(kStateStopped);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoEncodeV4l2::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (!mConfigured) {
        notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }
#if 0
    DEBUG("do_real_flush");

    // return the buffer to source component
    for (auto &it : mInputFrames) {
        it.mHandle = nullptr; //make sure init correctly
        it.mBuffer.reset();
    }


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


    while(!mInputIndces.empty())
        mInputIndces.pop();
    // the ownership of input buffer is client
    for (i=0; i<mInputQueueCapacity; i++) {
        mInputIndces.push(i);
    }

    // feed output frame to V4L2Codec
    for (i=0; i<mOutputQueueCapacity; i++) {
        struct v4l2_buffer buffer;
        struct v4l2_plane planes[1];

        memset(&buffer, 0, sizeof(buffer));
        memset(planes, 0, sizeof(planes));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //it indicates output buffer type
        buffer.memory = mOutputMemoryType;
        buffer.m.planes = planes;
        buffer.length = 1;
        buffer.index = i;


        int ioctlRet = mV4l2Decoder->ioctl(VIDIOC_QBUF, &buffer);
        CHECK_V4L2_CMD_RESULT(ioctlRet, VIDIOC_QBUF);
    }
#endif

    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoEncodeV4l2::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    // FIXME, onStop wait until the pthread_join of mDevicePollThread
    onStop(param1, param2, rspId);

    // close device
    mV4l2Encoder.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

// boilplate for MMMsgThread and Component
mm_status_t VideoEncodeV4l2::init()
{
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret)
        return MM_ERROR_OP_FAILED;

    return MM_ERROR_SUCCESS;
}

void VideoEncodeV4l2::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}

const char * VideoEncodeV4l2::name() const
{
    return mComponentName.c_str();
}

mm_status_t VideoEncodeV4l2::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, "is-live-stream") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mIsLive = (bool)item.mValue.ii;
            INFO("key: %s, value: %p\n", item.mName, item.mValue.ii);
        } else if ( !strcmp(item.mName, "live-stream-drop-threshhold2") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mDropFrameThreshHold2 = item.mValue.ii;
            if (mDropFrameThreshHold2<3)
                mDropFrameThreshHold2 = 3;
            INFO("key: %s, value: %d", item.mName, item.mValue.ii);
        } else if ( !strcmp(item.mName, MEDIA_ATTR_BIT_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mBitrate = item.mValue.ii;
            INFO("key: %s, value: %d", item.mName, item.mValue.ii);
        } else if ( !strcmp(item.mName, MEDIA_ATTR_IDR_FRAME) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            INFO("key: %s, value: %d", item.mName, item.mValue.ii);
            if (!strcmp(item.mValue.str, "yes")) {
                requestIDR();
            }
        }
    }
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoEncodeV4l2::getParameter(MediaMetaSP & meta) const
{
    FUNC_TRACK();
    WARNING("setParameter isn't supported yet\n");
    return MM_ERROR_SUCCESS;
}

// //////// for component factory
extern "C" {
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    FUNC_TRACK();
    if (strcmp(mimeType, "video/avc")) {
        mimeType = "video/h264";
    }
    YUNOS_MM::VideoEncodeV4l2 *VideoEncodeV4l2Component = new YUNOS_MM::VideoEncodeV4l2(mimeType, isEncoder);
    if (VideoEncodeV4l2Component == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(VideoEncodeV4l2Component);
}


void releaseComponent(YUNOS_MM::Component *component) {
    FUNC_TRACK();
    delete component;
}
} // extern "C"

} // YUNOS_MM

