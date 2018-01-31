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
#include <unistd.h>

#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mmthread.h"

#include "video_filter_gl.h"


#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(VideoFilterGL)

static const char * COMPONENT_NAME = "VideoFilterGL";
static const char * MMTHREAD_NAME = "FilterWorkerThread";

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

BEGIN_MSG_LOOP(VideoFilterGL)
    MSG_ITEM(MSG_prepare, onPrepare)
    MSG_ITEM(MSG_start, onStart)
    MSG_ITEM(MSG_stop, onStop)
    MSG_ITEM(MSG_flush, onFlush)
    MSG_ITEM(MSG_reset, onReset)
END_MSG_LOOP()

//////////////////////// WorkerThread
class WorkerThread : public MMThread {
  public:
    WorkerThread(VideoFilterGL * com)
        : MMThread(MMTHREAD_NAME, true)
        , mComponent(com)
        , mCondition(mComponent->mLock)
        , mContinue(true)
    {
        FUNC_TRACK();
    }

    ~WorkerThread()
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
    VideoFilterGL * mComponent;
    Condition mCondition;         // cork/uncork on pause/resume
    bool mContinue;     // terminate the thread
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(WorkerThread)

// process buffers in WorkerThread
#define RETRY_COUNT 500
void WorkerThread::main()
{
    FUNC_TRACK();
    MediaBufferSP buffer;
    mm_status_t status;
    int retry = 0;

    // EGL init/deinit and gl operation must run in the same thread from test
    EglWindowContextSP mGlProcessor;
    mGlProcessor.reset( new EglWindowContext());
    if (mGlProcessor) {
        bool ret = mGlProcessor->init();
        if (!ret)
            mGlProcessor.reset();
    }

    while(1) {
        {
            MMAutoLock locker(mComponent->mLock);
            // sainty check
            if (!mContinue) {
                break;
            }

            if (mComponent->mState != VideoFilterGL::StateType::STATE_STARTED) {
                INFO("WorkerThread waitting\n");
                mCondition.wait();
                INFO("WorkerThread wakeup \n");
                continue;
            }
        }

        DEBUG("mInputBufferCount: %d mOutputBufferCount: %d, in list size: %zu, out list size: %zu",
            mComponent->mInputBufferCount, mComponent->mOutputBufferCount, mComponent->mInputBuffers.size(), mComponent->mOutputBuffers.size());

        //acquire a buffer
        if (mComponent->mPortMode[0] == VideoFilterGL::PMT_Master) {
            do {
                if (!mContinue) {
                    buffer.reset();
                    return;
                }
                status = mComponent->mReader->read(buffer);
            } while((status == MM_ERROR_AGAIN) && (retry++ < RETRY_COUNT));
            if (buffer)
                mComponent->mInputBufferCount++;
        } else if (mComponent->mPortMode[0] == VideoFilterGL::PMT_Slave) {
            MMAutoLock locker(mComponent->mLock);
            if (!mComponent->mInputBuffers.empty()) {
                buffer = mComponent->mInputBuffers.front();
                mComponent->mInputBuffers.pop();
            }
        }
        if (!buffer) {
            usleep(5000);
            continue;
        }
        ASSERT(buffer);
        DEBUG("mInputBufferCount: %d mOutputBufferCount: %d, in list size: %zu, out list size: %zu",
            mComponent->mInputBufferCount, mComponent->mOutputBufferCount, mComponent->mInputBuffers.size(), mComponent->mOutputBuffers.size());

         // process the frame
        do {
            YNativeSurfaceBuffer* anb = NULL;
            if (buffer->isFlagSet(MediaBuffer::MBFT_EOS) || !(buffer->size())) {
                INFO("skip, EOS or buffer size is zero");
                break;
            }
            MediaMetaSP meta = buffer->getMediaMeta();
            if (!meta) {
                ERROR("no meta data");
                break;
            }

            meta->getPointer(MEDIA_ATTR_NATIVE_WINDOW_BUFFER, (void* &)anb);
            int32_t width = 0, height = 0;
            meta->getInt32(MEDIA_ATTR_WIDTH, width);
            meta->getInt32(MEDIA_ATTR_HEIGHT, height);
            DEBUG("width: %d, height: %d", width, height);
            if (anb && mGlProcessor) {
                mGlProcessor->processBuffer(anb, width, height);
            }
        } while (0);

        // pass the buffer to downlink components
        {
            MMAutoLock locker(mComponent->mLock);
            if (mComponent->mPortMode[1] == VideoFilterGL::PMT_Master) {
                mm_status_t ret = MM_ERROR_SUCCESS;
                do {
                    ret = mComponent->mWriter->write(buffer);
                } while (ret == MM_ERROR_AGAIN);
                ASSERT(ret == MM_ERROR_SUCCESS);
                mComponent->mOutputBufferCount++;
            } else {
                mComponent->mOutputBuffers.push(buffer);
                mComponent->mCondition.signal();
            }
            DEBUG("mOutputBufferCount: %d", mComponent->mOutputBufferCount);
        }

        // important, make sure not hold a reference of buffer
        buffer.reset();
    }

    if (mGlProcessor)
        mGlProcessor->deinit();
    mGlProcessor.reset();

    INFO("Poll thread exited\n");
}

mm_status_t VideoFilterGL::FilterReader::read(MediaBufferSP & buffer)
{
    FUNC_TRACK();
    MMAutoLock locker(mComponent->mLock);
    if (mComponent->mOutputBuffers.empty()) {
        mComponent->mCondition.timedWait(5000);
    }

    //notify, but not by available buffer
    if (mComponent->mOutputBuffers.empty()) {
        return MM_ERROR_AGAIN;
    }

    buffer = mComponent->mOutputBuffers.front();
    mComponent->mOutputBuffers.pop();
    mComponent->mOutputBufferCount++;
    DEBUG("mOutputBufferCount: %d", mComponent->mOutputBufferCount);
    return MM_ERROR_SUCCESS;
}

MediaMetaSP VideoFilterGL::FilterReader::getMetaData()
{
    FUNC_TRACK();
    return mComponent->mInputFormat;
}

mm_status_t VideoFilterGL::FilterWriter::write(const MediaBufferSP & buffer)
{
    FUNC_TRACK();
    if (!buffer)
        return MM_ERROR_INVALID_PARAM;

    MMAutoLock locker(mComponent->mLock);
    mComponent->mInputBuffers.push(buffer);
    mComponent->mInputBufferCount++;
    DEBUG("mInputBufferCount %d", mComponent->mInputBufferCount);
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoFilterGL::FilterWriter::setMetaData(const MediaMetaSP & metaData)
{
    FUNC_TRACK();
    mComponent->mInputFormat = metaData->copy();
    mComponent->mOutputFormat = mComponent->mInputFormat; // we doesn't change buffer format
    return MM_ERROR_SUCCESS;
}

VideoFilterGL::VideoFilterGL() : MMMsgThread(COMPONENT_NAME)
                          , mCondition(mLock)
                          , mInputBufferCount(0), mOutputBufferCount(0)
{
    FUNC_TRACK();
    int i = 0;
    mInputFormat = MediaMeta::create();
    mOutputFormat = MediaMeta::create();
    for(i=0; i<2; i++) {
        mPortMode[i] = PMT_UnDecided;
    }
}

VideoFilterGL::~VideoFilterGL()
{
    FUNC_TRACK();
}

mm_status_t VideoFilterGL::addSource(Component * component, MediaType mediaType)
{
    FUNC_TRACK();

    ASSERT(mPortMode[0] == PMT_UnDecided);
    if (!component || mediaType != kMediaTypeVideo)
        return MM_ERROR_INVALID_PARAM;

    mReader = component->getReader(kMediaTypeVideo);
    if (!mReader)
        return MM_ERROR_INVALID_PARAM;

    MediaMetaSP meta = mReader->getMetaData();
    if (!meta)
        return MM_ERROR_INVALID_PARAM;

    int32_t fps = 30;
    int32_t colorFormat = 0;
    float srcFps;
    int32_t width = 0;
    int32_t height = 0;
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

    mPortMode[0] = PMT_Master;
    return MM_ERROR_SUCCESS;
}

Component::WriterSP VideoFilterGL::getWriter(MediaType mediaType)
{
    FUNC_TRACK();
    Component::WriterSP writer;

     ASSERT(mPortMode[0] == PMT_UnDecided);
     if ( mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return writer;
        }

    writer.reset(new VideoFilterGL::FilterWriter(this));
    mPortMode[0] = PMT_Slave;
    return writer;
}

mm_status_t VideoFilterGL::addSink(Component * component, MediaType mediaType)
{
    FUNC_TRACK();

    ASSERT(mPortMode[1] == PMT_UnDecided);
    if (!component || mediaType != kMediaTypeVideo)
        return MM_ERROR_INVALID_PARAM;

    mWriter = component->getWriter(kMediaTypeVideo);
    MediaMetaSP meta;

    if (!mWriter)
        return MM_ERROR_INVALID_PARAM;

    // mWriter->setMeta(mOutputFormat);
    mPortMode[1] = PMT_Master;
    return MM_ERROR_SUCCESS;
}

Component::ReaderSP VideoFilterGL::getReader(MediaType mediaType)
{
     FUNC_TRACK();
    Component::ReaderSP reader;

     ASSERT(mPortMode[1] == PMT_UnDecided);
     if ( mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return reader;
        }

    reader.reset(new VideoFilterGL::FilterReader(this));
    mPortMode[1] = PMT_Slave;
    return reader;
}

mm_status_t VideoFilterGL::init()
{
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        return MM_ERROR_OP_FAILED;
    }

    return MM_ERROR_SUCCESS;
}

void VideoFilterGL::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}


