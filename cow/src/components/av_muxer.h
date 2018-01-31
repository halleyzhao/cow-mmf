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

#ifndef __av_muxer_H
#define __av_muxer_H

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

#include <semaphore.h>
#include <vector>

#include <multimedia/component.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/codec.h>
#include "multimedia/mm_audio.h"
#include <multimedia/mmmsgthread.h>

namespace YUNOS_MM {

class AVMuxer : public FilterComponent, public MMMsgThread {
public:
    AVMuxer();
    ~AVMuxer();

public:
    virtual mm_status_t init();
    virtual void uninit();

public:
    virtual const char * name() const { return "AVMuxer"; }
    COMPONENT_VERSION;
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType);

    //
    // { key:       "output-format"
    //    param:    can be one of the following:
    //                      "mp4" -- mpeg4 file format
    //                      "adts" -- adts aac file format
    // }
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t reset();
    bool hasMedia(MediaType mediaType);

private:
    enum state_e {
        STATE_NONE,
        STATE_ERROR,
        STATE_IDLE,
        STATE_STARTED,
        STATE_STOPPING,
    };

    class StreamInfo {
    public:
        StreamInfo(AVMuxer *muxer, MediaType mediaType);
        ~StreamInfo();
        AVSampleFormat convertAudioFormat(snd_format_t sampleFormat);

        bool addStream();
        mm_status_t pause();
        mm_status_t resume();
        mm_status_t stop_l();
        mm_status_t write_l(MediaBufferSP buffer);
        MediaBufferSP read_l();
        int64_t bufferedDurationUs_l() const;
        int64_t bufferFirstUs_l() const;

    private:
        StreamInfo();
        AVMuxer *mComponent;

    public:
        MediaType mMediaType;
        CowCodecID mCodecId;
        MediaMetaSP mCodecMeta;
        AVRational mTimeBase;
        bool mStreamEOS;
        int64_t mFormerDts; // basing on time_base
        int64_t mFormerDtsAbs; // absolute time
        int64_t mStartTimeUs;
        int64_t mTrackDurationUs;
        int64_t mFrameDurationUs;
        int64_t mPausedDurationUs;
        bool mPaused;
        bool mResumeFirstFrameDone;
        AVStream * mStream;
        int mSeq;
        bool mExtraDataDetermined;

        std::list<MediaBufferSP> mEncodedBuffers;

        MM_DISALLOW_COPY(StreamInfo)
    };

    class AVMuxWriter : public Writer {
    public:
        AVMuxWriter(AVMuxer * muxer, StreamInfo * si);
        ~AVMuxWriter();

    public:
        virtual mm_status_t write(const MediaBufferSP & buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

    private:
        AVMuxWriter();

    private:
        AVMuxer * mComponent;
        StreamInfo * mStreamInfo;
        TimeCostStaticsSP mTimeCostWriter;

        MM_DISALLOW_COPY(AVMuxWriter)
        DECLARE_LOGTAG()
    };

    class MuxThread : public MMThread {
    public:
        MuxThread(AVMuxer * muxer);
        ~MuxThread();

    public:
        mm_status_t prepare();
        mm_status_t reset();
        mm_status_t mux();

    protected:
        virtual void main();

    private:
        AVMuxer * mMuxer;
        sem_t mSem;
        bool mContinue;

        DECLARE_LOGTAG()
        MM_DISALLOW_COPY(MuxThread)
    };


protected:
    bool hasMediaInternal(MediaType mediaType);
    mm_status_t write(const MediaBufferSP & buffer, StreamInfo * si);
    void avWrite(uint8_t *buf, int buf_size);
    static int avWrite(void *opaque, uint8_t *buf, int buf_size);
    static int64_t avSeek(void *opaque, int64_t offset, int whence);
    int64_t avSeek(int64_t offset, int whence);
    mm_status_t createContext();
    void releaseContext();
    //bool addStream(StreamInfo * si);
    void stopInternal();
    void resetInternal();
    MediaBufferSP createSinkBuffer(const uint8_t * buf, size_t size);
    static bool releaseSinkBuffer(MediaBuffer* mediaBuffer);
    void sinkBufferReleased();
    bool writeHeader();
    bool writeTrailer();
    void signalEOS2Sink();
    mm_status_t mux();
    MediaBufferSP getMinFirstBuffer_l();

private:
    Lock mLock;
    Lock mBufferLock;
    state_e mState;
    std::string mOutputFormat;
    int32_t mRotation;
    float mLatitude;
    float mLongitude;
    int64_t mMaxDuration;
    int64_t mMaxFileSize;
    int64_t mCurFileSize;
    AVFormatContext * mAVFormatContext;
    AVIOContext * mAVIOContext;
    WriterSP mWriter;
    int64_t mOutputSeekOffset;
    int mOutputSeekWhence;
#ifdef HAVE_EIS_AUDIO_DELAY
    bool mIsAudioDelay;
#endif
    bool mSinkBufferReleased;
    bool mAllMediaExtraDataDetermined;
    bool mEOS;

    typedef std::vector<StreamInfo*> StreamInfoArray;
    StreamInfoArray mStreamInfoArray;

    MuxThread * mMuxThread;
    int64_t mCurrentDts;
    bool mCheckVideoKeyFrame;
    TimeCostStatics mTimeCostMux;
    bool mForceDisableMuxer;
    bool mConvertH264ByteStreamToAvcc;
    int32_t mStreamDriftMax;

    // MonitorSP mMonitorFPS;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(AVMuxer)
    DECLARE_LOGTAG()
};

}

#endif // __av_muxer_H

