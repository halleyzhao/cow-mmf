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

#include "video_filter_cl.h"
#include "cl_filter.h"


#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
#define kMaxInputPlaneCount     3

namespace YUNOS_MM {

DEFINE_LOGTAG(VideoFilterCL)

static const char * COMPONENT_NAME = "VideoFilterCL";
static const char * MMTHREAD_NAME = "FilterWorkerThread";

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

BEGIN_MSG_LOOP(VideoFilterCL)
    MSG_ITEM(MSG_prepare, onPrepare)
    MSG_ITEM(MSG_start, onStart)
    MSG_ITEM(MSG_stop, onStop)
    MSG_ITEM(MSG_flush, onFlush)
    MSG_ITEM(MSG_reset, onReset)
    MSG_ITEM(MSG_releaseBuffer, onReleaseOutBuffer)
END_MSG_LOOP()

//////////////////////// WorkerThread
class WorkerThread : public MMThread {
  public:
    WorkerThread(VideoFilterCL * com)
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
    VideoFilterCL * mComponent;
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
    mm_status_t status;
    int retry = 0;

    // EGL init/deinit and gl operation must run in the same thread from test
    OCLProcessorSP mClProcessor;
    mClProcessor.reset( new OCLProcessor());
    DEBUG("opencl_init begin");
    if (mClProcessor) {
        bool ret = mClProcessor->init();
        if (!ret)
            mClProcessor.reset();
    }
    mComponent->mCondition.signal(); // wakeup main class msg thread (onPrepare)
    DEBUG("opencl_init end");

