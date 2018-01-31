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

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/av_buffer_helper.h"
#include "multimedia/media_monitor.h"
#include "multimedia/mm_debug.h"

#include "audio_encode_ffmpeg.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("AudioEncodeFFmpeg")
static const char * MMMSGTHREAD_NAME = "AudioEncodeFFmpeg";
static const char * MMTHREAD_NAME = "AudioEncodeFFmpeg::CodecThread";

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)



#define AFF_MSG_prepare (msg_type)1
#define AFF_MSG_start (msg_type)2
#define AFF_MSG_resume (msg_type)3
#define AFF_MSG_pause (msg_type)4
#define AFF_MSG_stop (msg_type)5
#define AFF_MSG_flush (msg_type)6
#define AFF_MSG_seek (msg_type)7
#define AFF_MSG_reset (msg_type)8
#define AFF_MSG_setParameters (msg_type)9
#define AFF_MSG_getParameters (msg_type)10

BEGIN_MSG_LOOP(AudioEncodeFFmpeg)
    MSG_ITEM(AFF_MSG_prepare, onPrepare)
    MSG_ITEM(AFF_MSG_start, onStart)
    MSG_ITEM(AFF_MSG_stop, onStop)
    MSG_ITEM(AFF_MSG_flush, onFlush)
    MSG_ITEM(AFF_MSG_reset, onReset)
END_MSG_LOOP()


#define GET_INT32_PARAM_KEY_VALUE(metaData, key, value, event) do{          \
    if (!metaData->getInt32(key, value)) {                                  \
        ASSERT(0);                                                          \
        ERROR("fail to get int32_t data %s\n", #metaData);                  \
        notify(event, MM_ERROR_OP_FAILED, 0, nilParam);                      \
        EXIT();                                                             \
    }                                                                       \
}while(0)


///////////////////////////////////////////////////////////////////////////////////////////////////
//AudioFrameAllocator
AudioEncodeFFmpeg::AudioFrameAllocator::AudioFrameAllocator(AVCodecContext *c) {
    mAVFrame = allocAudioFrame(c);
    if (!mAVFrame) {
        ERROR("allocAudioFrame failed\n");
    }
}


AudioEncodeFFmpeg::AudioFrameAllocator::AudioFrameAllocator(enum AVSampleFormat sampleFmt,
                                      uint64_t channelLayout,
                                      int sampleRate, int nbSamples) {
    mAVFrame = allocAudioFrame(sampleFmt, channelLayout, sampleRate, nbSamples);
    if (!mAVFrame) {
        ERROR("allocAudioFrame failed\n");
    }
}

AudioEncodeFFmpeg::AudioFrameAllocator::~AudioFrameAllocator() {
    if (mAVFrame) {
        av_frame_free(&mAVFrame);
    }

}

///////////////////////////////////////////////////////////////////////////////////////////////////
//CodecThread
AudioEncodeFFmpeg::CodecThread::CodecThread(AudioEncodeFFmpeg* codec, StreamInfo *streamInfo)
    : MMThread(MMTHREAD_NAME)
    , mCodec(codec)
    , mStreamInfo(streamInfo)
    , mContinue(true) {
    ENTER();
    sem_init(&mSem, 0, 0);
    EXIT();
}

AudioEncodeFFmpeg::CodecThread::~CodecThread() {
    ENTER();
    sem_destroy(&mSem);
    EXIT();
}


