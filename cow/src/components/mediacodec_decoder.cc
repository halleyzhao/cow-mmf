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

#include "mediacodec_decoder.h"

#include <multimedia/mm_debug.h>
#include "media_notify.h"
#include <arpa/inet.h>

#include "multimedia/media_buffer.h"
#include "multimedia/codec.h"
#include "multimedia/media_attr_str.h"
#include "make_csd.h"
#include "multimedia/mm_audio.h"
#include "media_surface_texture.h"

MM_LOG_DEFINE_MODULE_NAME("MCD");

namespace YUNOS_MM {

using namespace YunOSMediaCodec;


class MediaSurfaceTexureListener : public YunOSMediaCodec::SurfaceTextureListener {
public:
    MediaSurfaceTexureListener(MediaCodecDec *owner) { mOwner = owner; }
    virtual ~MediaSurfaceTexureListener() {}

    virtual void onMessage(int msg, int param1, int param2) {
        if (mOwner) {
            mOwner->notify(Component::kEventUpdateTextureImage, param1, param2, nilParam);
         }
    }

private:
    MediaCodecDec* mOwner;
};


class MetaMediaCodecObject : public MediaMeta::MetaBase {
  public:
    virtual void* getRawPtr() { return sp.get();};
    MediaCodecSP sp;
};
typedef MMSharedPtr < MetaMediaCodecObject > MetaMediaCodecSP;

//#define MC_VERBOSE MC_WARNING
#undef DEBUG_STREAM

#define ENTER() MC_VERBOSE(">>>\n")
#define EXIT() do {MC_VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {MC_VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() MC_INFO(">>>\n")
#define EXIT1() do {MC_INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {MC_INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define MAX_CODEC_BUFFERING 800000 // 800ms
#define DEFAULT_FPS         24
#define CODEC_DISCONTINUITY 100000 // 100ms

#define SETPARAM_BEGIN()\
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {\
        const MediaMeta::MetaItem & item = *i;

#define CHECK_TYPE(_val_type)\
        if ( item.mType != MediaMeta::_val_type ) {\
            MC_WARNING("invalid type for %s, expect: %s, real: %d\n", item.mName, #_val_type, item.mType);\
            continue;\
        }

#define SETPARAM_POINTER(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Pointer);\
        mInputFormat->setPointer(_key_name, item.mValue.ptr);\
        MC_INFO("setparam key: %s, val: %p\n", item.mName, item.mValue.ptr);\
        continue;\
    }

#define SETPARAM_I32(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int32);\
        mInputFormat->setInt32(_key_name, item.mValue.ii);\
        MC_INFO("setparam key: %s, val: %d\n", item.mName, item.mValue.ii);\
        continue;\
    }

#define SETPARAM_I64(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int64);\
        mInputFormat->setInt64(_key_name, item.mValue.ld);\
        MC_INFO("setparam key: %s, val: %" PRId64 "\n", item.mName, item.mValue.ld);\
        continue;\
    }

#define SETPARAM_END() }

MediaCodecDec::MediaCodecDec(const char *mime)
    : MediaCodecComponent(mime),
      mMaxCodecBuffering(0),
      mTimeDiscontinuity(false),
      mDiscontinuityPts(-1ll),
      mCSDBufferIndex(0),
      mTargetTimeUs(-1ll),
      mWidth(0), mHeight(0),
      mNeedBufferCtrl(true) {
    ENTER();
    mIsEncoder = false;
#if __NO_DECODER_BUFFERING_CTRL__
    mNeedBufferCtrl = false;
#endif
    EXIT();
}

MediaCodecDec::~MediaCodecDec() {
    ENTER();

    EXIT();
}

/*
static void* getPointer(const MMParamSP param) {

    if (sizeof(void *) == 4)
       return (void *)param->readInt32();
    else if (sizeof(void *) == 8)
       return (void *)param->readInt64();
    else
       return NULL;
}
*/

mm_status_t MediaCodecDec::seek(int msec, int seekSequence) {

    mm_status_t status;
    if (mAsync) {
        status = MM_ERROR_ASYNC;
        postMsg(MCC_MSG_seek, 0, NULL);
    } else {
        status = processMsg(MCC_MSG_seek, 0, NULL);
    }
    MC_INFO("%s return status %d", __func__, status);

    EXIT_AND_RETURN(status);
}