    while(1) {
        MediaBufferSP buffer;
        {
            MMAutoLock locker(mComponent->mLock);
            // sainty check
            if (!mContinue) {
                break;
            }

            if (mComponent->mState != VideoFilterCL::StateType::STATE_STARTED) {
                INFO("WorkerThread waitting\n");
                mCondition.wait();
                INFO("WorkerThread wakeup \n");
                continue;
            }
        }

        DEBUG("mInputBufferCount: %d mOutputBufferCount: %d, in list size: %zu, out list size: %zu",
            mComponent->mInputBufferCount, mComponent->mOutputBufferCount, mComponent->mInputBuffers.size(), mComponent->mOutputBuffers.size());

        //acquire a buffer
        if (mComponent->mPortMode[0] == VideoFilterCL::PMT_Master) {
            do {
                if (!mContinue) {
                    buffer.reset();
                    return;
                }
                status = mComponent->mReader->read(buffer);
            } while((status == MM_ERROR_AGAIN) && (retry++ < RETRY_COUNT));
            if (buffer)
                mComponent->mInputBufferCount++;
        } else if (mComponent->mPortMode[0] == VideoFilterCL::PMT_Slave) {
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
        DEBUG("mInputBufferCount: %d mOutputBufferCount: %d, in list size: %zu, out list size: %zu, buffer age: %d",
            mComponent->mInputBufferCount, mComponent->mOutputBufferCount, mComponent->mInputBuffers.size(), mComponent->mOutputBuffers.size(), buffer->ageInMs());

         // process the frame
        do {
            MMBufferHandleT nativeTarget = NULL;
            if (buffer->isFlagSet(MediaBuffer::MBFT_EOS) || !(buffer->size())) {
                INFO("skip, EOS or buffer size is zero");
                break;
            }

            uintptr_t buffers[kMaxInputPlaneCount] = {0};
            int32_t offsets[kMaxInputPlaneCount] = {0};
            int32_t strides[kMaxInputPlaneCount] = {0};
            buffer->getBufferInfo((uintptr_t *)buffers, offsets, strides, kMaxInputPlaneCount);
            nativeTarget = *(MMBufferHandleT*)buffers[0];

            MediaMetaSP meta = buffer->getMediaMeta();
            if (!meta) {
                ERROR("no meta data");
                break;
            }

            // get one output buffer from surface (if exists)
            MMBufferHandleT dstNativeTarget = nativeTarget;
            MediaBufferSP newBuffer = mComponent->createOutputBuffer(buffer);
            do {
                if (!newBuffer)
                    break;
                MediaMetaSP newMeta = newBuffer->getMediaMeta();
                if (!newMeta)
                    break;
                void* ptr = NULL;
                int ret = newMeta->getPointer("native-buffer", ptr);
                if (ret && ptr) {
                    dstNativeTarget  = (MMBufferHandleT) mm_getBufferHandle((MMNativeBuffer*)ptr);
                }
            } while(0);

            DEBUG("nativeTarget: %p, width: %d, height: %d, dstNativeTarget: %p", nativeTarget, mComponent->mWidth, mComponent->mHeight, dstNativeTarget);
            if (nativeTarget && mClProcessor) {
                mClProcessor->processBuffer(nativeTarget, dstNativeTarget, mComponent->mWidth, mComponent->mHeight/* offsets, strides*/);
            }

            if (dstNativeTarget != nativeTarget) {
                // replace the buffer for output if we have created a new one
                buffer = newBuffer;
            }

        } while (0);

        // pass the buffer to downlink components
        {
            MMAutoLock locker(mComponent->mLock);
            if (mComponent->mPortMode[1] == VideoFilterCL::PMT_Master) {
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
            DEBUG("mOutputBufferCount: %d, buffer age: %d", mComponent->mOutputBufferCount, buffer->ageInMs());
        }

        if (buffer->isFlagSet(MediaBuffer::MBFT_EOS) || !(buffer->size())) {
            INFO("reach EOS, exit work loop");
            break;
        }
        // important, make sure not hold a reference of buffer
        buffer.reset();
    }

    if (mClProcessor)
        mClProcessor->deinit();
    mClProcessor.reset();

    INFO("Poll thread exited\n");
}

mm_status_t VideoFilterCL::FilterReader::read(MediaBufferSP & buffer)
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

MediaMetaSP VideoFilterCL::FilterReader::getMetaData()
{
    FUNC_TRACK();
    return mComponent->mInputFormat;
}

mm_status_t VideoFilterCL::FilterWriter::write(const MediaBufferSP & buffer)
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

mm_status_t VideoFilterCL::FilterWriter::setMetaData(const MediaMetaSP & metaData)
{
    FUNC_TRACK();
    mComponent->mInputFormat = metaData->copy();
    mComponent->mOutputFormat = mComponent->mInputFormat; // we doesn't change buffer format
    return MM_ERROR_SUCCESS;
}

VideoFilterCL::VideoFilterCL() : MMMsgThread(COMPONENT_NAME)
                          , mWidth(0), mHeight(0)
                          , mCondition(mLock)
                          , mState(STATE_IDLE)
                          , mSurface(NULL), mSurfaceWrapper(NULL)
                          , mBufferPool(NULL), mGeneration(0)
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

VideoFilterCL::~VideoFilterCL()
{
    FUNC_TRACK();
    if (mSurfaceWrapper)
        delete mSurfaceWrapper;
    if (mBufferPool)
        delete mBufferPool;
}

mm_status_t VideoFilterCL::addSource(Component * component, MediaType mediaType)
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
    int32_t fourcc = 0;
    float srcFps;
    int32_t width = 0;
    int32_t height = 0;
    // sanity check
    if (meta->getInt32(MEDIA_ATTR_WIDTH, width)) {
        DEBUG("get meta data, width is %d", width);
        mInputFormat->setInt32(MEDIA_ATTR_WIDTH, width);
        mWidth = width;
    }

    if (meta->getInt32(MEDIA_ATTR_HEIGHT, height)) {
        DEBUG("get meta data, height is %d", height);
        mInputFormat->setInt32(MEDIA_ATTR_HEIGHT, height);
        mHeight = height;
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

    mPortMode[0] = PMT_Master;
    return MM_ERROR_SUCCESS;
}

Component::WriterSP VideoFilterCL::getWriter(MediaType mediaType)
{
    FUNC_TRACK();
    Component::WriterSP writer;

     ASSERT(mPortMode[0] == PMT_UnDecided);
     if ( mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return writer;
        }

    writer.reset(new VideoFilterCL::FilterWriter(this));
    mPortMode[0] = PMT_Slave;
    return writer;
}

mm_status_t VideoFilterCL::addSink(Component * component, MediaType mediaType)
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

Component::ReaderSP VideoFilterCL::getReader(MediaType mediaType)
{
     FUNC_TRACK();
    Component::ReaderSP reader;

     ASSERT(mPortMode[1] == PMT_UnDecided);
     if ( mediaType != Component::kMediaTypeVideo ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return reader;
        }

    reader.reset(new VideoFilterCL::FilterReader(this));
    mPortMode[1] = PMT_Slave;
    return reader;
}

mm_status_t VideoFilterCL::init()
{
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        return MM_ERROR_OP_FAILED;
    }

    return MM_ERROR_SUCCESS;
}

void VideoFilterCL::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}


mm_status_t VideoFilterCL::prepare()
{
    FUNC_TRACK();
    postMsg(MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterCL::start()
{
    FUNC_TRACK();
    postMsg(MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterCL::stop()
{
    FUNC_TRACK();
    postMsg(MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoFilterCL::reset()
{
    FUNC_TRACK();
    postMsg(MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoFilterCL::flush()
{
    FUNC_TRACK();
    postMsg(MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}


mm_status_t VideoFilterCL::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();

    MMAutoLock locker(mLock);
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_VIDEO_SURFACE) ) {
            if ( item.mType != MediaMeta::MT_Pointer ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mSurface = (WindowSurface*)item.mValue.ptr;
            mOutputFormat->setPointer(item.mName, item.mValue.ptr);
            MMLOGI("key: %s, value: %p\n", item.mName, item.mValue.ptr);
        }
    }

    return MM_ERROR_SUCCESS;
}

void VideoFilterCL::setState(int state)
{
    FUNC_TRACK();
    // MMAutoLock  locker(mLock);
    mState = (StateType)state;
    DEBUG("mState set to %d", state);
}

void VideoFilterCL::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState >= STATE_PREPARED ) {
        DEBUG("mState is %d", mState);
        notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    }

    if (!mWorkerThread) {
        mWorkerThread.reset (new WorkerThread(this), MMThread::releaseHelper);
        mWorkerThread->create();
    }

    if (mSurface) {
        mSurfaceWrapper = new YunOSMediaCodec::WindowSurfaceWrapper(mSurface);
        mBufferPool = new BufferPoolSurface();
        if (!mBufferPool->configure(mWidth, mHeight, FMT_PREF(NV12), 8, mSurfaceWrapper)) {
            delete mBufferPool;
            mBufferPool = NULL;
        }
    }

    {
    MMAutoLock locker(mLock);
    DEBUG("wait for filter context init ...");
    mCondition.timedWait(5000000);
    DEBUG("filter context init done");
    }

    setState(STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterCL::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    if (mState == STATE_STARTED) {
        ERROR("Aready started\n");
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    }

    setState(STATE_STARTED);
    // move work thread init to prepare, since it cost much time; which lead to big delay for live stream
    mWorkerThread->signalContinue_l();
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterCL::onStop(param1_type param1, param2_type param2, uint32_t rspId)
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

    mGeneration++;
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterCL::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    clearSourceBuffers();

    mGeneration++;
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoFilterCL::onReset(param1_type param1, param2_type param2, uint32_t rspId)
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

void VideoFilterCL::clearSourceBuffers()
{
    FUNC_TRACK();
    while(!mOutputBuffers.empty()) {
        mOutputBuffers.pop();
    }
    while(!mInputBuffers.empty()) {
        mInputBuffers.pop();
    }
}

MediaBufferSP VideoFilterCL::createOutputBuffer(MediaBufferSP ref)
{
    MediaBufferSP mediaBuffer;

    FUNC_TRACK();
    if (!mBufferPool)
        return mediaBuffer;

    MMNativeBuffer* nwb = mBufferPool->getBuffer();
    mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_BufferIndex);
    if (ref->isFlagSet(MediaBuffer::MBFT_EOS)) {
        mediaBuffer->setFlag(MediaBuffer::MBFT_EOS);
    }

    MediaMetaSP meta = ref->getMediaMeta()->copy();
    meta->setInt32("generation", mGeneration);
    meta->setPointer("native-buffer", nwb);
    meta->setPointer("video-filter", this);

    mediaBuffer->setPts(ref->pts());
    mediaBuffer->setMediaMeta(meta);
    mediaBuffer->addReleaseBufferFunc(VideoFilterCL::releaseOutputBuffer);

    return mediaBuffer;
}

/* static */ bool VideoFilterCL::releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    if (!mediaBuffer)
        return false;

    ASSERT(mediaBuffer->type() == MediaBuffer::MBT_BufferIndex);

    bool ret = false;
    void *ptr = NULL;
    int32_t isRender = 0;

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    ASSERT_RET(meta, false);
    ret = meta->getPointer("video-filter", ptr);
    ASSERT_RET(ret && ptr, false);
    VideoFilterCL *filter = (VideoFilterCL*) ptr;

    ret = meta->getPointer("native-buffer", ptr);
    ASSERT_RET(ret && ptr, false);
    MMNativeBuffer* nwb = (MMNativeBuffer*)ptr;
    ASSERT_RET(nwb, false);


    ret = meta->getInt32(MEDIA_ATTR_IS_VIDEO_RENDER, isRender);
    int32_t generation = 0;
    ret = meta->getInt32("generation", generation);
    if (ret && generation != filter->mGeneration)
        isRender = 0;

    filter->postMsg(MSG_releaseBuffer, (int)isRender, nwb, 0);
    return true;
}

void VideoFilterCL::onReleaseOutBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    bool isRender = (param1 > 0);
    MMNativeBuffer* nwb = (MMNativeBuffer*)param2;

    if (mState == STATE_STOPED){
        DEBUG("ignore buffer %p in stop state.", nwb);
        return;
    }

    int ret = 0;
    if (!nwb) {
        ERROR("invalid native buffer");
        return ;
    }

    mBufferPool->putBuffer(nwb, isRender);

    return ret;
}

}  // namespace of  YUNOS_MM


extern "C" {

MM_LOG_DEFINE_MODULE_NAME("VideoFilterCL");

YUNOS_MM::Component * createComponent(const char* mimeType, bool isEncoder)
{
    if (strcmp(mimeType, "video/filter-cl"))
        return NULL;
    return new YUNOS_MM::VideoFilterCL();
}

void releaseComponent(YUNOS_MM::Component * component)
{
    DEBUG("%p\n", component);
    if ( component ) {
        YUNOS_MM::VideoFilterCL * c = DYNAMIC_CAST<YUNOS_MM::VideoFilterCL*>(component);
        MMASSERT(c != NULL);
        MM_RELEASE(c);
    }
}
}

