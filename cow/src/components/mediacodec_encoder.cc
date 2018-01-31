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
#include "OMX_Video.h"

#include <multimedia/mm_debug.h>
#include "media_notify.h"

#include "multimedia/media_buffer.h"
#include "multimedia/codec.h"
#include "multimedia/media_attr_str.h"
#include "MetadataBufferType.h"
#include "make_csd.h"
#include "multimedia/mm_surface_compat.h"
#include "mediacodec_encoder.h"


#undef DEBUG_DUMP
//#define DEBUG_DUMP

MM_LOG_DEFINE_MODULE_NAME("MCE");

#define RETRY_COUNT 500

#define SLOW_MOTION_FPS_COUNT 120
namespace YUNOS_MM {

using namespace YunOSMediaCodec;

class MetaMediaBufferObject : public MediaMeta::MetaBase {
  public:
    virtual ~MetaMediaBufferObject() {DEBUG("this %p, mediaBuffer %p", this, sp.get());};
    virtual void* getRawPtr() { return sp.get();};
    MediaBufferSP &getMediaBuffer() {return sp;}
    MediaBufferSP sp;
};
typedef MMSharedPtr < MetaMediaBufferObject > MetaMediaBufferSP;

//#define MC_VERBOSE MC_WARNING
#undef DEBUG_STREAM

#define ENTER() MC_VERBOSE(">>>\n")
#define EXIT() do {MC_VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {MC_VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)
static const char * MMMSGTHREAD_NAME = "MediaBufferThread";

#define SETPARAM_BEGIN()\
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {\
                const MediaMeta::MetaItem & item = *i;

#define SETPARAM_END() }

MediaCodecEnc::MediaCodecEnc(const char *mime)
    : MediaCodecComponent(mime), mBufferThread(NULL) {
    ENTER();
    mIsEncoder = true;

    /*
     * As we use MediaMeta instead of mWidth/mHeight... indiviual members,
     * create meta items and provide init values here
     */

    if (!mIsAudio) {
        mInputFormat->setInt32(MEDIA_ATTR_WIDTH, 0);
        mInputFormat->setInt32(MEDIA_ATTR_HEIGHT, 0);
        mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, 30);
        mInputFormat->setInt32(MEDIA_ATTR_BIT_RATE, 0);
        mInputFormat->setInt32("intraInterval", 2);
        mInputFormat->setString(MEDIA_ATTR_AVC_PROFILE, MEDIA_ATTR_AVC_PROFILE_BASELINE);
        mInputFormat->setString(MEDIA_ATTR_AVC_LEVEL, MEDIA_ATTR_AVC_LEVEL4);
    }

    mInputFormat->setString(MEDIA_ATTR_MIME, mime);

    EXIT();
}

MediaCodecEnc::~MediaCodecEnc() {
    ENTER();

    delete mBufferThread;

    EXIT();
}

mm_status_t MediaCodecEnc::setParameter(const MediaMetaSP & meta) {
    ENTER();

    SETPARAM_BEGIN()
        if ( !strcmp(item.mName, MEDIA_ATTR_IDR_FRAME) ) {
            MC_VERBOSE("now accept the request idr request:%s\n", item.mValue.str);
            if ( !strcmp(item.mValue.str, "yes") ) {
                if ( mCodec )
                    mCodec->requestIDRFrame();
                else
                    MC_VERBOSE("find the mCodec NULL");
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    SETPARAM_END()

    // For the sake of simple, don't support param setting after codec created
    if (mCodec) {
        MC_WARNING("should not set parameter after codec created");
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    // TODO Add encoder specific parameter

    mm_status_t status = MediaCodecComponent::setParameter(meta);

    EXIT_AND_RETURN(status);
}

bool MediaCodecEnc::handleInputBuffer() {
    ENTER();
    //If input buffer has received eos flag, no need to read input buffer again.
    if (mFormatChangePending || !mCodec || mStreamEOS) {
        EXIT_AND_RETURN(false);
    }

    size_t index = -1;
    int ret = mCodec->dequeueInputBuffer(&index);
    if (ret != MediaCodec::MEDIACODEC_OK) {
        if (ret != MediaCodec::MEDIACODEC_INFO_TRY_AGAIN_LATER) {
            MC_ERROR("encoder(%s) fail to dequeue input buffer w/h error %d\n", mComponentName.c_str(), ret);
            handleError(MM_ERROR_CODEC_ERROR);
        }
        EXIT_AND_RETURN(false);
    }


    if ((int)index >= mInputBufferCount) {
        MC_ERROR("bad input buffer index %d, input buffer count %d", index, mInputBufferCount);
        handleError();
        EXIT_AND_RETURN(false);
    }

    mAvailableCodecBuffers.push_back(index);

    // release mediaBuf, no need to retain it
    //mMediaBuffers.at(index).mediaBuf = mediaBuf;
    mMediaBuffers.at(index).shadowBuf.reset();

    while (submitBufferToCodec())
    {
        ;
    }

    acquireSourceBuffer();
    EXIT_AND_RETURN(true);
}

bool MediaCodecEnc::handleOutputBuffer() {
    ENTER();
    size_t index = -1;
    size_t offset, size;
    uint32_t flags;
    int64_t timeUs;

    if (!mCodec) {
        MC_WARNING("codec is not created");
        EXIT_AND_RETURN(false);
    }

    int ret = mCodec->dequeueOutputBuffer(
                      &index,
                      &offset,
                      &size,
                      &timeUs,
                      &flags);

    MC_INFO("mCodecOutputSequence: %06d, dequeue output buffer from mCodec, index(%d) offset(%d) size(%d) timeUs(%" PRId64 ") flags(%xh), ret(%d)",
            mCodecOutputSequence, index, offset, size, timeUs, flags, ret);

    if (ret == MediaCodec::MEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        MC_INFO("Codec output buffers change");

        mOutputBufferCount = mCodec->getOutputBufferCount();
        mOutputBufferData.resize(mOutputBufferCount);
        mOutputBufferSize.resize(mOutputBufferCount);

        for (int32_t i = 0; i < mOutputBufferCount; i++) {
            size_t codecBufSize;
            uint8_t *codecBuf;

            codecBuf = mCodec->getOutputBuffer((size_t)i, &codecBufSize);
            mOutputBufferData.at(i) = codecBuf;
            mOutputBufferSize.at(i) = codecBufSize;
        }

        EXIT_AND_RETURN(true);
    } else if (ret == MediaCodec::MEDIACODEC_INFO_FORMAT_CHANGED) {
        MediaMetaSP vec = mCodec->getOutputFormat();
        if (!vec) {
            MC_WARNING("fail to get output format");
            EXIT_AND_RETURN(true);
        }

        // TODO take crop and rotation_degree into account
        int32_t width = 0;
        int32_t height = 0;

        vec->getInt32(MEDIA_ATTR_WIDTH, width);
        vec->getInt32(MEDIA_ATTR_HEIGHT, height);

        // report video size change
        MMParamSP mmparam;
        mmparam.reset(new MMParam);
        mmparam->writeInt32(width);
        mmparam->writeInt32(height);

        //notify(kEventInfo, kEventVideoSizeChanged, 0, mmparam);
        EXIT_AND_RETURN(true);
    } else if (ret == MediaCodec::MEDIACODEC_INFO_DISCONTINUITY) {
        MC_WARNING("MediaCodec return output DISCONTINUITY, just ignore");
        EXIT_AND_RETURN(true);
    }

    if (ret != MediaCodec::MEDIACODEC_OK) {
        if (ret != MediaCodec::MEDIACODEC_INFO_TRY_AGAIN_LATER) {
            MC_ERROR("dequeue output buffer fail (err is %d)", ret);
            handleError(MM_ERROR_CODEC_ERROR);
        }
        EXIT_AND_RETURN(false);
    }

    if ((int)index >= mOutputBufferCount) {
        MC_ERROR("bad output buffer index %d", index);
        handleError();
        EXIT_AND_RETURN(false);
    }

    mCodecOutputSequence++;
    mCodecOutputTimeStamp = timeUs;

    MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    MediaMetaSP meta = mediaBuf->getMediaMeta();

    // meta->setInt32("offset", offset);
    // meta->setInt32("size", size);
    meta->setInt32("generation", mGeneration);

    {
        int32_t bufOffset = 64;
        int32_t bufStride = bufOffset + size;
        uint8_t *rawData = new uint8_t[bufStride]; // FIXME: malloc additional 64 bytes for protection
        bool ret;

        memcpy(rawData+bufOffset, mOutputBufferData.at(index), size);

        ret = mediaBuf->addReleaseBufferFunc(MediaCodecComponent::releaseMediaBuffer);

        // mediaBuf->setSize(sizeof(size_t));
        ret &= mediaBuf->setBufferInfo((uintptr_t *)&rawData, &bufOffset, &bufStride, 1);
        if (!ret)
            MC_WARNING("error when set mediaBuffer parameter");

        mediaBuf->setPts(timeUs);
        //mediaBuf->setDts(timeUs);
        mediaBuf->setSize(bufStride);
        mediaBuf->setDuration(0);

        mediaBuf->setMonitor(mMonitorFPS);

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            MC_INFO("encoder receive EOS");
            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
            mPaused = true;
        }

        if (flags & MediaCodec::BUFFER_FLAG_CODECCONFIG) {
            MC_INFO("encoder output codec data, size is %d", size);
            mediaBuf->setFlag(MediaBuffer::MBFT_CodecData);

            StreamMakeCSDSP csdMaker;
            csdMaker.reset(new StreamCSDMaker());

            if (csdMaker->getExtraDataFromMediaBuffer(mediaBuf)!= MM_ERROR_SUCCESS) {
                MC_ERROR("get ExtraData failed");

                handleError();
                EXIT_AND_RETURN(false);
            }
        } else if (!mIsAudio) {
            if (mDecodingTimeQueue.empty()) {
                MC_WARNING("decode time queue is empty, use pts instead");
                mediaBuf->setDts(timeUs);
            } else {
                int64_t dts = mDecodingTimeQueue.front();
                mediaBuf->setDts(dts);
                mDecodingTimeQueue.pop_front();
            }
        } else {
            //for audio
            mediaBuf->setDts(timeUs);
        }

        if (flags & MediaCodec::BUFFER_FLAG_SYNCFRAME) {
            MC_INFO("encoder output key frame");
            mediaBuf->setFlag(MediaBuffer::MBFT_KeyFrame);
        }

#ifdef DEBUG_DUMP
        dump(rawData + bufOffset, size);
#endif
    }

    ret = mCodec->releaseOutputBuffer(index, false);
    if (ret != MediaCodec::MEDIACODEC_OK) {
        MC_WARNING("fail to release output buffer(%d) to Codec", index);
    }

/*
    if (mResumePending) {
        mResumePending = false;
        asyncFuncExit(kEventResumed, MM_ERROR_SUCCESS, 0, mResumeRspId);
        mResumeRspId = 0;
    }
*/

    if (!mWriter) {
        MC_WARNING("sink is not ready");
        //mediaBuf.reset();
        //mCodec->releaseOutputBuffer(index, false);
        EXIT_AND_RETURN(true);
    }

    mm_status_t status = mWriter->write(mediaBuf);
    if (status != MM_ERROR_SUCCESS &&
        status != MM_ERROR_AGAIN &&
        status != MM_ERROR_ASYNC) {
        MC_ERROR("encoder fail to write Sink");
        handleError();
    }

    EXIT_AND_RETURN(true);
}

void MediaCodecEnc::scheduleAcquire() {
    ENTER();
    // do nothing, encoder has BufferThread to read source
    EXIT();
}

mm_status_t MediaCodecEnc::readSourceBuffer(MediaBufferSP &buf) {
    ENTER();
    MediaBufferSP sourceBuf;

    if (!mReader || !mBufferThread) {
        MC_WARNING("source is not ready");
        EXIT_AND_RETURN(MM_ERROR_INVALID_STATE);
    }

    // encoder directly push source media buffer info in mAvailableSourceBuffers
    sourceBuf.reset();
    buf = sourceBuf;

    if (mAvailableSourceBuffers.empty())
        EXIT_AND_RETURN(MM_ERROR_AGAIN);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

bool MediaCodecEnc::setupMediaCodecParam(MediaMetaSP &params) {
    ENTER();

    if (!(MediaCodecComponent::setupMediaCodecParam(params)))
        EXIT_AND_RETURN(false);

    if (!mIsAudio) {
        int32_t width = 0, height = 0, bitrate = 0, intraInterval = 10, colorFormat;
        int32_t fps = 0;

        mInputFormat->getInt32(MEDIA_ATTR_WIDTH, width);
        mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, height);

        if (width == 0 || width > MCC_MAX_DIMENSION ||
            height == 0 || height > MCC_MAX_DIMENSION) {
            MC_ERROR("invalid resolution %dx%d", width, height);
            EXIT_AND_RETURN(false);
        }

        mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
        if (fps < 0 || fps > MCC_MAX_FPS) {
            MC_ERROR("invalid fps %d", fps);
            EXIT_AND_RETURN(false);
        }

        mInputFormat->getInt32(MEDIA_ATTR_BIT_RATE, bitrate);
        if (bitrate <= 0) {
            if (width == 3840 && height == 2160) {
                bitrate = 55000000;
            } else {
                bitrate = width * height * (int)fps * 8 / 30;
            }
            mInputFormat->setInt32(MEDIA_ATTR_BIT_RATE, bitrate);
            MC_DEBUG("bitrate is not set, use defaut bit rate %d kbps", bitrate / 1024);
        }

        mInputFormat->getInt32("intraInterval", intraInterval);
        if (intraInterval <= 0) {
            intraInterval = 10;
            mInputFormat->setInt32("intraInterval", intraInterval);
            MC_INFO("intraInterval is not set, use defaut value %d", intraInterval);
        }

        int32_t slowMotionEnable = 0;
        mInputFormat->getInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, slowMotionEnable);

        float timeLapseFps = 0;
        mInputFormat->getFloat(MEDIA_ATTR_TIME_LAPSE_FPS, timeLapseFps);
        if (slowMotionEnable &&
                timeLapseFps < 120 + 0.000001 &&
                timeLapseFps > 120 - 0.000001) {
            // Using defalult bitrate for slow motion
            bitrate = 20000000;
            intraInterval = 1;
            MC_DEBUG("slowMotionEnable %d, fps %.2f, set bitrate to 20000000", slowMotionEnable, timeLapseFps);
        }

        // TODO make more parameter configurable

        MC_INFO("video encode parameters:\n"
              "bitrate %d kbps\n"
              "width %d\n"
              "height %d\n"
              "bitreteMode %d\n"
              "frameRate %d\n"
              "intraInterval %d\n",
              bitrate/1024, width, height, OMX_Video_ControlRateVariable, fps, intraInterval);

        params->setInt32(MEDIA_ATTR_STORE_META_INPUT, 1);
        params->setInt32(MEDIA_ATTR_STORE_META_OUTPUT, 0);
        params->setInt32(MEDIA_ATTR_PREPEND_SPS_PPS, 0);
        params->setInt64(MEDIA_ATTR_REPEAT_FRAME_DELAY_US, -1);
        params->setInt32(MEDIA_ATTR_BIT_RATE, bitrate);
        params->setInt32(MEDIA_ATTR_STRIDE, width);
        params->setInt32(MEDIA_ATTR_SLICE_HEIGHT, height);
        params->setInt32(MEDIA_ATTR_BITRATE_MODE,
                         (int32_t)OMX_Video_ControlRateVariable);
        params->setFloat(MEDIA_ATTR_FRAME_RATE, fps);

        colorFormat = 0x7F000789;
        if (mInputFormat->getInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat)) {
            MC_DEBUG("get color format from source 0x%0x\n", colorFormat);
        }
        params->setInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat);

        params->setInt32(MEDIA_ATTR_I_FRAME_INTERVAL, intraInterval);
        params->setInt32(MEDIA_ATTR_AVC_PROFILE,
                         (int32_t)OMX_VIDEO_AVCProfileBaseline);
        params->setInt32(MEDIA_ATTR_AVC_LEVEL,
                         (int32_t)OMX_VIDEO_AVCLevel4);
        params->setInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, slowMotionEnable);
        params->setFloat(MEDIA_ATTR_TIME_LAPSE_FPS, timeLapseFps);
    }
    EXIT_AND_RETURN(true);
}