mm_status_t MediaCodecDec::setParameter(const MediaMetaSP & meta) {
    ENTER();

    // For the sake of simple, don't support param setting after codec created
    if (mCodec) {
        MC_WARNING("should not set parameter after codec created");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    // TODO Add decoder specific parameter
    mm_status_t status = MediaCodecComponent::setParameter(meta);

    EXIT_AND_RETURN(status);
}

bool MediaCodecDec::handleInputBuffer() {
    ENTER();
    if (mFormatChangePending || !mCodec) {
        EXIT_AND_RETURN(false);
    }

    int32_t codecBufferingUs = 0;
    size_t index = -1;
    int ret = mCodec->dequeueInputBuffer(&index);
    if (ret != MediaCodec::MEDIACODEC_OK) {
        if (ret != MediaCodec::MEDIACODEC_INFO_TRY_AGAIN_LATER) {
            MC_ERROR("decoder(%s) fail to dequeue input buffer w/h error %d\n", mComponentName.c_str(), ret);
            handleError(MM_ERROR_CODEC_ERROR);
        }
        EXIT_AND_RETURN1(false);
    }

#if 0
    /* MediaBuffer sample is consumed by mCodec completely (emptyBufferDone) */
    if (mMediaBuffer[index]) {
        // TODO something to do to release MediaBuffer, needed by classic widevine only
        mMediaBuffer[index].reset();
    }
#endif

    // TODO fill csd data here if there is

    if ((int)index >= mInputBufferCount) {
        MC_ERROR("bad input buffer index %d, input buffer count %d", index, mInputBufferCount);
        handleError();
        EXIT_AND_RETURN1(false);
    }

    if (mCodecInputSequence && mCodecOutputSequence) {
        MC_VERBOSE("time stamp (in/out) %" PRId64 "/%" PRId64 "", mCodecInputTimeStamp, mCodecOutputTimeStamp);
        codecBufferingUs = mCodecInputTimeStamp - mCodecOutputTimeStamp;

        if (!mIsAudio && (codecBufferingUs >= mMaxCodecBuffering)) {
            MC_DEBUG("codecBufferingUs %" PRId64 ", mMaxCodecBuffering %" PRId64 " mTimeDiscontinuity is %d\n",
                     codecBufferingUs/1000ll, mMaxCodecBuffering/1000ll, mTimeDiscontinuity);
        }
    }

    if (mIsAudio || (codecBufferingUs < mMaxCodecBuffering) || mTimeDiscontinuity || !mNeedBufferCtrl) {
        mAvailableCodecBuffers.push_back(index);
        while (submitBufferToCodec())
        {
            ;
        }
        acquireSourceBuffer();
    }
    else {
        MC_DEBUG("not submit input buffer immediately due to buffering control");
        mPreAvailableCodecBuffers.push_back(index);
    }

    EXIT_AND_RETURN(true);
}

bool MediaCodecDec::handleOutputBuffer() {
    ENTER();
    size_t index = -1;
    size_t offset, size;
    uint32_t flags;
    int64_t timeUs;
    mm_status_t status;

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

    MC_VERBOSE("mCodecOutputSequence: %06d, dequeue output buffer from mCodec, index(%d) offset(%d) size(%d) timeUs(%" PRId64 ") flags(%xh), ret(%d)",
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
        MC_INFO("Codec output format change");
        MediaMetaSP vec = mCodec->getOutputFormat();
        if (!vec) {
            MC_WARNING("fail to get output format");
            EXIT_AND_RETURN(true);
        }

        if (!mIsAudio) {
            int32_t width = 0, height = 0, format = 0, stride = 0, sliceHeight = 0, fourcc = 'NONE';
            vec->getInt32(MEDIA_ATTR_WIDTH, width);
            vec->getInt32(MEDIA_ATTR_HEIGHT, height);
            vec->getInt32(MEDIA_ATTR_COLOR_FORMAT, format);
            vec->getInt32(MEDIA_ATTR_STRIDE, stride);
            vec->getInt32(MEDIA_ATTR_SLICE_HEIGHT, sliceHeight);
            vec->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);

            int32_t left = 0;
            int32_t top = 0;
            int32_t right = 0;
            int32_t bottom = 0;

            if (vec->getRect(MEDIA_ATTR_CROP_RECT, left, top, right, bottom)) {
                int32_t displayWidth = right - left + 1;
                int32_t displayHeight = bottom - top + 1;

                MC_INFO("Video output format changed to %d x %d "
                     "(crop: %d x %d @ (%d, %d))",
                     width, height,
                     displayWidth,
                     displayHeight,
                     left, top);
            } else {
                MC_WARNING("find no ParamRect\n");
                right = width - -1;
                bottom = height - 1;
            }


            MMParamSP mmparam;
            mmparam.reset(new MMParam);

            // TODO add actual crop and rotation_degree...
            mmparam->writeInt32(left);      // video crop x
            mmparam->writeInt32(top);      // video crop y
            mmparam->writeInt32(right);  // video crop right
            mmparam->writeInt32(bottom); // video crop bottom
            mmparam->writeInt32(0);      // video rotation degree
            mOutputFormat->setInt32(MEDIA_ATTR_WIDTH, width);
            mOutputFormat->setInt32(MEDIA_ATTR_HEIGHT, height);
            mOutputFormat->setInt32(MEDIA_ATTR_COLOR_FORMAT, format);
            mOutputFormat->setInt32(MEDIA_ATTR_STRIDE, stride);
            mOutputFormat->setInt32(MEDIA_ATTR_SLICE_HEIGHT, sliceHeight);
            mOutputFormat->setRect(MEDIA_ATTR_CROP_RECT, left, top, right, bottom);
            mOutputFormat->setInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
            mWidth = width;
            mHeight = height;

            int32_t fps = 0;
            mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
            calculateMaxBuffering(width, height, fps);

            notify(kEventGotVideoFormat, width, height, mmparam);
        } else {
            //do something for audio when format changed
        }
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
        MC_ERROR("bad output buffer index");
        handleError();
        EXIT_AND_RETURN(false);
    }

    mCodecOutputSequence++;
    mCodecOutputTimeStamp = timeUs;

    if (timeUs == mDiscontinuityPts) {
        MC_INFO("source time discontinuity (%" PRId64 " us) goes over");
        mTimeDiscontinuity = false;
    }

    // due to buffering control, submit input buffer
    if (mMaxCodecBuffering) {
        int32_t codecBufferingUs = 0;
        codecBufferingUs = mCodecInputTimeStamp - mCodecOutputTimeStamp;

        if (codecBufferingUs >= mMaxCodecBuffering &&
            !mPreAvailableCodecBuffers.empty()) {
            MC_WARNING("codecBufferingUs %" PRId64 ", cur/pre size (%d/%d)",
                codecBufferingUs, mAvailableCodecBuffers.size(), mPreAvailableCodecBuffers.size());
        }

        if ((codecBufferingUs < mMaxCodecBuffering) &&
            !mPreAvailableCodecBuffers.empty()) {
            size_t input = mPreAvailableCodecBuffers.front();
            if (!mAvailableCodecBuffers.empty())
                MC_WARNING("not empty before submit pre-available codec buffer");
            mAvailableCodecBuffers.push_back(input);
            acquireSourceBuffer();
            mPreAvailableCodecBuffers.pop_front();
        }
    }

    MediaBufferSP mediaBuf;
    MediaBuffer::MediaBufferType type = MediaBuffer::MBT_Undefined;
    MMNativeBuffer *nativeBuffer = NULL;

    if (!mIsAudio) {
        type = ((mNativeWindow == NULL) && (mSurfaceTexture == NULL) ?  MediaBuffer::MBT_RawVideo : MediaBuffer::MBT_BufferIndex);
        mediaBuf = MediaBuffer::createMediaBuffer(type);
        nativeBuffer = mCodec->getNativeWindowBuffer(index);
    } else {
        mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
    }
    MediaMetaSP meta = mediaBuf->getMediaMeta();

    if (type == MediaBuffer::MBT_RawVideo)
        attachRawVideoMeta(meta);

    meta->setInt32(MEDIA_ATTR_BUFFER_INDEX, (int32_t)index);
    if (nativeBuffer)
        meta->setPointer(MEDIA_ATTR_NATIVE_WINDOW_BUFFER, (void* &)nativeBuffer);
    int32_t mediaType = mIsAudio ? kMediaTypeAudio : kMediaTypeVideo;
    meta->setInt32(MEDIA_ATTR_MEDIA_TYPE, mediaType);
    MetaMediaCodecObject *metaMediaCodec = new MetaMediaCodecObject;
    metaMediaCodec->sp = mCodec;
    MediaMeta::MetaBaseSP tmp(metaMediaCodec);
    meta->setObject("MediaCodec", tmp);
    meta->setInt32("generation", mGeneration);
    meta->setInt32(MEDIA_ATTR_WIDTH, mWidth);
    meta->setInt32(MEDIA_ATTR_HEIGHT, mHeight);

    //if (mSurfaceTexture)
        //meta->setInt32("send-notify", 1);

    {
        ret = mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);
        ret &= mediaBuf->setBufferInfo((uintptr_t *)&mOutputBufferData.at(index), (int32_t*)&offset, (int32_t*)&size, 1);
        mediaBuf->setSize(size);

        mediaBuf->setPts(timeUs);

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            MC_INFO("decoder receive EOS");
            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
            mPaused = true;
        }

        if (!ret)
            MC_WARNING("error when set mediaBuffer parameter");
    }

