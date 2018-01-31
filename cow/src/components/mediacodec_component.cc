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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mediacodec_component.h"

#include <multimedia/mm_debug.h>
#include "media_notify.h"
#include <arpa/inet.h>

#include "multimedia/media_buffer.h"
#include "multimedia/codec.h"
#include "multimedia/media_attr_str.h"
#include "media_codec_priv.h"

MM_LOG_DEFINE_MODULE_NAME("MCC");
static const char * COMPONENT_NAME = "MediaCodecComponent";

namespace YUNOS_MM {

using namespace YunOSMediaCodec;

BEGIN_MSG_LOOP(MediaCodecComponent)
    MSG_ITEM(MCC_MSG_prepare, onPrepare)
    MSG_ITEM(MCC_MSG_pause, onPause)
    MSG_ITEM(MCC_MSG_flush, onFlush)
    MSG_ITEM(MCC_MSG_setComponent, onSetPeerComponent)
    MSG_ITEM(MCC_MSG_codecActivity, onCodecActivity)
    MSG_ITEM(MCC_MSG_acquireSourceBuffer, onAcquireSourceBuffer)
    MSG_ITEM2(MCC_MSG_setParameter, onSetParameter)
    MSG_ITEM(MCC_MSG_stop, onStop)
    MSG_ITEM(MCC_MSG_start, onStart)
    MSG_ITEM(MCC_MSG_resume, onResume)
    MSG_ITEM(MCC_MSG_reset, onReset)
    MSG_ITEM(MCE_MSG_bufferAvailable, onSourceBufferAvailable)
END_MSG_LOOP()

//#define MC_VERBOSE MC_WARNING
#undef DEBUG_STREAM

#define ENTER() MC_VERBOSE(">>>\n")
#define EXIT() do {MC_VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {MC_VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() MC_INFO(">>>\n")
#define EXIT1() do {MC_INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {MC_INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

MediaCodecComponent::MediaCodecComponent(const char *mime)
    : MMMsgThread(COMPONENT_NAME),
      mState(UNINITIALIZED),
      mPaused(true),
      mPendingAcquireBuffer(false),
      mFormatChangePending(false),
      mResumePending(false),
      mCodecActivityPending(false),
      mIsNal(false),
      mNalLengthSize(4),
      mIsAVCcType(false),
      mStreamEOS(false),
      mIsEncoder(false),
      mAsync(true),
      mCodecInputSequence(0),
      mCodecOutputSequence(0),
      mGeneration(0),
      mCodecInputTimeStamp(-1ll),
      mCodecOutputTimeStamp(0),
      mNativeWindow(NULL),
      mSurfaceTexture(NULL),
      mInputBufferCount(0),
      mOutputBufferCount(0),
      mNotify(NULL),
      mPreviousStop(false) {

    ENTER();

    mInputFormat = MediaMeta::create();
    mOutputFormat = MediaMeta::create();
    mComponentName = "MediaCodecComponent";

    mIsAudio = !strncmp(mime, "audio/", strlen("audio/"));

    EXIT();
}

MediaCodecComponent::~MediaCodecComponent() {
    ENTER();

    if (mCodecActivityPending) {
        if (mCodec)
            mCodec->registerNotify(NULL);
        delete mNotify;
    }

    mm_status_t status;
    int ret;

    if (mCodec) {
        status = stopInternal();

        if (status != MM_ERROR_SUCCESS)
            MC_ERROR("stop Internal fail in destructor %d", status);

        ret = mCodec->release();
        if (ret != MediaCodec::MEDIACODEC_OK)
            MC_ERROR("fail to release codec in destructor");
    }


#if 0
    mCodec.reset();

    mReader.reset();
    mWriter.reset();

    //std::string mComponentName;

    mInputFormat.reset();
    mOutputFormat.reset();

    mMediaBuffers.clear();
    mAvailableCodecBuffers.clear(); /* MediaCodec input buffer index */
    mAvailableSourceBuffers.clear();
    mInputBufferData.clear();
    mInputBufferSize.clear();

    mOutputBufferData.clear();
    mOutputBufferSize.clear();
#endif

    EXIT();
}

mm_status_t MediaCodecComponent::init() {
    ENTER();
    int ret;

    if (mState != UNINITIALIZED)
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);

