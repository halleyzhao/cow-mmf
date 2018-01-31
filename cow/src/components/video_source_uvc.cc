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


#include "video_source_uvc.h"
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
//#include <pthread.h>

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
//#define DUMP_UVC_DATA

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("VSUVC")

static const char * COMPONENT_NAME = "VideoSourceUVC";
static const char * MMTHREAD_NAME = "VideoSourceUVC::Private::InputThread";

#define CLOCK_TIME_NONE         -1

#define DEFAULT_BUF_TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE
#define DEFAULT_PIX_FMT V4L2_PIX_FMT_YUYV
#define DEFAULT_DATA_MODE CAMERA_DATA_MODE_MMAP
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480
#define DEFAULT_FRAME_RATE 30.0f
#define DEFAULT_COLOR_FORMAT 'YUYV'
#define DEFAULT_FRAME_PTS 0
#define DEFAULT_FRAME_DTS 0
#define DEFAULT_FRAME_DURATION 0.0f
#define DEFAULT_FRAME_COUNT 5
#define DEFAULT_FRAME_BUFFER_SIZE 0

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define setState(_param, _deststate) do {\
    INFO("change state from %d to %s\n", _param, #_deststate);\
    (_param) = (_deststate);\
}while(0)

#define IOCTL_CHECK_RET(fd, cmd, cmdStr, arg, retVal)  do {     \
        int ret = 0;                                            \
        do {                                                    \
            ret = ioctl(fd, cmd, &(arg));                       \
        } while (-1 == ret && EINTR == errno);                  \
                                                                \
        if (-1 == ret) {                                        \
            ERROR("io cmd %s failed", cmdStr);                  \
            return retVal;                                      \
        }                                                       \
    }while(0)

class VideoSourceUVC::Private
{
  public:

    enum state_t {
        STATE_IDLE,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_PAUSED,
        STATE_STOPED,
    };

    enum CameraDataMode{
        CAMERA_DATA_MODE_MMAP,
        // CAMERA_DATA_MODE_DMABUF_MMAP,
        // CAMERA_DATA_MODE_USRPTR,
        // CAMERA_DATA_MODE_DMABUF_USRPTR,
    };

    class VideoSrcReader : public Reader {
    public:
        VideoSrcReader(VideoSourceUVC * src){
            mSrc = src;
        }
        ~VideoSrcReader(){
            MMAutoLock locker(mSrc->mPriv->mLock);
            mSrc->mPriv->mCondition.signal();
        }

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        VideoSourceUVC *mSrc;
    };

/* start of inputthread*/
    class InputThread;
    typedef MMSharedPtr <InputThread> InputThreadSP;
    class InputThread : public MMThread {
    public:

        InputThread(Private* priv)
          : MMThread(MMTHREAD_NAME)
          , mPriv(priv)
          , mContinue(true)
        {
          ENTER();
          EXIT();
        }

        ~InputThread()
        {
          ENTER();
          EXIT();
        }

        void signalExit()
        {
          ENTER();
          MMAutoLock locker(mPriv->mLock);
          mContinue = false;
          mPriv->mCondition.signal();
          EXIT();
        }

        void signalContinue()
        {
          ENTER();
          mPriv->mCondition.signal();
          EXIT();
        }

        static bool releaseInputBuffer(MediaBuffer* mediaBuffer)
        {
            uint8_t *buffer = NULL;
            if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
                WARNING("error in release mediabuffer");
                return false;
            }
            MM_RELEASE_ARRAY(buffer);
            return true;
        }

        protected:

