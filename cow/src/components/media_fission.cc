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

#include "media_fission.h"

#include <unistd.h>
#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

namespace YUNOS_MM {

static const char * COMPONENT_NAME = "MediaFission";
static const char * MMSGTHREAD_NAME = "MFission";
static const char * MMTHREAD_NAME = "MFission-Push";
static const int32_t kInOutputRetryDelayUs = 20000;       // 20ms

#define MFISSION_MSG_prepare (msg_type)1
#define MFISSION_MSG_start (msg_type)2
#define MFISSION_MSG_pause (msg_type)3
#define MFISSION_MSG_resume (msg_type)4
#define MFISSION_MSG_stop (msg_type)5
#define MFISSION_MSG_flush (msg_type)6
#define MFISSION_MSG_reset (msg_type)7
#define MFISSION_MSG_handleInputBuffer (msg_type)8

BEGIN_MSG_LOOP(MediaFission)
    MSG_ITEM(MFISSION_MSG_prepare, onPrepare)
    MSG_ITEM(MFISSION_MSG_start, onStart)
    MSG_ITEM(MFISSION_MSG_pause, onPause)
    MSG_ITEM(MFISSION_MSG_resume, onResume)
    MSG_ITEM(MFISSION_MSG_stop, onStop)
    MSG_ITEM(MFISSION_MSG_flush, onFlush)
    MSG_ITEM(MFISSION_MSG_reset, onReset)
    MSG_ITEM(MFISSION_MSG_handleInputBuffer, onHandleInputBuffer)
END_MSG_LOOP()

#define INTERNAL_QUEUE_CAPACITY     4

///////////////////////// OutBufferQueue
MediaFission::OutBufferQueue::OutBufferQueue(uint32_t maxQSize)
    : mTraffic(new TrafficControl(1, maxQSize, "OutBufferQueue"))
    , MM_LOG_TAG(COMPONENT_NAME)
{
    FUNC_TRACK();
}
void MediaFission::OutBufferQueue::pushBuffer(MediaBufferSP buffer)
{
    FUNC_TRACK();
    struct BufferTracker bt;

    if (!buffer) {
        ERROR("empty buffer");
        return;
    }
    // in MediaFission::stop (not onStop), calls unblockWait()
    TrafficControl * traffic = DYNAMIC_CAST<TrafficControl*>(mTraffic.get());
    if(!traffic) {
        ERROR("DYNAMIC_CAST fail \n");
        return;
    }

    traffic->waitOnFull();

    bt.mBuffer = buffer;
    bt.mTracker = Tracker::create(mTraffic);
    mBuffers.push(bt);
    DEBUG("mBuffers.size(): %zu", mBuffers.size());

}
MediaBufferSP MediaFission::OutBufferQueue::frontBuffer()
{
    FUNC_TRACK();
    MediaBufferSP buffer;

    DEBUG("mBuffers.size(): %zu", mBuffers.size());
    if (mBuffers.empty())
        return buffer;

    buffer = mBuffers.front().mBuffer;

    return buffer;
}

void MediaFission::OutBufferQueue::popBuffer()
{
    FUNC_TRACK();
    mBuffers.pop();
}

void MediaFission::OutBufferQueue::unblockWait()
{
    FUNC_TRACK();
    TrafficControl * traffic = DYNAMIC_CAST<TrafficControl*>(mTraffic.get());
    if(!traffic) {
        ERROR("DYNAMIC_CAST fail \n");
        return;
    }

    traffic->unblockWait();
}

bool MediaFission::OutBufferQueue::isFull()
{
    FUNC_TRACK();
    // make it possible to avoid block/wait in pushBuffer() when input port is in slave mode
    TrafficControl * traffic = DYNAMIC_CAST<TrafficControl*>(mTraffic.get());
    ASSERT(traffic);
    return traffic->isFull();
}

//////////////////////// FissionReader
MediaFission::FissionReader::FissionReader(MediaFission * from, uint32_t index)
    : mComponent(from)
    , mIndex(index)
    , mLogTag("FissionReader")
    , MM_LOG_TAG(COMPONENT_NAME)
{
    FUNC_TRACK();
    char tmp[20];
    mLogTag = mComponent->name();
    sprintf(tmp, "-R%02d", index);
    mLogTag += tmp;
    MM_LOG_TAG = mLogTag.c_str();
}
MediaFission::FissionReader::~FissionReader()
{
    FUNC_TRACK();
}
mm_status_t MediaFission::FissionReader::read(MediaBufferSP & buffer)
{
    FUNC_TRACK();

    if (!mComponent->isRunning())
        return MM_ERROR_AGAIN;

    // FIXME, consider multiple readers with multiple buffer queues
    DEBUG("FissionReader mOutputBufferCount: %d", mComponent->mOutputBufferCount);
    DEBUG("mComponent->mBufferQueues.size(): %zu, mIndex: %d", mComponent->mBufferQueues.size(), mIndex);

    MediaBufferSP buf = mComponent->mBufferQueues[mIndex].frontBuffer();

    if (!buf) {
        if (mComponent->mEosState == MediaFission::kInputEOS) {
            mComponent->setEosState(MediaFission::kOutputEOS);
            return MM_ERROR_EOS;
        }
        return MM_ERROR_AGAIN;
    }

    mComponent->mOutputBufferCount++;
    buffer = buf;
    mComponent->mBufferQueues[mIndex].popBuffer();
    return MM_ERROR_SUCCESS;
}
MediaMetaSP MediaFission::FissionReader::getMetaData()
{
    FUNC_TRACK();
    return mComponent->mFormat;
}

// ////////////////////// PushDataThread
class MediaFission::PushDataThread : public MMThread {
  public:
    PushDataThread(MediaFission * fission)
        : MMThread(MMTHREAD_NAME)
        , mFission(fission)
        , mContinue(true)
        , MM_LOG_TAG(COMPONENT_NAME)
    {
        FUNC_TRACK();
        sem_init(&mSem, 0, 0);
         mLogTag = mFission->name();
         MM_LOG_TAG = mLogTag.c_str();
    }

