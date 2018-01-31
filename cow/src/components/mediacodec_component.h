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

#ifndef __mediacodec_component_h
#define __mediacodec_component_h

#include <multimedia/component.h>
#include <multimedia/mmmsgthread.h>
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
//#include <WindowSurface.h>
#endif
#include "media_codec.h"
#include <multimedia/media_buffer.h>

namespace YunOSMediaCodec {
class MediaSurfaceTexture;
}

namespace YUNOS_MM {

class Notify;

using namespace YunOSMediaCodec;

#define MC_DEBUG(x, ...) DEBUG("[%s] " x, mInitName.c_str(), ##__VA_ARGS__)
#define MC_INFO(x, ...) INFO("[%s] " x, mInitName.c_str(), ##__VA_ARGS__)
#define MC_ERROR(x, ...) ERROR("[%s] " x, mInitName.c_str(), ##__VA_ARGS__)
#define MC_WARNING(x, ...) WARNING("[%s] " x, mInitName.c_str(), ##__VA_ARGS__)
#define MC_VERBOSE(x, ...) VERBOSE("[%s] " x, mInitName.c_str(), ##__VA_ARGS__)


typedef MMSharedPtr <MediaCodec> MediaCodecSP;
class MediaCodecComponent : public FilterComponent, public MMMsgThread {

public:
    MediaCodecComponent(const char *mime);
    virtual ~MediaCodecComponent();

    const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);

    virtual mm_status_t init();
    virtual void uninit();
    virtual mm_status_t start();
    virtual mm_status_t resume();
    virtual mm_status_t stop();
    virtual mm_status_t prepare();
    virtual mm_status_t pause();
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }

protected:
    virtual bool handleInputBuffer() = 0; /* handle codec input buffer */
    virtual bool handleOutputBuffer() = 0; /* handle codec output buffer */
    virtual bool readSourceMeta() = 0;
    virtual bool writeSinkMeta() = 0;

    void handleError(int32_t err = MM_ERROR_UNKNOWN);
    void scheduleCodecActivity();
    mm_status_t processMsg(uint32_t msg,
                           param1_type param1,
                           param2_type param2,
                           param3_type param3=MMRefBaseSP((MMRefBase*)NULL));
    void clearBuffer();
    void asyncFuncExit(Component::Event event, int param1, int param2, uint32_t rspId);

    void acquireSourceBuffer(); /* request buffer from source */
    bool submitBufferToCodec();

    mm_status_t setParameterForAudio(const MediaMetaSP & meta);
    static bool releaseMediaBuffer(MediaBuffer *mediaBuf); // chekc if it's used
    static void releaseByteBuffer(MediaMeta::ByteBuffer *byteBuf);

    virtual void scheduleAcquire() = 0;
    virtual mm_status_t readSourceBuffer(MediaBufferSP &mediaBuf) = 0;
    virtual bool setupMediaCodecParam(MediaMetaSP &params);
    virtual void checkSourceTimeDiscontinuity(int64_t dts, int64_t pts) {return;}

    mm_status_t stopInternal();
    mm_status_t flushInternal();
    mm_status_t ensureCodec();
    mm_status_t releaseCodec();

protected:

    struct BufferInfo {
        MediaBufferSP mediaBuf;
        MediaMeta::MetaBaseSP shadowBuf; // sometimes need to keep mediaBuffer until finished by Codec
    };

    enum State {
        UNINITIALIZED,
        INITIALIZED,
        PREPARED,
        STARTED,
        STOPED
    };

// MediaCodec Commponent Message
#define MCC_MSG_prepare (msg_type)1
#define MCC_MSG_pause (msg_type)2
#define MCC_MSG_flush (msg_type)3
#define MCC_MSG_setComponent (msg_type)4
#define MCC_MSG_codecActivity (msg_type)5
#define MCC_MSG_acquireSourceBuffer (msg_type)6
#define MCC_MSG_setParameter (msg_type)7
#define MCC_MSG_stop (msg_type)8
#define MCC_MSG_start (msg_type)9
#define MCC_MSG_resume (msg_type)10
#define MCC_MSG_reset (msg_type)11
#define MCC_MSG_seek (msg_type)12
#define MCE_MSG_bufferAvailable (msg_type)13
#define MCC_MSG_END (msg_type)14

#define MCC_SOURCE 0
#define MCC_SINK 1
#define MCC_SCHEDULE_TIME (10 * 1000ll)
#define MCC_MAX_DIMENSION (4096)
#define MCC_MAX_FPS (120)

    State mState;

    bool mPaused;
    bool mPendingAcquireBuffer;
    bool mFormatChangePending;
    bool mResumePending;
    bool mCodecActivityPending;
    bool mIsNal;
    int mNalLengthSize;
    bool mIsAVCcType;
    //True indicates input buffer received eos flag or error occurs.
    //Note: it's different from mPaused.
    bool mStreamEOS;
    bool mIsEncoder;
    bool mAsync;

    uint32_t mCodecInputSequence;
    uint32_t mCodecOutputSequence;
    int32_t mGeneration;

    int64_t mCodecInputTimeStamp;
    int64_t mCodecOutputTimeStamp;

    WindowSurface *mNativeWindow;
    YunOSMediaCodec::MediaSurfaceTexture *mSurfaceTexture;

    MediaCodecSP mCodec;

    ReaderSP mReader;
    WriterSP mWriter;

    std::string mComponentName;

    MediaMetaSP mInputFormat;
    MediaMetaSP mOutputFormat;

    std::vector<BufferInfo> mMediaBuffers; /* input media samples in Codec */
    std::list<size_t> mAvailableCodecBuffers; /* MediaCodec input buffer index */
    std::list<BufferInfo> mAvailableSourceBuffers;
    std::vector<uint8_t*> mInputBufferData;
    std::vector<size_t> mInputBufferSize;

    std::vector<uint8_t*> mOutputBufferData;
    std::vector<size_t> mOutputBufferSize;

    int32_t mInputBufferCount;
    int32_t mOutputBufferCount;

    YunOSMediaCodec::Notify *mNotify;

    bool mIsAudio;
    std::string mInitName;

    int32_t mReportMemory = 0;
    bool mPreviousStop;
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onSetPeerComponent)
    DECLARE_MSG_HANDLER(onCodecActivity)
    DECLARE_MSG_HANDLER(onAcquireSourceBuffer)
    DECLARE_MSG_HANDLER2(onSetParameter)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSourceBufferAvailable)

private:
    MM_DISALLOW_COPY(MediaCodecComponent);
};

class MCCMMParamBase : public MMRefBase {
  public:
    explicit MCCMMParamBase(const MediaMetaSP param)
        : mMeta(param) {}

    //virtual ~MCCMMParamBase() {MC_INFO();}
    const MediaMetaSP mMeta;
};

} // end of namespace YUNOS_MM
#endif //__mediacodec_component_h
