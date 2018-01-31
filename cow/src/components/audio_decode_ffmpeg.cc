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

#include "audio_decode_ffmpeg.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MDFF")

static const char * COMPONENT_NAME = "AudioDecodeFFmpeg";
static const char * MMTHREAD_NAME = "AudioDecodeFFmpeg::DecodeThread";

#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_CHANNEL         2
#define RESAMPLE_AUDIO_SIZE     8192*4
static int TrafficControlLowBar = 5;
static int TrafficControlHighBar = 10;

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ADFF_MSG_prepare (msg_type)1
#define ADFF_MSG_start (msg_type)2
#define ADFF_MSG_resume (msg_type)3
#define ADFF_MSG_pause (msg_type)4
#define ADFF_MSG_stop (msg_type)5
#define ADFF_MSG_flush (msg_type)6
#define ADFF_MSG_reset (msg_type)7

BEGIN_MSG_LOOP(AudioDecodeFFmpeg)
    MSG_ITEM(ADFF_MSG_prepare, onPrepare)
    MSG_ITEM(ADFF_MSG_start, onStart)
    MSG_ITEM(ADFF_MSG_resume, onResume)
    MSG_ITEM(ADFF_MSG_pause, onPause)
    MSG_ITEM(ADFF_MSG_stop, onStop)
    MSG_ITEM(ADFF_MSG_flush, onFlush)
    MSG_ITEM(ADFF_MSG_reset, onReset)
END_MSG_LOOP()

class dummy {
}; //for source insight parse
// ////////////////////// DecodeThread
AudioDecodeFFmpeg::DecodeThread::DecodeThread(AudioDecodeFFmpeg* decoder)
    : MMThread(MMTHREAD_NAME)
    , mDecoder(decoder)
    , mContinue(true)
{
    ENTER();
    EXIT();
}

AudioDecodeFFmpeg::DecodeThread::~DecodeThread()
{
    ENTER();
    EXIT();
}

void AudioDecodeFFmpeg::DecodeThread::signalExit()
{
    ENTER();
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
    trafficControlWrite->unblockWait();
    MMAutoLock locker(mDecoder->mLock);
    mContinue = false;
    mDecoder->mCondition.signal();
    EXIT();
}

void AudioDecodeFFmpeg::DecodeThread::signalContinue()
{
    ENTER();
    mDecoder->mCondition.signal();
    EXIT();
}

static bool releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

