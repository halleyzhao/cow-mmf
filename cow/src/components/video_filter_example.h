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

#ifndef __video_filter_example_h__
#define __video_filter_example_h__

#include <semaphore.h>
#include <vector>

#include <multimedia/component.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mmmsgthread.h>

namespace YUNOS_MM {
class PollThread;
typedef MMSharedPtr <PollThread> PollThreadSP;

class VideoFilterExample : public FilterComponent, public MMMsgThread {
public:
    enum StateType {
        STATE_IDLE,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_PAUSED,
        STATE_STOPED,
    };

    VideoFilterExample();
    ~VideoFilterExample();

public:
    virtual mm_status_t init();
    virtual void uninit();

public:
    virtual const char * name() const { return "VideoFilterExample"; }
    COMPONENT_VERSION;
    virtual ReaderSP getReader(MediaType mediaType);
    virtual WriterSP getWriter(MediaType mediaType){ return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType){return MM_ERROR_UNSUPPORTED;}

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const { return MM_ERROR_SUCCESS; }
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();

private:


    enum msg_t {
        MSG_prepare = 0,
        MSG_start = 1,
        MSG_stop = 2,
        MSG_flush = 3,
        MSG_reset = 4,
        MSG_addSource = 5
    };

    class GrayReader : public Component::Reader {
    public:
        GrayReader(VideoFilterExample *comp) {
            mComponent = comp;
            mContinue = true;
        }
        virtual ~GrayReader() {
            MMAutoLock locker(mComponent->mLock);
            mContinue = false;
            mComponent->mCondition.signal();
        }

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        VideoFilterExample *mComponent;
        bool mContinue;
    };

    void clearSourceBuffers();
    void setState(int state);


private:
    friend class PollThread;
    std::queue<MediaBufferSP> mAvailableSourceBuffers;
    PollThreadSP mPollThread;

    bool mIsPaused;
    bool mIsEOS;
    Condition mCondition;
    Lock mLock;
    StateType mState;
    int32_t mTotalBuffersQueued;
    uint64_t mDoubleDuration;
    int64_t mStartTimeUs;
    ReaderSP mReader;
    MediaMetaSP mInputFormat;

protected:

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onAddSource)


    MM_DISALLOW_COPY(VideoFilterExample)
    DECLARE_LOGTAG()


};

}

#endif // __video_filter_example_h__