/*
    if (mResumePending) {
        mResumePending = false;
        asyncFuncExit(kEventResumed, MM_ERROR_SUCCESS, 0, mResumeRspId);
        MC_DEBUG("notify kEventResumed");
        mResumeRspId = 0;
    }
*/

    if (!mWriter) {
        MC_WARNING("sink is not ready");
        //mediaBuf.reset();
        //mCodec->releaseOutputBuffer(index, false); //return buffer when mediaBuf refcount descrease to 0
        EXIT_AND_RETURN(true);
    }

    if (!mediaBuf->isFlagSet(MediaBuffer::MBFT_EOS) && timeUs < mTargetTimeUs) {
        MC_DEBUG("ignore this frame, MediaTime %0.3fs, TargetTime %0.3fs",
            timeUs/1000000.0f, mTargetTimeUs/1000000.0f);
        //mCodec->releaseOutputBuffer(index, false);
        EXIT_AND_RETURN(true);
    } else {
        mTargetTimeUs = -1ll;
    }

    status = mWriter->write(mediaBuf);
    if (status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC) {
        MC_ERROR("decoder fail to write Sink, error -%d", status);
        handleError();
    }

    EXIT_AND_RETURN(true);
}

void MediaCodecDec::scheduleAcquire() {
    ENTER();
    mPendingAcquireBuffer = true;

    postMsg(MCC_MSG_acquireSourceBuffer, 0, NULL, MCC_SCHEDULE_TIME);
    EXIT();
}

