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
#include <gtest/gtest.h>
#include "multimedia/component.h"
#include "multimedia/media_meta.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>


#include "multimedia/mm_debug.h"
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "mediacodec_decoder.h"
#endif
#include "audio_decode_ffmpeg.h"
//#ifndef __AUDIO_CRAS__
//#define __AUDIO_CRAS__
//#endif
#ifdef __AUDIO_CRAS__
#include "audio_sink_cras.h"
#elif defined(__AUDIO_PULSE__)
#include <pthread.h>
#include <stdio.h>
#include "audio_sink_pulse.h"
#endif
#include "multimedia/mm_audio_compat.h"

#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_audio.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifndef __MM_NATIVE_BUILD__
#include <libavutil/avtime.h>
#else
#include <libavutil/time.h>
#endif

#include <libavutil/opt.h>
#include <libavutil/common.h>
#ifdef __cplusplus
}
#endif
#if 1
#undef INFO
#undef WARNING
#undef ERROR
#define INFO(format, ...)  fprintf(stderr, "[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define WARNING(format, ...)  fprintf(stderr, "[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  fprintf(stderr, "[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

MM_LOG_DEFINE_MODULE_NAME("Cow-Audio-Test")

namespace YUNOS_MM {

typedef MMSharedPtr < AVCodecContext > ContextSP;

class AVCodecContextObject : public MediaMeta::MetaBase {
  public:
    virtual void* getRawPtr() { return sp.get();};
    ContextSP sp;
};
typedef MMSharedPtr < AVCodecContextObject > AVCodecContextSP;


#define AT_MSG_start (msg_type)1
static const char * MMMSGTHREAD_NAME = "StubAudioSource";


class StubAudioSource : public Component, public MMMsgThread {



public:
    StubAudioSource(const char *filename):
                                         MMMsgThread(MMMSGTHREAD_NAME),
                                         mInCodecCtx(NULL),
                                         mInCodec(NULL),
                                         mPacket(NULL),
                                         mAudioStream(-1),
                                         mIsPause(true)
    {
        mFileName = filename;
        mMetaData = MediaMeta::create();
        mInFmtCtx = avformat_alloc_context();

    }
    virtual ~StubAudioSource() {
        avformat_close_input(&mInFmtCtx);
        MMMsgThread::exit();
    }
    virtual mm_status_t prepare();
    virtual mm_status_t stop(){mIsPause = true; return 0;}
    virtual mm_status_t start() {
        return postMsg(AT_MSG_start, 0, NULL);
    }
#ifdef __AUDIO_CRAS__
    static snd_format_t convertAudioFormat(AVSampleFormat avFormat);
#elif defined(__AUDIO_PULSE__)
    static pa_sample_format convertAudioFormat(AVSampleFormat avFormat);
#endif

public:
    virtual ReaderSP getReader(MediaType mediaType) {
        ReaderSP rsp;

        if (mediaType != kMediaTypeAudio) {
            ERROR("mediaType(%d) is not supported", mediaType);
            return rsp;
        }

        Reader *reader = new StubAudioReader(this);
        rsp.reset(reader);

        return rsp;
    }

    struct StubAudioReader : public Reader {
        StubAudioReader(StubAudioSource *source) {mSource = source;}

        virtual ~StubAudioReader() {}

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();
    private:
        StubAudioSource *mSource;

    };

public:
    virtual const char * name() const {return "stub audio source";}
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onStart)
    std::list<MediaBufferSP> mAvailableSourceBuffers;
    const char *mFileName;
    MediaMetaSP mMetaData;
    AVFormatContext *mInFmtCtx;
    AVCodecContext *mInCodecCtx;
    AVCodec *mInCodec;
    AVPacket *mPacket;
    int mAudioStream;
    bool mIsPause;
    Lock mLock;

};

BEGIN_MSG_LOOP(StubAudioSource)
    MSG_ITEM(AT_MSG_start, onStart)
END_MSG_LOOP()