mm_status_t VideoFilterGL::prepare()
{
    FUNC_TRACK();
    postMsg(MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterGL::start()
{
    FUNC_TRACK();
    postMsg(MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterGL::stop()
{
    FUNC_TRACK();
    postMsg(MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoFilterGL::reset()
{
    FUNC_TRACK();
    postMsg(MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterGL::flush()
{
    FUNC_TRACK();
    postMsg(MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}


mm_status_t VideoFilterGL::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();

    MMAutoLock locker(mLock);
    int ret = MM_ERROR_SUCCESS;

    return ret;
}

void VideoFilterGL::setState(int state)
{
    FUNC_TRACK();
    // MMAutoLock  locker(mLock);
    mState = (StateType)state;
    DEBUG("mState set to %d", state);
}

void VideoFilterGL::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState >= STATE_PREPARED ) {
        DEBUG("mState is %d", mState);
        notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    }

    setState(STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterGL::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    if (mState == STATE_STARTED) {
        ERROR("Aready started\n");
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    }

    if (!mWorkerThread) {
        mWorkerThread.reset (new WorkerThread(this), MMThread::releaseHelper);
        mWorkerThread->create();
    }

    setState(STATE_STARTED);
    mWorkerThread->signalContinue_l();
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterGL::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    {
        MMAutoLock locker(mLock);

        if (mState == STATE_IDLE || mState == STATE_STOPED) {
            notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        }

        // set state first, pool thread will check it
        setState(STATE_STOPED);
    }

    if (mWorkerThread) {
        mWorkerThread->signalExit();
        mWorkerThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mWorkerThread
    }

    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterGL::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    clearSourceBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterGL::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
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
}

void VideoFilterGL::clearSourceBuffers()
{
    FUNC_TRACK();
    while(!mOutputBuffers.empty()) {
        mOutputBuffers.pop();
    }
    while(!mInputBuffers.empty()) {
        mInputBuffers.pop();
    }
}

}


extern "C" {

MM_LOG_DEFINE_MODULE_NAME("VideoFilterGL");

YUNOS_MM::Component * createComponent(const char* mimeType, bool isEncoder)
{
    if (strcmp(mimeType, "video/filter-gl"))
        return NULL;
    return new YUNOS_MM::VideoFilterGL();
}

void releaseComponent(YUNOS_MM::Component * component)
{
    DEBUG("%p\n", component);
    if ( component ) {
        YUNOS_MM::VideoFilterGL * c = DYNAMIC_CAST<YUNOS_MM::VideoFilterGL*>(component);
        MMASSERT(c != NULL);
        MM_RELEASE(c);
    }
}
}