mm_status_t MediaCodecDec::readSourceBuffer(MediaBufferSP &buf) {
    ENTER();
    if ( mStreamCSDMaker && MM_UNLIKELY(mCSDBufferIndex < mStreamCSDMaker->getCSDCount()) ) {
        buf = mStreamCSDMaker->getMediaBufferFromCSD(mCSDBufferIndex);
        mCSDBufferIndex++;
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    if (MM_UNLIKELY(!mReader)) {
        MC_WARNING("source is not ready");
        EXIT_AND_RETURN(MM_ERROR_INVALID_STATE);
    }

    mm_status_t status = mReader->read(buf);
    if ( MM_LIKELY(status == MM_ERROR_SUCCESS) ) {
        //check target time
        if (buf) {
            int64_t targetTime = -1ll;
            if (buf->getMediaMeta()->getInt64(MEDIA_ATTR_TARGET_TIME, targetTime)) {
                mTargetTimeUs = targetTime;
                MC_INFO("mTargetTimeUs %0.3f\n", mTargetTimeUs/1000000.0f);
            }
        }
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }


    // TODO
    // 1. should check time/format change
    // 2. check if we can handle formatChange seamlessly
    // 3. flush decoder accordingly
    // 4. post discontinuity callback
    if ( status != MM_ERROR_AGAIN )
        MC_WARNING("read from source doesn't success (err = %d)\n", status);
    /*
    if (info_discontinuity) {
        MMParamSP param;
        param.reset();
        notify(kEventInfo, kEventDiscontinuety, 0, param);

    } else { //other error
        if (!sourceBuf)
            MediaMetaSP meta = mediaBuf->getMediaMeta();
            meta->setInt32("error", status);
    }
    */

    EXIT_AND_RETURN(status);
}

StreamMakeCSDSP MediaCodecDec::createStreamCSDMaker(MediaMetaSP &meta) {
    StreamMakeCSDSP csd;
    int32_t codec_id = 0;
    uint8_t *data = NULL;
    int32_t size = 0;

    if ((!meta->getInt32(MEDIA_ATTR_CODECID, codec_id)) ||
        ((codec_id != kCodecIDAAC) &&
        (!meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) )){
        MC_INFO("codecid %d, data %p\n", codec_id, data);
        return csd;
    }

    if (codec_id == kCodecIDAAC) {
        csd.reset(new AACCSDMaker(meta));
    } else if (codec_id == kCodecIDH264) {
        AVCCSDMaker *maker = new AVCCSDMaker(meta);
        if (maker == NULL) {
            MC_ERROR("no memory\n");
            return csd;
        }
        csd.reset(maker);
        int count = maker->getCSDCount();//make isAVCc valid
        mIsAVCcType = maker->isAVCc();
        mNalLengthSize = maker->getNalLengthSize();
        MC_INFO("264 stream AVCc %d, csd count %d, nal length size is %d", mIsAVCcType, count, mNalLengthSize);
    } else if (codec_id == kCodecIDHEVC) {
        HEVCCSDMaker *maker = new HEVCCSDMaker(meta);
        if (maker == NULL) {
            MC_ERROR("no memory\n");
            return csd;
        }
        csd.reset(maker);
        int count = maker->getCSDCount();//make isHEVCc valid
        mIsAVCcType = maker->isHEVCc();
        mNalLengthSize = maker->getNalLengthSize();
        MC_INFO("265 stream HVCC %d, csd count %d, nal length size is %d", mIsAVCcType, count, mNalLengthSize);
    } else {
        csd.reset(new StreamCSDMaker(meta));
    }

    MC_INFO("codec 0x%0x", codec_id);

    return csd;
}
bool MediaCodecDec::setupMediaCodecParam(MediaMetaSP &params) {
    ENTER();

    if (!(MediaCodecComponent::setupMediaCodecParam(params)))
        EXIT_AND_RETURN(false);

#if 0
    if (mInputFormat->getPointer(MEDIA_ATTR_EXTRADATA0, (void*&)byteBuffer)) {
        printf("should not here 1 %d\n", __LINE__);
    }

    if (mInputFormat->getPointer(MEDIA_ATTR_EXTRADATA1, (void*&)byteBuffer)) {
        printf("should not here 2 %d\n", __LINE__);
    }
#endif

    if (!mIsAudio) {
        int32_t width = 0;
        int32_t height = 0;
        mInputFormat->getInt32("width", width);
        mInputFormat->getInt32("height", height);

        if (width == 0 || width > MCC_MAX_DIMENSION ||
            height == 0 || height > MCC_MAX_DIMENSION) {
            MC_ERROR("invalid resolution %dx%d", width, height);
            EXIT_AND_RETURN(false);
        }

        /*
        param.type = kParamInputBufferNum;
        param.value.ii = 3;
        //params.push_back(param);

        param.type = kParamInputBufferSize;
        param.value.ii = width * height / 2;
        //params.push_back(param);
        */

        int32_t fps = 0, tmp = 0;
        mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps);
        calculateMaxBuffering(width, height, fps);
        if (mInputFormat->getInt32(MEDIA_ATTR_CODEC_DISABLE_HW_RENDER, tmp)) {
            DEBUG("codec not work video layer");
            params->setInt32(MEDIA_ATTR_CODEC_DISABLE_HW_RENDER, tmp);
        }

        if (mInputFormat->getInt32(MEDIA_ATTR_CODEC_DROP_ERROR_FRAME, tmp)) {
            DEBUG("drop error frame", tmp);
            params->setInt32(MEDIA_ATTR_CODEC_DROP_ERROR_FRAME, tmp);
        }

        void *decrypt = NULL;
        if (mInputFormat->getPointer(MEDIA_ATTR_CODEC_MEDIA_DECRYPT, decrypt) && decrypt) {
            MC_INFO("get media decrypt %p", decrypt);
            params->setPointer(MEDIA_ATTR_CODEC_MEDIA_DECRYPT, decrypt);
        }

        int32_t lowDelay = 0;
        if (mInputFormat->getInt32(MEDIA_ATTR_CODEC_LOW_DELAY, lowDelay)) {
            MC_INFO("get low delay %d", lowDelay);
            params->setInt32(MEDIA_ATTR_CODEC_LOW_DELAY, lowDelay);
        }
    } else {
        int32_t codec_id;
        if (!mInputFormat->getInt32(MEDIA_ATTR_CODECID, codec_id)) {
            EXIT_AND_RETURN(false);
        }

        if (codec_id == kCodecIDWMAV1 ||
            codec_id == kCodecIDWMAV2 ||
            codec_id == kCodecIDWMAPRO ||
            codec_id == kCodecIDWMALOSSLESS ||
            codec_id == kCodecIDWMAVOICE) {
            if (mStreamCSDMaker) {
                int32_t count = mStreamCSDMaker->getCSDCount();
                if (mCSDBufferIndex < count) {
                    ByteBufferSP byteBuffer = mStreamCSDMaker->getByteBufferFromCSD(mCSDBufferIndex);

                    params->setByteBuffer(MEDIA_ATTR_EXTRADATA0,
                                          byteBuffer->data,
                                          byteBuffer->size);
                    mCSDBufferIndex++;
                }
            }else  {
                EXIT_AND_RETURN(false);
            }
        }
    }

    EXIT_AND_RETURN(true);
}