#ifdef __AUDIO_CRAS__
/*static */snd_format_t StubAudioSource::convertAudioFormat(AVSampleFormat avFormat)
{
#define item(_av, _audio) \
    case _av:\
        MMLOGI("%s -> %s\n", #_av, #_audio);\
        return _audio

    switch ( avFormat ) {
        item(AV_SAMPLE_FMT_U8, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLT, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBL, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_U8P, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16P, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32P, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLTP, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBLP, SND_FORMAT_PCM_32_BIT);
        default:
            MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", avFormat);
            return SND_FORMAT_INVALID;
    }
}
#elif defined(__AUDIO_PULSE__)
/*static */pa_sample_format StubAudioSource::convertAudioFormat(AVSampleFormat avFormat)
{
#define item(_av, _audio) \
    case _av:\
        MMLOGI("%s -> %s\n", #_av, #_audio);\
        return _audio

    switch ( avFormat ) {
        item(AV_SAMPLE_FMT_U8, PA_SAMPLE_U8);
        item(AV_SAMPLE_FMT_S16, PA_SAMPLE_S16LE);
        item(AV_SAMPLE_FMT_S32, PA_SAMPLE_S32LE);
        item(AV_SAMPLE_FMT_FLT, PA_SAMPLE_FLOAT32LE);
        item(AV_SAMPLE_FMT_DBL, PA_SAMPLE_FLOAT32LE);
        item(AV_SAMPLE_FMT_U8P, PA_SAMPLE_U8);
        item(AV_SAMPLE_FMT_S16P, PA_SAMPLE_S16LE);
        item(AV_SAMPLE_FMT_S32P, PA_SAMPLE_S32LE);
        item(AV_SAMPLE_FMT_FLTP, PA_SAMPLE_FLOAT32LE);
        item(AV_SAMPLE_FMT_DBLP, PA_SAMPLE_FLOAT32LE);
        default:
            MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", avFormat);
            return PA_SAMPLE_INVALID;
    }
}
#endif

mm_status_t StubAudioSource::prepare()
{
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        return -1;

    av_register_all();

    // open input
    if (avformat_open_input(&mInFmtCtx, mFileName, NULL, NULL) < 0) {
        MMLOGE("fail to open input url: %s by avformat\n", mFileName);
        return -1;
    }
    if (!mInFmtCtx) {
        MMLOGE("fail to open input format context\n");
        return -1;
    }

    if (avformat_find_stream_info(mInFmtCtx,NULL) < 0) {
        MMLOGE("av_find_stream_info error\n");
        if (mInFmtCtx) {
            avformat_close_input(&mInFmtCtx);
        }
        return -1;
    }
    av_dump_format(mInFmtCtx,0,mFileName,0);

    unsigned int j;
    // Find the first audio stream

    for( j = 0; j < mInFmtCtx->nb_streams; j++) {
        if (mInFmtCtx->streams[j]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            mAudioStream = j;
            break;
        }
    }

    MMLOGV("audioStream = %d\n",mAudioStream);

    if (mAudioStream == -1) {
        MMLOGW("input file has no audio stream\n");
        return 0; // Didn't find a audio stream
    }
    MMLOGV("audio stream num: %d\n",mAudioStream);
    mInCodecCtx = mInFmtCtx->streams[mAudioStream]->codec;

    mMetaData->setPointer(MEDIA_ATTR_CODEC_CONTEXT, mInCodecCtx);

    mMetaData->setInt32(MEDIA_ATTR_CODECID, mInCodecCtx->codec_id);
    mMetaData->setInt32(MEDIA_ATTR_CODECTAG, mInCodecCtx->codec_tag);
    mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, mInCodecCtx->profile);
    mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, convertAudioFormat(mInCodecCtx->sample_fmt));
    mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, mInCodecCtx->sample_rate);
    mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, mInCodecCtx->channels);
    mMetaData->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, mInCodecCtx->channel_layout);
    mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, mInCodecCtx->bit_rate);

    mIsPause = false;

    mInCodec = avcodec_find_decoder(mInCodecCtx->codec_id);
    if (mInCodec == NULL) {
        MMLOGE("error no Codec found\n");
        return -1; // Codec not found
    }

    if (avcodec_open2(mInCodecCtx, mInCodec, NULL) < 0) {
        MMLOGE("error avcodec_open failed.\n");
        return -1; // Could not open codec
    }

    return 0;
}

void StubAudioSource::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    MMAutoLock lock(mLock);

    if (mIsPause || mAvailableSourceBuffers.size() >= 10)
        goto start_end;

    mPacket = (AVPacket *)malloc(sizeof(AVPacket));
    av_init_packet(mPacket);


    //SHOULD limit the buffer list size. TODO
    if ( av_read_frame(mInFmtCtx, mPacket) >= 0 && mPacket->stream_index == mAudioStream) {
       MediaBufferSP buf = AVBufferHelper::createMediaBuffer(mPacket, true);
       mAvailableSourceBuffers.push_back(buf);
       DEBUG("mAvailableSourceBuffers.size() %d", mAvailableSourceBuffers.size());
       usleep(1000ll);
    }


    //long end = clock();
    //MMLOGI("cost time :%f\n",double(end-start)/(double)CLOCKS_PER_SEC);
