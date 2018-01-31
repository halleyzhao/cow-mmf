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

#ifndef video_decode_ducati_h
#define video_decode_ducati_h

#include <queue>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "make_csd.h"
#include "multimedia/mm_debug.h"
//
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

#include "libdce.h"
#include <xf86drm.h>
#include <omap_drm.h>
#include <omap_drmif.h>

namespace YUNOS_MM {
typedef struct _outputBufferType OutputBufferType;
class VideoDecodeDucati : public FilterComponent, public MMMsgThread {
  public:
    class WorkerThread;
    typedef MMSharedPtr <WorkerThread> WorkerThreadSP;
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

    VideoDecodeDucati(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~VideoDecodeDucati();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t init();
    virtual void uninit();

    void setState(StateType state);
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

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual MediaMetaSP getMetaData() {return mOutputFormat;}
    void recycleOutputBuffer(OutputBufferType* outBuffer);

  private:
    void updateBufferGeneration();
    StreamMakeCSDSP createStreamCSDMaker(MediaMetaSP &meta);
    MediaBufferSP getOneInputBuffer();
    bool putOneOutputBuffer(MediaBufferSP buffer);

  private:
    Lock mLock;
    std::string mMime;
    std::string mComponentName;
    StateType mState;
    Component* mSource;
    Component* mSink;
    uint32_t mFormat;
    int32_t mRotationDegrees;

    ReaderSP mReader;
    WriterSP mWriter;
    MediaMetaSP mInputFormat, mOutputFormat;
    StreamMakeCSDSP mStreamCSDMaker;
    bool mIsAVCcType;
    bool mForceByteStream;
    int32_t mCSDBufferIndex;
    bool mPrefixCSD;
    int32_t mWidth, mHeight;

    WorkerThreadSP mWorkerThread;
    bool mConfigured; // codec had been started before.
    uint32_t mGeneration;
    std::queue<OutputBufferType*> mRecycleBuffers; // only main class has strict state machine to recycle buffer

    // FIXME, debug use only
    bool mDumpInput;
    DataDump mInputDataDump;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onHandleResolutionChange)
    DECLARE_MSG_HANDLER(onRecycleOutBuffer)

    MM_DISALLOW_COPY(VideoDecodeDucati)
};

}

#endif // video_decode_ducati_h