void MediaCodecDec::checkSourceTimeDiscontinuity(int64_t dts, int64_t pts) {
    ENTER();
    bool discontinuity = false;

    if (mCodecInputTimeStamp == -1ll)
        EXIT();

    if ((dts - mCodecInputTimeStamp) < 0 || (dts - mCodecInputTimeStamp) > CODEC_DISCONTINUITY) {
        MC_INFO("discontinuous CodecInputTimeStamp %" PRId64 " us and dts %" PRId64 " us", mCodecInputTimeStamp, dts);
        discontinuity = true;
        mDiscontinuityPts = pts;
    }

    mTimeDiscontinuity |= discontinuity;

    EXIT();
}

void MediaCodecDec::onFlush(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    // TODO add decoder flush
    mPreAvailableCodecBuffers.clear();
    mTimeDiscontinuity = false;
    mTargetTimeUs = -1ull;

    MediaCodecComponent::onFlush(param1, param2, rspId);
    EXIT();
}


bool MediaCodecDec::writeSinkMeta() {

    mOutputFormat = mInputFormat->copy();
    if (mIsAudio) {
        int32_t sample_format = 0;
        if (mInputFormat->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, sample_format)) {
            //plugin audio decoders always return SND_FORMAT_PCM_16_BIT
            mOutputFormat->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, SND_FORMAT_PCM_16_BIT);
        }
    }
    mWriter->setMetaData(mOutputFormat);
    mOutputFormat->dump();

    return true;
}

