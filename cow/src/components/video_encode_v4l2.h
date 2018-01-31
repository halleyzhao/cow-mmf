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

#ifndef video_encode_v4l2_h
#define video_encode_v4l2_h

#include <queue>

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "v4l2codec_device.h"
#include "multimedia/mm_surface_compat.h"

#if defined(__MM_YUNOS_YUNHAL_BUILD__)
#include <cutils/yalloc.h>
#endif

namespace YUNOS_MM {

class VideoEncodeV4l2 : public FilterComponent, public MMMsgThread {
  public:
    class DevicePollThread;
    typedef MMSharedPtr <DevicePollThread> DevicePollThreadSP;
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

    VideoEncodeV4l2(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~VideoEncodeV4l2();

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
    virtual mm_status_t pause() {return MM_ERROR_SUCCESS;}
    virtual mm_status_t resume() {return MM_ERROR_SUCCESS;}
    virtual mm_status_t seek(int msec, int seekSequence) {return MM_ERROR_SUCCESS;}
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual MediaMetaSP getMetaData() {return mOutputFormat;}
    static  bool releaseOutputBuffer(MediaBuffer* mediaBuffer);

  private:
    static uint32_t fourccConvert(uint32_t fourcc);
    void dumpResetInputBuffer(unsigned long handle, int w, int h, bool isDump = true);
    uint32_t returnInputBuffer(unsigned long address);
    uint32_t checkInputBuffer(unsigned long address);

    void queueOutputBuffer(uint32_t index);
    void dequeueInputBuffer();
    void dequeueOutputBuffer();
    int32_t getCodecDataSize(uint8_t *data, int32_t size);
    mm_status_t requestIDR();

  private:
    Lock mLock;
    std::string mMime;
    std::string mComponentName;
    StateType mState;
    EosStateType mEosState;
    bool mIsLive;
    Component* mSource;
    Component* mSink;
    MediaBuffer::MediaBufferType mInputBufferType;
    #define kMaxInputPlaneCount     3
    uint32_t mInputPlaneCount;
    int32_t mWidths[kMaxInputPlaneCount], mHeights[kMaxInputPlaneCount];
    uint32_t mFormat;
    uint32_t mFramerateNum, mFramerateDenom;
    uint32_t mBitrate;
    enum v4l2_memory mInputMemoryType;
    enum v4l2_memory mOutputMemoryType;

    ReaderSP mReader;
    WriterSP mWriter;
    MediaMetaSP mInputFormat, mOutputFormat;
    std::queue<int64_t> mEncodingDTSQueue;

    // buffers deal with V4l2Codec
    uint32_t mInputQueueCapacity;
    struct BufferInfo {
        uint32_t width;
        uint32_t height;
        uint32_t pitch[3];
        uint32_t offset[3];
        uint32_t fourcc;            //NV12
        uint8_t *data;
        uint32_t size; //for output, record size
        int32_t flags; //for output, record flag
#ifndef __MM_NATIVE_BUILD__
        MMBufferHandleT mHandle;
#endif
        MediaBufferSP mBuffer;
        int64_t pts;
    };
    std::vector<struct BufferInfo> mInputFrames;
    std::queue<uint32_t>    mInputIndces; //index dequeued from yami encoder

    std::list<uintptr_t> mInputBufferStaging; // used in V4L2_MEMORY_USERPTR only, buffer queued to device, but not dequed back
    std::map<uintptr_t, MediaBufferSP> mInputMediaBufferStaging;

    uint32_t mOutputQueueCapacity;
    std::vector<struct BufferInfo>   mOutputBuffers;
    std::queue<uint32_t>    mOutputIndces;

    uint32_t mStrideX = 0;
    uint32_t mStrideY = 0;

    /*
        input/output buffers are handled in MMMsgThread (handleInputBuffer/handleOutputBuffer),
        = these operations are non-block. operations includes:
            - deque/enque input/output buffer from/to V4l2Codec
            - read()/write() MediaBuffer from other components.
        = tasks are scheduled when there is event from mDevicePoolThread. if corresponding MediaBuffer
           is not available at that time, the task will schedule a new one by itself.
        == enque all input buffer (index) upon start(), while not necessary for output buffer (index)
    */
    DevicePollThreadSP mDevicePollThread;
    V4l2CodecDeviceSP mV4l2Encoder;
    bool mConfigured; // codec had been started before.
    MonitorSP mMonitorWrite;
    uint32_t mDropFrameThreshHold2;

    // debug use only
    uint32_t mInputBufferCount;
    uint32_t mInputBufferDropedCount;
    uint32_t mOutputBufferCount;
    uint32_t mOutputBufferRecycleCount;

    // for debug
    bool mDumpInputBuffer;
    bool mDumpOutputBuffer;
    bool mResetInputBuffer;
    bool mResetOutputBuffer;
    std::map<int64_t, int64_t> mTimeStampMap;

    int32_t mRepeated;
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    yalloc_device_t* mYalloc;
#endif
    bool mIsCodecDataSet;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onHandleInputBuffer)
    DECLARE_MSG_HANDLER(onHandleOutputBuffer)
    DECLARE_MSG_HANDLER(onHandleEncoderEvent)

    MM_DISALLOW_COPY(VideoEncodeV4l2)
};

}

#endif // video_encode_v4l2_h
