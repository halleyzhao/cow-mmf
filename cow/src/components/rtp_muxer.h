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

#ifndef __RTP_MUXER_H__
#define __RTP_MUXER_H__

#include <queue>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mm_debug.h>

#include <multimedia/component.h>
#include <multimedia/mmmsgthread.h>
#include "multimedia/media_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <libavformat/avformat.h>

#ifdef __MM_NATIVE_BUILD__
#include <libavutil/time.h>
#else
#include <libavutil/avtime.h>
#endif

#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>

#ifdef __cplusplus
}
#endif

#define DUMP_H264_DATA      0
#define DUMP_AAC_DATA       0
#define TRANSMIT_LOCAL_AV   0

namespace YUNOS_MM
{

class MediaBuffer;
class MediaMeta;
class RtpMuxerSink : public SinkComponent
{

public:
    RtpMuxerSink(const char *mimeType = NULL, bool isEncoder = false);
    virtual const char * name() const { return "RtpMuxerSink"; };
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType);

    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return  MM_ERROR_IVALID_OPERATION;}

    virtual mm_status_t prepare();
    mm_status_t internalPrepare();

    virtual mm_status_t start();
    virtual mm_status_t pause();
    virtual mm_status_t stop();
    mm_status_t internalStop();
    virtual mm_status_t reset();

    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS;}
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual int64_t getCurrentPosition() {return -1ll;}

protected:
    virtual ~RtpMuxerSink();

private:
    typedef enum
    {
        VideoType = 0,
        AudioType
    }TypeEnum;
    typedef struct StreamInfoTag {
        TypeEnum type;
        MediaMetaSP meta;
        uint8_t  *data;
        int       size;
    }StreamInfo;

    mm_status_t setStreamParameters(AVStream *stream, StreamInfo info);
    mm_status_t setExtraData(const MediaBufferSP & buffer,
                                 uint8_t **ppAVCHeader, int *avcHeaderSize,
                                 uint8_t **ppVExtraData, int *VExtraDataSize);
    mm_status_t openWebFile(int localPort,
                                 uint8_t *videoData, int vDataSize,
                                 uint8_t *audioData, int aDataSize);
    mm_status_t writeWebFile(MediaBufferSP buffer, TypeEnum type,
                                 uint8_t *avcHeader, int avcSize);
    mm_status_t closeWebFile();

    class RtpMuxerSinkBuffer
    {

    public:
        RtpMuxerSinkBuffer(RtpMuxerSink *sink, TypeEnum type);
        ~RtpMuxerSinkBuffer();

    public:
        static bool releaseOutputBuffer(MediaBuffer* mediaBuffer);
#if TRANSMIT_LOCAL_AV
        MediaBufferSP readOneFrameFromLocalFile();
#endif
        MediaBufferSP getBuffer();
        mm_status_t popBuffer();
        mm_status_t writeBuffer(MediaBufferSP buffer);
        int size();
    public:
        MonitorSP mMonitorWrite;
    private:
        TypeEnum mType;
        std::queue<MediaBufferSP> mBuffer;
        RtpMuxerSink *mSink;
        DECLARE_LOGTAG()
    };

    class RtpMuxerSinkWriter : public Writer
    {

     public:
        RtpMuxerSinkWriter(RtpMuxerSink *sink, TypeEnum type)
        {
            mSink = sink;
            mType = type;
        }
        ~RtpMuxerSinkWriter() { }

        virtual mm_status_t write(const MediaBufferSP &buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

    private:
        RtpMuxerSink *mSink;
        TypeEnum mType;
        DECLARE_LOGTAG()
    };

    class MuxThread : public MMThread
    {
    public:
        MuxThread(RtpMuxerSink *sink);
        ~MuxThread();
        void signalExit();
        void signalContinue();

    protected:
        virtual void main();

    private:
        RtpMuxerSink *mSink;
        bool mContinue;
        DECLARE_LOGTAG()
    };

private:
    typedef MMSharedPtr<RtpMuxerSinkBuffer> RtpMuxerSinkBufferSP;
    typedef MMSharedPtr<MuxThread> MuxThreadSP;

    MuxThreadSP mMuxThread;
    Lock mLock;
    Condition mCondition;
    bool mIsPaused;

    bool mHasVideoTrack;
    bool mHasAudioTrack;
    RtpMuxerSinkBufferSP mVideoBuffer;
    RtpMuxerSinkBufferSP mAudioBuffer;
    MediaMetaSP mVideoMetaData;
    MediaMetaSP mAudioMetaData;
    AVRational  mVTimeBase;
    AVRational  mATimeBase;

    std::string mRemoteURL;
    int mLocalPort;

    AVFormatContext *mFormatCtx;
    int64_t mWroteDts;
#if TRANSMIT_LOCAL_AV
    bool mSavedStartTime;
    MediaBufferSP mVideoTmpBuffer;
    MediaBufferSP mAudioTmpBuffer;
    int64_t mStartTime;
    FILE *mLocalVideoFp;
    FILE *mLocalAudioFp;
#endif
#if DUMP_H264_DATA
    FILE *mH264Fp;
#endif
#if DUMP_AAC_DATA
    FILE *mAacFp;
#endif
private:
    MM_DISALLOW_COPY(RtpMuxerSink);

    DECLARE_LOGTAG()
};
typedef MMSharedPtr<RtpMuxerSink> RtpMuxerSinkSP;

} // end of namespace YUNOS_MM
#endif//__RTP_MUXER_H__

