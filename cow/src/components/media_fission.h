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

#ifndef media_fission_h
#define media_fission_h

#include <queue>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_monitor.h"

namespace YUNOS_MM {

class MediaFission : public FilterComponent, public MMMsgThread {
  private: // ////// internal classes
    class OutBufferQueue {
      public:
        OutBufferQueue(uint32_t maxQSize);
        ~OutBufferQueue(){}
        void pushBuffer(MediaBufferSP buffer);
        MediaBufferSP frontBuffer();
        void popBuffer();
        void unblockWait();
        bool isFull();

      private:
        MonitorSP mTraffic;
        struct BufferTracker {
            MediaBufferSP mBuffer;
            TrackerSP mTracker;
        };
        std::queue<BufferTracker> mBuffers;
        const char* MM_LOG_TAG;
    };
    std::vector<OutBufferQueue> mBufferQueues;

    // for input master mode (pulls data from uplink component), it is supported in MMMsgThread by MFISSION_MSG_handleInputBuffer
    // input in slave mode: uplink component pushes data to FissionWriter
    class FissionWriter : public Writer {
        public:
            FissionWriter(MediaFission *fission);
            virtual ~FissionWriter(){}
            virtual mm_status_t write(const MediaBufferSP &buffer);
            virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
        private:
            MediaFission *mFission;
            std::string mLogTag;
            const char * MM_LOG_TAG;
    };

    // output in slave mode: downlink component pulls data from FissionReader
    class FissionReader : public Reader {
    public:
        FissionReader(MediaFission * from, uint32_t index);
        ~FissionReader();

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        MediaFission * mComponent;
        uint32_t mIndex;
        std::string mLogTag;
        const char * MM_LOG_TAG;
    };
    // output in master mode: push data to downlink components
    class PushDataThread;
    typedef MMSharedPtr <PushDataThread> PushDataThreadSP;

    // ////// MediaFission itself
  public:
    enum StateType{
        kStateNull = 0,     // 0 default state after creation
        kStatePreparing,    // 1 in the process of  construct pipeline (prepare)
        kStatePrepared,     // 2 after pipeline is created, initialized with necessary information
        kStatePaused,       // 3 paused
        kStatePlay,         // 4 going into play
        kStatePlaying,      // 5 is playing
        kStateStopping,     // 6 goint into stop
        kStateStopped,      // 7 stopped
        kStateInvalid,      // 8
    };
    enum EosStateType {
        kNoneEOS,
        kInputEOS,
        kOutputEOS,
    };

    MediaFission(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~MediaFission();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t init();
    virtual void uninit();

    void setState(StateType state);
    void setEosState(EosStateType state);
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual MediaMetaSP getMetaData() {return mFormat;}

  private:
    void clearInternalBuffers();
    bool isRunning();
    Lock mLock;
    std::string mMime;
    std::string mComponentName;
    StateType mState;
    EosStateType mEosState;

    bool mInputMaster; // true if Fission actively pulls data from uplink component
    ReaderSP mReader;
    MediaMetaSP mFormat;

    uint32_t mBufferCapacity;

    // output in slave mode
    // FIXME, should we keep one reference of these Readers? it may help to differentiate stop() and reset() well.
    // std::vector<FissionReader> mReaders;
    // output in master mode
    PushDataThreadSP mPushThread;
    class MasterWriter {
      public:
        MasterWriter(WriterSP writer, uint32_t idx)
            : mWriter(writer)
            , mBufQueIndex(idx)
            , mOutputBufferCount(0) {}
        virtual ~MasterWriter() {}
        WriterSP mWriter;
        uint32_t mBufQueIndex;
        uint32_t mOutputBufferCount;
    };
    std::vector<MasterWriter> mMasterWriters;

    /*
        - input buffer is pulled from uplink component in MMMsgThread, push_back to buffer queue (mBuffers)
        - FIXME, do not use push, but wait for downlink pull?
        - output buffer is also pulled by downlink components by supporting getReader()
        - input and output run in different thread,  the cached buffer size (mBuffers) is controlled by TrafficControl (mTraffic)
    */

    // debug use only
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onHandleInputBuffer)

    MM_DISALLOW_COPY(MediaFission)

    const char * MM_LOG_TAG;
};

}

#endif // media_fission_h