void MediaCodecEnc::onFlush(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    // TODO add encoder flush

    if (!mIsAudio)
        mDecodingTimeQueue.clear();

    MediaCodecComponent::onFlush(param1, param2, rspId);
    EXIT();
}

bool MediaCodecEnc::writeSinkMeta() {

    mOutputFormat = mInputFormat->copy();
    mWriter->setMetaData(mOutputFormat);
    return true;
}

bool MediaCodecEnc::readSourceMeta() {
    ENTER();
    int32_t width, height;

    MediaMetaSP meta;

    if (!mReader)
        EXIT_AND_RETURN(false);

    meta = mReader->getMetaData();
    if (!meta)
        EXIT_AND_RETURN(false);

    mInputFormat->dump();

    if (!mIsAudio) {
        // int32_t bitrate = 0;
        int32_t fps = 30;
        int32_t colorFormat = 0;
        float srcFps;

        // sanity check
        mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
        if (fps <= 0 || fps > MCC_MAX_FPS) {
            mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
            MC_INFO("meta data has invalid fps, use default value %d", fps);
        }

        if (meta->getInt32(MEDIA_ATTR_WIDTH, width)) {
            MC_INFO("get meta data, width is %d", width);
            mInputFormat->setInt32(MEDIA_ATTR_WIDTH, width);
        }

        if (meta->getInt32(MEDIA_ATTR_HEIGHT, height)) {
            MC_INFO("get meta data, height is %d", height);
            mInputFormat->setInt32(MEDIA_ATTR_HEIGHT, height);
        }

        if (meta->getFloat(MEDIA_ATTR_FRAME_RATE, srcFps)) {
            fps = (int32_t) srcFps;
            MC_INFO("get meta data, frame rate is %d", fps);
            mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
        } else {
            MC_INFO("use default fps value %d", fps);
            mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
        }

        mInputFormat->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

        if (meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat)) {
            MC_INFO("get meta data, color foramt is %x", colorFormat);
            mInputFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, colorFormat);
        }

    } else {

        //merge mInputFormat into meta
        MediaMetaSP metaTmp = meta->copy();
        if (metaTmp && metaTmp->merge(mInputFormat)) {
            mInputFormat = metaTmp;
        } else {
            MC_ERROR("merge meta to mInputFormat failed\n");
            EXIT_AND_RETURN(false);
        }

        int32_t sr = -1;
        bool ret_ = mInputFormat->getInt32(MEDIA_ATTR_SAMPLE_RATE, sr);
        ASSERT(ret_);

        MediaMeta::Fraction timeBase = {1, 1000000};
        if (mInputFormat->getFraction(MEDIA_ATTR_TIMEBASE, timeBase.num, timeBase.denom)) {
            MC_INFO("get timebase from source, num %d, den %d", timeBase.num, timeBase.denom);
        } else {
            mInputFormat->setFraction(MEDIA_ATTR_TIMEBASE, timeBase.num, timeBase.denom);
            MC_INFO("get no timebase, using default timebase, num %d, den %d", timeBase.num, timeBase.denom);
        }

    }

    const char *mime = NULL;
    bool ret_ = mInputFormat->getString(MEDIA_ATTR_MIME, mime);
    ASSERT(ret_);
    CowCodecID codecId= mime2CodecId(mime);
    mInputFormat->setInt32(MEDIA_ATTR_CODECID, codecId);

    mInputFormat->dump();

    EXIT_AND_RETURN(true);
}

