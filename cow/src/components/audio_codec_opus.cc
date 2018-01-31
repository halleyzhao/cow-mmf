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
#include <vector>

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/media_monitor.h"
#include "multimedia/mm_debug.h"

#include "audio_codec_opus.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("AudioCodecOpus")
static const char * MMMSGTHREAD_NAME = "AudioCodecOpus";
static const char * MMTHREAD_NAME = "AudioCodecOpus::CodecThread";

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define MAX_PACKET 1500 // 4000 bytes is recommended


#define AFF_MSG_prepare (msg_type)1
#define AFF_MSG_start (msg_type)2
#define AFF_MSG_stop (msg_type)5
// #define AFF_MSG_flush (msg_type)6
#define AFF_MSG_reset (msg_type)8
#define AFF_MSG_setParameters (msg_type)9
#define AFF_MSG_getParameters (msg_type)10

BEGIN_MSG_LOOP(AudioCodecOpus)
    MSG_ITEM(AFF_MSG_prepare, onPrepare)
    MSG_ITEM(AFF_MSG_start, onStart)
    MSG_ITEM(AFF_MSG_stop, onStop)
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
//CodecThread
AudioCodecOpus::CodecThread::CodecThread(AudioCodecOpus* codec, StreamInfo *streamInfo)
    : MMThread(MMTHREAD_NAME)
    , mCodec(codec)
    , mStreamInfo(streamInfo)
    , mContinue(true) {
    ENTER();
    sem_init(&mSem, 0, 0);
    EXIT();
}

AudioCodecOpus::CodecThread::~CodecThread() {
    ENTER();
    sem_destroy(&mSem);
    EXIT();
}


mm_status_t AudioCodecOpus::CodecThread::start() {
    ENTER();
    mContinue = true;
    if ( create() ) {
        ERROR("failed to create thread\n");
        return MM_ERROR_NO_MEM;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioCodecOpus::CodecThread::stop() {
    ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    EXIT();
}

void AudioCodecOpus::CodecThread::signal() {
    ENTER();
    sem_post(&mSem);
    EXIT();
}


bool AudioCodecOpus::CodecThread::releaseMediaBuffer(MediaBuffer* mediaBuffer)
{
    size_t *data = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&data, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }

    delete []data;
    return true;
}

MediaBufferSP AudioCodecOpus::CodecThread::createMediaBuffer(uint8_t *data, int32_t size)
{
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);

    if (!buffer)
        return buffer;

    buffer->setFlag(MediaBuffer::MBFT_KeyFrame);

    // map AVPacket fields to MediaBuffer
    buffer->setBufferInfo((uintptr_t*)&data, NULL, NULL, 1);

    int64_t delta = mStreamInfo->mFrameSize * 1000000 / mStreamInfo->mSampleRate;
    mCodec->mPts += delta;
    buffer->setSize(size);
    buffer->setDts(mCodec->mPts);
    buffer->setPts(mCodec->mPts);
    buffer->setDuration(delta);

    buffer->addReleaseBufferFunc(releaseMediaBuffer);
    return buffer;
}


