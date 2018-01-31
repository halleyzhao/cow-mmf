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

#ifndef __RTP_DEMUXER_H__
#define __RTP_DEMUXER_H__

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_debug.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/av_buffer_helper.h"
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

#include "multimedia/mm_audio.h"

#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>

#ifdef __cplusplus
}
#endif

#include <queue>

namespace YUNOS_MM
{

#define RTP_DEMUXER_DUMP_DATA 0

class RtpDemuxer : public PlaySourceComponent, public MMMsgThread
{
public:
    RtpDemuxer();
    virtual ~RtpDemuxer();

public:
    virtual mm_status_t init();
    virtual void uninit();
    virtual const char * name() const { return "RtpDemuxer"; }
    COMPONENT_VERSION;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t resume() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t reset();
    virtual mm_status_t flush() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setUri(const char * uri,
                                const std::map<std::string,
                                std::string> * headers = NULL);

    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) { return MM_ERROR_UNSUPPORTED; }

    virtual const std::list<std::string> & supportedProtocols() const;
    virtual bool isSeekable() { return false; }
    virtual mm_status_t getDuration(int64_t & durationMs) { return MM_ERROR_IVALID_OPERATION;}
    virtual bool hasMedia(MediaType mediaType) { return true;}
    virtual MediaMetaSP getMetaData() { return MediaMetaSP((MediaMeta*)NULL); }
    virtual MMParamSP getTrackInfo();
    virtual mm_status_t selectTrack(MediaType mediaType, int index) { return MM_ERROR_SUCCESS; }
    virtual int getSelectedTrack(MediaType mediaType) { return 0;}

private:
    static int exitFunc(void *handle);
    mm_status_t openRtpStream();
    int readRtpStream(AVPacket** pkt);
    void checkRequestIDR(AVPacket *packet, int& readNDIRFrameCnt);
    void closeRtpStream();
    mm_status_t getStreamParameter(MediaMetaSP & meta, MediaType type);
    static snd_format_t convertAudioFormat(AVSampleFormat avFormat);
    mm_status_t internalStop();

    class RtpDemuxerBuffer
    {
     public:
        RtpDemuxerBuffer(RtpDemuxer *source, MediaType type);
        ~RtpDemuxerBuffer();

     public:
        static bool releaseOutputBuffer(MediaBuffer* mediaBuffer);
        MediaBufferSP readBuffer();
        mm_status_t writeBuffer(MediaBufferSP buffer);
        int size();
        MonitorSP mMonitorWrite;

     private:
        MediaType mType;
        std::queue<MediaBufferSP> mBuffer;
        RtpDemuxer *mSource;
        DECLARE_LOGTAG()
    };

    class RtpDemuxerReader : public Reader
    {
     public:
        RtpDemuxerReader(RtpDemuxer *source, MediaType type)
        {
            mSource = source;
            mType = type;
        }
        ~RtpDemuxerReader() { }

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        RtpDemuxer *mSource;
        MediaType mType;
        DECLARE_LOGTAG()
    };

    class ReaderThread : public MMThread
    {
    public:
        ReaderThread(RtpDemuxer *source);
        ~ReaderThread();
        void signalExit();
        void signalContinue();

    protected:
        virtual void main();

    private:
        RtpDemuxer *mSource;
        bool mContinue;
        int mVideoPktNum;
        int mCorruptedVideoPktNum;
        bool mSkipCorruptedPtk;
        DECLARE_LOGTAG()
    };

private:
    typedef std::list<MediaBufferSP> BufferList;
    typedef MMSharedPtr<RtpDemuxerBuffer> RtpDemuxerBufferSP;
    typedef MMSharedPtr<ReaderThread> ReaderThreadSP;

    Lock mLock;
    Condition mCondition;
    bool mIsPaused;
    bool mExitFlag;
    ReaderThreadSP mReaderThread;
    RtpDemuxerBufferSP mVideoBuffer;
    RtpDemuxerBufferSP mAudioBuffer;

    bool mHasVideoTrack;
    bool mHasAudioTrack;
    int  mVideoIndex;
    int  mAudioIndex;
    AVRational  mVTimeBase;
    AVRational  mATimeBase;

    std::string mRtpURL;

    AVFormatContext *mFormatCtx;
    int mWidth;
    int mHeight;
    int64_t mVideoPts;
    int64_t mAudioPts;

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onReset)
    MM_DISALLOW_COPY(RtpDemuxer);

private:
    DECLARE_LOGTAG()
};

typedef MMSharedPtr<RtpDemuxer> RtpDemuxerSP;

}

#endif // __av_demuxer_H