void MediaCodecEnc::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    // TODO encoder specific prepare
    MediaCodecComponent::onPrepare(param1, param2, rspId);
    EXIT();
}

void MediaCodecEnc::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    MediaCodecComponent::onStart(param1, param2, rspId);

    mMonitorFPS.reset(new TimeStatics(SLOW_MOTION_FPS_COUNT, 0, 0, "MCEMonitorFPS"));
    mm_status_t status;

    mBufferThread = new MediaBufferThread(mInitName.c_str());

    status = mBufferThread->init();
    if (status != MM_ERROR_SUCCESS) {
        MC_ERROR("buffer thread init fail");
        asyncFuncExit(kEventStartResult, status, 0, rspId);
        EXIT();
    }

    YunOSMediaCodec::Notify *notify = new YunOSMediaCodec::Notify;

    notify->setWhat(MCE_MSG_bufferAvailable);
    notify->registerThreadHandler(this);
    notify->setInt32("generation", mGeneration);

    mBufferThread->configure(mReader, notify);
    if (status != MM_ERROR_SUCCESS) {
        MC_ERROR("buffer thread configure fail");
        asyncFuncExit(kEventStartResult, status, 0, rspId);
        delete notify;
        EXIT();
    }

    status = mBufferThread->start();
    if (status != MM_ERROR_SUCCESS) {
        MC_ERROR("buffer thread start fail");
        asyncFuncExit(kEventStartResult, status, 0, rspId);
        delete notify;
        EXIT();
    }

    EXIT();
}