        // Read Raw data from UVC
        void main()
        {
            ENTER();
            int index;
            while(1) {
                {
                    MMAutoLock locker(mPriv->mLock);
                    if (!mContinue) {
                        MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
                        mediaBuf->setSize((int64_t)0);
                        mediaBuf->setPts(mPriv->mPTS);
                        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
                        mPriv->mAvailableSourceBuffers.push(mediaBuf);
                        mPriv->mCondition.signal();
                        break;
                    }
                    if (mPriv->mIsPaused) {
                        VERBOSE("waitting condition\n");
                        mPriv->mCondition.wait();
                        VERBOSE("wakeup condition\n");
                        continue;
                    }
                }
                index = -1;
                index = mPriv->dequeFrame();
                if( index == -1 || (uint32_t)index > mPriv->mFrameBufferCount)
                    continue;
                MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
                uint8_t *buffer = NULL;
                buffer = new uint8_t[mPriv->mFrameBufferSize];
                memcpy(buffer, mPriv->mFrameBuffers[index], mPriv->mFrameBufferSize);

#ifdef DUMP_UVC_DATA
                fwrite(buffer, 1, mPriv->mFrameBufferSize, mPriv->mDumpFile);
#endif
                mediaBuf->setPts(mPriv->mDTS);
                //mediaBuf->setDuration(mPriv->mDTS);
                mediaBuf->setDts(mPriv->mDTS);
                mPriv->mPTS += mPriv->mFrameDuration;
                mPriv->mDTS += mPriv->mFrameDuration;
                mediaBuf->setBufferInfo((uintptr_t *)&buffer, NULL, (int32_t *)&mPriv->mFrameBufferSize, 1);
                mediaBuf->setSize((int64_t)mPriv->mFrameBufferSize);
                mediaBuf->addReleaseBufferFunc(releaseInputBuffer);
                {
                    MMAutoLock locker(mPriv->mLock);
                    mPriv->mAvailableSourceBuffers.push(mediaBuf);
                    mPriv->mCondition.signal();
                }
                mPriv->enqueFrame(index);

            }

            INFO("Input thread exited\n");
            EXIT();
        }

      private:
        VideoSourceUVC::Private *mPriv;
        bool mContinue;
    };
/* end of InputThread*/

    static PrivateSP create()
    {
        ENTER();
        PrivateSP priv(new Private());
        if (priv) {
            INFO("private create success");
        }
        return priv;
    }

    mm_status_t init(VideoSourceUVC *Source) {
        ENTER();
        mVideoSource = Source;
#ifdef DUMP_UVC_DATA
        mDumpFile = fopen("/data/video_source_uvc.yuv","wb");
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    mm_status_t uninit() {
#ifdef DUMP_UVC_DATA
        fclose(mDumpFile);
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    ~Private() {}

    mm_status_t openDevice();
    mm_status_t initMmap();
    mm_status_t initBuffers();
    mm_status_t stopCapture();
    mm_status_t closeDevice();
    uint32_t dequeFrame();
    mm_status_t enqueFrame(uint32_t index);
    mm_status_t release();

    void clearSourceBuffers();
    mm_status_t resumeInternal();


    std::queue<MediaBufferSP> mAvailableSourceBuffers;
    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    state_t mState;

    InputThreadSP mInputThread;
    VideoSourceUVC *mVideoSource;
    MediaMetaSP mMetaData;

    uint64_t mDoubleDuration;

    uint32_t mBufType;
    uint32_t mPixFMT;
    uint32_t mDataMode;
    uint32_t mWidth;
    uint32_t mHeight;
    float mFrameRate;
    uint32_t mVideoFourcc;
    uint64_t mPTS;
    uint64_t mDTS;
    uint64_t mFrameDuration;

    int32_t mDevice;
    std::vector<uint8_t*> mFrameBuffers;
    uint32_t mFrameBufferCount;
    uint32_t mFrameBufferSize;
    std::string mUri;

#ifdef DUMP_UVC_DATA
            FILE* mDumpFile;
#endif

    Private()
        :mIsPaused(true),
        mCondition(mLock),
        mState(STATE_IDLE),
        mDoubleDuration(0),
        mBufType(DEFAULT_BUF_TYPE),
        mPixFMT(DEFAULT_PIX_FMT),
        mDataMode(DEFAULT_DATA_MODE),
        mWidth(DEFAULT_WIDTH),
        mHeight(DEFAULT_HEIGHT),
        mFrameRate(DEFAULT_FRAME_RATE),
        mVideoFourcc(DEFAULT_COLOR_FORMAT),
        mPTS(DEFAULT_FRAME_PTS),
        mDTS(DEFAULT_FRAME_DTS),
        mFrameDuration(DEFAULT_FRAME_DURATION),
        mDevice(-1),
        mFrameBufferCount(DEFAULT_FRAME_COUNT),
        mFrameBufferSize(DEFAULT_FRAME_BUFFER_SIZE)
    {
        ENTER();
        mMetaData = MediaMeta::create();
        EXIT();
    }

    MM_DISALLOW_COPY(Private);

};

#define ASP_MSG_prepare (msg_type)1
#define ASP_MSG_start (msg_type)2
#define ASP_MSG_resume (msg_type)3
#define ASP_MSG_pause (msg_type)4
#define ASP_MSG_stop (msg_type)5
#define ASP_MSG_flush (msg_type)6
#define ASP_MSG_reset (msg_type)7
#define ASP_MSG_setParameters (msg_type)8
#define ASP_MSG_getParameters (msg_type)9

BEGIN_MSG_LOOP(VideoSourceUVC)
    MSG_ITEM(ASP_MSG_prepare, onPrepare)
    MSG_ITEM(ASP_MSG_start, onStart)
    MSG_ITEM(ASP_MSG_resume, onResume)
    MSG_ITEM(ASP_MSG_pause, onPause)
    MSG_ITEM(ASP_MSG_stop, onStop)
    MSG_ITEM(ASP_MSG_flush, onFlush)
    MSG_ITEM(ASP_MSG_reset, onReset)
    MSG_ITEM(ASP_MSG_setParameters, onSetParameters)
    MSG_ITEM(ASP_MSG_getParameters, onGetParameters)
END_MSG_LOOP()

VideoSourceUVC::VideoSourceUVC(const char *mimeType, bool isEncoder) :MMMsgThread(COMPONENT_NAME)
                                                                ,mComponentName(COMPONENT_NAME)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no priv");
}

