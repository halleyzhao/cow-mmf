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

#include "video_sink.h"
#include <math.h>
#include <multimedia/component.h>
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("VideoSink")
static const char * COMPONENT_NAME = "VideoSink";
//#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

#define MAX_DIMENSION 4096

#define MCD_MSG_PREPARE                 (msg_type)1
#define MCD_MSG_START                   (msg_type)2
#define MCD_MSG_PAUSE                   (msg_type)3
#define MCD_MSG_RESUME                  (msg_type)4
#define MCD_MSG_STOP                    (msg_type)5
#define MCD_MSG_RESET                   (msg_type)6
#define MCD_MSG_FLUSH                   (msg_type)7
#define MCD_MSG_SET_PARAMETER           (msg_type)8
#define MCD_MSG_WRITE_BUFFER            (msg_type)9
#define MCD_MSG_RENDER_FRAME            (msg_type)10

BEGIN_MSG_LOOP(VideoSink)
    MSG_ITEM(MCD_MSG_PREPARE, onPrepare)
    MSG_ITEM(MCD_MSG_START, onStart)
    MSG_ITEM(MCD_MSG_PAUSE, onPause)
    MSG_ITEM(MCD_MSG_RESUME, onResume)
    MSG_ITEM(MCD_MSG_STOP, onStop)
    MSG_ITEM(MCD_MSG_RESET, onReset)
    MSG_ITEM(MCD_MSG_FLUSH, onFlush)
    // MSG_ITEM(MCD_MSG_SET_PARAMETER, onSetParameter)
    MSG_ITEM(MCD_MSG_WRITE_BUFFER, onWrite)
    MSG_ITEM(MCD_MSG_RENDER_FRAME, onRenderFrame)
END_MSG_LOOP()

#define FPS_STATICS_COUNT 100//calculate average fps every 100 frames
#define SKIP_AV_SYNC_FRM_COUNT_AT_BEGINING  150

//////////////////////////////////////////////////////////////////////////////////////////////////
mm_status_t VideoSink::VideoSinkWriter::write(const MediaBufferSP &buffer)
{
    FUNC_TRACK();
    // FIXME, use struct directly instead of SP
    VideoSink::QueueEntry *pEntry = new VideoSink::QueueEntry;
    pEntry->mBuffer = buffer;
    ++mRender->mTotalBuffersQueued;
    {
        MMAutoLock locker(mRender->mLock);
        buffer->setMonitor(mRender->mMonitorFPS);
    }

    mRender->postMsg(MCD_MSG_WRITE_BUFFER, 0, (param2_type)pEntry);

    if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        VideoSink::QueueEntry *pEntry = new VideoSink::QueueEntry;
        pEntry->mBuffer = MediaBufferSP((MediaBuffer*)NULL);

        mRender->postMsg(MCD_MSG_WRITE_BUFFER, 0, (param2_type)pEntry);
    }

    // FIXME, push the buffer to queue directly; uses std::queue instead of std::list
    // cheat the uplink component, since we always success. otherwise, we need post additional event to noticy uplink component
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSink::VideoSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    FUNC_TRACK();
    int32_t fps = 0;
    int32_t width = 0;
    int32_t height = 0;

    metaData->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
    metaData->getInt32(MEDIA_ATTR_WIDTH, width);
    metaData->getInt32(MEDIA_ATTR_HEIGHT, height);
    if (width == 0 || width > MAX_DIMENSION ||
        height == 0 || height > MAX_DIMENSION) {
        ERROR("invalid resolution %dx%d", width, height);
        return MM_ERROR_INVALID_PARAM;
    }

    DEBUG("fps %d, width %d, height %d", fps, width, height);
    mRender->mWidth = width;
    mRender->mHeight = height;
    mRender->mFps = fps;
#if defined(_ENABLE_EGL) || defined(_ENABLE_X11)
    void *ptr;
    if (metaData->getPointer("x11-display", ptr))
        mRender->mX11Display = (intptr_t)ptr;
    else
        mRender->mX11Display = 0;

    INFO("got x11 display from upstream component: %" PRId64 "", mRender->mX11Display);
#endif

    return MM_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