    ret = MMMsgThread::run();
    mState = INITIALIZED;
    EXIT_AND_RETURN(ret == 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void MediaCodecComponent::uninit() {
    ENTER();
    MMMsgThread::exit();
    mState = UNINITIALIZED;
    EXIT();
}

#define MCC_MSG_BRIDGE(msg, param1, param2) \
    mm_status_t status;                                 \
    if (mAsync) {                                       \
        status = MM_ERROR_ASYNC;                        \
        postMsg(msg, param1, param2);                   \
    } else {                                            \
        status = processMsg(msg, param1, param2);       \
    }                                                   \
    MC_INFO("%s return status %d", __func__, status);

mm_status_t MediaCodecComponent::start() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_start, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::resume() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_resume, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::stop() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_stop, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::processMsg(uint32_t msg, param1_type param1, param2_type param2, param3_type param3) {
    param1_type rsp_param1;
    param2_type rsp_param2;

    if (sendMsg(msg, param1, param2, param3, &rsp_param1, &rsp_param2, NULL)) {
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    if (rsp_param1)
        EXIT_AND_RETURN(rsp_param1);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t MediaCodecComponent::prepare() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_prepare, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::pause() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_pause, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::reset() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_reset, 0, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::flush() {
    ENTER();

    MCC_MSG_BRIDGE(MCC_MSG_flush, mAsync, NULL)

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::addSource(Component * component, MediaType mediaType) {
    ENTER();
    MediaType type = mIsAudio ? kMediaTypeAudio : kMediaTypeVideo;

    if (mediaType != type)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    if (mState == UNINITIALIZED) {
        handleError();
        EXIT_AND_RETURN(MM_ERROR_NOT_INITED);
    }

    //postMsg(MCC_MSG_setComponent, (param1_type)MCC_SOURCE, (param2_type)component);
    //EXIT_AND_RETURN(MM_ERROR_ASYNC);

    mm_status_t status = processMsg(MCC_MSG_setComponent,
                                    (param1_type)MCC_SOURCE,
                                    (param2_type)component);
    MC_INFO("addSource status %d", status);
    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::addSink(Component * component, MediaType mediaType) {
    ENTER();
    MediaType type = mIsAudio ? kMediaTypeAudio : kMediaTypeVideo;

    if (mediaType != type)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    if (mState == UNINITIALIZED) {
        handleError();
        EXIT_AND_RETURN(MM_ERROR_NOT_INITED);
    }
    //postMsg(MCC_MSG_setComponent, (param1_type)MCC_SINK, (param2_type)component);
    //EXIT_AND_RETURN(MM_ERROR_ASYNC);

    mm_status_t status = processMsg(MCC_MSG_setComponent,
                                    (param1_type)MCC_SINK,
                                    (param2_type)component);
    MC_INFO("addSink status %d", status);
    EXIT_AND_RETURN(status);
}

const char *MediaCodecComponent::name() const {
    return mComponentName.c_str();
}



mm_status_t MediaCodecComponent::setParameter(const MediaMetaSP & meta) {
    ENTER();

    mm_status_t status;
    MMRefBaseSP refBase(new MCCMMParamBase(meta));
    if ( !refBase ) {
        EXIT_AND_RETURN(MM_ERROR_NO_MEM);
    }
    status = processMsg(MCC_MSG_setParameter, 0, NULL, refBase);
    MC_INFO("setParameter status %d", status);

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::getParameter(MediaMetaSP & meta) const {

    ENTER();

    if (!mIsAudio) {
        int32_t val = 0;
        mOutputFormat->getInt32(MEDIA_ATTR_WIDTH, val);
        meta->setInt32(MEDIA_ATTR_WIDTH, val);
        mOutputFormat->getInt32(MEDIA_ATTR_HEIGHT, val);
        meta->setInt32(MEDIA_ATTR_HEIGHT, val);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void MediaCodecComponent::scheduleCodecActivity() {
    ENTER();
    if (mPaused)
        EXIT();

    if (mFormatChangePending)
        EXIT();

    if (mCodecActivityPending) {
        // sometimes codec has no activity
        // .e.g. when pause, codec returns an input index but reader return NO_MORE, componet won't schedule futher reads
        // try to kick off
        acquireSourceBuffer();
        EXIT();
    }

    if (mCodec) {
        YunOSMediaCodec::Notify *notify = new YunOSMediaCodec::Notify;

        notify->setWhat(MCC_MSG_codecActivity);
        notify->registerThreadHandler(this);

        mNotify = notify;

        mNotify->setInt32("generation", mGeneration);
        mCodec->registerNotify(mNotify);
        mCodecActivityPending = true;
    }

    EXIT();
}

static int mp4toannexb(uint8_t *codecBuf, uint8_t *sourceBuf, const int32_t length, int nalOffset) {

#define START_CODE_OFFSET 4
#define START_CODE 0x01000000

    uint32_t nalLength;
    uint32_t *ptr;
    int32_t offset = 0;
    int32_t codecBufLength = 0;
#ifdef DEBUG_STREAM
    uint8_t *origin = codecBuf;
#endif

    while (offset < length) {
        ptr = (uint32_t*)(sourceBuf);

        if (nalOffset == 4)
            nalLength = ntohl(*ptr);
        else if (nalOffset == 2)
            nalLength = ntohs(*ptr);
        else if (nalOffset == 1)
            nalLength = *(uint8_t*)ptr;
        else {
            ERROR("nal offset %d which is invalid", nalOffset);
            return 0;
        }

        sourceBuf += nalOffset;;

        *((uint32_t *)codecBuf) = START_CODE;
        codecBuf += START_CODE_OFFSET;
        codecBufLength += START_CODE_OFFSET;
        offset += nalOffset + nalLength;

        if (offset > length || offset < 0) {
            WARNING("got bad buffer, buffer length %d current NAL length %d(0x%x) offset %d\n",
                    length, nalLength, nalLength, offset);
            return (codecBufLength - START_CODE_OFFSET);
        }

        memcpy(codecBuf, sourceBuf, nalLength);
        codecBuf += nalLength;
        sourceBuf += nalLength;
        codecBufLength += nalLength;
    }

#ifdef DEBUG_STREAM
    INFO("mp4toannexb: length is %d\n", length);
    for (int32_t i = 0; i < ((length > 32) ? 32 : length); i++)
        INFO("%x ", origin[i]);
#endif
    return codecBufLength;
}

bool MediaCodecComponent::submitBufferToCodec() {
    ENTER();
    if (mAvailableCodecBuffers.empty() || mAvailableSourceBuffers.empty()) {
        MC_VERBOSE("no buffer available for codec, codec/source buffer empty (%d/%d)",
                mAvailableCodecBuffers.empty(), mAvailableSourceBuffers.empty());
        return false;
    }

    MediaBufferSP srcBuffer;

    size_t index = mAvailableCodecBuffers.front();
    BufferInfo info = mAvailableSourceBuffers.front();
    uint8_t *sourceBuf = NULL, *codecBuf = NULL;

    int32_t offset, length;
    size_t codecBufSize;
    int err = 0;
    int sourceErr = 0;
    bool annexB = true;

    srcBuffer = info.mediaBuf;

    if ((srcBuffer && srcBuffer->type() == MediaBuffer::MBT_ByteBuffer)
        || (mIsEncoder && srcBuffer->type() == MediaBuffer::MBT_RawAudio)) {
        srcBuffer->getBufferInfo((uintptr_t *)&sourceBuf, &offset, &length, 1);
        length = srcBuffer->size();

        // source buffer read error
        MediaMetaSP meta = srcBuffer->getMediaMeta();
        meta->getInt32("error", sourceErr);
        if (mIsAVCcType) {
            annexB = false;
        }
    }

    if (!sourceBuf || sourceErr) { //no compress data case, send eos
        MC_INFO("invalid mediabuffer (buf %p, error %d), signal EOS", sourceBuf, sourceErr);

#ifndef _PLATFORM_TV
        err = mCodec->queueInputBuffer(index,
                       0, 0, 0, MediaCodec::BUFFER_FLAG_EOS);

#else
        if (mIsAudio || mIsEncoder)
            err = mCodec->queueInputBuffer(index, 0, 0, 0, MediaCodec::BUFFER_FLAG_EOS);
        else //for video decoder, don't queue eos buffer to component for amlogic's bug
            asyncFuncExit(kEventEOS, 0, 0, 0);
#endif
        if (err != MediaCodec::MEDIACODEC_OK) {
            MC_ERROR("queue eos flag fail\n");
            handleError(sourceErr);
            usleep(10*1000);
        }
        mStreamEOS = true;

        mAvailableCodecBuffers.pop_front();
        mAvailableSourceBuffers.pop_front();
        EXIT_AND_RETURN(true);
    }

    //codecBuf = mCodec->getInputBuffer(index, &codecBufSize);
    codecBuf = mInputBufferData.at(index);
    codecBufSize = mInputBufferSize.at(index);

    if (!codecBuf || codecBufSize < (size_t)length) {
        MC_ERROR("cannot use codec input buffer, NULL pointer(%p) or not enough size(%d/%d)\n",
              codecBuf, codecBufSize, length);
        handleError();
        EXIT_AND_RETURN(false);
    }

    //for decoder
    if (!mIsEncoder && !annexB &&
        sourceBuf[0] == 0x00 &&
        sourceBuf[1] == 0x00 &&
        sourceBuf[2] == 0x00 &&
        sourceBuf[3] == 0x01) {
        MC_WARNING("stream is not annexB, but start with 0x00000001, assume it's annexB\n");
        annexB = true;
    }

#ifdef DEBUG_STREAM
    MC_INFO("length is %d, annexb is %d\n", length, annexB);
    for (int32_t i = 0; i < ((length > 64) ? 64 : length); i++)
        MC_INFO("%x ", sourceBuf[i]);
#endif

    if (mIsNal && !annexB && !mIsEncoder)
        length = mp4toannexb(codecBuf, sourceBuf + offset, length, mNalLengthSize);
    else
        memcpy(codecBuf, sourceBuf + offset, length);

    int64_t timeUs = 0;
    uint32_t flags = 0;

    timeUs = srcBuffer->pts();
    if (srcBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
#ifdef _PLATFORM_TV
        if (mIsAudio || mIsEncoder)
#endif
        {
            MC_INFO("queue eos flag\n");
            flags |= MediaCodec::BUFFER_FLAG_EOS;
            mStreamEOS = true;
        }
    }

    if (srcBuffer->isFlagSet(MediaBuffer::MBFT_CodecData)) {
        flags |= MediaCodec::BUFFER_FLAG_CODECCONFIG;
        MC_INFO("get config buffer\n");
    } else {
        checkSourceTimeDiscontinuity(srcBuffer->dts(), srcBuffer->pts());
        mCodecInputTimeStamp = srcBuffer->dts();
    }

    mCodecInputSequence++;
    DEBUG("mCodecInputSequence: %06d, index %d, pts %" PRId64 "us", mCodecInputSequence, index, timeUs);

    err = mCodec->queueInputBuffer(index,
                   0, length, timeUs, flags);

    if (err != MediaCodec::MEDIACODEC_OK) {
        MC_ERROR("queue input buffer fail\n");
        handleError();
    }

    mAvailableCodecBuffers.pop_front();
    mAvailableSourceBuffers.pop_front();

    // release srcBuffer, no need to retain it
    //mMediaBuffers.at(index).srcBuffer = srcBuffer;
    ASSERT(mMediaBuffers.at(index).shadowBuf == NULL);
    mMediaBuffers.at(index).shadowBuf = info.shadowBuf;

    EXIT_AND_RETURN(true);
}

void MediaCodecComponent::clearBuffer() {
    ENTER();
    MC_DEBUG("clear buffer, mAvailableSourceBuffers.size %d, mMediaBuffers size %d\n",
        mAvailableSourceBuffers.size(), mMediaBuffers.size());
    mAvailableCodecBuffers.clear();
    mAvailableSourceBuffers.clear();

    size_t size = mMediaBuffers.size();

    mMediaBuffers.resize(mInputBufferCount);

    size = mMediaBuffers.size();
    for (size_t i = 0; i < size; i++) {
        mMediaBuffers[i].mediaBuf.reset();;
        mMediaBuffers[i].shadowBuf.reset();;
    }

    EXIT();
}

void MediaCodecComponent::acquireSourceBuffer() {
    ENTER();
    if (mFormatChangePending || mPendingAcquireBuffer)
        EXIT();

    mm_status_t status = MM_ERROR_SUCCESS;

    while (!mAvailableCodecBuffers.empty()) {
        MediaBufferSP sourceBuf;

        status = readSourceBuffer(sourceBuf);
        if (status != MM_ERROR_SUCCESS) {
            MC_DEBUG("read source status %d, codec/source queue size(%d/%d)",
                    status, mAvailableCodecBuffers.size(), mAvailableSourceBuffers.size());
            break;
        }

        //bool empty = availableSourceBuffers.empty();
        if (sourceBuf) {
            BufferInfo info;
            info.mediaBuf = sourceBuf;
            mAvailableSourceBuffers.push_back(info);
        }

        if (!submitBufferToCodec())
            break;
    }

    //If reader return MM_ERROR_SUCCESS with NULL sourceBuf
    //source buffer will never be read
    if (status == MM_ERROR_AGAIN && !mPaused)
        scheduleAcquire();

    EXIT();
}

bool MediaCodecComponent::setupMediaCodecParam(MediaMetaSP &params) {
    ENTER();

    if (!params) {
        MC_ERROR("no param");
        EXIT_AND_RETURN(false);
    }

    int32_t inputSize = 0;
    //for audio and video both
    if (mInputFormat->getInt32(MEDIA_ATTR_MAX_INPUT_SIZE, inputSize) &&
        inputSize > 0) {
        params->setInt32(MEDIA_ATTR_MAX_INPUT_SIZE, inputSize);
    }

    if (!mIsAudio) {
        int32_t width = 0;
        int32_t height = 0;
        int32_t rotation = 0;
        mInputFormat->getInt32(MEDIA_ATTR_WIDTH, width);
        mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
        mInputFormat->getInt32(MEDIA_ATTR_ROTATION, rotation);

        if (width == 0 || width > MCC_MAX_DIMENSION ||
            height == 0 || height > MCC_MAX_DIMENSION) {
            MC_ERROR("invalid resolution %dx%d", width, height);
            EXIT_AND_RETURN(false);
        }

        /* looks like amlogic doesn't support adaptive playback
        param.type = kParamMaxWidth;
        param.value.ii = 1920;
        params.push_back(param);

        param.type = kParamMaxHeight;
        param.value.ii = 1088;
        params.push_back(param);
        */

        params->setInt32(MEDIA_ATTR_WIDTH, width);
        params->setInt32(MEDIA_ATTR_HEIGHT, height);
        params->setInt32(MEDIA_ATTR_ROTATION, rotation);
    } else {
        int32_t sampleRate;
        if (mInputFormat->getInt32(MEDIA_ATTR_SAMPLE_RATE, sampleRate)) {
            params->setInt32(MEDIA_ATTR_SAMPLE_RATE, sampleRate);
            MC_DEBUG("sample rate %d", sampleRate);
        }

        int32_t sampleFormat;
        if (mInputFormat->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, sampleFormat)) {
            params->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, sampleFormat);
            MC_DEBUG("sample format %d", sampleFormat);
        }

        int32_t codecTag;
        if (mInputFormat->getInt32(MEDIA_ATTR_CODECTAG, codecTag)) {
            params->setInt32(MEDIA_ATTR_CODECTAG, codecTag);
            MC_DEBUG("codec tag %d", codecTag);
        }

        int32_t blockAlign;
        if (mInputFormat->getInt32(MEDIA_ATTR_BLOCK_ALIGN, blockAlign)) {
            params->setInt32(MEDIA_ATTR_BLOCK_ALIGN, blockAlign);
            MC_DEBUG("block align %d", blockAlign);
        }

        int32_t channelCnt;
        if (mInputFormat->getInt32(MEDIA_ATTR_CHANNEL_COUNT, channelCnt)) {
            params->setInt32(MEDIA_ATTR_CHANNEL_COUNT, channelCnt);
            MC_DEBUG("channel count %d", channelCnt);
        }

        int32_t bitRate;
        if (mInputFormat->getInt32(MEDIA_ATTR_BIT_RATE, bitRate)) {
            params->setInt32(MEDIA_ATTR_BIT_RATE, bitRate);
            MC_DEBUG("bit rate %d", bitRate);
        }

        int32_t isAdts;
        if (mInputFormat->getInt32(MEDIA_ATTR_IS_ADTS, isAdts)) {
            params->setInt32(MEDIA_ATTR_IS_ADTS, isAdts);
            MC_DEBUG("is adts %d", isAdts);
        }

        int32_t profile;
        if (mInputFormat->getInt32(MEDIA_ATTR_CODECPROFILE, profile)) {
            params->setInt32(MEDIA_ATTR_CODECPROFILE, profile);
            MC_DEBUG("profile %d", profile);
        }

        const char *mime = NULL;
        if (mInputFormat->getString(MEDIA_ATTR_MIME, mime)) {
            params->setString(MEDIA_ATTR_MIME, mime);
            MC_DEBUG("mime %s", mime);
        }
    }
    EXIT_AND_RETURN(true);
}

mm_status_t MediaCodecComponent::flushInternal() {
    ENTER();
    if (mState != STARTED) {
        MC_WARNING("abnormal seek in state %d, just return success", mState);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    int ret = MediaCodec::MEDIACODEC_OK;
    if (mCodec) {
        ret = mCodec->flush();
        // TODO
        //save csd data;
    }

    if (ret != MediaCodec::MEDIACODEC_OK) {
        MC_ERROR("fail to flush decoder\n");
        handleError();
        EXIT_AND_RETURN(MM_ERROR_INVALID_STATE);
    }

    MC_INFO("codec input buffer sequence is %d", mCodecInputSequence);
    MC_INFO("codec output buffer sequence is %d", mCodecOutputSequence);
    mGeneration++;
    mCodecInputSequence = 0;
    mCodecOutputSequence = 0;

    mCodecInputTimeStamp = -1ll;
    mCodecOutputTimeStamp = 0;

    mStreamEOS = false;

    clearBuffer();
    // TODO store csd data here

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void MediaCodecComponent::onFlush(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mm_status_t status;

    status = flushInternal();

    asyncFuncExit(kEventFlushComplete, status, 0, rspId);

    EXIT();
}

void MediaCodecComponent::onSetPeerComponent(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    int type = (int )param1;
    Component *component = (Component*)param2;
    MediaType mediaType = mIsAudio ? kMediaTypeAudio : kMediaTypeVideo;
    mm_status_t status = MM_ERROR_SUCCESS;

    if (!component) {
        MC_WARNING("add NULL component");
        asyncFuncExit(kEventInfoExt, MM_ERROR_INVALID_PARAM, 0, rspId);
        EXIT();
    }

    if (type == MCC_SOURCE) {
        MC_INFO("set source component(%s)\n", component->name());
        mReader = component->getReader(mediaType);

        if (!readSourceMeta()) {
            status = MM_ERROR_UNKNOWN;
            MC_WARNING("fail to get meta from source");
        }
    } else if (type == MCC_SINK) {
        MC_INFO("set sink component(%s)\n", component->name());
        mWriter = component->getWriter(mediaType);
        if (!writeSinkMeta()) {
            status = MM_ERROR_UNKNOWN;
            MC_WARNING("fail to write sink meta");
        }
    }

    /*
    if (mReader && mWriter && mCodec)
        scheduleCodecActivity();
    */

    asyncFuncExit(kEventInfoExt, status, 0, rspId);
    EXIT();
}

void MediaCodecComponent::onCodecActivity(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    int32_t generation;
    int32_t input_buffers = 0;
    int32_t output_buffers = 0;
    int32_t newMemory = 0;

    YunOSMediaCodec::Notify *notify = (YunOSMediaCodec::Notify *)param2;

    if (!notify) {
        MC_WARNING("NULL notify msg\n");
        goto activity_out;
    }

    if (!notify->findInt32("generation", &generation)) {
        MC_WARNING("unknown activity notify from codec, no seq number\n");
        goto activity_out;
    }

    if (generation != mGeneration) {
        MC_WARNING("out of date activity notify from codec\n");
        goto activity_out;
    }

    notify->findInt32("num-input-buffers", &input_buffers);
    notify->findInt32("num-output-buffers", &output_buffers);
    notify->findInt32("codec-memory-cost", &newMemory);
    if (mReportMemory != newMemory) {
        MC_INFO("codec-memory-cost %d", newMemory);
        mReportMemory = newMemory;
        asyncFuncExit(kEventInfo, kEventCostMemorySize, mReportMemory, 0);
    }

    MC_INFO("available codec buffer num (in/out)%d/%d, codec buffer seq (in/out)%d/%d",
           input_buffers, output_buffers, mCodecInputSequence, mCodecOutputSequence);

    while (!mPaused && input_buffers--) {
        if (!handleInputBuffer())
            break;
    }

    while (!mPaused && output_buffers--) {
        if (!handleOutputBuffer())
            break;
    }

activity_out:
    delete notify;
    mNotify = NULL;
    mCodecActivityPending = false;
    scheduleCodecActivity();
    EXIT();
}

void MediaCodecComponent::asyncFuncExit(Component::Event event, int param1, int param2, uint32_t rspId) {
    ENTER();
    if (rspId) {
        postReponse(rspId, param1, NULL);
        EXIT();
    }

    MMParamSP param;

    notify(event, param1, param2, param);
    EXIT();
}

void MediaCodecComponent::onSourceBufferAvailable(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    EXIT();
}


void MediaCodecComponent::onPause(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    mPaused = true;
    MC_INFO("paused...");
    asyncFuncExit(kEventPaused, MM_ERROR_SUCCESS, 0, rspId);
    EXIT();
}

mm_status_t MediaCodecComponent::ensureCodec()
{
    if (!mCodec) {
        mm_status_t err;
        const char *mime = NULL;
        int isVideoRawData = 0;
        MediaCodec *codec;

        if (!mInputFormat->getString(MEDIA_ATTR_MIME, (const char *&)mime)) {
            MC_ERROR("error, absense of mime parameter");
            EXIT_AND_RETURN(MM_ERROR_NOT_INITED);
        }

        mInputFormat->getInt32(MEDIA_ATTR_VIDEO_RAW_DATA, isVideoRawData);

        mInitName = mime;
        const char *mimeConvert = mime;
        if (isVideoRawData && !mIsAudio) {
            const char *componentName = parseSoftwareComponentName(mIsEncoder, mime);
            MC_INFO("video codec is required to generate raw data for client");
            codec = MediaCodec::CreateByComponentName(componentName, &err);
        } else {
            // convert audio/amr to audio/3gpp for container decoder
            if (!strcmp(mime, "audio/amr")) {
                mimeConvert = "audio/3gpp";
            }
            int64_t rid = -1;
            if (mInputFormat->getInt64(MEDIA_ATTR_CODEC_RESOURCE_ID, rid) && rid > 0) {
                INFO("resource id is %" PRId64 "", rid);
                codec = MediaCodec::CreateByType(mime, mIsEncoder, &err /*, rid */);
            } else
                codec = MediaCodec::CreateByType(mimeConvert, mIsEncoder, &err);
        }

        if (err != MM_ERROR_SUCCESS || codec == NULL) {
            if(codec){
                codec->release();
                delete codec;
                codec = NULL;
            }
            MC_ERROR("fail to create MediaCodec, mime is %s", mimeConvert);
            EXIT_AND_RETURN(MM_ERROR_NOT_INITED);
        }

        if (!strcmp(mimeConvert, "video/avc") || !strcmp(mimeConvert, "video/hevc"))
            mIsNal = true;

        mCodec.reset(codec);
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void MediaCodecComponent::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    if (mState != INITIALIZED) {
        MC_ERROR("Not valid, codec is in state %d\n", mState);
        asyncFuncExit(kEventPrepareResult, MM_ERROR_IVALID_OPERATION, 0, rspId);
        EXIT();
    }

    if (!mReader || !mWriter) {
        MC_ERROR("No source or sink, cannot prepare");
        asyncFuncExit(kEventPrepareResult, MM_ERROR_INVALID_PARAM, 0, rspId);
        EXIT();
    }

    mState = PREPARED;

    /*
    if (mReader && mWriter && codec)
        scheduleCodecActivity();
    */
    mm_status_t ret = ensureCodec();

    asyncFuncExit(kEventPrepareResult, ret, 0, rspId);

    EXIT();
}

void MediaCodecComponent::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mm_status_t status = MM_ERROR_SUCCESS;

    if (mState == STARTED) {
        if (mPaused)
            mResumePending = true;
    } else if (mState == PREPARED || mState == STOPED) {
        if (mPreviousStop) {
            status = releaseCodec();
            mPreviousStop = false;
        }
        if (status != MM_ERROR_SUCCESS) {
            MC_ERROR("release codec fail %d", status);
        }
        mm_status_t ret = ensureCodec();
        if (ret != MM_ERROR_SUCCESS) {
            asyncFuncExit(kEventStartResult, ret, 0, rspId);
            EXIT();
        }

        uint32_t flags = 0;
        MediaMetaSP params = MediaMeta::create();
        mComponentName = mCodec->getName();
        if (mIsEncoder)
            mComponentName.append(" encoder");
        else
            mComponentName.append(" decoder");

        mInputFormat->getInt32("flags", (int32_t &)flags);

        if (mIsEncoder)
            flags |= MediaCodec::kFlagIsEncoder;

        if (!setupMediaCodecParam(params)) {
            mReader.reset();
            mWriter.reset();
            mCodec->release();
            mCodec.reset();
            handleError(MM_ERROR_UNSUPPORTED);
            asyncFuncExit(kEventStartResult, MM_ERROR_UNSUPPORTED, 0, rspId);
            EXIT();
        }

        // save csd here for discontinuty seamless switch
        void *handle = (void*)mNativeWindow;
        if (mSurfaceTexture) {
            MC_INFO("use media texture %x", mSurfaceTexture);
            handle = mSurfaceTexture;
            flags |= MediaCodec::CONFIGURE_FLAG_SURFACE_TEXTURE;
        }

        ret = mCodec->configure(params, (WindowSurface*)handle, flags);
        if (ret) {
            mReader.reset();
            mWriter.reset();
            mCodec->release();
            mCodec.reset();
            handleError(MM_ERROR_OP_FAILED);
            MC_ERROR("fail to configure MediaCodec");
            asyncFuncExit(kEventStartResult, MM_ERROR_OP_FAILED, 0, rspId);
            EXIT();
        }

        mPaused = false;
        mStreamEOS = false;
        mResumePending = false;
        mGeneration++;
        mCodecActivityPending = false;

        ret = mCodec->start();
        if (ret) {
            mCodec->stop();
            mCodec->release();
            //delete mCodec.get();
            mCodec.reset();
            handleError(MM_ERROR_OP_FAILED);
            MC_ERROR("fail to start MediaCodec");
            status = MM_ERROR_OP_FAILED;
            asyncFuncExit(kEventStartResult, status, 0, rspId);
            EXIT();
        }
        mInputBufferCount = mCodec->getInputBufferCount();
        mOutputBufferCount = mCodec->getOutputBufferCount();

        mInputBufferData.resize(mInputBufferCount);
        mInputBufferSize.resize(mInputBufferCount);

        mOutputBufferData.resize(mOutputBufferCount);
        mOutputBufferSize.resize(mOutputBufferCount);

        size_t codecBufSize;
        uint8_t *codecBuf;
        for (int32_t i = 0; i < mInputBufferCount; i++) {
            codecBuf = mCodec->getInputBuffer((size_t)i, &codecBufSize);
            mInputBufferData.at(i) = codecBuf;
            mInputBufferSize.at(i) = codecBufSize;
        }

        for (int32_t i = 0; i < mOutputBufferCount; i++) {
            codecBuf = mCodec->getOutputBuffer((size_t)i, &codecBufSize);
            mOutputBufferData.at(i) = codecBuf;
            mOutputBufferSize.at(i) = codecBufSize;
        }
        clearBuffer();
    } else {
        mReader.reset();
        mWriter.reset();
        status = MM_ERROR_OP_FAILED;
        asyncFuncExit(kEventStartResult, status, 0, rspId);
        EXIT();
    }

    if (mResumePending) {
        asyncFuncExit(kEventResumed, status, 0, rspId);
        mResumePending = false;
    } else
        asyncFuncExit(kEventStartResult, status, 0, rspId);

    mPaused = false;
    mStreamEOS = false;

    scheduleCodecActivity();

    if (status == MM_ERROR_SUCCESS)
        mState = STARTED;

    EXIT();
}

void MediaCodecComponent::onResume(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mm_status_t status = MM_ERROR_SUCCESS;

    if (mState != STARTED) {
        status = MM_ERROR_INVALID_STATE;
        MC_INFO("onResume() while not started, signal err %d", status);
        asyncFuncExit(kEventResumed, status, 0, rspId);
    }

    if (!mPaused) {
        MC_INFO("onResume() while not pasued, just signal resume complete");
        asyncFuncExit(kEventResumed, status, 0, rspId);
        EXIT();
    }

    mResumePending = true;
    mPaused = false;

    scheduleCodecActivity();

    mResumePending = false;

    asyncFuncExit(kEventResumed, MM_ERROR_SUCCESS, 0, rspId);
}

void MediaCodecComponent::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    mm_status_t status;

    status = stopInternal();
    mPreviousStop = true;

    asyncFuncExit(kEventStopped, status, 0, rspId);
    EXIT();
}

mm_status_t MediaCodecComponent::stopInternal() {
    ENTER();

    int ret;
    mm_status_t status = MM_ERROR_SUCCESS;
    MC_INFO("codec input buffer sequence is %d", mCodecInputSequence);
    MC_INFO("codec output buffer sequence is %d", mCodecOutputSequence);

    if (mState == STOPED) {
        MC_INFO("already stopped, reset finish");
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    if (!mCodec) {
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
        mState = STOPED;
    }

    ret = mCodec->flush();
    if (ret != MediaCodec::MEDIACODEC_OK) {
        MC_ERROR("fail to flush codec");
        status = MM_ERROR_INVALID_STATE;
    }

    ret = mCodec->stop();
    if (ret != MediaCodec::MEDIACODEC_OK) {
        MC_ERROR("fail to stop codec");
        status = MM_ERROR_INVALID_STATE;
    }
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    if (mNativeWindow)
        mNativeWindow->destroyBuffers();
#endif

    mGeneration++;
    mCodecInputSequence = 0;
    mCodecOutputSequence = 0;
    mCodecInputTimeStamp = -1ll;
    mCodecOutputTimeStamp = 0;
    clearBuffer();
    mInputBufferData.clear();
    mInputBufferSize.clear();
    mOutputBufferData.clear();
    mOutputBufferSize.clear();
    mIsNal = false;
    //mIsAVCcType = false;//No need to reset
    mStreamEOS = false;
    mPaused = true;
    mReportMemory = 0;

    mState = STOPED;
    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecComponent::releaseCodec()
{
    mm_status_t status = MM_ERROR_SUCCESS;
    if (mCodec) {
        int ret = mCodec->release();
        if (ret != MediaCodec::MEDIACODEC_OK) {
            MC_WARNING("codec released.");
            status = MM_ERROR_INVALID_STATE;
        }
        mCodec.reset();
    }
    EXIT_AND_RETURN(status);
}

void MediaCodecComponent::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mReader.reset();
    mWriter.reset();

    mm_status_t status;

    status = stopInternal();

    if (status != MM_ERROR_SUCCESS) {
        MC_ERROR("stop Internal fail in reset sequence %d", status);
        //asyncFuncExit(kEventResetComplete, status, 0, rspId);
    }
    status = releaseCodec();
    if (status != MM_ERROR_SUCCESS) {
        MC_ERROR("release codec fail %d", status);
    }

    mSurfaceTexture = NULL;

    asyncFuncExit(kEventResetComplete, MM_ERROR_SUCCESS, 0, rspId);
    EXIT();
}

#define SETPARAM_BEGIN()\
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {\
        const MediaMeta::MetaItem & item = *i;

#define CHECK_TYPE(_val_type)\
        if ( item.mType != MediaMeta::_val_type ) {\
            MC_WARNING("invalid type for %s, expect: %s, real: %d\n", item.mName, #_val_type, item.mType);\
            continue;\
        }

#define SETPARAM_Float(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Float);\
        mInputFormat->setFloat(_key_name, item.mValue.f);\
        MC_INFO("setparam key: %s, val: %f\n", item.mName, item.mValue.f);\
        continue;\
    }

#define SETPARAM_I32(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int32);\
        mInputFormat->setInt32(_key_name, item.mValue.ii);\
        MC_INFO("setparam key: %s, val: %d\n", item.mName, item.mValue.ii);\
        continue;\
    }

#define SETPARAM_POINTER(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Pointer);\
        mInputFormat->setPointer(_key_name, item.mValue.ptr);\
        MC_INFO("setparam key: %s, val: %p\n", item.mName, item.mValue.ptr);\
        continue;\
    }

#define SETPARAM_BUFFER(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_ByteBuffer);\
        mInputFormat->setByteBuffer(_key_name, item.mValue.buf.data, item.mValue.buf.size);\
        MC_INFO("setparam key: %s, size: %zu\n", item.mName, item.mValue.buf.size);\
        continue;\
    }

#define SETPARAM_STRING(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_String);\
        mInputFormat->setString(_key_name, item.mValue.str);\
        MC_INFO("setparam key: %s, val: %s\n", item.mName, item.mValue.str);\
        continue;\
    }

#define SETPARAM_END() }


mm_status_t MediaCodecComponent::setParameterForAudio(const MediaMetaSP & meta) {
    ENTER();

    SETPARAM_BEGIN()
        SETPARAM_I32(MEDIA_ATTR_BIT_RATE)
        SETPARAM_STRING(MEDIA_ATTR_MIME)
    SETPARAM_END()

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


void MediaCodecComponent::onSetParameter(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId) {
    ENTER();

    MCCMMParamBase* paramRef = DYNAMIC_CAST<MCCMMParamBase*>(param3.get());
    const MediaMetaSP & meta = paramRef->mMeta;
    if ( !meta ) {
        MC_ERROR("invalid params\n");
        return;
    }

    mm_status_t status = MM_ERROR_SUCCESS;

    // For the sake of simple, don't support param setting after codec created
    if (mState != INITIALIZED) {
        MC_WARNING("not in INITIALIZED state, set parameter fail");
        postReponse(rspId, MM_ERROR_INVALID_STATE, NULL);
        EXIT();
    }

    if (mIsAudio) {
        status = setParameterForAudio(meta);
        postReponse(rspId, status, NULL);
        EXIT();
    }

    //params for video
    SETPARAM_BEGIN()
        SETPARAM_I32(MEDIA_ATTR_WIDTH)
        SETPARAM_I32(MEDIA_ATTR_HEIGHT)
        SETPARAM_POINTER(MEDIA_ATTR_VIDEO_SURFACE)
        SETPARAM_POINTER(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE)
        SETPARAM_BUFFER(MEDIA_ATTR_EXTRADATA0)
        SETPARAM_BUFFER(MEDIA_ATTR_EXTRADATA1)
        SETPARAM_STRING(MEDIA_ATTR_MIME)
        SETPARAM_I32(MEDIA_ATTR_VIDEO_RAW_DATA)
        SETPARAM_I32(MEDIA_ATTR_ROTATION)
        SETPARAM_I32(MEDIA_ATTR_BIT_RATE)
        SETPARAM_Float(MEDIA_ATTR_TIME_LAPSE_FPS)
        SETPARAM_I32(MEDIA_ATTR_TIME_LAPSE_ENABLE)
    SETPARAM_END()

    postReponse(rspId, status, NULL);
    EXIT();
}

void MediaCodecComponent::onAcquireSourceBuffer(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    mPendingAcquireBuffer = false;
    acquireSourceBuffer();
    EXIT();
}

void MediaCodecComponent::handleError(int32_t err) {
    // notify error
    MMParamSP param;
    notify(kEventError, err, 0, param);

    if (err == MM_ERROR_CODEC_ERROR) {
        ERROR("codec error, pause data flow");
        mPaused = true;
    }
}

bool MediaCodecComponent::releaseMediaBuffer(MediaBuffer *mediaBuf) {

    size_t *pIndex = NULL;
    if (!(mediaBuf->getBufferInfo((uintptr_t *)&pIndex, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }

    delete []pIndex;

    return true;
}

void MediaCodecComponent::releaseByteBuffer(MediaMeta::ByteBuffer *byteBuf) {
    if (byteBuf != NULL) {
        DEBUG("releaseByteBuffer: byteBuf->data %p", byteBuf->data);
        delete []byteBuf->data;
        byteBuf->data = NULL;

        delete byteBuf;

    }
}


} // YUNOS_MM