// decode Buffer
void AudioDecodeFFmpeg::DecodeThread::main()
{
    ENTER();

    while(1) {
        MediaBufferSP mediaBuffer;

        MMAutoLock locker(mDecoder->mLock);
        if (!mContinue) {
            break;
        }
        if (mDecoder->mIsPaused) {
            INFO("pause wait");
            mDecoder->mCondition.wait();
            INFO("pause wait wakeup");
            continue;
        }
        mDecoder->mReader->read(mediaBuffer);

        if (mediaBuffer) {
            int64_t targetTime = -1ll;
            if (mediaBuffer->getMediaMeta()->getInt64(MEDIA_ATTR_TARGET_TIME, targetTime)) {
                mDecoder->mTargetTimeUs = targetTime;
                INFO("mTargetTimeUs %0.3f\n", mDecoder->mTargetTimeUs/1000000.0f);
            }

            int gotFrame = 0;
            uint8_t *buffer = NULL;
            int64_t bufferSize = 0;
            int decodedSize = 0;

            AVBufferHelper::convertToAVPacket(mediaBuffer, &mDecoder->mAVPacket);
            // AVDemuxer ensures that EOS mediaBuffer comes w/o data
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
                mDecoder->mIsPaused = true;
                if (!mDecoder->mAVPacket || mediaBuffer->size() == 0) {
                    mediaBuf->setSize(0);
                    if (!mDecoder->mWriter || mDecoder->mWriter->write(mediaBuf) != MM_ERROR_SUCCESS) {
                        ERROR("decoder fail to write Sink");
                        EXIT();
                    }
                    continue;
                }
            }
            // If flush flag is set, discard all the decoded frames and flush internel buffers in codecs.
            if (mDecoder->mNeedFlush) {
                DEBUG("need flush old buffer in codec\n");
                avcodec_flush_buffers(mDecoder->mAVCodecContext);
                mDecoder->mNeedFlush = false;
            }

            AVPacket tempPkt;
            memcpy(&tempPkt, mDecoder->mAVPacket, sizeof(AVPacket));
            while(tempPkt.size > 0) {
                // ensure an AVFrame to accommodate output frame
                if (!mDecoder->mAVFrame)
                    mDecoder->mAVFrame = av_frame_alloc();
                if (!mDecoder->mAVFrame) {
                    ERROR("fail to av_frame_alloc");
                    break;
                }

                int len = avcodec_decode_audio4(mDecoder->mAVCodecContext, mDecoder->mAVFrame, &gotFrame, &tempPkt);
                if (len < 0) {
                    INFO("Error while decoding.\n");
                    break;
                } else if (mDecoder->mChannelCount != mDecoder->mAVCodecContext->channels){
                    INFO("ignore the invalid frame\n");
                    break;
                }

                if (gotFrame > 0) {
                    MediaBufferSP mediaBuf;
                    if (mDecoder->mHasResample) {
                        bufferSize = mDecoder->mAVFrame->nb_samples * mDecoder->mAVCodecContext->channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                        bufferSize = bufferSize * mDecoder->mSampleRateOut / mDecoder->mAVCodecContext->sample_rate + 8;
                        buffer = new uint8_t[bufferSize];
                        decodedSize = swr_convert(mDecoder->mAVResample, &buffer,
                            bufferSize/mDecoder->mAVCodecContext->channels/av_get_bytes_per_sample(AV_SAMPLE_FMT_S16),
                            mDecoder->mAVFrame->data,
                            mDecoder->mAVFrame->nb_samples);

                        decodedSize = decodedSize*mDecoder->mAVCodecContext->channels*av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                        mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                        mediaBuf->setBufferInfo((uintptr_t *)&buffer, NULL, &decodedSize, 1);
                        mediaBuf->setSize(decodedSize);
                        mediaBuf->setPts(mDecoder->mAVFrame->pkt_pts);
                        mediaBuf->addReleaseBufferFunc(releaseOutputBuffer);
                    } else {
                        decodedSize = mDecoder->mAVFrame->linesize[0];
                        mediaBuf = AVBufferHelper::createMediaBuffer(mDecoder->mAVFrame, true, true);
                        mediaBuf->setSize(decodedSize);
                        buffer = mDecoder->mAVFrame->data[0];
                        mDecoder->mAVFrame = NULL; // transfer the AVFrame ownership to MediaBuffer
                    }

#ifdef DUMP_DECODE_FFMPEG_DATA
                    if (mDecoder->mOutputDataDump) {
                        mDecoder->mOutputDataDump->dump(buffer, decodedSize);
                    }
#endif

                    if (mediaBuf->pts() < mDecoder->mTargetTimeUs) {
                        VERBOSE("ignore this frame, timeUs %0.3f, mTargetTimeUs %0.3f",
                            mediaBuf->pts()/1000000.0f, mDecoder->mTargetTimeUs/1000000.0f);
                    } else {
                        mDecoder->mTargetTimeUs = -1ll;

                        mediaBuf->setMonitor(mDecoder->mMonitorWrite);

                        if (!mDecoder->mWriter || mDecoder->mWriter->write(mediaBuf) != MM_ERROR_SUCCESS) {
                            ERROR("decoder fail to write Sink");
                            EXIT();
                        }

                        TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
                        trafficControlWrite->waitOnFull();
                    }
                }
                tempPkt.data += len;
                tempPkt.size -= len;
            }

        }else {
            VERBOSE("read NULL buffer from demuxer\n");
            usleep(10*1000);
        }

    }

    INFO("audio Output thread exited\n");
    EXIT();
}

// /////////////////////////////////////

AudioDecodeFFmpeg::AudioDecodeFFmpeg(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME),
                                                mState(UNINITIALIZED),
                                                mSampleRate(DEFAULT_SAMPLE_RATE),
                                                mChannelCount(DEFAULT_CHANNEL),
                                                mChannelsMap(NULL),
                                                mFormat(-1),
                                                mSampleRateOut(DEFAULT_SAMPLE_RATE),
                                                mChannelCountOut(DEFAULT_CHANNEL),
                                                mFormatOut(1),
                                                mComponentName(COMPONENT_NAME),
                                                mIsPaused(true),
                                                mNeedFlush(false),
                                                mAVCodecContext(NULL),
                                                mAVCodecContextByUs(false),
                                                mAVCodecContextLock(NULL),
                                                mAVCodec(NULL),
                                                mAVPacket(NULL),
                                                mAVFrame(NULL),
                                                mCodecID(0),
                                                mAVResample(NULL),
                                                mHasResample(false),
                                                mCondition(mLock),
                                                mTargetTimeUs(-1ll)
{
    DEBUG("mimeType: %s", mimeType);
}