bool MediaCodecDec::readSourceMeta() {
    ENTER();

    MediaMetaSP meta;

    if (!mReader)
        EXIT_AND_RETURN1(false);

    meta = mReader->getMetaData();
    if (!meta)
        EXIT_AND_RETURN1(false);

    mInputFormat->merge(meta);
    if (!mInputFormat)
        EXIT_AND_RETURN1(false);


    if (mIsAudio) {
        int32_t sample_format = 0;
        if (mInputFormat->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, sample_format)) {
            MC_DEBUG("get meta data, sample_format is %d", sample_format);
            if (sample_format != SND_FORMAT_PCM_16_BIT) {
                MC_WARNING("format is not SND_FORMAT_PCM_16_BIT\n",
                    sample_format);
            }
            mInputFormat->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, sample_format);
        }


        const char* mime = NULL;
        if (mInputFormat->getString(MEDIA_ATTR_MIME, mime)) {
            #ifdef _PLATFORM_TV
            uint8_t *data = NULL;
            int32_t size = 0;
            if (!strncmp(mime, MEDIA_MIMETYPE_AUDIO_AAC, strlen(MEDIA_MIMETYPE_AUDIO_AAC)) &&
                !meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) {
                mInputFormat->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_AUDIO_ADTS_PROFILE);
            }
            #endif
        }
    } else {
        int32_t fps = 0;
        if (!mInputFormat->getInt32(MEDIA_ATTR_AVG_FRAMERATE, fps)) {
            mInputFormat->setInt32(MEDIA_ATTR_AVG_FRAMERATE, 0);
        }

        int32_t rotationDegrees = 0;
        if (!mInputFormat->getInt32(MEDIA_ATTR_ROTATION, rotationDegrees)) {
            mInputFormat->setInt32(MEDIA_ATTR_ROTATION, 0);
        } else {
            MC_DEBUG("rotationDegrees %d\n", rotationDegrees);
        }
    }
    mInputFormat->dump();

    mStreamCSDMaker = createStreamCSDMaker(mInputFormat);

    EXIT_AND_RETURN(true);
}