void AudioCodecOpus::CodecThread::main() {
    ENTER();
    mm_status_t status;

    int32_t frameLen = mStreamInfo->mFrameSize * mStreamInfo->mChannels * sizeof(int16_t);

    uint8_t *dataIn = new uint8_t[mStreamInfo->mSampleRate * 2 * mStreamInfo->mChannels * sizeof(int16_t)];
    ASSERT(dataIn);
    int32_t dataInOffset = 0;
    int32_t dataInRemaining = frameLen;

    while (1) {
        int retry = 0;
        if (!mContinue) {
            break;
        }
        if (mCodec->mIsPaused) {
            sem_wait(&mSem);
            continue;
        }

        // If buffer is still has data, do not read from mReader.
        MediaBufferSP srcBuffer;
        do {
            status = mCodec->mReader->read(srcBuffer);
            if (!mContinue) {
                srcBuffer.reset();
                break;
            }
        } while((status == MM_ERROR_AGAIN) && (retry++ < 200));

        if (!srcBuffer || srcBuffer->size() == 0) {// empty EOS frame
            INFO("source media buffer is invalid, signal eos");
            return mCodec->writeEOSBuffer();
        }


        uint8_t *src = NULL;
        int32_t offset = 0;
        int32_t size = 0;
        srcBuffer->getBufferInfo((uintptr_t*)&src, &offset, &size, 1);
        src += offset;
        size -= offset;
        DEBUG("frame pts %" PRId64 " ms, data %p, offset %d, size %d\n",
            srcBuffer->pts()/1000LL, src, offset, size);

        int32_t srcRemaining = size;
        int32_t srcOffset = 0;

        opus_uint32 encFinalRange = 0;
        while (srcRemaining) {
            int32_t copy = srcRemaining > dataInRemaining ? dataInRemaining : srcRemaining;
            memcpy(dataIn+dataInOffset, src+srcOffset, copy);
            dataInOffset += copy;
            srcOffset += copy;
            dataInRemaining -= copy;
            srcRemaining -= copy;

            // dataIn buffer is not full filled, continue to read
            if (dataInRemaining > 0) {
                continue;
            }

            // dataOut is released by MediaBuffer
            uint8_t *dataOut = new uint8_t[mStreamInfo->mMaxPayloadSize];
            ASSERT(dataOut);

            // opus_encode() must be called with exactly one frame (2.5, 5, 10, 20, 40 or 60 ms) of audio data
            // refer to opus.h
            int32_t encodedLen = opus_encode(mCodec->mEncoder, (int16_t*)dataIn, mStreamInfo->mFrameSize,
                dataOut, mStreamInfo->mMaxPayloadSize);

            int32_t nbEncoded = opus_packet_get_samples_per_frame(dataOut,
                mStreamInfo->mSampleRate)*opus_packet_get_nb_frames(dataOut, encodedLen);
            DEBUG("opus_encode() mFrameSize %d, nb_encoded %d\n",
                mStreamInfo->mFrameSize, nbEncoded);

            opus_encoder_ctl(mCodec->mEncoder, OPUS_GET_FINAL_RANGE(&encFinalRange));
            if (encodedLen < 0) {
                ERROR("opus_encode() returned %d\n", encodedLen);
                EXIT();
            }
            DEBUG("encodedLen %d, encFinalRange %d", encodedLen, encFinalRange);


            // write to muxer
            MediaBufferSP returnedBuffer = createMediaBuffer(dataOut, encodedLen);
            if (srcBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                returnedBuffer->setFlag(MediaBuffer::MBFT_EOS);
                mCodec->mIsPaused = true;
                DEBUG("source input buffer eos");
            }

            if (mCodec->mWriter->write(returnedBuffer) != MM_ERROR_SUCCESS) {
                ERROR("encoder fail to write to muxer\n");
                EXIT();
            }

            // reset offset
            dataInOffset = 0;
            dataInRemaining = frameLen;

        }


    }
    INFO("encoder thread exited\n");
    if (dataIn) {
        delete []dataIn;
        dataIn = NULL;
    }
    EXIT();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//AudioCodecOpus

AudioCodecOpus::AudioCodecOpus(const char *mimeType, bool isEncoder) : MMMsgThread(MMMSGTHREAD_NAME)
    , mCondition(mLock)
    , mIsPaused(true)
    , mIsEncoder(isEncoder)
    , mPts(0)
#ifdef DUMP_RAW_AUDIO_DATA
    ,mOutputDataDump(NULL)
#endif
    , mEncoder(NULL)
{
    mInputMetaData = MediaMeta::create();
    mOutputMetaData = MediaMeta::create();
}

AudioCodecOpus::~AudioCodecOpus() {
    ENTER();

    EXIT();
}

mm_status_t AudioCodecOpus::init() {
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

void AudioCodecOpus::uninit() {
    ENTER1();
    MMMsgThread::exit();
#ifdef DUMP_RAW_AUDIO_DATA
    if (mOutputDataDump) {
        delete mOutputDataDump;
    }
#endif

    EXIT1();
}

const char * AudioCodecOpus::name() const {
    return mComponentName.c_str();
}

mm_status_t AudioCodecOpus::addSource(Component * component, MediaType mediaType) {
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

            int32_t num = 0;
            int32_t den = 0;
            if (mOutputMetaData->getFraction(MEDIA_ATTR_TIMEBASE, num, den)) {
                INFO("get timebase from source, num %d, den %d", num, den);
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

mm_status_t AudioCodecOpus::addSink(Component * component, MediaType mediaType) {
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
mm_status_t AudioCodecOpus::setParameter(const MediaMetaSP & meta) {
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

mm_status_t AudioCodecOpus::getParameter(MediaMetaSP & meta) const {
    ENTER();
    meta = mOutputMetaData->copy();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


mm_status_t AudioCodecOpus::prepare() {
    ENTER();
    postMsg(AFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioCodecOpus::start() {
    ENTER();
    postMsg(AFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioCodecOpus::stop() {
    ENTER();
    postMsg(AFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t AudioCodecOpus::reset() {
    ENTER();
    postMsg(AFF_MSG_reset, 0, NULL);
    mIsPaused = true;
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

void AudioCodecOpus::writeEOSBuffer() {
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


void AudioCodecOpus::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();
    MMAutoLock locker(mLock);
    ASSERT(mOutputMetaData);
    // mm_status_t status = MM_ERROR_SUCCESS;

    mStreamInfo.reset(new StreamInfo);
    if (!mStreamInfo) {
        ERROR("no memory\n");
        notify(kEventPrepareResult, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT();
    }


    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_SAMPLE_RATE, mStreamInfo->mSampleRate, kEventPrepareResult);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_SAMPLE_FORMAT, mStreamInfo->mSampleFormat, kEventPrepareResult);
    //ASSERT(mStreamInfo->mSampleFormat == 3);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_CHANNEL_COUNT, mStreamInfo->mChannels, kEventPrepareResult);
    ASSERT(mStreamInfo->mChannels == 1 || mStreamInfo->mChannels == 2);
    GET_INT32_PARAM_KEY_VALUE(mOutputMetaData, MEDIA_ATTR_BIT_RATE, mStreamInfo->mBitrate, kEventPrepareResult);
    mStreamInfo->mBitrate = mStreamInfo->mSampleRate * 2; // FIXME:


    // fmt is 16int
    // sample rate is 48000

    DEBUG("mSampleRate: %d, mSampleFormat: %d, mChannels %d, mBitrate %d\n",
        mStreamInfo->mSampleRate,
        mStreamInfo->mSampleFormat,
        mStreamInfo->mChannels,
        mStreamInfo->mBitrate);

    //set timebase
    mOutputMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    std::vector<uint8_t> decodeInfo;
    // start with "OpusHead"
    decodeInfo.push_back(0x4f);
    decodeInfo.push_back(0x70);
    decodeInfo.push_back(0x75);
    decodeInfo.push_back(0x73);
    decodeInfo.push_back(0x48);
    decodeInfo.push_back(0x65);
    decodeInfo.push_back(0x61);
    decodeInfo.push_back(0x64);

    decodeInfo.push_back(0x01);

    decodeInfo.push_back(mStreamInfo->mChannels);
    uint16_t initialPadding = 1024; //FIXME: refer to opus encoder of ffmpeg. why?
    decodeInfo.push_back(initialPadding & 0xff);
    decodeInfo.push_back((initialPadding >> 8) & 0xff);

    decodeInfo.push_back((mStreamInfo->mSampleRate) & 0xff);
    decodeInfo.push_back((mStreamInfo->mSampleRate >> 8) & 0xff);
    decodeInfo.push_back((mStreamInfo->mSampleRate >> 16) & 0xff);
    decodeInfo.push_back((mStreamInfo->mSampleRate >> 24) & 0xff);

    decodeInfo.push_back(0x00);
    decodeInfo.push_back(0x00);
    decodeInfo.push_back(0x00); /* Default layout */


    mOutputMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, &decodeInfo[0], decodeInfo.size());
    hexDump(&decodeInfo[0], decodeInfo.size(), 16);

    //update metaData.
    mWriter->setMetaData(mOutputMetaData);

    int err = OPUS_OK;
    mEncoder = opus_encoder_create(mStreamInfo->mSampleRate,
        mStreamInfo->mChannels,
        OPUS_APPLICATION_AUDIO,
        &err);
    if (err != OPUS_OK) {
        DEBUG("Cannot create encoder: %s\n", opus_strerror(err));
        notify(kEventPrepareResult, MM_ERROR_INVALID_PARAM, 0, nilParam);
        EXIT1();
    }
    ASSERT(mEncoder);


    opus_int32 skip=0;
    int bandwith = OPUS_AUTO;
    int use_vbr = 1;
    int cvbr = 0;
    int complexity = 10;
    int use_inbandfec = 0;
    int forcechannels = OPUS_AUTO;
    int use_dtx = 0;
    int packet_loss_perc = 0;
    int variable_duration = 5000;

    mStreamInfo->mFrameSize = mStreamInfo->mSampleRate/50;
    mStreamInfo->mMaxPayloadSize = MAX_PACKET;


    opus_encoder_ctl(mEncoder, OPUS_SET_BITRATE(mStreamInfo->mBitrate));
    opus_encoder_ctl(mEncoder, OPUS_SET_BANDWIDTH(bandwith)); //auto bandwidth
    opus_encoder_ctl(mEncoder, OPUS_SET_VBR(use_vbr)); //using vbr as default value
    opus_encoder_ctl(mEncoder, OPUS_SET_VBR_CONSTRAINT(cvbr));
    opus_encoder_ctl(mEncoder, OPUS_SET_COMPLEXITY(complexity));
    opus_encoder_ctl(mEncoder, OPUS_SET_INBAND_FEC(use_inbandfec));
    opus_encoder_ctl(mEncoder, OPUS_SET_FORCE_CHANNELS(forcechannels));
    opus_encoder_ctl(mEncoder, OPUS_SET_DTX(use_dtx));
    opus_encoder_ctl(mEncoder, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));

    opus_encoder_ctl(mEncoder, OPUS_GET_LOOKAHEAD(&skip));
    opus_encoder_ctl(mEncoder, OPUS_SET_LSB_DEPTH(16));
    opus_encoder_ctl(mEncoder, OPUS_SET_EXPERT_FRAME_DURATION(variable_duration));



    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

void AudioCodecOpus::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
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


void AudioCodecOpus::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
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

void AudioCodecOpus::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER1();

    MMAutoLock locker(mLock);
    mIsPaused = true;
    if (mCodecThread) {
        mCodecThread->stop();
        mCodecThread.reset();
    }

    if (mEncoder) {
        opus_encoder_destroy(mEncoder);
    }
    mReader.reset();
    mWriter.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}
}

extern "C" {

using namespace YUNOS_MM;
Component * createComponent(const char* mimeType, bool isEncoder)
{
    ASSERT(isEncoder);
    return new AudioCodecOpus(mimeType, isEncoder);
}

void releaseComponent(Component * component)
{
    if ( component ) {
        AudioCodecOpus * c = DYNAMIC_CAST<AudioCodecOpus*>(component);
        ASSERT(c != NULL);
        delete (c);
        c = NULL;
    }
}
}


