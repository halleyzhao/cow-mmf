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

#ifndef video_sink_h
#define video_sink_h
#include <queue>
#include "multimedia/mmmsgthread.h"
#include <multimedia/component.h>
#include <multimedia/media_monitor.h>
#include <multimedia/clock.h>
#include "../clock_wrapper.h"

namespace YUNOS_MM {

class VideoSink : public PlaySinkComponent, public MMMsgThread {
public:
    VideoSink();
    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType);
    // we support pushed data only (not pull initiatively)
    virtual ClockSP provideClock();
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return  MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setClock(ClockSP clock);
    virtual mm_status_t init();
    virtual void uninit();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t prepare();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
//    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual int64_t getCurrentPosition();

protected:
    virtual ~VideoSink();

private:
    struct QueueEntry {
        MediaBufferSP mBuffer;
        mm_status_t mFinalResult;
    };
    enum RenderMode {
        RM_NORMAL,
        RM_SEEKPREVIEW,
        RM_SEEKPREVIEW_DONE
    };
    typedef MMSharedPtr<QueueEntry> QueueEntrySP;
    typedef std::queue<QueueEntrySP> VideoEntryList;

    class VideoSinkWriter : public Writer {
    public:
        VideoSinkWriter(VideoSink *sink, VideoEntryList *list){
            mRender = sink;
            mVideoList = list;
        }
        virtual ~VideoSinkWriter(){}
        virtual mm_status_t write(const MediaBufferSP &buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
    private:
        VideoSink *mRender;
        VideoEntryList *mVideoList;
    };

    mm_status_t scheduleRenderFrame_l();
    void notifyRenderStartIfNeed();
    void flushBufferQueue(bool isClearVideoStartRendering = true);

    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t flushCanvas() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t uninitCanvas();

public:
#if defined(_ENABLE_EGL)||defined(_ENABLE_X11)
    intptr_t  mX11Display;
#endif
    int32_t mWidth;
    int32_t mHeight;
    int32_t mFps;

private:
    void sendEosEvent();
    Lock mLock;
    bool mPaused;

    VideoEntryList mVideoQueue;
    int32_t mVideoDrainGeneration;
    // FIXME, how it works? simplify it
    // bool mDrainVideoQueuePending;
    bool mVideoRenderingStarted;
    bool mMediaRenderingStartedNeedNotify;
    uint32_t mSegmentFrameCount; // frame count after discontinue: start/pause/seek

    int32_t mRenderMode;

    int64_t mCurrentPosition;
    int64_t mDurationUs;
    int64_t mLastRenderUs;

    WriterSP mWriter;
    ClockWrapperSP mClockWrapper;

    int32_t mScaledPlayRate;
    MonitorSP mMonitorFPS;
    uint32_t mForceRender; // 0: force not render, 1: force render, other: normaly depending on a/v sync
    bool mEosSent;

    // debug use only
    int32_t mTotalBuffersQueued;
    int32_t mRendedBuffers;
    int32_t mDroppedBuffers;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onWrite)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onRenderFrame)
    DECLARE_MSG_HANDLER(onReset)


    MM_DISALLOW_COPY(VideoSink);
};

} // end of namespace YUNOS_MM
#endif//video_sink_egl_h