VideoSink::VideoSink() :
        MMMsgThread(COMPONENT_NAME),
        mWidth(0),
        mHeight(0),
        mFps(0),
        mPaused(true),
        mVideoDrainGeneration(0),
        // mDrainVideoQueuePending(false),
        mVideoRenderingStarted(false),
        mMediaRenderingStartedNeedNotify(true),
        mSegmentFrameCount(0),
        mRenderMode(RM_NORMAL),
        mCurrentPosition(-1ll),
        mDurationUs(-1ll),
        mLastRenderUs(-1ll),
        mScaledPlayRate(SCALED_PLAY_RATE),
        mForceRender(2),
        mEosSent(false),
        mTotalBuffersQueued(0),
        mRendedBuffers(0),
        mDroppedBuffers(0)
{
    FUNC_TRACK();
    //default clock
    mClockWrapper.reset(new ClockWrapper(ClockWrapper::kFlagVideoSink));

    std::string envStr = mm_get_env_str("mm.video.force.render", "MM_VIDEO_FORCE_RENDER");
    if(!envStr.empty())
        mForceRender = envStr[0] - '0';
    if (mForceRender == 0)
        INFO("forcely disable video rendering");
    if (mForceRender == 1)
        INFO("forcely enable video rendering (regardless of a/v sync)");
}

VideoSink::~VideoSink()
{
    FUNC_TRACK();
    mMonitorFPS.reset();
}

mm_status_t VideoSink::setClock(ClockSP clock)
{
    FUNC_TRACK();
    mm_status_t ret = mClockWrapper->setClock(clock);
    return ret;
}

mm_status_t VideoSink::init()
{
    FUNC_TRACK();
    if (MMMsgThread::run() != 0) {
        ERROR("VideoSink init failed");
        return MM_ERROR_OP_FAILED;
    }

    return MM_ERROR_SUCCESS;
}

void VideoSink::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}

