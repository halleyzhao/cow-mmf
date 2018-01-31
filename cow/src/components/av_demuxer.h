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

#ifndef __av_demuxer_H
#define __av_demuxer_H

extern "C" {
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <inttypes.h>
#include <libavformat/avformat.h>
}

#include <vector>

#include <multimedia/component.h>
#include <multimedia/mmmsgthread.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/elapsedtimer.h>
#include <multimedia/codec.h>
#include "multimedia/mm_audio.h"
#include <queue>

namespace YUNOS_MM {

//#define DUMP_OUTPUT
//#define DUMP_INPUT


class AVDemuxer : public PlaySourceComponent, public MMMsgThread {
public:
    AVDemuxer();
    virtual ~AVDemuxer();

public:
    virtual mm_status_t init();
    virtual void uninit();

    virtual const std::list<std::string> & supportedProtocols() const;
    virtual bool isSeekable();
    virtual mm_status_t getDuration(int64_t & durationMs);
    virtual bool hasMedia(MediaType mediaType);
    virtual MediaMetaSP getMetaData();
    virtual MMParamSP getTrackInfo();
    virtual mm_status_t selectTrack(MediaType mediaType, int index);
    virtual int getSelectedTrack(MediaType mediaType);

public:
    virtual const char * name() const { return "AVDemuxer"; }
    COMPONENT_VERSION;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length);
    static CowCodecID AVCodecId2CodecId(AVCodecID id);

private:
    enum state_e {
        STATE_NONE,
        STATE_ERROR,
        STATE_IDLE,
        STATE_PREPARING,
        STATE_PREPARED,
        STATE_STARTED,
    };

    class InterruptHandler : public AVIOInterruptCB {
    public:
        InterruptHandler(AVDemuxer* demuxer);
        ~InterruptHandler();

        void start(int64_t timeout);
        void end();
        void exit(bool isExit);
        bool isExiting() const;
        bool isTimeout() const;

        static int handleTimeout(void* obj);

    private:
        bool mIsTimeout;
        ElapsedTimer mTimer;
        bool mExit;
        bool mActive;

        MM_DISALLOW_COPY(InterruptHandler)
        DECLARE_LOGTAG()
    };

    typedef std::list<MediaBufferSP> BufferList;

    struct StreamInfo {
        StreamInfo();
        ~StreamInfo();

        bool init();
        void reset();

        int64_t bufferedTime() const;
        int64_t bufferedFirstTs(bool isPendingList = false) const;
        int64_t bufferedLastTs(bool isPendingList = false) const;
        int64_t bufferedFirstTs_l(bool isPendingList = false) const;
        int64_t bufferedLastTs_l(bool isPendingList = false) const;
        bool removeUntilTs(int64_t ts, int64_t * realTs);
        bool shortCircuitSelectTrack(int trackIndex);

        mm_status_t read(MediaBufferSP & buffer);
        /* in order to seamlessly switch track, we'd buffer two track data into mBufferList[2]
         * and switch to new list for the future key frame
         */
        bool write(MediaBufferSP buffer, int32_t trackIndex);

        mutable Lock mLock;
        MediaType mMediaType;
        int mSelectedStream;
        int mSelectedStreamPending;
        std::vector<int> mAllStreams;
        BufferList mBufferList[2];
        uint32_t mCurrentIndex; // index for current mBufferList
        bool mBeginWritePendingList;
        bool mHasAttachedPic;
        bool mNotCheckBuffering;
        AVRational mTimeBase;
        bool mPeerInstalled;

        int mPacketCount;
        int64_t mLastDts;
        int64_t mLastPts;

        MediaMetaSP mMetaData;
        CowCodecID mCodecId;

        int64_t mTargetTimeUs;
        int64_t mStartTimeUs;
    };

    struct SeekSequence {
        uint32_t index;
        bool internal;
    };