void MediaCodecDec::onPause(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    // TODO decoder pause
    MediaCodecComponent::onPause(param1, param2, rspId);
    EXIT();
}

void MediaCodecDec::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO decoder specific prepare

    // we don't do buffering control for audio
    if (!mIsAudio)
        mMaxCodecBuffering = MAX_CODEC_BUFFERING;

    mPreAvailableCodecBuffers.clear();
    mTimeDiscontinuity = false;

    mInputFormat->getPointer(MEDIA_ATTR_VIDEO_SURFACE, (void *&)mNativeWindow);
    MC_INFO("get mNativeWindow parameter: %p\n", mNativeWindow);

    mInputFormat->getPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, (void *&)mSurfaceTexture);
    MC_INFO("get mSurfaceTexture parameter: %p\n", mSurfaceTexture);

    if (mSurfaceTexture && !(mSurfaceTexture->listenerExisted())) {
        mSurfaceTextureListener.reset(new MediaSurfaceTexureListener(this));
        mSurfaceTexture->setListener(mSurfaceTextureListener.get());
    }

    int temp = 0;
    if (mInputFormat->getInt32(MEDIA_ATTR_CODEC_LOW_DELAY, temp) && temp > 0) {
        MC_INFO("low delay mode, disable buffering control");
        mNeedBufferCtrl = false;
    }

    MediaCodecComponent::onPrepare(param1, param2, rspId);
    EXIT();
}

void MediaCodecDec::onStart(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO decoder specific start
    MediaCodecComponent::onStart(param1, param2, rspId);
    EXIT();
}

void MediaCodecDec::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO decoder specific stop

    // clear below member is ok in any state
    mPreAvailableCodecBuffers.clear();
    mTimeDiscontinuity = false;
    mCSDBufferIndex = 0;

    MediaCodecComponent::onStop(param1, param2, rspId);
    EXIT();
}

void MediaCodecDec::onReset(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO decoder specific reset
    mPreAvailableCodecBuffers.clear();
    mTimeDiscontinuity = false;
    mCSDBufferIndex = 0;

    if (mSurfaceTexture) {
        mSurfaceTexture->setListener(NULL);
        mSurfaceTextureListener.reset();
    }

    MediaCodecComponent::onReset(param1, param2, rspId);
    EXIT();
}

void MediaCodecDec::onSeek(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    mm_status_t status;

    status = flushInternal();

    asyncFuncExit(kEventSeekComplete, status, 0, rspId);
    EXIT();
}

void MediaCodecDec::onSetParameter(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId) {
    ENTER();

    MCCMMParamBase* paramRef = DYNAMIC_CAST<MCCMMParamBase*>(param3.get());
    const MediaMetaSP & meta = paramRef->mMeta;

    if ( !meta )
        MC_ERROR("invalid params\n");

    if (!mIsAudio && meta) {
        SETPARAM_BEGIN()
            SETPARAM_POINTER(MEDIA_ATTR_CODEC_MEDIA_DECRYPT)
            SETPARAM_I64(MEDIA_ATTR_CODEC_RESOURCE_ID)
            SETPARAM_I32(MEDIA_ATTR_CODEC_LOW_DELAY)
            SETPARAM_I32(MEDIA_ATTR_CODEC_DISABLE_HW_RENDER)
            SETPARAM_I32(MEDIA_ATTR_CODEC_DROP_ERROR_FRAME)
        SETPARAM_END()
    }

    MediaCodecComponent::onSetParameter(param1, param2, param3, rspId);
    EXIT();
}

