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

#ifndef video_decode_v4l2_h
#define video_decode_v4l2_h

#include <queue>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "v4l2codec_device.h"
#include "make_csd.h"
#include "multimedia/mm_surface_compat.h"
#include "multimedia/mm_debug.h"
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
#include <cutils/yalloc.h>
#endif


namespace YunOSMediaCodec {
    class MediaSurfaceTexture;
    class SurfaceWrapper;
}

namespace YUNOS_MM {
class V4l2TexureListener;
typedef MMSharedPtr < V4l2TexureListener > V4l2TexureListenerSP;

class VideoDecodeV4l2 : public FilterComponent, public MMMsgThread {
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

    VideoDecodeV4l2(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~VideoDecodeV4l2();

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

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual MediaMetaSP getMetaData() {return mOutputFormat;}
    static  bool releaseOutputBuffer(MediaBuffer* mediaBuffer);

friend class V4l2TexureListener;

  private:
    struct BufferInfo {
        enum BufferStatus {
            OWNED_BY_US,            // owned by VideoDecodeV4l2
            OWNED_BY_V4L2CCODEC,    // owned by hw codec
            OWNED_BY_NATIVE_WINDOW, // owned by 'display server'
            OWNED_BY_CLIENT         // owned by sink component
        };
        BufferInfo(MMNativeBuffer *anb = NULL) : mANB(anb)
                                            , mOwner(OWNED_BY_US)
                                            , width(-1)
                                            , height(-1)
                                            , fourcc(-1)
                                            , data(NULL)
                                            {}
        MMNativeBuffer *mANB;

        BufferStatus mOwner;
        uint32_t width;
        uint32_t height;
        uint32_t pitch[4];
        uint32_t offset[4];
        uint32_t fourcc;            //NV12
        uint8_t *data;
    };

    mm_status_t renderOutputBuffer(struct v4l2_buffer *buf, bool renderIt);
    void updateBufferGeneration();
    StreamMakeCSDSP createStreamCSDMaker(MediaMetaSP &meta);

    MediaBufferSP createRawMediaBuffer(v4l2_buffer *buf);
    MediaBufferSP createHwMediaBuffer(v4l2_buffer *buf);
    bool allBuffersBelongToUs();
    void dumpBuffer(MMBufferHandleT target, int w, int h);
    void dbgSurfaceBufferStatus(int line, bool forceErrorDebug = false);
    void dbgSurfaceBufferStatus(bool gotBufferCount = false);
    const char *bufferStatusToString(BufferInfo::BufferStatus s);
    static int getTransform(int degree);
    mm_status_t allocateOutputBuffers();
    mm_status_t handlePortSettingChanged();
    void processDeferMessage();
    void deferMessage(msg_type what, param1_type param1, param2_type param2, uint32_t rspId);
    int getCSDInfo(uint8_t *data, int32_t &size);

  private:
    Lock mLock;
    std::string mMime;
    std::string mComponentName;
    StateType mState;
    EosStateType mEosState;
    Component* mSource;
    Component* mSink;
    int32_t mWidth, mHeight;
    int32_t mFp = 0;
    int32_t mVideoDpbSize = 0;
    uint32_t mFormat;
    int32_t mRotationDegrees;

    ReaderSP mReader;
    WriterSP mWriter;
    MediaMetaSP mInputFormat, mOutputFormat;
    StreamMakeCSDSP mStreamCSDMaker;
    bool mIsAVCcType;
    int32_t mCSDBufferIndex;
    bool mForceByteStream = false;

    // buffers deal with V4l2Codec
    enum v4l2_memory mInputMemoryType;
    enum v4l2_memory mOutputMemoryType;
    uint32_t mInputQueueCapacity;
    std::vector<uint8_t*>   mInputBuffers;
    std::queue<uint32_t>    mInputIndces;
    std::queue<MediaBufferSP> mInputBufferStaging; // used in V4L2_MEMORY_USERPTR only, buffer queued to device, but not dequed back
    /* work around: use similar schedule as VEV4L2 does may be better
     * we scheduled more onHandleInputBuffer/onHandleOutputBuffer than required.
     * usually, extra onHandleInputBuffer isn't harm when there is no available input buffer slot.
     * when there is available input buffer slot from v4l2codec but no input data from demuxer; another onHandleInputBuffer will be scheduled in the future.
     * then VDV4L2_MSG_handleInputBuffer becomes more and more w/o drop.
     */
    int64_t mLastTimeInputRetry;

    uint32_t mOutputQueueCapacity;
    uint32_t mOutputPlaneCount;
    bool mDumpInput;
    bool mDumpOutput;
    mm_status_t configOutputSurface();

    mm_status_t cancelBufferToNativeWindow(BufferInfo *info);
    mm_status_t allocateOutputBuffersFromNativeWindow();
    bool setSurface();
    YunOSMediaCodec::SurfaceWrapper *mSurfaceWrapper;
    YunOSMediaCodec::MediaSurfaceTexture *mSurfaceTexture;

    bool mSurfaceOwnedByUs;
    WindowSurface* mSurface;
    std::vector<BufferInfo> mOutputFrames;

    /*
        input/output buffers are handled in MMMsgThread (handleInputBuffer/handleOutputBuffer),
        = these operations are non-block. operations includes:
            - deque/enque input/output buffer from/to V4l2Codec
            - read()/write() MediaBuffer from other components.
        = tasks are scheduled when there is event from mDevicePoolThread. if corresponding MediaBuffer
           is not available at that time, the task will schedule a new one by itself.
        == the initial buffer ownership is VDV4L2.
          enque all input buffer (index) during onStartInput(), enque all output buffer during onStartOutput()
    */
    DevicePollThreadSP mDevicePollThread;
    V4l2CodecDeviceSP mV4l2Decoder;
    bool mConfigured; // codec had been started before.
    uint32_t mGeneration;

    // FIXME, debug use only
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;
    uint32_t mOutputBufferCountRender;
    uint32_t mOutputDequeueRetryCount;
    DataDump mInputDataDump;
    DataDump *mOutputDataDump;
    V4l2TexureListenerSP mSurfaceTextureListener;

    bool mPortSettingChange = false;
    std::list<Message> mDeferMessages;
    int64_t mTargetTimeUs = -1ll;
    bool mPrefixCSD = false;
    int32_t mMemorySize = 0;
    MediaBuffer::MediaBufferType mMediaBufferType = MediaBuffer::MBT_BufferIndexV4L2;
    uint32_t mCodecFormat = 0;
    bool mDecodeThumbNail = false;

#if defined(__USING_YUNOS_MODULE_LOAD_FW__)
    struct __vendor_module_t* mModule = NULL;
#endif
#if defined(__MM_YUNOS_YUNHAL_BUILD__)
    yalloc_device_t* mYalloc = NULL;
#endif

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStartInput)
    DECLARE_MSG_HANDLER(onStartOutput)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onHandleInputBuffer)
    DECLARE_MSG_HANDLER(onHandleOutputBuffer)
    DECLARE_MSG_HANDLER(onHandleResolutionChange)
    DECLARE_MSG_HANDLER(onScheduleOutputBuffer)

    MM_DISALLOW_COPY(VideoDecodeV4l2)
};

}

#endif // video_decode_v4l2_h