mm_status_t VideoSink::prepare()
{
    FUNC_TRACK();
    postMsg(MCD_MSG_PREPARE, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::start()
{
    FUNC_TRACK();
    mLastRenderUs = -1ll;
    postMsg(MCD_MSG_START, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::pause()
{
    FUNC_TRACK();
    postMsg(MCD_MSG_PAUSE, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::resume()
{
    FUNC_TRACK();
    postMsg(MCD_MSG_RESUME, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::seek(int msec, int seekSequence)
{
    mSegmentFrameCount = 0;
    mLastRenderUs = -1ll;
    return MM_ERROR_SUCCESS;
 }

mm_status_t VideoSink::stop()
{
    FUNC_TRACK();
    postMsg(MCD_MSG_STOP, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::reset()
{
    FUNC_TRACK();
    postMsg(MCD_MSG_RESET, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoSink::flush()
{
    FUNC_TRACK();
    mLastRenderUs = -1ll;
    postMsg(MCD_MSG_FLUSH, 0, NULL);
    return MM_ERROR_ASYNC;
}

const char *VideoSink::name() const
{
    return COMPONENT_NAME;
}

mm_status_t VideoSink::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();

    mm_status_t ret = MM_ERROR_SUCCESS;
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;

        if ( !strcmp(item.mName, MEDIA_ATTR_PALY_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }

            if (mScaledPlayRate == item.mValue.ii) {
                DEBUG("play rate is already %d, just return\n", mScaledPlayRate);
                continue;
            }

            mScaledPlayRate = item.mValue.ii;
            DEBUG("key: %s, val: %d\n", item.mName, mScaledPlayRate);

            if (mClockWrapper) {
                ret = mClockWrapper->setPlayRate(mScaledPlayRate);
                if (ret != MM_ERROR_SUCCESS) {
                    ERROR("set playrate to clock failed\n");
                    continue;
                }
            }

            continue;
        } else if (!strcmp(item.mName, "render-mode") ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mRenderMode = item.mValue.ii;
            DEBUG("render-mode %d\n", mRenderMode);
        } else if (!strcmp(item.mName, "video-force-render") ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mForceRender = item.mValue.ii;
            INFO("mForceRender %d\n", mForceRender);
        }
    }

    return MM_ERROR_SUCCESS;
}

int64_t VideoSink::getCurrentPosition()
{

    if (mClockWrapper && mClockWrapper->getCurrentPosition(mCurrentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        mCurrentPosition = -1ll;
    }
    if (mDurationUs > 0 && mCurrentPosition > mDurationUs)
        mCurrentPosition = mDurationUs;
    VERBOSE("getCurrentPosition %" PRId64 " ms", mCurrentPosition/1000ll);
    return mCurrentPosition;
}


//only can be called on video render thread
mm_status_t VideoSink::scheduleRenderFrame_l()
{
    FUNC_TRACK();
/*
 * scheduleRenderFrame_l() is trigger from 3 places
 * 1) 'first' buffer comes in onWrite; buffer queue changes from empty
 * 2) after render one frame in normal state
 * 3) in onResume()
 */
#if 0
    if (mDrainVideoQueuePending)
        return MM_ERROR_SUCCESS;
#endif
    if (mPaused || mVideoQueue.empty() || mRenderMode == RM_SEEKPREVIEW_DONE) {
        DEBUG("mPaused %d, mRenderMode %d, size %d\n", mPaused, mRenderMode, mVideoQueue.size());
        return MM_ERROR_SUCCESS;
    }

    QueueEntrySP entry = mVideoQueue.front();
    MediaBufferSP buffer = entry->mBuffer;
    if (!buffer) {
        //EOS frame, buffer is NULL
        postMsg(MCD_MSG_RENDER_FRAME, mVideoDrainGeneration, NULL);
        return MM_ERROR_SUCCESS;
    }

    int64_t pts = buffer->pts();
    if (MM_LIKELY(!buffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
        // Some packet pts is -1 for TS file, DO NOT set anchor time when pts is invalid
        if (pts >= 0) {
            mClockWrapper->setAnchorTime(pts, Clock::getNowUs());
            // smooth video for 1000000us
            mClockWrapper->updateMaxAnchorTime(pts+1000000);
            // todo: got audio eos flag and set to clockwrapper
        }
    } else {
        MMLOGV("EOS, not set anchor\n");
    }
    int64_t lateUs = mClockWrapper->getMediaLateUs(pts);

    //lateUs = lateUs/1000ll;
    //TODO. lateUs needs to re-sync

    // video is ahead of audio
    // video too early is invalid pts, render immedicately
    int64_t delayUs = -1ll;
    if (lateUs <= -10*1000ll && lateUs >= -1000*1000ll && mForceRender != 1){
        delayUs = lateUs + 10*1000ll;
    } else {
        // when force-render, schedule to render the frame immediately
        delayUs = 0;
    }

    if (lateUs < -60*1000ll || lateUs > 40*1000ll) {
        DEBUG("pts %" PRId64 " ms, lateUs %" PRId64 " ms, delayUs %" PRId64 " ms", pts/1000ll, lateUs/1000ll, delayUs/1000ll);
    }
    postMsg(MCD_MSG_RENDER_FRAME, mVideoDrainGeneration, NULL, -delayUs);
    // mDrainVideoQueuePending = true;

    return MM_ERROR_SUCCESS;
}

void VideoSink::sendEosEvent()
{
    if (mEosSent)
        return;

    notify(kEventEOS, 0, 0, nilParam);
    mEosSent = true;
}

//called on video render thread
void VideoSink::onRenderFrame(param1_type param1, param2_type param2, uint32_t rspId)
{
    if (param1 != (param1_type)mVideoDrainGeneration) {
        DEBUG("obsolete, param1 %d, mVideoDrainGeneration %d",
                param1, mVideoDrainGeneration);
        return;
    }

    // mDrainVideoQueuePending = false;
    if (mVideoQueue.empty()) {
        return;
    }

    // FIXME, why use queue<SP>, not queue<QueueEntry>?
    QueueEntrySP entry = mVideoQueue.front();
    MediaBufferSP buffer = entry->mBuffer;
    if (!buffer) {
        INFO("EOS frame");
        if (mRenderMode == RM_SEEKPREVIEW) {
            mRenderMode = RM_SEEKPREVIEW_DONE;
            notify(kEventInfo, kEventInfoSeekPreviewDone, 0, nilParam);
        }
        mVideoQueue.pop();
        sendEosEvent();
        return;
    }

    // FIXME, assert the buffer type is drm name
    MediaMetaSP meta = buffer->getMediaMeta();
    if (!meta) {
        ERROR("no meta data");
        if (mRenderMode == RM_SEEKPREVIEW) {
            mRenderMode = RM_SEEKPREVIEW_DONE;
            notify(kEventInfo, kEventInfoSeekPreviewDone, 0, nilParam);
        }
        mVideoQueue.pop();
        sendEosEvent();
        return;
    }

    int64_t pts = buffer->pts();
    int64_t lateUs = mClockWrapper->getMediaLateUs(pts);

    //check the buffer is too late only in slower/normal playback speed
    bool render = true;
    if (mScaledPlayRate <= SCALED_PLAY_RATE) {
        // on start/resume/seek, ignore a/v sync for the first several frames
        if (mSegmentFrameCount++ > SKIP_AV_SYNC_FRM_COUNT_AT_BEGINING)
            render = lateUs < 150*1000ll;
    }

    if (mForceRender == 0)
        render = false;
    if (mForceRender == 1)
        render = true;

    if (!render) {
        DEBUG("discard this frame, pts %0.3f, lateUs %0.3f\n", pts/1000000.0f, lateUs/1000000.0f);
    }

    meta->setInt32(MEDIA_ATTR_IS_VIDEO_RENDER, (int32_t)render);
    //show one frame
    int64_t begin = getTimeUs();

    if (render && mLastRenderUs && mFps < 40) {
        begin = getTimeUs();
        if (begin - mLastRenderUs < 16000) {
            INFO("render time %dms, last render time %dms", (int)(begin/1000), (int)(mLastRenderUs/1000));
            int64_t delayUs = begin - mLastRenderUs;
            postMsg(MCD_MSG_RENDER_FRAME, mVideoDrainGeneration, NULL, delayUs);
            return;
        }
    }

    drawCanvas(buffer);
    VERBOSE("drawCanvas cost %0.3f\n", (getTimeUs()- begin)/1000000.0f);
    mLastRenderUs = getTimeUs();
    mVideoQueue.pop();

    if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        INFO("EOS frame");
        sendEosEvent();
    }
    if (buffer->pts() > 0) {
        mDurationUs = buffer->pts();
    }
    DEBUG("mTotalBuffersQueued: %d, mRendedBuffers: %d, mDroppedBuffers: %d, buffer age: %d", mTotalBuffersQueued, mRendedBuffers,  mDroppedBuffers, buffer->ageInMs());
    if (render) {
        // in some condition, video rendering happends in MediaBuffer's deletor func, trigger it
        buffer.reset();
        notifyRenderStartIfNeed();
        if (mRenderMode == RM_SEEKPREVIEW) {
            mRenderMode = RM_SEEKPREVIEW_DONE;
            notify(kEventInfo, kEventInfoSeekPreviewDone, 0, nilParam);
        }

        mRendedBuffers++;
    } else {
        mDroppedBuffers++;
    }
    if (mDroppedBuffers && mDroppedBuffers % 10 == 0) {
        WARNING("dropped frame count %d", mDroppedBuffers);
    }


    scheduleRenderFrame_l();

}

void VideoSink::notifyRenderStartIfNeed()
{
    FUNC_TRACK();
    if (!mPaused && !mVideoRenderingStarted) {
        INFO("kEventInfoVideoRenderStart");
        mVideoRenderingStarted = true;
        notify(kEventInfo, kEventInfoVideoRenderStart, 0, nilParam);
    }

    // FIXME, should it check mPaused?
    if (mMediaRenderingStartedNeedNotify) {
        INFO("kEventInfoMediaRenderStarted");
        mMediaRenderingStartedNeedNotify = false;
        notify(kEventInfo, kEventInfoMediaRenderStarted, 0, nilParam);
    }
}

void VideoSink::onWrite(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);

    QueueEntry *pEntry = (VideoSink::QueueEntry *)param2;
    mVideoQueue.push(QueueEntrySP(pEntry));

    // onRenderFrame();
    if (mVideoQueue.size() == 1)
        scheduleRenderFrame_l();
}


Component::WriterSP VideoSink::getWriter(MediaType mediaType)
{
    FUNC_TRACK();
    Component::WriterSP writer;

    if ( (int)mediaType != Component::kMediaTypeVideo ) {
           ERROR("not supported mediatype: %d\n", mediaType);
           return writer;
       }
    writer.reset(new VideoSink::VideoSinkWriter(this, &mVideoQueue));

    return writer;
}

//must be called on msg thread
void VideoSink::flushBufferQueue(bool isClearVideoStartRendering)
{
    FUNC_TRACK();
    while(!mVideoQueue.empty()) {
        // discard buffers in queue
        QueueEntrySP entry = mVideoQueue.front();
        MediaBufferSP buffer = entry->mBuffer;
        if (buffer) {
            MediaMetaSP meta = buffer->getMediaMeta();
            if (meta) {
                DEBUG("skip queued buffers");
                meta->setInt32(MEDIA_ATTR_IS_VIDEO_RENDER, 0);
            }
        }
        mVideoQueue.pop();
    }

    if (mClockWrapper) {
        mClockWrapper->flush();
    }

    // mDrainVideoQueuePending = false;
    mMediaRenderingStartedNeedNotify = true;
    if (isClearVideoStartRendering) {
        mVideoRenderingStarted = false;
    }
    mRenderMode = RM_NORMAL;
    mVideoDrainGeneration++;
    mScaledPlayRate = SCALED_PLAY_RATE;

    // FIXME, the flushed buffer in mVideoQueue isn't counted in mDrpoppedBuffers
    DEBUG("mRendedBuffers %d, mDroppedBuffer %d", mRendedBuffers, mDroppedBuffers);
    mRendedBuffers = 0;
    mDroppedBuffers = 0;
}

void VideoSink::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);
    flushBufferQueue(false);
    flushCanvas();
    notify(kEventFlushComplete, 0, 0, nilParam);
}

void VideoSink::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);

    flushBufferQueue();
    uninitCanvas();
    notify(kEventResetComplete, 0, 0, nilParam);
    mEosSent = false;
}

void VideoSink::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    // FIXME setup x11/egl/gles environments
    mm_status_t ret;
    //FIXME: sometimes mX11Display is null,
    //           I don't know why, it is better than print the variables
#if defined(_ENABLE_EGL) || defined(_ENABLE_X11)
    INFO("now the mX11Display :%p\n",mX11Display);
#endif
    ret = initCanvas();
    if (ret != MM_ERROR_SUCCESS) {
        notify(kEventPrepareResult, ret, 0, nilParam);
        return ;
    }

    notify(kEventPrepareResult, 0, 0, nilParam);
    return;
}

// FIXME, make sure onStartonResume use different event
void VideoSink::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);

    onResume(param1, param2, rspId);
}

void VideoSink::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);

    if (mPaused) {
        INFO("already paused, just return");
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    if (mClockWrapper) {
        mClockWrapper->pause();
    }

    mRenderMode = RM_NORMAL;
    DEBUG("set renderMode to RM_NORMAL\n");
    mPaused = true;

    // mDrainVideoQueuePending = false;
    mMediaRenderingStartedNeedNotify = true;
    mVideoDrainGeneration++;

    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoSink::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);

    if (!mPaused) {
        INFO("already started, just return");
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    mSegmentFrameCount = 0;
    //resume from pause state
    if (mClockWrapper) {
        mClockWrapper->resume();
    }
    mPaused = false;
    mEosSent = false;

    if (!mVideoQueue.empty()) {
        scheduleRenderFrame_l();
    }

    {
        MMAutoLock locker(mLock);
        mMonitorFPS.reset(new TimeStatics(FPS_STATICS_COUNT, 0, 0, "MonitorFPS"));
    }
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
}



void VideoSink::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(!rspId);
    flushBufferQueue();
    mPaused = true;
    mVideoDrainGeneration = 0;
    flushCanvas();
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
}

mm_status_t VideoSink::initCanvas()
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSink::drawCanvas(MediaBufferSP buffer)
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSink::uninitCanvas()
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

ClockSP VideoSink::provideClock()
{
    FUNC_TRACK();
    return mClockWrapper->provideClock();
}

} // YUNOS_MM

#ifdef _CREATE_VIDEOSINKBASIC
/////////////////////////////////////////////////////////////////////////////////////
extern "C"
{

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::VideoSink *sinkComponent = new YUNOS_MM::VideoSink();
    if (sinkComponent == NULL) {
       return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}

void releaseComponent(YUNOS_MM::Component *component)
{
    // FIXME, uninit()?
    delete component;
}

}
#endif
