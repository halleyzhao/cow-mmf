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
#include <stdio.h>

#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mmthread.h"

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "multimedia/mm_surface_compat.h"
#endif

#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#include <hardware/gralloc.h>
#endif

#include "video_filter_example.h"


#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(VideoFilterExample)

static const char * COMPONENT_NAME = "VideoFilterExample";
static const char * MMTHREAD_NAME = "PollThread";

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)


BEGIN_MSG_LOOP(VideoFilterExample)
    MSG_ITEM(MSG_prepare, onPrepare)
    MSG_ITEM(MSG_start, onStart)
    MSG_ITEM(MSG_stop, onStop)
    MSG_ITEM(MSG_flush, onFlush)
    MSG_ITEM(MSG_reset, onReset)
    MSG_ITEM(MSG_addSource, onAddSource)
END_MSG_LOOP()


//////////////////////// PollThread
class PollThread : public MMThread {
  public:
    PollThread(VideoFilterExample * com)
        : MMThread(MMTHREAD_NAME, true)
        , mComponent(com)
        , mCondition(mComponent->mLock)
        , mContinue(true)
    {
        FUNC_TRACK();
    }

    ~PollThread()
    {
        FUNC_TRACK();
    }

    void signalExit()
    {
        FUNC_TRACK();
        MMAutoLock locker(mComponent->mLock);
        mContinue = false;
        mCondition.signal();
    }

    void signalContinue_l()
    {
        FUNC_TRACK();
        mCondition.signal();
    }

  protected:
    virtual void main();

  private:
    Lock mLock;
    VideoFilterExample * mComponent;
    Condition mCondition;         // cork/uncork on pause/resume
    bool mContinue;     // terminate the thread
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(PollThread)

// poll the device for notification of available input/output buffer index
#define RETRY_COUNT 500
void PollThread::main()
{
    FUNC_TRACK();
    MediaBufferSP buffer;
    mm_status_t status;
    int retry = 0;

    while(1) {
        {
            MMAutoLock locker(mComponent->mLock);
            if (!mContinue) {
                break;
            }

            if (mComponent->mState != VideoFilterExample::StateType::STATE_STARTED) {
                INFO("PollThread waitting\n");
                mCondition.wait();
                INFO("PollThread wakeup \n");
                continue;
            }
        }

        //read buffer from camera source
        do {
            if (!mContinue) {
                buffer.reset();
                EXIT();
            }
            status = mComponent->mReader->read(buffer);
        } while((status == MM_ERROR_AGAIN) && (retry++ < RETRY_COUNT));

        ASSERT(buffer);
        DEBUG("source media buffer size %d pts %" PRId64 " dts %" PRId64 "",
                buffer->size(), buffer->pts(), buffer->dts());

        uint8_t *sourceBuf = NULL;
        int32_t offset = 0;
        int32_t length = 0;

        static int32_t width = 0;
        static int32_t height = 0;
        int ret = 0;
        if (!width) {
            mComponent->mInputFormat->getInt32(MEDIA_ATTR_WIDTH, width);
            DEBUG("got width %d", width);
        }
        if (!height) {
            mComponent->mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
            DEBUG("got height %d", height);
        }

        if (buffer->type() != MediaBuffer::MBT_GraphicBufferHandle) {
            MMAutoLock locker(mComponent->mLock);
            mComponent->mAvailableSourceBuffers.push(buffer);
            mComponent->mCondition.signal();
            continue;
        }

        ret = buffer->getBufferInfo((uintptr_t *)&sourceBuf, &offset, &length, 1);
        if (ret && sourceBuf && length) {

            MMBufferHandleT *handle = (MMBufferHandleT *)(sourceBuf);
#ifdef __MM_YUNOS_YUNHAL_BUILD__
            static YunAllocator &allocator(YunAllocator::get());
#else
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE

            static const hw_module_t *mModule = NULL;
            static gralloc_module_t *allocator;
            if (mModule == NULL) {
                int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mModule);
                ASSERT(err == 0);
                ASSERT(mModule);
                allocator = (gralloc_module_t*)mModule;
                ASSERT(allocator);
            }
#else
            static YunAllocator &allocator(YunAllocator::get());
#endif
#endif
            void* vaddr = NULL;

#ifdef __MM_YUNOS_YUNHAL_BUILD__
            int ret = allocator.lock(*handle, ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
                0, 0, width, height, &vaddr);
#else
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
            int ret = allocator->lock(allocator, *handle,
                ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
                0, 0, width, height, &vaddr);
#else
            int ret = allocator.lock(*handle, ALLOC_USAGE_PREF(SW_READ_OFTEN) | ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
                0, 0, width, height, &vaddr);
#endif
#endif
            ASSERT(ret == 0);

            // do stuff, dig hole
            uint8_t *y = (uint8_t*)vaddr;
            int i = height*3/8;
            for (; i < height*5/8; i++) {
                memset(y+i*width+width*3/8, 0, width/4);
            }
            // uint8_t *uv = (uint8_t*)vaddr + width * height;

#ifdef __MM_YUNOS_YUNHAL_BUILD__
            ret = allocator.unlock(*handle);
#else
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
            ret = allocator->unlock(allocator, *handle);
#else
            ret = allocator.unlock(*handle);
#endif
#endif
            ASSERT(ret == 0);

        } else {
            WARNING("cannot get source buffer info");

            if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
                DEBUG("got eos buffer");
            } else {
                EXIT();
            }
        }