void MediaCodecEnc::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();


    // stop buffer thread after component stop which clear shadow buffer
    if (mBufferThread) {
        mBufferThread->stop();
        mBufferThread->uninit();
        delete mBufferThread;
        mBufferThread = NULL;
    }
    MediaCodecComponent::onStop(param1, param2, rspId);

    mMonitorFPS.reset();
    EXIT();
}

void MediaCodecEnc::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    if (mBufferThread)
        mBufferThread->reset();

    MediaCodecComponent::onReset(param1, param2, rspId);
    EXIT();
}

// notify from MediaBufferThread
void MediaCodecEnc::onSourceBufferAvailable(param1_type param1,
                                            param2_type param2,
                                            uint32_t rspId) {
    ENTER();
    int32_t generation;
    bool ret;
    int32_t offset, length;
    MediaBufferSP bufferSP;
    MediaMeta::MetaBaseSP objSP;
    BufferInfo info;
    uint8_t *sourceBuf = NULL;
    MediaBuffer *mediaBuf = NULL;

    YunOSMediaCodec::Notify *notify = (YunOSMediaCodec::Notify *)param2;

    if (!notify) {
        MC_WARNING("NULL notify msg\n");
        goto buffer_end;
    }

    if (!notify->findInt32("generation", &generation)) {
        MC_WARNING("unknown notify from source, no seq number\n");
        goto buffer_end;
    }

    if (generation != mGeneration) {
        MC_INFO("out of date notify from source (%d/%d)\n", generation, mGeneration);
        goto buffer_end;
    }

    if (!notify->findObject("mediabuffer", objSP) || !objSP) {
        MC_INFO("the notify doesn't contain media buffer\n");
        goto buffer_end;
    }

    if (!mIsAudio) {

        bufferSP =
            MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);

        mediaBuf = static_cast<MediaBuffer*>(objSP->getRawPtr());
        if (!mediaBuf ||
            (mediaBuf->type() != MediaBuffer::MBT_GraphicBufferHandle &&
            mediaBuf->type() != MediaBuffer::MBT_CameraSourceMetaData)) {
            MC_INFO("source media buffer is invalid");
            if (mediaBuf)
                MC_INFO("not suppoted media buffer type %d, signal eos", mediaBuf->type());
            else
                MC_INFO("mediaBuf is NULL, signal eos");
            bufferSP->setFlag(MediaBuffer::MBFT_EOS);
            info.mediaBuf = bufferSP;
            mAvailableSourceBuffers.push_back(info);
            goto buffer_end;
        }

        ret = mediaBuf->getBufferInfo((uintptr_t *)&sourceBuf, &offset, &length, 1);

        MC_VERBOSE("source media buffer info: sourceBuf %p, offset %d length %d", sourceBuf, offset, length);

        if (ret && sourceBuf && length) {
            int32_t size;
            uint8_t *destBuf;

            if (mediaBuf->type() == MediaBuffer::MBT_GraphicBufferHandle) {
                uint32_t type = kMetadataBufferTypeGrallocSource;
                size = length + sizeof(type);
                destBuf = new uint8_t[size];
                memcpy(destBuf, &type, sizeof(type));
                memcpy(destBuf + sizeof(type), sourceBuf + offset, length);
            } else if (mediaBuf->type() == MediaBuffer::MBT_CameraSourceMetaData) {
                size = length;
                destBuf = new uint8_t[size];
                memcpy(destBuf, sourceBuf + offset, length);
            }

            offset = 0;
            bufferSP->setBufferInfo((uintptr_t *)&destBuf, &offset, &size, 1);
            bufferSP->addReleaseBufferFunc(MediaCodecComponent::releaseMediaBuffer);
            bufferSP->setPts(mediaBuf->pts());
            bufferSP->setDts(mediaBuf->dts());
            bufferSP->setDuration(mediaBuf->duration());
            bufferSP->setSize(size);
        } else {
            MC_WARNING("cannot get source buffer info");
            //bufferSP->setFlag(MediaBuffer::MBFT_EOS);
        }
    } else {

        //Don't need to copy audio data here,
        //but we can keep input buffer until omx called emptybufferdone
        MetaMediaBufferObject* MediaBufferobj = DYNAMIC_CAST<MetaMediaBufferObject*>(objSP.get());
        if (!MediaBufferobj) {
            MC_INFO();
            goto buffer_end;
        }

        bufferSP = MediaBufferobj->getMediaBuffer();
        mediaBuf = bufferSP.get();
        if (!mediaBuf) {
            MC_INFO();
            goto buffer_end;
        }

        //TODO. buffer type MBT_ByteBuffer
    }

    if (mediaBuf->isFlagSet(MediaBuffer::MBFT_EOS)) {
        MC_INFO("encoder get EOS flag from source, clean buffer queue");
        mAvailableSourceBuffers.clear();
        bufferSP->setFlag(MediaBuffer::MBFT_EOS);
    }

    if (mediaBuf->isFlagSet(MediaBuffer::MBFT_KeyFrame) && mCodec) {
        MC_INFO("get keyframe flag from source, request IDR");
        mCodec->requestIDRFrame();
    }

    info.mediaBuf = bufferSP;
    info.shadowBuf = objSP;
    mAvailableSourceBuffers.push_back(info);

    if (mAvailableSourceBuffers.size() > (uint32_t)mInputBufferCount)
        MC_WARNING("mAvailableSourceBuffers size is %d",
        mAvailableSourceBuffers.size());

    if (!mIsAudio)
        mDecodingTimeQueue.push_back(bufferSP->dts());

