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

#ifndef __mediacodec_decoder_h
#define __mediacodec_decoder_h

#include "mediacodec_component.h"

namespace YUNOS_MM {

using namespace YunOSMediaCodec;
class StreamCSDMaker;
typedef MMSharedPtr < StreamCSDMaker > StreamMakeCSDSP;
class MediaSurfaceTexureListener;
typedef MMSharedPtr < MediaSurfaceTexureListener > MediaSurfaceTexureListenerSP;

class MediaCodecDec : public MediaCodecComponent {

public:
    MediaCodecDec(const char *mime);
    virtual ~MediaCodecDec();


    virtual mm_status_t seek(int msec, int seekSequence);

    virtual mm_status_t setParameter(const MediaMetaSP & meta);

    /* used by secure content decode
    bool getInputBuffers(std::vector<MediaBufferPtr> buffers);
    */

friend class MediaSurfaceTexureListener;

private:
    virtual bool handleInputBuffer(); /* handle codec input buffer */
    virtual bool handleOutputBuffer(); /* handle codec output buffer */
    virtual bool readSourceMeta();
    virtual bool writeSinkMeta();

    StreamMakeCSDSP createStreamCSDMaker(MediaMetaSP &meta);

    virtual void scheduleAcquire();
    virtual mm_status_t readSourceBuffer(MediaBufferSP &mediaBuf);
    virtual bool setupMediaCodecParam(MediaMetaSP &params);
    virtual void checkSourceTimeDiscontinuity(int64_t dts, int64_t pts);

    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

    void calculateMaxBuffering(uint32_t width, uint32_t height, int fps);

    void attachRawVideoMeta(MediaMetaSP meta);

private:
    int32_t mMaxCodecBuffering; /* input data buffered in codec in micro-second */
    std::list<size_t> mPreAvailableCodecBuffers; /* MediaCodec input buffer index list for buffering control */
    bool mTimeDiscontinuity;
    int64_t mDiscontinuityPts;
    StreamMakeCSDSP mStreamCSDMaker;
    int32_t mCSDBufferIndex;
    MediaSurfaceTexureListenerSP mSurfaceTextureListener;

    int64_t mTargetTimeUs;
    uint32_t mWidth, mHeight;
    bool mNeedBufferCtrl;

private:
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSeek)
    DECLARE_MSG_HANDLER2(onSetParameter)

    MM_DISALLOW_COPY(MediaCodecDec);
};



} // end of namespace YUNOS_MM
#endif //__mediacodec_decoder_h