        {
            MMAutoLock locker(mComponent->mLock);
            mComponent->mAvailableSourceBuffers.push(buffer);
            mComponent->mCondition.signal();
        }
    }

    INFO("Poll thread exited\n");
}



mm_status_t VideoFilterExample::GrayReader::read(MediaBufferSP & buffer)
{
    ENTER();
    MMAutoLock locker(mComponent->mLock);
    if (mComponent->mAvailableSourceBuffers.empty()) {
        mComponent->mCondition.wait();
    }

    //notify, but not by available buffer
    if (mComponent->mAvailableSourceBuffers.empty()) {
        EXIT_AND_RETURN(MM_ERROR_AGAIN);
    }

    buffer = mComponent->mAvailableSourceBuffers.front();
    mComponent->mAvailableSourceBuffers.pop();
    static int count = 0;
    VERBOSE("buffer count %d", ++count);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

MediaMetaSP VideoFilterExample::GrayReader::getMetaData()
{
    mComponent->mInputFormat->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    return mComponent->mInputFormat;
}


VideoFilterExample::VideoFilterExample() : MMMsgThread(COMPONENT_NAME)
                          , mCondition(mLock)
{
    mInputFormat = MediaMeta::create();
}

VideoFilterExample::~VideoFilterExample()
{
}

mm_status_t VideoFilterExample::addSource(Component * component, MediaType mediaType) {
    ENTER();

    if (mediaType != kMediaTypeVideo)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    param1_type rsp_param1;
    param2_type rsp_param2;

    if (sendMsg(MSG_addSource, 0, component, &rsp_param1, &rsp_param2)) {
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    if (rsp_param1)
        EXIT_AND_RETURN(rsp_param1);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

Component::ReaderSP VideoFilterExample::getReader(MediaType mediaType)
{
     ENTER();
     if ( mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::ReaderSP((Component::Reader*)NULL);
        }

    Component::ReaderSP rsp(new VideoFilterExample::GrayReader(this));
    return rsp;
}

mm_status_t VideoFilterExample::init()
{
    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoFilterExample::uninit()
{
    ENTER();
    MMMsgThread::exit();
    EXIT();
}


mm_status_t VideoFilterExample::prepare()
{
    postMsg(MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterExample::start()
{
    postMsg(MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterExample::stop()
{
    postMsg(MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoFilterExample::reset()
{
    postMsg(MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterExample::flush()
{
    postMsg(MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}


mm_status_t VideoFilterExample::setParameter(const MediaMetaSP & meta)
{
    ENTER();

    MMAutoLock locker(mLock);
    int ret = MM_ERROR_SUCCESS;

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        #if 0
        if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mSampleRate = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mSampleRate);
            ret = MM_ERROR_SUCCESS;
        }
        #endif
    }

    EXIT_AND_RETURN(ret);

}

void VideoFilterExample::setState(int state)
{
    mState = (StateType)state;
    DEBUG("mState set to %d", state);
}

void VideoFilterExample::onAddSource(param1_type param1, param2_type param2, uint32_t rspId)
{

    Component *component = (Component*)param2;
    mReader = component->getReader(kMediaTypeVideo);
    mm_status_t status = MM_ERROR_SUCCESS;
    MediaMetaSP meta;
    // int32_t bitrate = 0;
    int32_t fps = 30;
    int32_t colorFormat = 0;
    int32_t fourcc = 0;
    float srcFps;
    int32_t width = 0;
    int32_t height = 0;
    if (!mReader) {
        status = MM_ERROR_INVALID_PARAM;
        goto RETURN;
    }

    meta = mReader->getMetaData();
    if (!meta) {
        status = MM_ERROR_INVALID_PARAM;
        goto RETURN;
    }

    mInputFormat->dump();


    // sanity check
    if (meta->getInt32(MEDIA_ATTR_WIDTH, width)) {
        DEBUG("get meta data, width is %d", width);
        mInputFormat->setInt32(MEDIA_ATTR_WIDTH, width);
    }

    if (meta->getInt32(MEDIA_ATTR_HEIGHT, height)) {
        DEBUG("get meta data, height is %d", height);
        mInputFormat->setInt32(MEDIA_ATTR_HEIGHT, height);
    }

    if (meta->getFloat(MEDIA_ATTR_FRAME_RATE, srcFps)) {
        fps = (int32_t) srcFps;
        DEBUG("get meta data, frame rate is %d", fps);
        mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
    } else {
        DEBUG("use default fps value %d", fps);
        mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
    }

    mInputFormat->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    if (meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat)) {
        DEBUG("get meta data, color foramt is %x", colorFormat);
        mInputFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat);
    }

    if (meta->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc)) {
        DEBUG("get meta data, input fourcc is %08x\n", fourcc);
        DEBUG_FOURCC("fourcc: ", fourcc);
        mInputFormat->setInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
    }

RETURN:
    if (rspId) {
        postReponse(rspId, status, NULL);
        EXIT();
    }

}

void VideoFilterExample::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();

    if (mState >= STATE_PREPARED ) {
        DEBUG("mState is %d", mState);
        notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);

    }

    setState(STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

void VideoFilterExample::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mLock);

    if (mState == STATE_STARTED) {
        ERROR("Aready started\n");
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }

    if (!mPollThread) {
        mPollThread.reset (new PollThread(this), MMThread::releaseHelper);
        mPollThread->create();
    }

    setState(STATE_STARTED);
    mPollThread->signalContinue_l();
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoFilterExample::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();

    {
        MMAutoLock locker(mLock);

        if (mState == STATE_IDLE || mState == STATE_STOPED) {
            notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
            EXIT1();
        }

        // set state first, pool thread will check it
        setState(STATE_STOPED);
    }

    if (mPollThread) {
        mPollThread->signalExit();
        mPollThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mPollThread
    }

    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoFilterExample::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mLock);
    clearSourceBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoFilterExample::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    if (mState == STATE_IDLE) {
        DEBUG("mState is %d", mState);
        notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    }

    {
        MMAutoLock locker(mLock);
        clearSourceBuffers();
    }

    // reset external resource before dynamic lib unload
    mReader.reset();
    setState(STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void VideoFilterExample::clearSourceBuffers()
{
    #if 0
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
    #endif
}

}


extern "C" {

MM_LOG_DEFINE_MODULE_NAME("VideoGrayPlug");

YUNOS_MM::Component * createComponent(const char* mimeType, bool isEncoder)
{
    MMLOGI();
    return new YUNOS_MM::VideoFilterExample();
}

void releaseComponent(YUNOS_MM::Component * component)
{
    MMLOGI("%p\n", component);
    if ( component ) {
        YUNOS_MM::VideoFilterExample * c = DYNAMIC_CAST<YUNOS_MM::VideoFilterExample*>(component);
        MMASSERT(c != NULL);
        MM_RELEASE(c);
    }
}
}