    ~PushDataThread()
    {
        FUNC_TRACK();
        sem_destroy(&mSem);
    }

    // FIXME, since we have signalExit, MMThread::destroy()/pthread_join() is not necessary for us.
    // FIXME, uses pthrad_detach
    void signalExit()
    {
        FUNC_TRACK();
        MMAutoLock locker(mFission->mLock);
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
    MediaFission * mFission;
    // FIXME, define a class for sem_t, does init/destroy automatically
    sem_t mSem;         // cork/uncork on pause/resume
    // FIXME, change to something like mExit
    bool mContinue;     // terminate the thread
    std::string mLogTag;
    const char* MM_LOG_TAG;
};

// poll the device for notification of available input/output buffer index
void MediaFission::PushDataThread::main()
{
    FUNC_TRACK();

    while(1) {
        {
            MMAutoLock locker(mFission->mLock);
            if (!mContinue) {
                break;
            }
        }
        if (mFission->mState != kStatePlaying) {
            INFO("PushDataThread waitting_sem\n");
            sem_wait(&mSem);
            INFO("PushDataThread wakeup_sem\n");
            continue;
        }

        // push data downstream
        uint32_t i=0;
        DEBUG("mMasterWriters.size(): %zu, mBufQueIndex: %d, ", mFission->mMasterWriters.size(), mFission->mMasterWriters[i].mBufQueIndex);
        for (i=0; i<mFission->mMasterWriters.size(); i++) {
            // FIXME, consider mWriter->writer() fail, we may need front()/pop()
            MediaBufferSP buf = mFission->mBufferQueues[mFission->mMasterWriters[i].mBufQueIndex].frontBuffer();
            if (buf) {
                mm_status_t st = mFission->mMasterWriters[i].mWriter->write(buf);
                if (st == MM_ERROR_SUCCESS) {
                    mFission->mMasterWriters[i].mOutputBufferCount++;
                    DEBUG("%s, mOutputBufferCount: %d, timestamp: (%" PRId64 ",%" PRId64 "), buffer age: %d", mFission->mMime.c_str(), mFission->mMasterWriters[i].mOutputBufferCount, buf->dts(), buf->pts(), buf->ageInMs());
                    mFission->mBufferQueues[mFission->mMasterWriters[i].mBufQueIndex].popBuffer();
                } else if (st == MM_ERROR_AGAIN) {
                    if (mFission->mMime.c_str()) // FIXME, why NULL?
                        DEBUG("%s, too fast, have a rest. mOutputBufferCount: %d", mFission->mMime.c_str(), mFission->mMasterWriters[i].mOutputBufferCount);
                    // FIXME: for multiple output, we should sleep for current one only
                    usleep(5000);
                } else
                    ERROR("fail to push buffer to downlink component");
            } else
                usleep(5000);
        }
    }

    INFO("Poll thread exited\n");
}

//////////////////////////////////////// FissionWriter
MediaFission::FissionWriter::FissionWriter(MediaFission *fission) : MM_LOG_TAG(COMPONENT_NAME) {
    FUNC_TRACK();
    mFission = fission;
    mLogTag = mFission->name();
    MM_LOG_TAG = mLogTag.c_str();
}

mm_status_t MediaFission::FissionWriter::write(const MediaBufferSP &buffer) {
    FUNC_TRACK();
    MMAutoLock locker(mFission->mLock);

    if (!buffer) {
        WARNING("buffer is nil");
        // FIXME
        return MM_ERROR_SUCCESS;
    }

    DEBUG("mBufferQueues.size(): %zu", mFission->mBufferQueues.size());

    uint32_t i=0;
    // check whether the buffer queue is full or not
    for (i=0; i<mFission->mBufferQueues.size(); i++) {
        if (mFission->mBufferQueues[i].isFull())
            return MM_ERROR_AGAIN;
    }

    // push buffer to the buffer queues, to be read by downlink components
    // if output port pushes data acvtively, the PushDataThread will handle it
    for (i=0; i<mFission->mBufferQueues.size(); i++) {
        mFission->mBufferQueues[i].pushBuffer(buffer);
    }

    mFission->mInputBufferCount++;
    DEBUG("mInputBufferCount: %d", mFission->mInputBufferCount);
    return MM_ERROR_SUCCESS;
}


mm_status_t MediaFission::FissionWriter::setMetaData(const MediaMetaSP & metaData) {
    FUNC_TRACK();
    MMAutoLock locker(mFission->mLock);
    mFission->mFormat->merge(metaData);
    return  MM_ERROR_SUCCESS;
}

// /////////////////////////////////////
MediaFission::MediaFission(const char* mimeType, bool isEncoder)
    : MMMsgThread(MMSGTHREAD_NAME)
    , mMime(mimeType)
    , mComponentName(COMPONENT_NAME)
    , mState(kStateNull)
    , mEosState(kNoneEOS)
    , mInputMaster(true)
    , mBufferCapacity(INTERNAL_QUEUE_CAPACITY)
    , mInputBufferCount(0)
    , mOutputBufferCount(0)
    , MM_LOG_TAG(COMPONENT_NAME)
{
    FUNC_TRACK();
    mBufferCapacity = INTERNAL_QUEUE_CAPACITY;
    mFormat = MediaMeta::create();
}

MediaFission::~MediaFission()
{
    FUNC_TRACK();
    DEBUG("mime: %s", mMime.c_str());
    // it is incorrect to call onReset() here, since onReset() wille notify(kEventResetComplete)
    // then PipelinePlayer will visit current Component during Component's decostruction func.
    // it lead to undefined behavior.
}

Component::ReaderSP MediaFission::getReader(MediaType mediaType)
{
    // ASSERT on mediaType;
    FUNC_TRACK();

    mBufferQueues.push_back(OutBufferQueue(mBufferCapacity));
    DEBUG("mBufferQueues.size(): %zu", mBufferQueues.size());
    return ReaderSP(new FissionReader(this, mBufferQueues.size()-1));
}

Component::WriterSP MediaFission::getWriter(MediaType mediaType)
{
    FUNC_TRACK();
    Component::WriterSP writer;

    if ( (int)mediaType != Component::kMediaTypeVideo &&
        (int)mediaType != Component::kMediaTypeAudio) {
           ERROR("not supported mediatype: %d\n", mediaType);
           return writer;
       }

   // return writer.reset(new MediaFission::FissionWriter(this));
   FissionWriter* w = new MediaFission::FissionWriter(this);
   writer.reset(w);
   mInputMaster = false;
   return writer;
}

mm_status_t MediaFission::prepare()
{
    FUNC_TRACK();
    postMsg(MFISSION_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t MediaFission::start()
{
    FUNC_TRACK();
    postMsg(MFISSION_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t MediaFission::stop()
{
    FUNC_TRACK();
    postMsg(MFISSION_MSG_stop, 0, NULL);

    // onHandleInputBuffer may be blocked since the queue is full, unblock it
    uint32_t i=0;
    for (i=0; i<mBufferQueues.size(); i++) {
        mBufferQueues[i].unblockWait();
    }

    return MM_ERROR_ASYNC;
}

mm_status_t MediaFission::pause()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(MFISSION_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t MediaFission::resume()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(MFISSION_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t MediaFission::seek(int msec, int seekSequence)
{
    FUNC_TRACK();
    // Pipeline calls flush()
    return MM_ERROR_SUCCESS;
}
mm_status_t MediaFission::reset()
{
    FUNC_TRACK();
    postMsg(MFISSION_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t MediaFission::flush()
{
    FUNC_TRACK();
    postMsg(MFISSION_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t MediaFission::addSource(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    mReader = component->getReader(mediaType);
    if (mReader) {
        mFormat->merge(mReader->getMetaData());

        const char* mime = NULL;
        if (mFormat->getString("mime", mime)) {
            if (mime) {
                INFO("update mime to %s", mime);
                mMime = std::string(mime);
            }
        }

        return MM_ERROR_SUCCESS;
    }

    return MM_ERROR_OP_FAILED;
}

mm_status_t MediaFission::addSink(Component * component, MediaType mediaType)
{
    FUNC_TRACK();

    DEBUG("component: %p, name: %s, mediaType: %d", component, component->name(), mediaType);
    WriterSP writer = component->getWriter(mediaType);
    if (writer) {
        if (mFormat)
            mFormat->dump();
        writer->setMetaData(mFormat);
        mBufferQueues.push_back(OutBufferQueue(mBufferCapacity));
        DEBUG("mBufferQueues.size(): %zu", mBufferQueues.size());
        MasterWriter mw (writer, mBufferQueues.size()-1);
        mMasterWriters.push_back(mw);
        if (!mPushThread) {
            mPushThread.reset(new PushDataThread(this));
            if (mPushThread) {
                mPushThread->create();
            }
        }
        return MM_ERROR_SUCCESS;
    }

    return MM_ERROR_OP_FAILED;
}

void MediaFission::setState(StateType state)
{
    // FIXME, seem Lock isn't necessary
    // caller thread update state to kStatePaused
    // other state are updated by MMMsgThread
    // MMAutoLock locker(mLock);
    FUNC_TRACK();
    mState = state;
}

void MediaFission::setEosState(EosStateType state)
{
    // FIXME, seem Lock isn't necessary
    FUNC_TRACK();
    mEosState = state;
}

bool MediaFission::isRunning()
{
    FUNC_TRACK();
    if (mState == kStatePrepared || mState == kStatePlay || mState == kStatePlaying)
        return true;

    return false;
}

void MediaFission::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock); // big lock

    // seems not necessary for async
    // mState = kStatePreparing;
    mState = kStatePrepared;
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void MediaFission::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    if (mInputMaster)
        postMsg(MFISSION_MSG_handleInputBuffer, 0, NULL, 0);
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);

    mState = kStatePlaying;
    DEBUG("signal output thread to continue");
    if (mPushThread) {
        mPushThread->signalContinue();
    }
}

void MediaFission::onHandleInputBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    /* onHandleInputBuffer() should not be blocked, options:
      - check the size of mBufferQueues before mReader->read()
      - add a PullDataThread
        */
    DEBUG("onHandleInputBuffer mInputBufferCount: %d", mInputBufferCount);
    if (!isRunning())
        return;

    DEBUG("mimetype: %s, mInputBufferCount: %d", mMime.c_str(), mInputBufferCount);
    MediaBufferSP buffer;
    mm_status_t status = mReader->read(buffer);


    if(status == MM_ERROR_SUCCESS && buffer) {
        DEBUG("mimetype: %s, mInputBufferCount: %d, buffer age: %d", mMime.c_str(), mInputBufferCount, buffer->ageInMs());
        uint32_t i=0;
        mInputBufferCount++;
        DEBUG("mBufferQueues.size(): %zu", mBufferQueues.size());
        for (i=0; i<mBufferQueues.size(); i++) {
            // FIXME, it may be blocked when buffer queue is full
            mBufferQueues[i].pushBuffer(buffer);
        }
    } else {
        usleep(5000);
        DEBUG("fail to get input data\n");
    }

    // run as fast as we can, and will be blocked if the mBufferQueues are full
    postMsg(MFISSION_MSG_handleInputBuffer, 0, NULL, kInOutputRetryDelayUs);
}

void MediaFission::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState != kStatePlaying) {
        ERROR("invalid pause command, not in kStatePlaying");
        notify(kEventPaused, MM_ERROR_OP_FAILED, 0, nilParam);
    }

    setState(kStatePaused);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
}

void MediaFission::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState != kStatePaused) {
        ERROR("invalid resume command, not in kStatePaused");
        notify(kEventResumed, MM_ERROR_OP_FAILED, 0, nilParam);
    }

    setState(kStatePlaying);
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    mPushThread->signalContinue();
}

void MediaFission::clearInternalBuffers()
{
    FUNC_TRACK();
    uint32_t i=0;
    // if MMMsgThread is blocked by pushBuffer() to mBufferQueues, it will be unblocked for the following popBuffer().
    for (i=0; i<mBufferQueues.size(); i++) {
        while (mBufferQueues[i].frontBuffer())
            mBufferQueues[i].popBuffer();
    }

}

void MediaFission::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    // MMAutoLock locker(mLock);

    if (mState == kStateStopped) {
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    setState(kStateStopping);
    if (mPushThread) {
        mPushThread->signalExit();
        mPushThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mPushThread
    }

    clearInternalBuffers();
    mBufferQueues.clear();
    mReader.reset();
    mMasterWriters.clear();
    setState(kStateStopped);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
}

void MediaFission::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    clearInternalBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void MediaFission::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    // FIXME, onStop wait until the pthread_join of mPushThread
    onStop(param1, param2, rspId);

    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

// boilplate for MMMsgThread and Component
mm_status_t MediaFission::init()
{
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret)
        return MM_ERROR_OP_FAILED;

    return MM_ERROR_SUCCESS;
}

void MediaFission::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}

const char * MediaFission::name() const
{
    return mComponentName.c_str();
}

mm_status_t MediaFission::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    // assume we needn't merge these meta to mFormat
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;

#if 0
        if ( !strcmp(item.mName, "queue-size") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mBufferCapacity = item.mValue.ui;
            TrafficControl * traffic = DYNAMIC_CAST<TrafficControl*>(mTraffic.get());
            traffic->updateHighBar(mBufferCapacity);
            MMLOGI("key: %s, value: %d\n", item.mName, item.mValue.ui);
        } else
#endif
        if ( !strcmp(item.mName, "comp-name") ) {
            if ( item.mType != MediaMeta::MT_String) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            MMLOGI("key: %s, value: %s\n", item.mName, item.mValue.str);
            mComponentName = item.mValue.str;
            MM_LOG_TAG = mComponentName.c_str();
        } else {
            ERROR("unknown parameter %s\n", item.mName);
        }
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t MediaFission::getParameter(MediaMetaSP & meta) const
{
    FUNC_TRACK();
    WARNING("setParameter isn't supported yet\n");
    return MM_ERROR_SUCCESS;
}

// //////// for component factory
extern "C" {
const char* MM_LOG_TAG="MediaFission";
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    FUNC_TRACK();
    const char* mime = mimeType;
    if (!mime)
        mime = "generic";
    YUNOS_MM::MediaFission *MediaFissionComponent = new YUNOS_MM::MediaFission(mime, isEncoder);
    if (MediaFissionComponent == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(MediaFissionComponent);
}


void releaseComponent(YUNOS_MM::Component *component) {
    FUNC_TRACK();
    delete component;
}
} // extern "C"

} // YUNOS_MM

