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
#ifndef __audio_encode_ffmpeg_h
#define __audio_encode_ffmpeg_h

#include <map>

#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "multimedia/mm_audio.h"

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/mm_cpp_utils.h"


#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#ifdef __MM_NATIVE_BUILD__
#include <libavutil/time.h>
#else
#include <libavutil/avtime.h>
#endif
#include <libavutil/audio_fifo.h>
#include <libavutil/common.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

// #define DUMP_RAW_AUDIO_DATA
#ifdef DUMP_RAW_AUDIO_DATA
class DataDump;
#endif

namespace YUNOS_MM {

class AudioEncodeFFmpeg : public FilterComponent, public MMMsgThread {

public:

    AudioEncodeFFmpeg(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioEncodeFFmpeg();

    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t init();
    virtual void uninit();

    const char * name() const;

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }


protected:
    class AudioFrameAllocator {
        public:
            AudioFrameAllocator(AVCodecContext *c);

            AudioFrameAllocator(enum AVSampleFormat sampleFmt,
                                      uint64_t channelLayout,
                                      int sampleRate, int nbSamples);
            ~AudioFrameAllocator();

            AVFrame *mAVFrame;
    };
    typedef MMSharedPtr <AudioFrameAllocator> AudioFrameAllocatorSP;


    // a wrapper around a single output AVStream
    struct StreamInfo {
        AVStream *st;

        /* pts of the next frame that will be generated */
        int64_t mNextPts;
        int32_t mSampleCount;


        int32_t mSampleRate;
        int32_t mChannelCount;
        int32_t mSampleFormat;//bit format, 16, 24, 32 etc
        int32_t mBitrate;


        AVFrame *mAVFrame;//codec's sample_fmt
        AVFrame *mTmpAVFrame;//int16_t sample_fmt

        int32_t mCodecID;
        AVCodec *mAVCodec;
        AVCodecContext *mAVCodecContext;
        AVPacket mAVPacket;
        struct SwrContext *mAVResample;

        AVAudioFifo *mFifo;

        AudioFrameAllocatorSP mFrameAllocator;

    };
    typedef MMSharedPtr <StreamInfo> StreamInfoSP;



    class CodecThread : public MMThread {
    public:
        CodecThread(AudioEncodeFFmpeg *codec, StreamInfo *streamInfo);
        ~CodecThread();

        mm_status_t start();
        void stop();
        void signal();

    protected:
        virtual void main();

    private:

    private:
        AudioEncodeFFmpeg *mCodec;
        StreamInfo *mStreamInfo;
        bool mContinue;
        sem_t mSem;
    };
    typedef MMSharedPtr <CodecThread> CodecThreadSP;




protected:

    static AVSampleFormat convertAudioFormat(snd_format_t formt);

    static AVFrame *allocAudioFrame(AVCodecContext *c);

    static AVFrame *allocAudioFrame(enum AVSampleFormat sampleFmt,
                                      uint64_t channelLayout,
                                      int sampleRate, int nb_samples);

    static bool checkSampleFormatSupport(AVCodec *codec, enum AVSampleFormat sample_fmt);


    static int selectSampleRate(AVCodec *codec, int sampleRate);

    static int selectChannelLayout(AVCodec *codec);


    static int initFIFO(AVAudioFifo **fifo, AVCodecContext *outputCodecContext);

    static int addSamplesToFifo(AVAudioFifo *fifo, uint8_t **convertetInputSamples, const int frameSize);

    static int32_t formatSize(AVSampleFormat format);

    void writeEOSBuffer();

    Component * mSource;
    Component * mSink;

    Condition mCondition;
    Lock mLock;

    std::string mComponentName;
    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;

    ReaderSP mReader;
    WriterSP mWriter;
    bool mIsPaused;
    CodecThreadSP mCodecThread;
    AVRational mSourceTimebase;

    bool mIsEncoder;
    int64_t mPts;


#ifdef DUMP_RAW_AUDIO_DATA
    DataDump *mOutputDataDump;
#endif

    StreamInfoSP mStreamInfo;

private:

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(AudioEncodeFFmpeg);

};

typedef MMSharedPtr <AudioEncodeFFmpeg> AudioEncodeFFmpegSP;


}

#endif //__audio_encode_ffmpeg_h