bool MediaCodecDec::releaseMediaBuffer(MediaBuffer *mediaBuffer) {

    MediaCodec *codec = NULL;

    int32_t index = -1;
    int32_t offset = 0;
    int32_t size = 0;
    uint8_t *sourceBuf = NULL;
    int32_t isRender = 0;
    int32_t isAudio = true;

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    if (!meta) {
        ERROR("no meta data");
        return false;
    }

    meta->getInt32(MEDIA_ATTR_BUFFER_INDEX, index);

    if (meta->getInt32(MEDIA_ATTR_IS_VIDEO_RENDER, isRender)) {
        VERBOSE("GET render flag, %d", isRender);
    }

    int32_t mediaType;
    meta->getInt32(MEDIA_ATTR_MEDIA_TYPE, mediaType);
    isAudio = (mediaType == kMediaTypeAudio) ? true : false;

    MediaMeta::MetaBaseSP tmp;
    //TODO. MediaCodec pointer can be got from writeMetaData method
    if (!meta->getObject("MediaCodec", tmp)) {
        ERROR("cannot render buffer due to no mediacodec");
        return false;
    }

    codec = static_cast<MediaCodec*> (tmp->getRawPtr());
    if (codec == NULL) {
        ERROR("no codec");
        return false;
    }

    //Just for debug, delete in future
    if (!mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1)) {
        ERROR("getBufferInfo failed");
        return false;
    }

    INFO("isAudio(%d): buffer %p, offset %d, size %d, pts %" PRId64 " ms, index %d, isRender %d",
        isAudio, sourceBuf, offset, size, mediaBuffer->pts()/1000ll, index, isRender);

    sourceBuf += offset;

    int ret = codec->releaseOutputBuffer(index, isRender ? true : false);
    if (ret != MediaCodec::MEDIACODEC_OK) {
        DEBUG("releaseOutputBuffer failed, ret %d", ret);
    }

    return true;
}

void MediaCodecDec::calculateMaxBuffering(uint32_t width, uint32_t height, int fps) {

    mMaxCodecBuffering = MAX_CODEC_BUFFERING;
    if (width * height < 1280 * 720) {
        // Take fps into account
        mMaxCodecBuffering = (int32_t)((MAX_CODEC_BUFFERING * 720.0f * 1280) / (width * height));
    }

    if (fps > 0 && fps < (DEFAULT_FPS * 2)) {
        mMaxCodecBuffering = (int32_t)(mMaxCodecBuffering * 1.0f * DEFAULT_FPS / fps);
    }

    MC_INFO("calculate max buffering is %d ms for content %dx%d %dfps", mMaxCodecBuffering / 1000, width, height, fps);
}

void MediaCodecDec::attachRawVideoMeta(MediaMetaSP meta) {
    int32_t width = 0, height = 0, format = 0, stride = 0, sliceHeight = 0, rotation = 0, fourcc = 'NONE';

    // TODO add actual crop and
    mOutputFormat->getInt32(MEDIA_ATTR_WIDTH, width);
    mOutputFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
    mOutputFormat->getInt32(MEDIA_ATTR_COLOR_FORMAT, format);
    mOutputFormat->getInt32(MEDIA_ATTR_STRIDE, stride);
    mOutputFormat->getInt32(MEDIA_ATTR_SLICE_HEIGHT, sliceHeight);
    mOutputFormat->getInt32(MEDIA_ATTR_ROTATION, rotation);
    mOutputFormat->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);

    meta->setInt32(MEDIA_ATTR_WIDTH, width);
    meta->setInt32(MEDIA_ATTR_HEIGHT, height);
    meta->setInt32(MEDIA_ATTR_COLOR_FORMAT, format);
    meta->setInt32(MEDIA_ATTR_STRIDE, stride);
    meta->setInt32(MEDIA_ATTR_SLICE_HEIGHT, sliceHeight);
    meta->setInt32(MEDIA_ATTR_ROTATION, rotation);
    meta->setInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
}

} // YUNOS_MM