    class AVDemuxReader : public Reader {
    public:
        AVDemuxReader(AVDemuxer * from, StreamInfo * si);
        ~AVDemuxReader();

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        AVDemuxer * mComponent;
        StreamInfo * mStreamInfo;
#ifdef DUMP_OUTPUT
        FILE * mOutputDumpFpAudio;
        FILE * mOutputDumpFpVideo;
#endif
        DECLARE_LOGTAG()
    };

    class ReadThread : public MMThread {
    public:
        ReadThread(AVDemuxer * demuxer);
        ~ReadThread();

    public:
        mm_status_t prepare();
        mm_status_t reset();
        mm_status_t cork(int ck);
        mm_status_t read();

    protected:
        virtual void main();

    private:
        AVDemuxer * mDemuxer;
        sem_t mSem;
        bool mContinue;
        int mCorded;

        DECLARE_LOGTAG()
    };

    enum BufferState {
        kBufferStateNone,
        kBufferStateBuffering,
        kBufferStateNormal,
        kBufferStateFull,
        kBufferStateEOS
    };


protected:
    bool hasMediaInternal(MediaType mediaType);
    mm_status_t selectTrackInternal(MediaType mediaType, int index);
    mm_status_t readFrame();

    bool checkPacketWritable(StreamInfo * si, AVPacket * packet);

    mm_status_t read(MediaBufferSP & buffer, StreamInfo * si);
    void checkSeek();
    bool checkBufferSeek(int64_t seekUs);
    bool setTargetTimeUs(int64_t seekTimeUs);
    mm_status_t flushInternal();
    int64_t startTimeUs();
    int64_t durationUs();
    bool isSeekableInternal();
    void stopInternal();
    void resetInternal();
    mm_status_t duration(int64_t &durationMs);
    bool checkBuffer(int64_t & min);
    mm_status_t extractH264CSD(const uint8_t* data, size_t size, MediaBufferSP & sps, MediaBufferSP & pps);
    mm_status_t extractAACCSD(int32_t profile, int32_t sr, int32_t channel_configuration, MediaBufferSP & pps);
    void checkHighWater(int64_t readCosts, int64_t dur);
    mm_status_t createContext();
    void releaseContext();

    int avRead(uint8_t *buf, int buf_size);
    static int avRead(void *opaque, uint8_t *buf, int buf_size);
    static int64_t avSeek(void *opaque, int64_t offset, int whence);
    int64_t avSeek(int64_t offset, int whence);

    static const std::list<std::string> & getSupportedProtocols();

    static snd_format_t convertAudioFormat(AVSampleFormat avFormat);

    int getConfigBufferingTime();
    char * getLocalUri(const char* uri);

private:
    Lock mLock;
    Lock mBufferLock;
    state_e mState;
    std::string mUri;
    AVFormatContext * mAVFormatContext;
    AVInputFormat * mAVInputFormat;
    AVIOContext * mAVIOContext;

    int mPrepareTimeout;
    int mSeekTimeout;
    int mReadTimeout;
    int64_t mBufferingTime;
    int64_t mBufferingTimeHigh;
    int32_t mScaledPlayRate;
    int32_t mScaledThresholdRate;
    int32_t mScaledPlayRateCur;

    InterruptHandler * mInterruptHandler;
    bool mEOF;

    StreamInfo mStreamInfoArray[kMediaTypeCount];
    std::map<int, StreamInfo*> mStreamIdx2Info;
    int mReportedBufferingPercent;
    BufferState mBufferState;
    MediaMetaSP mMetaData;

    int64_t mSeekUs;
    bool mCheckVideoKeyFrame;//true means demuxer disards all the audio/video buffer before we get first video key frame
    std::queue<SeekSequence> mSeekSequence;

    ReadThread * mReadThread;
    int mFd;
    int64_t mLength;
    int64_t mOffset;
    int64_t mBufferSeekExtra;
    std::string mDownloadPath;

    Lock mAVLock; // send to downlink components for mutex operation between audio and video stream processing

#ifdef DUMP_INPUT
    FILE * mInputFile;
#endif


    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)

    MM_DISALLOW_COPY(AVDemuxer)
    DECLARE_LOGTAG()
};

}

#endif // __av_demuxer_H