AudioDecodeFFmpeg::~AudioDecodeFFmpeg()
{
    ENTER();

    EXIT();
}

mm_status_t AudioDecodeFFmpeg::release()
{
    ENTER();
    if (mAVResample) {
        swr_free(&mAVResample);
        mAVResample = NULL;
    }
    if (mAVFrame) {
        av_free(mAVFrame);
        mAVFrame = NULL;
    }
    if (mAVCodecContextByUs && mAVCodecContext) {
        av_free(mAVCodecContext);
        mAVCodecContext = NULL;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t AudioDecodeFFmpeg::init()
{
    if (mState != UNINITIALIZED)
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);

    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
#ifdef DUMP_DECODE_FFMPEG_DATA
    mOutputDataDump = new DataDump("/data/audio_decode_ffmpeg.pcm");
    if (!mOutputDataDump) {
        ERROR("dump file:/data/audio_decode_ffmpeg.pcm can't been open\n");
    }
#endif
    mState = INITIALIZED;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioDecodeFFmpeg::uninit()
{
    ENTER();
    MMMsgThread::exit();
#ifdef DUMP_DECODE_FFMPEG_DATA
    if (mOutputDataDump) {
        delete mOutputDataDump;
    }
#endif
    mState = UNINITIALIZED;

    EXIT();
}

const char * AudioDecodeFFmpeg::name() const
{
    ENTER();
    return mComponentName.c_str();
}

mm_status_t AudioDecodeFFmpeg::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeAudio) {
        mReader = component->getReader(kMediaTypeAudio);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (metaData) {
                av_register_all();
                AVCodec *codec = NULL;
                mInputMetaData = metaData->copy();
                mOutputMetaData = metaData->copy();

                bool ret = mOutputMetaData->getInt32(MEDIA_ATTR_CODECID, mCodecID);
                if (!ret) {
                    const char *mime = NULL;
                    ret = mOutputMetaData->getString(MEDIA_ATTR_MIME, mime);
                    if (!ret || !mime) {
                        ERROR("fail to get getString data %s\n", MEDIA_ATTR_MIME);
                        EXIT_AND_RETURN(MM_ERROR_NO_AUDIODECODER);
                    }
                    CowCodecID codecId= mime2CodecId(mime);
                    mCodecID = CodecId2AVCodecId(codecId);
                }
                codec = avcodec_find_decoder((AVCodecID)mCodecID);
                if (!codec) {
                    ERROR("Audio decode not find ");
                    EXIT_AND_RETURN(MM_ERROR_NO_AUDIODECODER);
                }
                ret = mOutputMetaData->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, mFormat);
                if (!ret) {
                    ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_FORMAT);
                }
                ret = mOutputMetaData->getInt32(MEDIA_ATTR_SAMPLE_RATE, mSampleRate);
                if (!ret) {
                    ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
                }
                ret = mOutputMetaData->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mChannelCount);
                if (!ret) {
                    ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
                    notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
                    EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
                }


                if (mSampleRate > 192000) {
                    TrafficControlLowBar = 120;
                    TrafficControlHighBar = 150;
                } else if (mSampleRate >= 96000) {
                    TrafficControlLowBar = 80;
                    TrafficControlHighBar = 100;
                }
                if (mFormat != AV_SAMPLE_FMT_NONE) {
                    mHasResample = true;
                    mOutputMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, SND_FORMAT_PCM_16_BIT);
                    mOutputMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, mSampleRateOut);
                    mOutputMetaData->setInt32(MEDIA_ATTR_BUFFER_LIST, TrafficControlHighBar);
                }
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}