buffer_end:
    while(submitBufferToCodec()) {
        ;
    }
    delete notify;
    EXIT();
}

/*
void MediaCodecEnc::onSetParameter(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    EXIT();
}
*/

BEGIN_MSG_LOOP(MediaBufferThread)
    MSG_ITEM(MBT_MSG_stop, onStop)
    MSG_ITEM(MBT_MSG_start, onStart)
    MSG_ITEM(MBT_MSG_reset, onReset)
    MSG_ITEM(MBT_MSG_configure, onConfigure)
    MSG_ITEM(MBT_MSG_acquireSourceBuffer, acquireSourceBuffer)
END_MSG_LOOP()


MediaBufferThread::MediaBufferThread(const char* initName) :
    MMMsgThread(MMMSGTHREAD_NAME),
    mPaused(false),
    mThreadGeneration(0) {

    ENTER();

    mReader.reset();
    mNotify.reset();
    mInitName = initName;

    EXIT();
}

MediaBufferThread::~MediaBufferThread() {
    ENTER();
    EXIT();
}

mm_status_t MediaBufferThread::init() {
    ENTER();

    int ret;

    ret = MMMsgThread::run();

    EXIT_AND_RETURN(ret == 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void MediaBufferThread::uninit() {
    ENTER();

    MMMsgThread::exit();

    EXIT();
}

mm_status_t MediaBufferThread::processMsg(uint32_t msg, param1_type param1, param2_type param2) {
    param1_type rsp_param1;
    param2_type rsp_param2;

    if (sendMsg(msg, param1, param2, &rsp_param1, &rsp_param2)) {
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    if (rsp_param1)
        EXIT_AND_RETURN(rsp_param1);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t MediaBufferThread::start() {
    ENTER();
    mm_status_t status;

    status = processMsg(MBT_MSG_start, 0, NULL);
    MC_INFO("buffer thread start status %d", status);
    EXIT_AND_RETURN(status);
}

mm_status_t MediaBufferThread::stop() {
    ENTER();
    mm_status_t status;

    mPaused = true; //accelerate stopping progress, otherwise try to read until 500 count to exit
    status = processMsg(MBT_MSG_stop, 0, NULL);
    MC_INFO("buffer thread stop status %d", status);
    EXIT_AND_RETURN(status);
}

mm_status_t MediaBufferThread::reset() {
    ENTER();
    mm_status_t status;

    status = processMsg(MBT_MSG_reset, 0, NULL);
    MC_INFO("buffer thread reset status %d", status);
    EXIT_AND_RETURN(status);
}

typedef struct configParam {
    Component::ReaderSP reader;
    YunOSMediaCodec::Notify *notify;
} configParam;

mm_status_t MediaBufferThread::configure(Component::ReaderSP reader, YunOSMediaCodec::Notify *notify) {
    ENTER();
    mm_status_t status;
    configParam param;

    param.reader = reader;
    param.notify = notify;

    status = processMsg(MBT_MSG_configure, 0, (param2_type)&param);
    MC_INFO("buffer thread configure status %d", status);
    EXIT_AND_RETURN(status);
}

void MediaBufferThread::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    if (!mNotify || !mReader) {
        postReponse(rspId, MM_ERROR_NOT_INITED, NULL);
        EXIT();
    }

    postMsg(MBT_MSG_acquireSourceBuffer, mThreadGeneration, NULL);
    postReponse(rspId, MM_ERROR_SUCCESS, NULL);
    mPaused = false;

    EXIT();
}

void MediaBufferThread::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mReader.reset();

    mThreadGeneration++;
    mPaused = true;
    postReponse(rspId, MM_ERROR_SUCCESS, NULL);

    EXIT();
}

void MediaBufferThread::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();


    mNotify.reset();
    mReader.reset();

    mThreadGeneration++;
    mPaused = true;

    postReponse(rspId, MM_ERROR_SUCCESS, NULL);

    EXIT();
}

void MediaBufferThread::onConfigure(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    configParam *param = (configParam *)param2;

    if (!param || !param->notify || !param->reader) {
        MC_ERROR("invalid parameter, cannot configure MediaBufferThread");
        postReponse(rspId, MM_ERROR_INVALID_PARAM, NULL);
        EXIT();
    }

    mNotify.reset(param->notify);
    mReader = param->reader;

    postReponse(rspId, MM_ERROR_SUCCESS, NULL);
    EXIT();
}

void MediaBufferThread::acquireSourceBuffer(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    int32_t generation = (int32_t)param1;
    MediaBufferSP buffer;
    mm_status_t status;
    int retry = 0;

    if (!mNotify || !mReader) {
        MC_ERROR("MediaBufferThread is not configured or stopped");
        EXIT();
    }

    if (mThreadGeneration != generation) {
        MC_INFO("MediaBufferThread got old generation");
        EXIT();
    }

    do {
        if (mPaused) {
            buffer.reset();
            EXIT();
        }
        status = mReader->read(buffer);
    } while((status == MM_ERROR_AGAIN) && (retry++ < RETRY_COUNT));

    if (buffer)
        MC_VERBOSE("source media buffer size %d pts %" PRId64 " dts %" PRId64 "",
                buffer->size(), buffer->pts(), buffer->dts());

    YunOSMediaCodec::Notify *notify = new YunOSMediaCodec::Notify(*mNotify.get());

    generation = 0;
    mNotify->findInt32("generation", &generation);
    notify->setInt32("generation", generation);

    if (status != MM_ERROR_SUCCESS) {
        MC_WARNING("read not success, err %d", status);

        mPaused = true;

        if (buffer)
           buffer->setFlag(MediaBuffer::MBFT_EOS);
    }

    if (buffer && buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        MC_INFO("media buffer thread get EOS, pause thread");
        mPaused = true;
    }

    MetaMediaBufferObject *tempSP = new MetaMediaBufferObject;
    tempSP->sp = buffer;
    MediaMeta::MetaBaseSP tmp;
    tmp.reset(tempSP);

    //mediabuffer object may be not retrieved by MediaCodecEnc thread.
    notify->setObject("mediabuffer", tmp);
    // eos is signalled:
    // 1. mNotify->findInt32("eos")
    // 2. bufferSP wraps NULL pointer
    // 3. MediaBuffer::MBFT_EOS in buffer

    notify->postAsync();

    if (status == MM_ERROR_SUCCESS || mPaused)
        postMsg(MBT_MSG_acquireSourceBuffer, (param1_type)mThreadGeneration, NULL);

    EXIT();
}



void MediaCodecEnc::dump(uint8_t *data, uint32_t size) {
    ENTER();

    static FILE *fp = NULL;

    if (fp == NULL)
        fp = fopen("/data/encoder.dump.rawOutput", "wb");

    if (fp == NULL || !data)
        EXIT();

    fwrite(data, size, 1, fp);
}

} // YUNOS_MM
