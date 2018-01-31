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
#ifndef __audio_codec_opus_h__
#define __audio_codec_opus_h__

#include <map>

#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/mm_cpp_utils.h"


#ifdef __cplusplus
extern "C" {
#endif
#include "opus.h"
#ifdef __cplusplus
}
#endif

// #define DUMP_RAW_AUDIO_DATA
#ifdef DUMP_RAW_AUDIO_DATA
class DataDump;
#endif

class OpusEncoder;

namespace YUNOS_MM {

class AudioCodecOpus : public FilterComponent, public MMMsgThread {

public:

    AudioCodecOpus(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioCodecOpus();

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
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }


protected:

    // a wrapper around a single output AVStream
    struct StreamInfo {
        /* pts of the next frame that will be generated */
        int64_t mNextPts;
        int32_t mSampleCount;


        int32_t mSampleRate;
        int32_t mChannels;
        int32_t mSampleFormat;//bit format, 16, 24, 32 etc
        int32_t mBitrate;


        int32_t mFrameSize;
        int32_t mMaxPayloadSize;

    };
    typedef MMSharedPtr <StreamInfo> StreamInfoSP;


    class CodecThread : public MMThread {
    public:
        CodecThread(AudioCodecOpus *codec, StreamInfo *streamInfo);
        ~CodecThread();

        mm_status_t start();
        void stop();
        void signal();
        MediaBufferSP createMediaBuffer(uint8_t *data, int32_t size);
        static bool releaseMediaBuffer(MediaBuffer* mediaBuffer);

    protected:
        virtual void main();

    private:

    private:
        AudioCodecOpus *mCodec;
        StreamInfo *mStreamInfo;
        bool mContinue;
        sem_t mSem;
    };
    typedef MMSharedPtr <CodecThread> CodecThreadSP;

protected:
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

    bool mIsEncoder;
    int64_t mPts;


#ifdef DUMP_RAW_AUDIO_DATA
    DataDump *mOutputDataDump;
#endif

    StreamInfoSP mStreamInfo;
    OpusEncoder* mEncoder;


private:

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(AudioCodecOpus);

};

typedef MMSharedPtr <AudioCodecOpus> AudioCodecOpusSP;


}

#endif //__audio_codec_opus_h__