mm_status_t AudioEncodeFFmpeg::CodecThread::start() {
    ENTER();
    mContinue = true;
    if ( create() ) {
        ERROR("failed to create thread\n");
        return MM_ERROR_NO_MEM;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioEncodeFFmpeg::CodecThread::stop() {
    ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    EXIT();
}

void AudioEncodeFFmpeg::CodecThread::signal() {
    ENTER();
    sem_post(&mSem);
    EXIT();
}


void AudioEncodeFFmpeg::CodecThread::main() {
    ENTER();
    mm_status_t status;

    while (1) {
        MediaBufferSP srcBuffer;
        int retry = 0;
        if (!mContinue) {
            break;
        }
        if (mCodec->mIsPaused) {
            sem_wait(&mSem);
            continue;
        }
        do {
            status = mCodec->mReader->read(srcBuffer);
            if (!mContinue) {
                srcBuffer.reset();
                break;
            }
            if (status == MM_ERROR_AGAIN) {
                usleep(5*1000);
            } else  {
                break;
            }
        } while(retry++ < 200);

        if (!srcBuffer || srcBuffer->size() == 0) {// empty EOS frame
            INFO("source media buffer is invalid, signal eos");
            return mCodec->writeEOSBuffer();
        }

        int gotFrame = 0;
        AVFrame *frame = NULL;

        if (!AVBufferHelper::convertToAVFrame(srcBuffer, &frame)) {
            return mCodec->writeEOSBuffer();
        }

        frame->nb_samples = frame->linesize[0]/mStreamInfo->mChannelCount/2;
        frame->sample_rate = mStreamInfo->mSampleRate;
        frame->channel_layout = av_get_default_channel_layout(mStreamInfo->mChannelCount);

        DEBUG("frame pts %" PRId64 " ms, data %p, linesize %d, nb_samples %d, frame_size %d\n",
                frame->pts/1000LL, frame->data, frame->linesize[0], frame->nb_samples,
                mStreamInfo->mAVCodecContext->frame_size);

        // Allocate Frame here because AVCodecContext.frame_size is right
        if (!mStreamInfo->mFrameAllocator) {
            mStreamInfo->mFrameAllocator.reset(new AudioFrameAllocator(mStreamInfo->mAVCodecContext));
            if (!(mStreamInfo->mFrameAllocator)) {
                ERROR("no mem\n");
                EXIT();
            }
            DEBUG("mFrameAllocator is allocated");
        }

        // mFifo means whether using audio fifo
        AVFrame *resampleFrame = NULL;
        AudioFrameAllocatorSP frameAllocator;
        if ((mStreamInfo->mFifo &&
                    av_audio_fifo_size(mStreamInfo->mFifo) < mStreamInfo->mAVCodecContext->frame_size) ||
                !mStreamInfo->mFifo) {
            if (mStreamInfo->mAVResample) {
                frameAllocator.reset(new AudioFrameAllocator(mStreamInfo->mAVCodecContext->sample_fmt,
                            mStreamInfo->mAVCodecContext->channel_layout,
                            mStreamInfo->mAVCodecContext->sample_rate,
                            frame->nb_samples));
                if (!frameAllocator) {
                    ERROR("no mem\n");
                    EXIT();
                }
                resampleFrame = frameAllocator->mAVFrame;
                if (!resampleFrame) {
                    ERROR("failed to create AudioFrame\n");
                    EXIT();
                }


                int outSamples = 0;
                if ((outSamples = swr_convert(mStreamInfo->mAVResample,
                                resampleFrame->data, frame->nb_samples,
                                (const uint8_t**)frame->data, frame->nb_samples)) < 0) {
                    ERROR("Convert failed");
                    EXIT();
                }
                ASSERT(outSamples == frame->nb_samples);
                VERBOSE("convert nb_samples %d to float", frame->nb_samples);

            } else {
                resampleFrame = frame;
            }


            if (mStreamInfo->mFifo &&
                    addSamplesToFifo(mStreamInfo->mFifo, resampleFrame->data, resampleFrame->nb_samples)) {
                ERROR("addSamplesToFifo failed\n");
                EXIT();
            }
        }


        while ((mStreamInfo->mFifo &&
                    (av_audio_fifo_size(mStreamInfo->mFifo) >= mStreamInfo->mAVCodecContext->frame_size ||
                     (srcBuffer->isFlagSet(MediaBuffer::MBFT_EOS) && av_audio_fifo_size(mStreamInfo->mFifo) > 0))) ||
                !mStreamInfo->mFifo) {

            AVFrame *outputFrame;
            int nbSamples = 0;

            AudioFrameAllocatorSP frameAllocate;
            if (mStreamInfo->mFifo) {
                nbSamples = FFMIN(av_audio_fifo_size(mStreamInfo->mFifo),
                        mStreamInfo->mAVCodecContext->frame_size);

                if (nbSamples != mStreamInfo->mAVCodecContext->frame_size) {
                    /** Initialize temporary storage for one output frame. */
                    frameAllocate.reset(new AudioFrameAllocator(mStreamInfo->mAVCodecContext->sample_fmt,
                                mStreamInfo->mAVCodecContext->channel_layout,
                                mStreamInfo->mAVCodecContext->sample_rate,
                                nbSamples));
                    if (!frameAllocate) {
                        ERROR("no mem\n");
                        EXIT();
                    }
                } else {
                    frameAllocate = mStreamInfo->mFrameAllocator;
                }


                outputFrame = frameAllocate->mAVFrame;
                if (!outputFrame) {
                    ERROR("failed to create AudioFrame\n");
                    EXIT();
                }


                //Read as many samples from the FIFO buffer as required to fill the frame.
                if (av_audio_fifo_read(mStreamInfo->mFifo, (void **)outputFrame->data, nbSamples) < nbSamples) {
                    ERROR("Could not read data from FIFO\n");
                    EXIT();
                }
            } else {
                // nbSamples = resampleFrame->nb_samples;
                // resampleFrame is resampled
                outputFrame = resampleFrame;
                nbSamples = outputFrame->nb_samples;
            }


#ifdef  DUMP_RAW_AUDIO_DATA
            if (mCodec->mOutputDataDump)
                // FIXME: how to dump float format
                mCodec->mOutputDataDump->dump(outputFrame->data[0],
                        nbSamples*mStreamInfo->mChannelCount*AudioEncodeFFmpeg::formatSize(mStreamInfo->mAVCodecContext->sample_fmt));
#endif
            AVPacket *pkt = (AVPacket *)malloc(sizeof(AVPacket));
            if ( !pkt ) {
                ERROR("no mem\n");
                break;
            }

            memset(pkt, 0, sizeof(AVPacket));
            av_init_packet(pkt);
            if (outputFrame) {
                outputFrame->pts = mCodec->mPts;
                mCodec->mPts += nbSamples;
            }

            int ret = avcodec_encode_audio2(mStreamInfo->mAVCodecContext, pkt, outputFrame, &gotFrame);
            if (ret < 0) {
                ERROR("encode error, ret %d.\n", ret);
                av_free_packet(pkt);
                free(pkt);
                pkt = NULL;
                break;
            }


            if (gotFrame > 0 && pkt->size > 0) {
                MediaBufferSP returnedBuffer = AVBufferHelper::createMediaBuffer(pkt, true);
                VERBOSE("packet dts %" PRId64 ", size %d\n", pkt->dts, pkt->size);

                if (srcBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                    returnedBuffer->setFlag(MediaBuffer::MBFT_EOS);
                    mCodec->mIsPaused = true;
                    DEBUG("source input buffer eos");
                }

                if (mCodec->mWriter->write(returnedBuffer) != MM_ERROR_SUCCESS) {
                    ERROR("encoder fail to write to muxer\n");
                    EXIT();
                }
            } else {
                av_free_packet(pkt);
                free(pkt);
                pkt = NULL;
            }

            if (!mStreamInfo->mFifo) {
                break;
            }

        }

    }


    INFO("encoder thread exited\n");
    EXIT();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//AudioEncodeFFmpeg

AudioEncodeFFmpeg::AudioEncodeFFmpeg(const char *mimeType, bool isEncoder) : MMMsgThread(MMMSGTHREAD_NAME)
    , mCondition(mLock)
    , mIsPaused(true)
    , mIsEncoder(isEncoder)
    , mPts(0)
#ifdef DUMP_RAW_AUDIO_DATA
    ,mOutputDataDump(NULL)
#endif
{
    mInputMetaData = MediaMeta::create();
    mOutputMetaData = MediaMeta::create();
    memset(&mSourceTimebase, 0 , sizeof(AVRational));
}

AudioEncodeFFmpeg::~AudioEncodeFFmpeg() {
    ENTER();

    EXIT();
}

mm_status_t AudioEncodeFFmpeg::init() {
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN1(MM_ERROR_OP_FAILED);

#ifdef DUMP_RAW_AUDIO_DATA
    mOutputDataDump = new DataDump("/data/raw_audio_data.pcm");
    if (!mOutputDataDump) {
        ERROR("open file faied\n");
    }
#endif
    EXIT_AND_RETURN1(MM_ERROR_SUCCESS);
}

void AudioEncodeFFmpeg::uninit() {
    ENTER1();
    MMMsgThread::exit();
#ifdef DUMP_RAW_AUDIO_DATA
    if (mOutputDataDump) {
        delete mOutputDataDump;
    }
#endif

    EXIT1();
}

const char * AudioEncodeFFmpeg::name() const {
    return mComponentName.c_str();
}

mm_status_t AudioEncodeFFmpeg::addSource(Component * component, MediaType mediaType) {
    ENTER1();
    mm_status_t status = MM_ERROR_SUCCESS;
    if (component && mediaType == kMediaTypeAudio) {
        mReader = component->getReader(kMediaTypeAudio);
        ASSERT(mReader);
        MediaMetaSP metaData = mReader->getMetaData();
        ASSERT(metaData);
        if (!mIsEncoder) {
            //decoder
        } else {
            MediaMetaSP metaTmp = metaData->copy();
            if (metaTmp && metaTmp->merge(mOutputMetaData)) {
                mOutputMetaData = metaTmp;
            } else {
                ERROR("merge meta to mInputFormat failed\n");
                status = MM_ERROR_OP_FAILED;
            }

            if (mOutputMetaData->getFraction(MEDIA_ATTR_TIMEBASE, mSourceTimebase.num, mSourceTimebase.den)) {
                INFO("get timebase from source, num %d, den %d", mSourceTimebase.num, mSourceTimebase.den);
            } else {
                mOutputMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
                INFO("get no timebase, using default timebase, [1, 1000000]");
            }

            const char *mime = NULL;
            bool ret = mOutputMetaData->getString(MEDIA_ATTR_MIME, mime);
            ASSERT(ret);
            CowCodecID codecId= mime2CodecId(mime);
            mOutputMetaData->setInt32(MEDIA_ATTR_CODECID, codecId);
            DEBUG("audio codecId %d", codecId);
            mOutputMetaData->dump();
        }
    } else {
        status = MM_ERROR_INVALID_PARAM;
    }
    EXIT_AND_RETURN1(status);
}

mm_status_t AudioEncodeFFmpeg::addSink(Component * component, MediaType mediaType) {
    ENTER1();
    if (component && mediaType == kMediaTypeAudio) {
        mWriter = component->getWriter(kMediaTypeAudio);
        if (mWriter && mOutputMetaData) {
            mWriter->setMetaData(mOutputMetaData);
        }

        EXIT_AND_RETURN1(MM_ERROR_SUCCESS);
    } else {
        EXIT_AND_RETURN1(MM_ERROR_INVALID_PARAM);
    }
}

//pipeline setPrameter before addSink and addSource
mm_status_t AudioEncodeFFmpeg::setParameter(const MediaMetaSP & meta) {
    ENTER();
    if (mIsEncoder) {
        for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
            const MediaMeta::MetaItem & item = *i;
            if ( !strcmp(item.mName, MEDIA_ATTR_BIT_RATE) ) {
                if ( item.mType != MediaMeta::MT_Int32 ) {
                    WARNING("invalid type for %s\n", item.mName);
                    continue;
                }

                int32_t bitRate = item.mValue.ii;
                // bitRate =  96000;
                INFO("key: %s, value: %d\n", item.mName, bitRate);
                mOutputMetaData->setInt32(MEDIA_ATTR_BIT_RATE, bitRate);
                continue;
            }
            if ( !strcmp(item.mName, MEDIA_ATTR_MIME) ) {
                if ( item.mType != MediaMeta::MT_String ) {
                    WARNING("invalid type for %s\n", item.mName);
                    continue;
                }

                mComponentName = item.mValue.str;
                INFO("key: %s, value: %s\n", item.mName, mComponentName.c_str());
                mOutputMetaData->setString(MEDIA_ATTR_MIME, mComponentName.c_str());
                mComponentName.append(mIsEncoder ? " encoder" : " decoder");
                continue;
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioEncodeFFmpeg::getParameter(MediaMetaSP & meta) const {
    ENTER();
    meta = mOutputMetaData->copy();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


mm_status_t AudioEncodeFFmpeg::prepare() {
    ENTER();
    postMsg(AFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioEncodeFFmpeg::start() {
    ENTER();
    postMsg(AFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioEncodeFFmpeg::stop() {
    ENTER();
    postMsg(AFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioEncodeFFmpeg::reset() {
    ENTER();
    postMsg(AFF_MSG_reset, 0, NULL);
    mIsPaused = true;
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioEncodeFFmpeg::flush() {
    ENTER();
    postMsg(AFF_MSG_flush, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

/*static*/ int32_t AudioEncodeFFmpeg::formatSize(AVSampleFormat format) {
    switch ( format ) {
        case AV_SAMPLE_FMT_U8:
            return 1;
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            return 2;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_FLTP:
            return 4;
        default:
            ASSERT(0);
    }
}


/*static */AVSampleFormat AudioEncodeFFmpeg::convertAudioFormat(snd_format_t formt) {
#define item(_audio, _av) \
    case _audio:\
        INFO("%s -> %s\n", #_audio, #_av);\
        return _av

    switch ( formt ) {
        item(SND_FORMAT_PCM_8_BIT, AV_SAMPLE_FMT_U8);
        item(SND_FORMAT_PCM_16_BIT, AV_SAMPLE_FMT_S16);
        item(SND_FORMAT_PCM_32_BIT, AV_SAMPLE_FMT_S32);
    default:
        INFO("%d -> AUDIO_SAMPLE_INVALID\n", formt);
        return AV_SAMPLE_FMT_NONE;
    }
}

/*static */bool AudioEncodeFFmpeg::checkSampleFormatSupport(AVCodec *codec, enum AVSampleFormat sample_fmt) {
    const enum AVSampleFormat *p = codec->sample_fmts;
    if (!codec->sample_fmts) {
        return false;
    }

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return true;
        p++;
    }
    return false;
}

/* just pick the highest supported samplerate, but should small than 48000*/
/*static*/ int AudioEncodeFFmpeg::selectSampleRate(AVCodec *codec, int sampleRate) {
    const int *p;
    int bestSamplerate = 0;
    int betterSamplerate = 0;

    if (!codec->supported_samplerates)
        return sampleRate;

    p = codec->supported_samplerates;
    while (*p) {
        //best_samplerate = FFMAX(*p, best_samplerate);
        if (*p == sampleRate) {
            bestSamplerate = *p;
            break;
        }
        if (*p <= 48000) {
            betterSamplerate = *p;
        }

        p++;
        DEBUG("supported samplerate %d", *p);
    }
    return bestSamplerate > 0 ? bestSamplerate : betterSamplerate;
}

/* select layout with the highest channel count */
/*static*/ int AudioEncodeFFmpeg::selectChannelLayout(AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels   = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout    = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return (int)best_ch_layout;
}


/*static*/int AudioEncodeFFmpeg::initFIFO(AVAudioFifo **fifo, AVCodecContext *outputCodecContext) {
    /** Create the FIFO buffer based on the specified output sample format. */
    if (!(*fifo = av_audio_fifo_alloc(outputCodecContext->sample_fmt,
                                      outputCodecContext->channels, 1))) {
        ERROR("allocate FIFO failed\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}


/*static*/int AudioEncodeFFmpeg::addSamplesToFifo(AVAudioFifo *fifo, uint8_t **convertetInputSamples, const int nbSamples) {
    int error;

    //Make the FIFO as large as it needs to be to hold both, the old and the new samples.
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + nbSamples)) < 0) {
        ERROR("Could not reallocate FIFO\n");
        return error;
    }

    //Store the new samples in the FIFO buffer.
    if (av_audio_fifo_write(fifo, (void **)convertetInputSamples, nbSamples) < nbSamples) {
        ERROR("Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }

    return 0;
}


/*static */AVFrame *AudioEncodeFFmpeg::allocAudioFrame(AVCodecContext *c) {
    if (!c)
        return NULL;

    return allocAudioFrame(c->sample_fmt, c->channel_layout, c->sample_rate, c->frame_size);
}

/*static */AVFrame *AudioEncodeFFmpeg::allocAudioFrame(enum AVSampleFormat sample_fmt,
        uint64_t channel_layout,
        int sample_rate, int nb_samples) {
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        ERROR("Error allocating an audio frame\n");
        return NULL;
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            ERROR("Error allocating an audio buffer\n");
            av_frame_free(&frame);//remember to free allcated frame
            return NULL;
        }
    }

    VERBOSE("format %d, channel_layout %lld, sample_rate %d, nb_samples %d",
        frame->format, frame->channel_layout, frame->sample_rate, frame->nb_samples);
    return frame;
}

void AudioEncodeFFmpeg::writeEOSBuffer() {
    MMAutoLock locker(mLock);
    ENTER1();

    MediaBufferSP EOSBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!EOSBuffer) {
        ERROR("createMediaBuffer failed");
        EXIT();
    }
    EOSBuffer->setFlag(MediaBuffer::MBFT_EOS);
    EOSBuffer->setSize(0);

    if (mWriter->write(EOSBuffer) != MM_ERROR_SUCCESS) {
        ERROR("fail to write eos buffer to muxer\n");
        EXIT();
    }
    EXIT1();
}


void AudioEncodeFFmpeg::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    MMAutoLock locker(mLock);
    ASSERT(mOutputMetaData);
    AVCodecContext *c = NULL;
    AVCodec *codec = NULL;
    mm_status_t status = MM_ERROR_SUCCESS;
    bool needResampler = false;

    mStreamInfo.reset(new StreamInfo);
    if (!mStreamInfo) {
        ERROR("no memory\n");
        notify(kEventPrepareResult, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT();
    }

    av_register_all();

    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_SAMPLE_RATE, mStreamInfo->mSampleRate, kEventPrepareResult);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_SAMPLE_FORMAT, mStreamInfo->mSampleFormat, kEventPrepareResult);
    //ASSERT(mStreamInfo->mSampleFormat == 3);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_CHANNEL_COUNT, mStreamInfo->mChannelCount, kEventPrepareResult);
    ASSERT(mStreamInfo->mChannelCount == 1 || mStreamInfo->mChannelCount == 2);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_BIT_RATE, mStreamInfo->mBitrate, kEventPrepareResult);


    mOutputMetaData->getInt32(MEDIA_ATTR_CODECID, mStreamInfo->mCodecID);
    mStreamInfo->mAVCodec = avcodec_find_encoder((AVCodecID)mStreamInfo->mCodecID);
    if (mStreamInfo->mAVCodec == NULL) {
        ERROR("error no Codec found\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    codec = mStreamInfo->mAVCodec;


    mStreamInfo->mAVCodecContext = avcodec_alloc_context3(mStreamInfo->mAVCodec);
    if (!mStreamInfo->mAVCodecContext) {
        ERROR("no memory\n");
        notify(kEventPrepareResult, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT();
    }
    c = mStreamInfo->mAVCodecContext;
    mOutputMetaData->setPointer("AVCodecContext", c);


    c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    //set bitrate
    c->bit_rate = mStreamInfo->mBitrate;

    //check sample format
    c->sample_fmt = convertAudioFormat((snd_format_t)mStreamInfo->mSampleFormat);
    if (!checkSampleFormatSupport(codec, c->sample_fmt)) {

        const enum AVSampleFormat *p = codec->sample_fmts;
        if (p != NULL) {
            INFO("Encoder does not support sample format %d, USE resampler convert to sample_fmt %d",
                c->sample_fmt, *p);
            c->sample_fmt = *p;
        }

        needResampler = true;
    }

    //check sample rate
    c->sample_rate = selectSampleRate(codec, mStreamInfo->mSampleRate);
    if (mStreamInfo->mSampleRate != c->sample_rate) {
        ERROR("select sample_rate %d\n", c->sample_rate);
        status = MM_ERROR_INVALID_PARAM;
        goto FREE_AVCODEC;
    }

    //get channel layout
    c->channels = mStreamInfo->mChannelCount;
    c->channel_layout = av_get_default_channel_layout(c->channels);

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        ERROR("Could not open codec\n");
        status = MM_ERROR_OP_FAILED;
        goto FREE_AVCODEC;
    }

    DEBUG("c->channel_layout: %lld, channels: %d %d, frame_size %d, sample_rate %d\n",
        c->channel_layout, c->channels, mStreamInfo->mChannelCount, c->frame_size, c->sample_rate);

    //set timebase
    mOutputMetaData->setFraction(MEDIA_ATTR_TIMEBASE, c->time_base.num, c->time_base.den);
    DEBUG("timebase is updated from {%d: %d} to {%d: %d}\n",
        mSourceTimebase.num, mSourceTimebase.den, c->time_base.num, c->time_base.den);

    if (c->extradata_size > 0) {
        mOutputMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, c->extradata, c->extradata_size);
        INFO("transit the extra data :size:%d,data:%p\n", c->extradata_size, c->extradata);
        // hexDump(c->extradata, c->extradata_size, 16);
    }
    //update metaData.
    mWriter->setMetaData(mOutputMetaData);

#if 0
    mStreamInfo->mFrameAllocator.reset(new AudioFrameAllocator(c));
    if (!(mStreamInfo->mFrameAllocator)) {
        status = MM_ERROR_NO_MEM;
        goto FREE_AVCODEC;
    }
#endif

    mStreamInfo->mFifo = NULL;
    if (strstr(mComponentName.c_str(), "audio/opus") == NULL) {
        if (initFIFO(&(mStreamInfo->mFifo), c)) {
            status = MM_ERROR_OP_FAILED;
            goto FREE_AVCODEC;
        }
        DEBUG("using audio fifo");
    }

    if (needResampler) {
        mStreamInfo->mAVResample = swr_alloc();
        if (!mStreamInfo->mAVResample) {
            status = MM_ERROR_OP_FAILED;
            goto FREE_FIFO;
        }

        int64_t in_channle_layout = av_get_default_channel_layout(mStreamInfo->mChannelCount);
        //int64_t out_channle_layout = av_get_default_channel_layout(c->channels);
        DEBUG("in_channle_layout %lld, out_channle_layout: %lld\n", in_channle_layout, c->channel_layout);
        av_opt_set_int(mStreamInfo->mAVResample, "in_channel_layout",  in_channle_layout, 0);
        av_opt_set_int(mStreamInfo->mAVResample, "in_sample_fmt",      AV_SAMPLE_FMT_S16,0);
        av_opt_set_int(mStreamInfo->mAVResample, "in_sample_rate",     c->sample_rate, 0);
        av_opt_set_int(mStreamInfo->mAVResample, "out_channel_layout", c->channel_layout, 0);
        av_opt_set_int(mStreamInfo->mAVResample, "out_sample_fmt",     c->sample_fmt, 0);
        av_opt_set_int(mStreamInfo->mAVResample, "out_sample_rate",    c->sample_rate,0);
        if ( swr_init(mStreamInfo->mAVResample) < 0) {
            ERROR("error initializing libswresample\n");
            status = MM_ERROR_OP_FAILED;
            goto FREE_FIFO;
        }
    }

    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

FREE_FIFO:
    if (mStreamInfo->mFifo) {
        av_audio_fifo_free(mStreamInfo->mFifo);
        mStreamInfo->mFifo = NULL;
    }


FREE_AVCODEC:
    avcodec_free_context(&(mStreamInfo->mAVCodecContext));
    mStreamInfo->mAVCodecContext = NULL;

    notify(kEventPrepareResult, status, 0, nilParam);
    EXIT1();
}

void AudioEncodeFFmpeg::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    MMAutoLock locker(mLock);//no need lock, delete in feture
    //for start->stop->start->stop repeatedly
    mIsPaused = false;
    mPts = 0;

    if (!mCodecThread) {
        mCodecThread.reset(new CodecThread(this, mStreamInfo.get()));
        if (!mCodecThread) {
            notify(kEventStartResult, MM_ERROR_NO_MEM, 0, nilParam);
            EXIT1();
        }

        mm_status_t status = mCodecThread->start();
        if (status != MM_ERROR_SUCCESS) {
            notify(kEventStartResult, status, 0, nilParam);
            EXIT1();
        }
    }

    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

/*
void AudioEncodeFFmpeg::onResume(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    AutoLock locker(mLock);
    mIsPaused = false;

    if (mCodecThread) {
        mCodecThread->signal();
    }

    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}
*/

void AudioEncodeFFmpeg::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    {
        MMAutoLock locker(mLock);
        mIsPaused = true;
    }
    if (mCodecThread) {
        mCodecThread->stop();
        mCodecThread.reset();
    }
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

/*
void AudioEncodeFFmpeg::onPause(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    AutoLock locker(mLock);

    mIsPaused = true;
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}
*/

void AudioEncodeFFmpeg::onFlush(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioEncodeFFmpeg::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();

    MMAutoLock locker(mLock);
    mIsPaused = true;
    if (mCodecThread) {
        mCodecThread->stop();
        mCodecThread.reset();
    }

    if (mStreamInfo->mFifo) {
        av_audio_fifo_free(mStreamInfo->mFifo);
    }
    if (mStreamInfo->mAVCodecContext) {
        avcodec_free_context(&(mStreamInfo->mAVCodecContext));
    }
    if (mStreamInfo->mAVResample) {
        swr_free(&mStreamInfo->mAVResample);
    }

    mReader.reset();
    mWriter.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

}

