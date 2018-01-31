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

#ifndef __video_filter_cl_h__
#define __video_filter_cl_h__

#include <vector>

#include <multimedia/component.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mmmsgthread.h>
#include "media_surface_utils.h"
#include "buffer_pool_surface.h"

namespace YUNOS_MM {
class WorkerThread ;
typedef MMSharedPtr <WorkerThread > WorkerThreadSP;

class VideoFilterCL : public FilterComponent, public MMMsgThread {
public:
    enum StateType {
        STATE_IDLE,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_PAUSED,
        STATE_STOPED,
    };

    VideoFilterCL();
    ~VideoFilterCL();

public:
    virtual mm_status_t init();
    virtual void uninit();

public:
    virtual const char * name() const { return "VideoFilterCL"; }
    COMPONENT_VERSION;
    virtual ReaderSP getReader(MediaType mediaType);
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const { return MM_ERROR_SUCCESS; }
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();

    // create new buffer from surface buffer pool
    MediaBufferSP createOutputBuffer(MediaBufferSP ref);
    static bool releaseOutputBuffer(MediaBuffer* mediaBuffer);

private:


    enum msg_t {
        MSG_prepare = 0,
        MSG_start = 1,
        MSG_stop = 2,
        MSG_flush = 3,
        MSG_reset = 4,
        MSG_releaseBuffer = 5,
    };

    class FilterReader : public Component::Reader {
    public:
        FilterReader(VideoFilterCL *comp) {
            mComponent = comp;
            // mContinue = true;
        }
        virtual ~FilterReader() {
            MMAutoLock locker(mComponent->mLock);
            // mContinue = false;
            mComponent->mCondition.signal();
        }

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        VideoFilterCL *mComponent;
        // bool mContinue;
    };
    class FilterWriter : public Component::Writer {
    public:
        FilterWriter(VideoFilterCL *comp) {
            mComponent = comp;
            // mContinue = true;
        }
        virtual ~FilterWriter() {
            MMAutoLock locker(mComponent->mLock);
            // mContinue = false;
            mComponent->mCondition.signal();
        }

        virtual mm_status_t write(const MediaBufferSP & buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

    private:
        VideoFilterCL *mComponent;
        // bool mContinue;
    };

    void clearSourceBuffers();
    void setState(int state);


private:
    friend class WorkerThread;
    std::queue<MediaBufferSP> mInputBuffers;
    std::queue<MediaBufferSP> mOutputBuffers;
    WorkerThreadSP mWorkerThread;

    // filter supports either AddSource() or getWriter(), either AddSink() or getReader()
    // another option is: use MediaFission for master/slave conversion
    typedef enum {
        PMT_UnDecided,
        PMT_Master, // AddSource() or AddSink()
        PMT_Slave, // getWriter() or getReader()
    } PortModeType;
    PortModeType mPortMode[2];

    // bool mIsPaused;
    // bool mIsEOS;
    uint32_t mWidth;
    uint32_t mHeight;
    Condition mCondition;
    Lock mLock;
    StateType mState;
    // int32_t mTotalBuffersQueued;
    // uint64_t mDoubleDuration;
    // int64_t mStartTimeUs;
    ReaderSP mReader;
    WriterSP mWriter;
    MediaMetaSP mInputFormat;
    MediaMetaSP mOutputFormat;

    WindowSurface* mSurface;
    YunOSMediaCodec::SurfaceWrapper *mSurfaceWrapper;
    BufferPoolSurface* mBufferPool;
    uint32_t mGeneration;

    // debug use
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;
protected:

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onReleaseOutBuffer)

    MM_DISALLOW_COPY(VideoFilterCL)
    DECLARE_LOGTAG()
};

}

#endif // __video_filter_cl_h__


