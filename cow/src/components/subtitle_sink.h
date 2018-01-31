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

#ifndef subtitle_sink_h
#define subtitle_sink_h
#include <queue>
#include "multimedia/mmmsgthread.h"
#include <multimedia/component.h>
#include <multimedia/media_monitor.h>
#include <multimedia/clock.h>
#include "../clock_wrapper.h"
#include <unistd.h>


namespace YUNOS_MM {

class SubtitleSink : public FilterComponent, public MMMsgThread {

public:

    class ReadThread;
    typedef MMSharedPtr <ReadThread> ReadThreadSP;
    class ReadThread : public MMThread {
      public:
        ReadThread(SubtitleSink *subtitlesink);
        ~ReadThread();
        void signalExit();
        void signalContinue();

      protected:
        virtual void main();

      private:
        SubtitleSink *mSubtitleSink;
        bool mContinue;
    };

    SubtitleSink(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~SubtitleSink();
    void bufferDump(unsigned char *buf, int len);

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t resume();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t read(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t write(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual MediaMetaSP getMetaData() {return MediaMetaSP((MediaMeta*)NULL);}
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData) { return MM_ERROR_UNSUPPORTED; }
    mm_status_t scheduleRenderSubtitle();
    void clearBuffers();
    virtual mm_status_t setClock(ClockSP clock);


private:


    pthread_mutex_t mMutex;

    std::string mComponentName;

    ReaderSP mReader;
    bool mIsPaused;
    bool mFirstPTS;

    MonitorSP mMonitorWrite;
    Condition mCondition;
    Lock mLock;
    ReadThreadSP mReadThread;
    std::queue<MediaBufferSP> mSubtitleQueue;
    ClockWrapperSP mClockWrapper;
    MediaBufferSP mMediaBuffer;
    uint64_t mLastDuration;


    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onSeek)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSetParameters)
    DECLARE_MSG_HANDLER(onGetParameters)
    DECLARE_MSG_HANDLER(onRenderSubtitle)

    MM_DISALLOW_COPY(SubtitleSink);

};

}

#endif //subtitle_sink_h