VideoSourceUVC::~VideoSourceUVC()
{
    //release();
}

Component::ReaderSP VideoSourceUVC::getReader(MediaType mediaType)
{
     ENTER();
     if ( (int)mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::ReaderSP((Component::Reader*)NULL);
        }

    Component::ReaderSP rsp(new VideoSourceUVC::Private::VideoSrcReader(this));
    return rsp;
}

mm_status_t VideoSourceUVC::init()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    int ret = mPriv->init(this); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoSourceUVC::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();
    EXIT();
}

const char * VideoSourceUVC::name() const
{
    return mComponentName.c_str();
}

mm_status_t VideoSourceUVC::signalEOS() {
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
    }
    if (mPriv->mInputThread) {
        mPriv->mInputThread->signalExit();
        mPriv->mInputThread.reset();
    }
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::setUri(const char * uri,
                            const std::map<std::string, std::string> * headers/* = NULL*/)
{
    MMLOGI("uri: %s\n", uri);
    MMAutoLock locker(mPriv->mLock);
    mPriv->mUri = uri;
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::prepare()
{
    postMsg(ASP_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::start()
{
    postMsg(ASP_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::resume()
{
    postMsg(ASP_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::stop()
{
    postMsg(ASP_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::pause()
{
    postMsg(ASP_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::reset()
{
    postMsg(ASP_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::flush()
{
    postMsg(ASP_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSourceUVC::Private::VideoSrcReader::read(MediaBufferSP & buffer)
{
    ENTER();
    MMAutoLock locker(mSrc->mPriv->mLock);
    if (mSrc->mPriv->mAvailableSourceBuffers.empty()) {
        mSrc->mPriv->mCondition.timedWait(mSrc->mPriv->mDoubleDuration);
        EXIT_AND_RETURN(MM_ERROR_AGAIN);
    } else {
        buffer = mSrc->mPriv->mAvailableSourceBuffers.front();
        mSrc->mPriv->mAvailableSourceBuffers.pop();
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
}

MediaMetaSP VideoSourceUVC::Private::VideoSrcReader::getMetaData()
{
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_WIDTH, mSrc->mPriv->mWidth);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_HEIGHT, mSrc->mPriv->mHeight);
    mSrc->mPriv->mMetaData->setFloat(MEDIA_ATTR_FRAME_RATE, mSrc->mPriv->mFrameRate);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_COLOR_FOURCC, mSrc->mPriv->mVideoFourcc);
    //mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_COLOR_FOURCC, V4L2_PIX_FMT_YUYV);

    return mSrc->mPriv->mMetaData;
}

mm_status_t VideoSourceUVC::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    MMAutoLock locker(mPriv->mLock);

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_WIDTH) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mWidth = item.mValue.ii;
            INFO("key: %s, value: %d\n", item.mName, mPriv->mWidth);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_HEIGHT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mHeight = item.mValue.ii;
            INFO("key: %s, value: %d\n", item.mName, mPriv->mHeight);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_FRAME_RATE) ) {
            if ( item.mType != MediaMeta::MT_Float ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mFrameRate = item.mValue.f;
            INFO("key: %s, value: %" PRId64 "\n", item.mName, mPriv->mFrameRate);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_COLOR_FOURCC) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mVideoFourcc = item.mValue.ii;
            INFO("key: %s, value: 0x%0x\n", item.mName, mPriv->mVideoFourcc);
            DEBUG_FOURCC(NULL, mPriv->mVideoFourcc);
            continue;
        }
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t VideoSourceUVC::getParameter(MediaMetaSP & meta) const
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;


    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoSourceUVC::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    mm_status_t ret = mPriv->openDevice();
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to open device\n");
        notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT1();
    }

    ret = mPriv->initBuffers();
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to start capture\n");
        notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT1();
    }

    mPriv->mFrameDuration = (1000.0f / mPriv->mFrameRate) * 1000;
    mPriv->mDoubleDuration = mPriv->mFrameDuration * 2ll;
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

mm_status_t VideoSourceUVC::Private::resumeInternal()
{
    if (!mIsPaused) {
        ERROR("Aready started\n");
        mVideoSource->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    mIsPaused = false;
    setState(mState, STATE_STARTED);
    mVideoSource->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mInputThread->signalContinue();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoSourceUVC::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    // create thread to handle output buffer
    if (!mPriv->mInputThread) {
        mPriv->mInputThread.reset (new VideoSourceUVC::Private::InputThread(mPriv.get()),MMThread::releaseHelper);
        mPriv->mInputThread->create();
    }
    mPriv->resumeInternal();
    EXIT1();
}

void VideoSourceUVC::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    mPriv->resumeInternal();
    EXIT1();
}

void VideoSourceUVC::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        {
            MMAutoLock locker(mPriv->mLock);
            mPriv->mIsPaused = true;
        }
        if (mPriv->mInputThread) {
            mPriv->mInputThread->signalExit();
            mPriv->mInputThread.reset();
        }
    }
    MMAutoLock locker(mPriv->mLock);
    if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED) {
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }
    setState(mPriv->mState, mPriv->STATE_STOPED);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoSourceUVC::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    mPriv->mIsPaused = true;
    if (mPriv->mState == mPriv->STATE_PAUSED) {
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }
    setState(mPriv->mState, mPriv->STATE_PAUSED);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoSourceUVC::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoSourceUVC::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        {
            MMAutoLock locker(mPriv->mLock);
            mPriv->mIsPaused = true;
        }
        if (mPriv->mInputThread) {
            mPriv->mInputThread->signalExit();
            mPriv->mInputThread.reset();
        }
    }
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->clearSourceBuffers();
    }
    mPriv->release();
    setState(mPriv->mState, mPriv->STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoSourceUVC::onSetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //notify(EVENT_SETPARAMETERSCOMPLETE, MM_ERROR_SUCCESS, 0, NULL);
    EXIT();
}

void VideoSourceUVC::onGetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    EXIT();
}

uint32_t VideoSourceUVC::fourccConvert(uint32_t fourcc) {

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

        default:
            ASSERT(0 && "SHOULD NOT BE HERE");

    }
    return V4L2_PIX_FMT_YUYV;

}
mm_status_t VideoSourceUVC::Private::openDevice()
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    mm_status_t ret = MM_ERROR_INVALID_PARAM;

    INFO();
    mUri = "/dev/video0";
    //mDevice = open(mUri.c_str(), O_RDWR | O_NONBLOCK, 0);
    mDevice = open(mUri.c_str(), O_RDWR | O_NONBLOCK, 0);

    if (-1 == mDevice) {
        ERROR("Cannot open '%s': %d, %s\n", mUri.c_str(), errno, strerror(errno));
        return MM_ERROR_INVALID_PARAM;
    }

    IOCTL_CHECK_RET(mDevice, VIDIOC_QUERYCAP, "VIDIOC_QUERYCAP", cap, false);
    DEBUG("cap.capabilities 0x%0x\n", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        ERROR("the device doesn't support video capture!!");
        return MM_ERROR_INVALID_PARAM;
    }

    switch (mDataMode) {
        case CAMERA_DATA_MODE_MMAP:
            if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                ERROR("camera device does not support video streaming\n");
                return MM_ERROR_INVALID_PARAM;
            }
            break;
        default:
            ERROR("unsupported camera data mode");
            break;
    }


    // set video format and resolution, XXX get supported formats/resolutions first
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = mWidth;
    fmt.fmt.pix.height      = mHeight;
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.pixelformat = fourccConvert(mVideoFourcc);
    IOCTL_CHECK_RET(mDevice, VIDIOC_S_FMT, "VIDIOC_S_FMT", fmt, false);
    DEBUG("video resolution: %dx%d = %dx%d, format 0x%0x:0x%0x",
        mWidth, mHeight, fmt.fmt.pix.width, fmt.fmt.pix.height, mVideoFourcc, fmt.fmt.pix.pixelformat);
    IOCTL_CHECK_RET(mDevice, VIDIOC_G_FMT, "VIDIOC_G_FMT", fmt, false);
    if (mWidth != fmt.fmt.pix.width || mHeight != fmt.fmt.pix.height) {
        ERROR("not supported resolution(%dx%d), use %dx%d from camera\n", mWidth, mHeight, fmt.fmt.pix.width, fmt.fmt.pix.height);
        mWidth = fmt.fmt.pix.width;
        mHeight = fmt.fmt.pix.height;
    }

    switch (mDataMode) {
        case CAMERA_DATA_MODE_MMAP:
            ret = initMmap();
            break;
        default:
            ERROR("unsupported yet");
            break;
    }

    return ret;
}