start_end:
    start();

    return;

}

bool releaseMediaBuffer(MediaBuffer *mediaBuf) {

    uint8_t *pIndex = NULL;
    if (!(mediaBuf->getBufferInfo((uintptr_t *)&pIndex, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }

    delete pIndex;

    return true;
}

mm_status_t StubAudioSource::StubAudioReader::read(MediaBufferSP & buffer) {
    MMAutoLock lock(mSource->mLock);
    VERBOSE("StubAudioReader, read, SIZE %d", mSource->mAvailableSourceBuffers.size());

    if ( !mSource->mAvailableSourceBuffers.empty() ) {
        buffer = mSource->mAvailableSourceBuffers.front();
        mSource->mAvailableSourceBuffers.pop_front();

        return MM_ERROR_SUCCESS;
    }

    return MM_ERROR_NO_MORE;//remeber when return NULL buffer , status is MM_ERROR_NO_MORE
}

MediaMetaSP StubAudioSource::StubAudioReader::getMetaData() {
    return mSource->mMetaData;
}

} // YUNOS_MM


using namespace YUNOS_MM;

static const char *input_url = NULL;
static   Component *source = NULL;
static   Component *decoder = NULL;
static   Component *sink = NULL;


class CowAudioTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

static void release(){

    if(decoder){
        delete decoder;
    }

    if(sink){
        delete sink;
    }

   if(source) {
       delete source;
   }
}

TEST_F(CowAudioTest, audioTest) {
    bool detectError = false;

    source = new StubAudioSource(input_url);
    #ifndef __MM_NATIVE_BUILD__
    decoder = new AudioDecodeFFmpeg(NULL, false);
    //decoder = new MediaCodecDec("audio/amr", false);//amrnb
    //decoder = new MediaCodecDec("audio/mpeg");//mp3
    #else
    decoder = new AudioDecodeFFmpeg(NULL, false);
    #endif
#ifdef __AUDIO_CRAS__
    sink = new AudioSinkCras(NULL, false);
#elif defined(__AUDIO_PULSE__)
    sink = new AudioSinkPulse(NULL, false);
#endif
    mm_status_t status;

    status = source->prepare();
    if (status != MM_ERROR_SUCCESS) {
        ERROR("source prepare source fail %d \n", status);
        release();
    }
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);

    status = decoder->addSource(source, Component::kMediaTypeAudio);
    #if 0
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder add source fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder add source ASYNC\n");
        }
    }
    #endif
    ASSERT_TRUE(!detectError);

    //sink->prepare();
    status = decoder->addSink(sink, Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder add sink fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder add sink ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    status = decoder->init();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder init fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder add sink ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    status = decoder->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder prepare fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder prepare ASYNC, wait 10ms for prepare completion\n");
            usleep(10*1000);
        }
    }
    ASSERT_TRUE(!detectError);

    status = sink->init();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("sink init fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder add sink ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    status = sink->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("sink prepare fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("sink prepare ASYNC, wait 10ms for prepare completion\n");
            usleep(10*1000);
        }
    }
    ASSERT_TRUE(!detectError);

    status = source->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("source start fail %d \n", status);
            detectError = true;
           release();
        }
        else{
            INFO("source start ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    status = decoder->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder start fail %d \n", status);
            detectError = true;
            release();
        }
        else{
            INFO("decoder start ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    status = sink->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("sink start fail %d \n", status);
            detectError = true;
            release();
        }
        else{
             INFO("sink start ASYNC\n");
        }
    }
    ASSERT_TRUE(!detectError);

    INFO("playing...\n");
    usleep(10*1000*1000);
    INFO("stopping...\n");
    source->stop();

    decoder->stop();

    sink->stop();

    decoder->reset();
    usleep(100*1000);

    sink->reset();
    usleep(100*1000);

    decoder->uninit();

    sink->uninit();
    release();
    INFO("done");
}


int main(int argc, char** argv) {
    int ret ;
    if (argc < 2) {
        printf("no input media file\n");
        return -1;
    }

    //input url
    input_url = argv[1];
    printf("input_url = %s\n",input_url);

    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        printf("InitGoogleTest failed!");
        ret =  -1;
    }
    return ret;
}