mm_status_t AudioDecodeFFmpeg::addSink(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeAudio) {
        mWriter = component->getWriter(kMediaTypeAudio);
        if (mWriter && mOutputMetaData) {
            mWriter->setMetaData(mOutputMetaData);
        }
        MediaMetaSP meta = MediaMeta::create();
        mm_status_t status = MM_ERROR_SUCCESS;
        status = component->getParameter(meta);
        if (status == MM_ERROR_SUCCESS) {
            bool ret = meta->getInt32(MEDIA_ATTR_SAMPLE_RATE, mSampleRateOut);
            if (!ret) {
                ERROR("fail to get int32_t data %s, use default output sample rate.\n", MEDIA_ATTR_SAMPLE_RATE);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioDecodeFFmpeg::prepare()
{
    ENTER();
    postMsg(ADFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::start()
{
    ENTER();
    postMsg(ADFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::resume()
{
    ENTER();
    postMsg(ADFF_MSG_resume, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::stop()
{
    ENTER();
    postMsg(ADFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::pause()
{
    ENTER();
    postMsg(ADFF_MSG_pause, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::reset()
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    postMsg(ADFF_MSG_reset, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioDecodeFFmpeg::flush()
{
    ENTER();
    postMsg(ADFF_MSG_flush, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

void AudioDecodeFFmpeg::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    ASSERT(mOutputMetaData);
    int64_t wanted_channel_layout = 0;
    if (mState != INITIALIZED) {
        ERROR("Not valid, audio decode ffmpeg is in state %d\n", mState);
        notify(kEventPrepareResult, MM_ERROR_IVALID_OPERATION, 0, nilParam);
        EXIT();
    }

    void* ptr = NULL;
    bool ret = mOutputMetaData->getPointer(MEDIA_ATTR_CODEC_CONTEXT, ptr);
    if (ret && ptr) {
        DEBUG("USING existed avcodec context");
        mAVCodecContext = static_cast<AVCodecContext*> (ptr);

        ret = mOutputMetaData->getPointer(MEDIA_ATTR_CODEC_CONTEXT_MUTEX, ptr);
        if (!ret) {
            // compressed data may not come from AVDemuxer, still go on
            ERROR("fail to get data pointer %s\n", MEDIA_ATTR_CODEC_CONTEXT_MUTEX);
            mAVCodecContextLock = NULL;
        } else
            mAVCodecContextLock = static_cast<Lock*> (ptr);
    } else {
        mAVCodecContext = avcodec_alloc_context3(mAVCodec);

        ret = mInputMetaData->getInt32(MEDIA_ATTR_SAMPLE_RATE, mAVCodecContext->sample_rate);
        if (!ret) {
            ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
            notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
        ret = mInputMetaData->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mAVCodecContext->channels);
        if (!ret) {
            ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
            notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
        mAVCodecContext->sample_fmt = mFormat;
        mAVCodecContext->channel_layout = av_get_default_channel_layout(mAVCodecContext->channels);
        mAVCodecContextByUs = true;
    }
    DEBUG("mChannelCount %d, mFormat %d, mAVCodecContext->sample_rate %d, mSampleRate %d, mSampleRateOut %d",
        mChannelCount, mFormat, mAVCodecContext->sample_rate, mSampleRate, mSampleRateOut);

    mAVFrame = av_frame_alloc();

    if (mHasResample) {
        mAVResample = swr_alloc();
        if (!mAVResample) {
            ERROR("swr_alloc failed\n");
            notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }

        DEBUG("using swresample");

        wanted_channel_layout =
            (mAVCodecContext->channel_layout &&
            mAVCodecContext->channels ==
            av_get_channel_layout_nb_channels(mAVCodecContext->channel_layout)) ? mAVCodecContext->channel_layout : av_get_default_channel_layout(mAVCodecContext->channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;

        av_opt_set_int(mAVResample, "in_channel_layout",  wanted_channel_layout, 0);
        av_opt_set_int(mAVResample, "in_sample_fmt",      mAVCodecContext->sample_fmt, 0);
        av_opt_set_int(mAVResample, "in_sample_rate",     mAVCodecContext->sample_rate, 0);
        av_opt_set_int(mAVResample, "out_channel_layout", wanted_channel_layout, 0);
        av_opt_set_int(mAVResample, "out_sample_fmt",     AV_SAMPLE_FMT_S16, 0);
        av_opt_set_int(mAVResample, "out_sample_rate",    mSampleRateOut, 0);
        if ( swr_init(mAVResample) < 0) {
            ERROR("error initializing libswresample\n");
            notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
    }
    mState = PREPARED;
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();

}

void AudioDecodeFFmpeg::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (mAVCodec == NULL) {
        int ret = -1;
        mAVCodec = avcodec_find_decoder((AVCodecID)mCodecID);
        if (mAVCodec == NULL) {
            ERROR("error no Codec found\n");
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
        {
            if(mAVCodecContextLock)
                mAVCodecContextLock->acquire();
            ret = avcodec_open2(mAVCodecContext, mAVCodec, NULL) ;
            if(mAVCodecContextLock)
                mAVCodecContextLock->release();
        }
        if (ret < 0) {
            ERROR("error avcodec_open failed.\n");
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
    }

    if (!mDecodeThread) {
        // create thread to decode buffer
        mDecodeThread.reset (new DecodeThread(this), MMThread::releaseHelper);
        mDecodeThread->create();
    }
    mIsPaused = false;
    mState = STARTED;
    mTargetTimeUs = -1ll;
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegAudioDecodeWrite"));
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mDecodeThread->signalContinue();
    EXIT();
}

void AudioDecodeFFmpeg::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (mState != STARTED) {
        INFO("onResume() while not started");
        notify(kEventResumed, MM_ERROR_INVALID_STATE, 0, nilParam);
        EXIT();
    }
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegAudioDecodeWrite"));
    mIsPaused = false;
    mTargetTimeUs = -1ll;
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    mDecodeThread->signalContinue();
    EXIT();
}

void AudioDecodeFFmpeg::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    MMAutoLock locker(mLock);
    mNeedFlush = true;
    if (mAVCodecContext) {
        if(mAVCodecContextLock)
            mAVCodecContextLock->acquire();
        avcodec_close(mAVCodecContext);
        mAVCodec = NULL;
        if(mAVCodecContextLock)
            mAVCodecContextLock->release();
    }
    mState = STOPED;
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void AudioDecodeFFmpeg::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    MMAutoLock locker(mLock);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void AudioDecodeFFmpeg::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (mState == STARTED) {
        mNeedFlush = true;
    }
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void AudioDecodeFFmpeg::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mDecodeThread) {
            mDecodeThread->signalExit();
            mDecodeThread.reset();
        }
    }
    MMAutoLock locker(mLock);
    release();
    mReader.reset();
    mWriter.reset();
    mMonitorWrite.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

AVCodecID AudioDecodeFFmpeg::CodecId2AVCodecId(CowCodecID id)
{
#undef item
#define item(_avid, _cowid) \
        case _cowid:\
            MMLOGI("%s -> %s\n", #_avid, #_cowid);\
            return _avid

        switch ( id ) {
            item(AV_CODEC_ID_NONE, kCodecIDNONE);
            item(AV_CODEC_ID_PCM_S16LE, kCodecIDPCM_S16LE);
            item(AV_CODEC_ID_PCM_S16BE, kCodecIDPCM_S16BE);
            item(AV_CODEC_ID_PCM_U16LE, kCodecIDPCM_U16LE);
            item(AV_CODEC_ID_PCM_U16BE, kCodecIDPCM_U16BE);
            item(AV_CODEC_ID_PCM_S8, kCodecIDPCM_S8);
            item(AV_CODEC_ID_PCM_U8, kCodecIDPCM_U8);
            item(AV_CODEC_ID_PCM_MULAW, kCodecIDPCM_MULAW);
            item(AV_CODEC_ID_PCM_ALAW, kCodecIDPCM_ALAW);
            item(AV_CODEC_ID_PCM_S32LE, kCodecIDPCM_S32LE);
            item(AV_CODEC_ID_PCM_S32BE, kCodecIDPCM_S32BE);
            item(AV_CODEC_ID_PCM_U32LE, kCodecIDPCM_U32LE);
            item(AV_CODEC_ID_PCM_U32BE, kCodecIDPCM_U32BE);
            item(AV_CODEC_ID_PCM_S24LE, kCodecIDPCM_S24LE);
            item(AV_CODEC_ID_PCM_S24BE, kCodecIDPCM_S24BE);
            item(AV_CODEC_ID_PCM_U24LE, kCodecIDPCM_U24LE);
            item(AV_CODEC_ID_PCM_U24BE, kCodecIDPCM_U24BE);
            item(AV_CODEC_ID_PCM_S24DAUD, kCodecIDPCM_S24DAUD);
            item(AV_CODEC_ID_PCM_ZORK, kCodecIDPCM_ZORK);
            item(AV_CODEC_ID_PCM_S16LE_PLANAR, kCodecIDPCM_S16LE_PLANAR);
            item(AV_CODEC_ID_PCM_DVD, kCodecIDPCM_DVD);
            item(AV_CODEC_ID_PCM_F32BE, kCodecIDPCM_F32BE);
            item(AV_CODEC_ID_PCM_F32LE, kCodecIDPCM_F32LE);
            item(AV_CODEC_ID_PCM_F64BE, kCodecIDPCM_F64BE);
            item(AV_CODEC_ID_PCM_F64LE, kCodecIDPCM_F64LE);
            item(AV_CODEC_ID_PCM_BLURAY, kCodecIDPCM_BLURAY);
            item(AV_CODEC_ID_PCM_LXF, kCodecIDPCM_LXF);
            item(AV_CODEC_ID_S302M, kCodecIDS302M);
            item(AV_CODEC_ID_PCM_S8_PLANAR, kCodecIDPCM_S8_PLANAR);
            item(AV_CODEC_ID_PCM_S24LE_PLANAR, kCodecIDPCM_S24LE_PLANAR);
            item(AV_CODEC_ID_PCM_S32LE_PLANAR, kCodecIDPCM_S32LE_PLANAR);
            item(AV_CODEC_ID_PCM_S16BE_PLANAR, kCodecIDPCM_S16BE_PLANAR);
            item(AV_CODEC_ID_PCM_S64LE, kCodecIDPCM_S64LE);
            item(AV_CODEC_ID_PCM_S64BE, kCodecIDPCM_S64BE);
            item(AV_CODEC_ID_ADPCM_IMA_QT, kCodecIDADPCM_IMA_QT);
            item(AV_CODEC_ID_ADPCM_IMA_WAV, kCodecIDADPCM_IMA_WAV);
            item(AV_CODEC_ID_ADPCM_IMA_DK3, kCodecIDADPCM_IMA_DK3);
            item(AV_CODEC_ID_ADPCM_IMA_DK4, kCodecIDADPCM_IMA_DK4);
            item(AV_CODEC_ID_ADPCM_IMA_WS, kCodecIDADPCM_IMA_WS);
            item(AV_CODEC_ID_ADPCM_IMA_SMJPEG, kCodecIDADPCM_IMA_SMJPEG);
            item(AV_CODEC_ID_ADPCM_MS, kCodecIDADPCM_MS);
            item(AV_CODEC_ID_ADPCM_4XM, kCodecIDADPCM_4XM);
            item(AV_CODEC_ID_ADPCM_XA, kCodecIDADPCM_XA);
            item(AV_CODEC_ID_ADPCM_ADX, kCodecIDADPCM_ADX);
            item(AV_CODEC_ID_ADPCM_EA, kCodecIDADPCM_EA);
            item(AV_CODEC_ID_ADPCM_G726, kCodecIDADPCM_G726);
            item(AV_CODEC_ID_ADPCM_CT, kCodecIDADPCM_CT);
            item(AV_CODEC_ID_ADPCM_SWF, kCodecIDADPCM_SWF);
            item(AV_CODEC_ID_ADPCM_YAMAHA, kCodecIDADPCM_YAMAHA);
            item(AV_CODEC_ID_ADPCM_SBPRO_4, kCodecIDADPCM_SBPRO_4);
            item(AV_CODEC_ID_ADPCM_SBPRO_3, kCodecIDADPCM_SBPRO_3);
            item(AV_CODEC_ID_ADPCM_SBPRO_2, kCodecIDADPCM_SBPRO_2);
            item(AV_CODEC_ID_ADPCM_THP, kCodecIDADPCM_THP);
            item(AV_CODEC_ID_ADPCM_IMA_AMV, kCodecIDADPCM_IMA_AMV);
            item(AV_CODEC_ID_ADPCM_EA_R1, kCodecIDADPCM_EA_R1);
            item(AV_CODEC_ID_ADPCM_EA_R3, kCodecIDADPCM_EA_R3);
            item(AV_CODEC_ID_ADPCM_EA_R2, kCodecIDADPCM_EA_R2);
            item(AV_CODEC_ID_ADPCM_IMA_EA_SEAD, kCodecIDADPCM_IMA_EA_SEAD);
            item(AV_CODEC_ID_ADPCM_IMA_EA_EACS, kCodecIDADPCM_IMA_EA_EACS);
            item(AV_CODEC_ID_ADPCM_EA_XAS, kCodecIDADPCM_EA_XAS);
            item(AV_CODEC_ID_ADPCM_EA_MAXIS_XA, kCodecIDADPCM_EA_MAXIS_XA);
            item(AV_CODEC_ID_ADPCM_IMA_ISS, kCodecIDADPCM_IMA_ISS);
            item(AV_CODEC_ID_ADPCM_G722, kCodecIDADPCM_G722);
            item(AV_CODEC_ID_ADPCM_IMA_APC, kCodecIDADPCM_IMA_APC);
            item(AV_CODEC_ID_ADPCM_VIMA, kCodecIDADPCM_VIMA);
            item(AV_CODEC_ID_ADPCM_AFC, kCodecIDADPCM_AFC);
            item(AV_CODEC_ID_ADPCM_IMA_OKI, kCodecIDADPCM_IMA_OKI);
            item(AV_CODEC_ID_ADPCM_DTK, kCodecIDADPCM_DTK);
            item(AV_CODEC_ID_ADPCM_IMA_RAD, kCodecIDADPCM_IMA_RAD);
            item(AV_CODEC_ID_ADPCM_G726LE, kCodecIDADPCM_G726LE);
            item(AV_CODEC_ID_ADPCM_THP_LE, kCodecIDADPCM_THP_LE);
            item(AV_CODEC_ID_ADPCM_PSX, kCodecIDADPCM_PSX);
            item(AV_CODEC_ID_ADPCM_AICA, kCodecIDADPCM_AICA);
            item(AV_CODEC_ID_ADPCM_IMA_DAT4, kCodecIDADPCM_IMA_DAT4);
            item(AV_CODEC_ID_ADPCM_MTAF, kCodecIDADPCM_MTAF);
            item(AV_CODEC_ID_AMR_NB, kCodecIDAMR_NB);
            item(AV_CODEC_ID_AMR_WB, kCodecIDAMR_WB);
            item(AV_CODEC_ID_RA_144, kCodecIDRA_144);
            item(AV_CODEC_ID_RA_288, kCodecIDRA_288);
            item(AV_CODEC_ID_ROQ_DPCM, kCodecIDROQ_DPCM);
            item(AV_CODEC_ID_INTERPLAY_DPCM, kCodecIDINTERPLAY_DPCM);
            item(AV_CODEC_ID_XAN_DPCM, kCodecIDXAN_DPCM);
            item(AV_CODEC_ID_SOL_DPCM, kCodecIDSOL_DPCM);
            item(AV_CODEC_ID_SDX2_DPCM, kCodecIDSDX2_DPCM);
            item(AV_CODEC_ID_MP2, kCodecIDMP2);
            item(AV_CODEC_ID_MP3, kCodecIDMP3);
            item(AV_CODEC_ID_AAC, kCodecIDAAC);
            item(AV_CODEC_ID_AC3, kCodecIDAC3);
            item(AV_CODEC_ID_DTS, kCodecIDDTS);
            item(AV_CODEC_ID_VORBIS, kCodecIDVORBIS);
            item(AV_CODEC_ID_DVAUDIO, kCodecIDDVAUDIO);
            item(AV_CODEC_ID_WMAV1, kCodecIDWMAV1);
            item(AV_CODEC_ID_WMAV2, kCodecIDWMAV2);
            item(AV_CODEC_ID_MACE3, kCodecIDMACE3);
            item(AV_CODEC_ID_MACE6, kCodecIDMACE6);
            item(AV_CODEC_ID_VMDAUDIO, kCodecIDVMDAUDIO);
            item(AV_CODEC_ID_FLAC, kCodecIDFLAC);
            item(AV_CODEC_ID_MP3ADU, kCodecIDMP3ADU);
            item(AV_CODEC_ID_MP3ON4, kCodecIDMP3ON4);
            item(AV_CODEC_ID_SHORTEN, kCodecIDSHORTEN);
            item(AV_CODEC_ID_ALAC, kCodecIDALAC);
            item(AV_CODEC_ID_WESTWOOD_SND1, kCodecIDWESTWOOD_SND1);
            item(AV_CODEC_ID_GSM, kCodecIDGSM);
            item(AV_CODEC_ID_QDM2, kCodecIDQDM2);
            item(AV_CODEC_ID_COOK, kCodecIDCOOK);
            item(AV_CODEC_ID_TRUESPEECH, kCodecIDTRUESPEECH);
            item(AV_CODEC_ID_TTA, kCodecIDTTA);
            item(AV_CODEC_ID_SMACKAUDIO, kCodecIDSMACKAUDIO);
            item(AV_CODEC_ID_QCELP, kCodecIDQCELP);
            item(AV_CODEC_ID_WAVPACK, kCodecIDWAVPACK);
            item(AV_CODEC_ID_DSICINAUDIO, kCodecIDDSICINAUDIO);
            item(AV_CODEC_ID_IMC, kCodecIDIMC);
            item(AV_CODEC_ID_MUSEPACK7, kCodecIDMUSEPACK7);
            item(AV_CODEC_ID_MLP, kCodecIDMLP);
            item(AV_CODEC_ID_GSM_MS, kCodecIDGSM_MS);
            item(AV_CODEC_ID_ATRAC3, kCodecIDATRAC3);
            item(AV_CODEC_ID_VOXWARE, kCodecIDVOXWARE);
            item(AV_CODEC_ID_NELLYMOSER, kCodecIDNELLYMOSER);
            item(AV_CODEC_ID_MUSEPACK8, kCodecIDMUSEPACK8);
            item(AV_CODEC_ID_SPEEX, kCodecIDSPEEX);
            item(AV_CODEC_ID_WMAVOICE, kCodecIDWMAVOICE);
            item(AV_CODEC_ID_WMAPRO, kCodecIDWMAPRO);
            item(AV_CODEC_ID_WMALOSSLESS, kCodecIDWMALOSSLESS);
            item(AV_CODEC_ID_ATRAC3P, kCodecIDATRAC3P);
            item(AV_CODEC_ID_EAC3, kCodecIDEAC3);
            item(AV_CODEC_ID_SIPR, kCodecIDSIPR);
            item(AV_CODEC_ID_MP1, kCodecIDMP1);
            item(AV_CODEC_ID_TWINVQ, kCodecIDTWINVQ);
            item(AV_CODEC_ID_TRUEHD, kCodecIDTRUEHD);
            item(AV_CODEC_ID_MP4ALS, kCodecIDMP4ALS);
            item(AV_CODEC_ID_ATRAC1, kCodecIDATRAC1);
            item(AV_CODEC_ID_BINKAUDIO_RDFT, kCodecIDBINKAUDIO_RDFT);
            item(AV_CODEC_ID_BINKAUDIO_DCT, kCodecIDBINKAUDIO_DCT);
            item(AV_CODEC_ID_AAC_LATM, kCodecIDAAC_LATM);
            item(AV_CODEC_ID_QDMC, kCodecIDQDMC);
            item(AV_CODEC_ID_CELT, kCodecIDCELT);
            item(AV_CODEC_ID_G723_1, kCodecIDG723_1);
            item(AV_CODEC_ID_G729, kCodecIDG729);
            item(AV_CODEC_ID_8SVX_EXP, kCodecID8SVX_EXP);
            item(AV_CODEC_ID_8SVX_FIB, kCodecID8SVX_FIB);
            item(AV_CODEC_ID_BMV_AUDIO, kCodecIDBMV_AUDIO);
            item(AV_CODEC_ID_RALF, kCodecIDRALF);
            item(AV_CODEC_ID_IAC, kCodecIDIAC);
            item(AV_CODEC_ID_ILBC, kCodecIDILBC);
            item(AV_CODEC_ID_OPUS, kCodecIDOPUS);
            item(AV_CODEC_ID_COMFORT_NOISE, kCodecIDCOMFORT_NOISE);
            item(AV_CODEC_ID_TAK, kCodecIDTAK);
            item(AV_CODEC_ID_METASOUND, kCodecIDMETASOUND);
            item(AV_CODEC_ID_PAF_AUDIO, kCodecIDPAF_AUDIO);
            item(AV_CODEC_ID_ON2AVC, kCodecIDON2AVC);
            item(AV_CODEC_ID_DSS_SP, kCodecIDDSS_SP);
            item(AV_CODEC_ID_FFWAVESYNTH, kCodecIDFFWAVESYNTH);
            item(AV_CODEC_ID_SONIC, kCodecIDSONIC);
            item(AV_CODEC_ID_SONIC_LS, kCodecIDSONIC_LS);
            item(AV_CODEC_ID_EVRC, kCodecIDEVRC);
            item(AV_CODEC_ID_SMV, kCodecIDSMV);
            item(AV_CODEC_ID_DSD_LSBF, kCodecIDDSD_LSBF);
            item(AV_CODEC_ID_DSD_MSBF, kCodecIDDSD_MSBF);
            item(AV_CODEC_ID_DSD_LSBF_PLANAR, kCodecIDDSD_LSBF_PLANAR);
            item(AV_CODEC_ID_DSD_MSBF_PLANAR, kCodecIDDSD_MSBF_PLANAR);
            item(AV_CODEC_ID_4GV, kCodecID4GV);
            item(AV_CODEC_ID_INTERPLAY_ACM, kCodecIDINTERPLAY_ACM);
            item(AV_CODEC_ID_XMA1, kCodecIDXMA1);
            item(AV_CODEC_ID_XMA2, kCodecIDXMA2);
            item(AV_CODEC_ID_DST, kCodecIDDST);
            default:
                MMLOGV("%d -> AV_CODEC_ID_NONE\n", id);
                return AV_CODEC_ID_NONE;
        }

}

}