mm_status_t VideoSourceUVC::Private::initMmap()
{
    struct v4l2_requestbuffers rqbufs;
    uint32_t index;

    INFO();
    memset(&rqbufs, 0, sizeof(rqbufs));
    rqbufs.count = mFrameBufferCount;
    rqbufs.memory = V4L2_MEMORY_MMAP;
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    IOCTL_CHECK_RET(mDevice, VIDIOC_REQBUFS, "VIDIOC_REQBUFS", rqbufs, false);
    INFO("rqbufs.count: %d\n", rqbufs.count);

    mFrameBuffers.resize(rqbufs.count);
    mFrameBufferCount = rqbufs.count;

    DEBUG("map video frames: ");
    for (index = 0; index < rqbufs.count; ++index) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = index;

        IOCTL_CHECK_RET(mDevice, VIDIOC_QUERYBUF, "VIDIOC_QUERYBUF", buf, false);
        if (mFrameBufferSize)
            ASSERT(mFrameBufferSize == buf.length);
        mFrameBufferSize = buf.length;

        mFrameBuffers[index]= (uint8_t*)mmap(NULL, buf.length,
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  mDevice, buf.m.offset);

        if (MAP_FAILED == mFrameBuffers[index]) {
            ERROR("mmap failed");
            return MM_ERROR_INVALID_PARAM;
        }

        DEBUG("index: %d, buf.length: %d, addr: %p", buf.index, buf.length, mFrameBuffers[index]);
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::Private::initBuffers()
{
    unsigned int i;
    enum v4l2_buf_type type;

    INFO();
    switch (mDataMode) {
        case CAMERA_DATA_MODE_MMAP:
            for (i = 0; i < mFrameBufferCount; ++i) {
                struct v4l2_buffer buf;
                memset(&buf, 0, sizeof(buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                IOCTL_CHECK_RET(mDevice, VIDIOC_QBUF, "VIDIOC_QBUF", buf, false);
            }

            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            IOCTL_CHECK_RET(mDevice, VIDIOC_STREAMON, "VIDIOC_STREAMON", type, false);
            INFO("STREAMON ok\n");
            break;
        default:
            return MM_ERROR_INVALID_PARAM;
            //break;
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::Private::stopCapture()
{
    enum v4l2_buf_type type;

    INFO();
    switch (mDataMode) {
        case CAMERA_DATA_MODE_MMAP:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            IOCTL_CHECK_RET(mDevice, VIDIOC_STREAMOFF, "VIDIOC_STREAMOFF", type, false);
            break;
        default:
            return MM_ERROR_INVALID_PARAM;
            break;
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::Private::closeDevice()
{
    unsigned int i;

    INFO();
    switch (mDataMode) {
        case CAMERA_DATA_MODE_MMAP:
            for (i = 0; i < mFrameBufferCount; ++i)
                if (-1 == munmap(mFrameBuffers[i], mFrameBufferSize)) {
                    ERROR("munmap failed\n");
                    return MM_ERROR_INVALID_PARAM;
                }
            break;
        default:
            return MM_ERROR_INVALID_PARAM;
            break;
    }

    if (-1 == close(mDevice)) {
        ERROR("close device failed\n");
        return MM_ERROR_INVALID_PARAM;
    }

    mDevice = -1;
    return MM_ERROR_SUCCESS;
}

uint32_t VideoSourceUVC::Private::dequeFrame()
{
    struct v4l2_buffer buf;
    int ret = 0;

    INFO();
    memset(&buf, 0, sizeof(buf));

    switch (mDataMode) {
    case CAMERA_DATA_MODE_MMAP: {
        // poll until there is available frames
        while(1) {
            fd_set fds;
            struct timeval tv;

            FD_ZERO(&fds);
            FD_SET(mDevice, &fds);

            /* Timeout. */
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            ret= select(mDevice + 1, &fds, NULL, NULL, &tv);

            if (-1 == ret) {
                if (EINTR == errno)
                    continue;
                ERROR("select failed");
                return -1;
            } else if (0 == ret) {
                ERROR("select timeout\n");
                return -1;
            } else
                break;
        }

        DEBUG("get one frame");
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = mFrameBufferSize;

        do {
            ret = ioctl(mDevice, VIDIOC_DQBUF, &buf);
            if (-1 == ret) {
                usleep(5000);
            } else
                break;
        } while (errno == EAGAIN || EINTR == errno);

        ASSERT(ret != -1);
        ASSERT(buf.index < mFrameBufferCount);
    }
    break;
    default:
        ASSERT(0);
        break;
    }

    return buf.index;
}

mm_status_t VideoSourceUVC::Private::enqueFrame(uint32_t index)
{
    assert(index >=0 && index < mFrameBufferCount);
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));

    switch (mDataMode) {
    case CAMERA_DATA_MODE_MMAP:
        DEBUG("recycle one frame (index: %d)\n", index);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;
        buf.length = mFrameBufferSize;

        IOCTL_CHECK_RET(mDevice, VIDIOC_QBUF, "VIDIOC_QBUF", buf, false);
        break;
    default:
        ASSERT(0);
        break;
    }
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSourceUVC::Private::release()
{
    ENTER();
    mm_status_t ret;
    ret = closeDevice();
    clearSourceBuffers();

    EXIT_AND_RETURN(ret);
}

void VideoSourceUVC::Private::clearSourceBuffers()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    //INFO("createComponent");
    YUNOS_MM::VideoSourceUVC *sourceComponent = new YUNOS_MM::VideoSourceUVC(mimeType, isEncoder);
    if (sourceComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sourceComponent);
}


void releaseComponent(YUNOS_MM::Component *component)
{
    //INFO("createComponent");
    delete component;
}
}
