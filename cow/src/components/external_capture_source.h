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

#ifndef __external_capture_source_H
#define __external_capture_source_H

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

class ExternalCaptureSource : public RecordSourceComponent, public MMMsgThread {
public:
    ExternalCaptureSource();
    ~ExternalCaptureSource();

public:
    virtual mm_status_t init();
    virtual void uninit();

    virtual bool hasMedia(MediaType mediaType);
    virtual MediaMetaSP getMetaData();
    virtual MMParamSP getTrackInfo();
    virtual mm_status_t selectTrack(MediaType mediaType, int index);
    virtual int getSelectedTrack(MediaType mediaType);

public:
    virtual const char * name() const { return "ExternalCaptureSource"; }
    COMPONENT_VERSION;

    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t reset();
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t signalEOS();
    void doSignalEOS();

    static CowCodecID AVCodecId2CodecId(AVCodecID id);

private:
    enum state_e {
        STATE_NONE,
        STATE_ERROR,
        STATE_IDLE,
        STATE_PREPARING,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_PAUSED
    };

    class InterruptHandler : public AVIOInterruptCB {
    public:
        InterruptHandler();
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
        void addSink(Component * component);
        void reset();

        bool write(MediaBufferSP buffer);

        bool peerInstalled() const { return mWriter != NULL; }

        MediaType mMediaType;
        int mSelectedStream;
        std::vector<int> mAllStreams;
        bool mHasAttachedPic;
        AVRational mTimeBase;

        MediaMetaSP mMetaData;

        WriterSP mWriter;
    };


    class ReadThread : public MMThread {
    public:
        ReadThread(ExternalCaptureSource * watcher);
        ~ReadThread();

    public:
        mm_status_t prepare();
        mm_status_t reset();
        mm_status_t start();
        mm_status_t pause();
        mm_status_t resume();
        mm_status_t stop();
        mm_status_t read();
        mm_status_t signalEOS();

    protected:
        virtual void main();
        void doSignalEOS();

    private:
        ExternalCaptureSource * mWatcher;
        sem_t mSem;
        bool mContinue;
        bool mCorded;
        bool mSignalEOSReq;
        bool mSignalEOSDone;

        DECLARE_LOGTAG()
    };

protected:
    bool hasMediaInternal(MediaType mediaType);
    mm_status_t selectTrackInternal(MediaType mediaType, int index);
    mm_status_t readFrame();

    mm_status_t flushInternal();
    void stopInternal();
    void resetInternal();
    mm_status_t createContext();
    void releaseContext();

    static const std::list<std::string> & getSupportedProtocols();

    static snd_format_t convertAudioFormat(AVSampleFormat avFormat);

private:
    mutable Lock mLock;
    state_e mState;
    std::string mUri;
    AVFormatContext * mAVFormatContext;
    AVInputFormat * mAVInputFormat;
    AVIOContext * mAVIOContext;

    int mPrepareTimeout;
    int mReadTimeout;

    InterruptHandler * mInterruptHandler;

    StreamInfo mStreamInfoArray[kMediaTypeCount];
    std::map<int, StreamInfo*> mStreamIdx2Info;
    MediaMetaSP mMetaData;

    ReadThread * mReadThread;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)

    MM_DISALLOW_COPY(ExternalCaptureSource)
    DECLARE_LOGTAG()
};

}

#endif

