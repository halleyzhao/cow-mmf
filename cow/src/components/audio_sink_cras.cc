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
#include "audio_sink_cras.h"
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_audio.h"
#include "multimedia/mm_audio_compat.h"

#include <stdio.h>

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("ASC")

static const char * COMPONENT_NAME = "AudioSinkCras";
//static const char * MMTHREAD_NAME = "AudioSinkCras::Private::OutputThread";

#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            false
#define MAX_VOLUME              10.0
#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_CHANNEL         2
#define CLOCK_TIME_NONE         -1

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define setState(_param, _deststate) do {\
    INFO("change state from %d to %s\n", _param, #_deststate);\
    (_param) = (_deststate);\
}while(0)

struct FormatSize {
    snd_format_t format;
    uint32_t size;
};

static const FormatSize SizeTable[] = {
    {SND_FORMAT_PCM_8_BIT, 1},
    {SND_FORMAT_PCM_16_BIT, 2},
    {SND_FORMAT_PCM_32_BIT, 4}
};

static const float VOLUME_NOT_SET = -1.0f;
static const float EPSINON = 0.00001;

class AudioSinkCras::Private
{
  public:

    enum state_t {
        STATE_IDLE,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_PAUSED,
        STATE_STOPED,
    };

    struct QueueEntry {
        MediaBufferSP mBuffer;
        mm_status_t mFinalResult;
    };

    static PrivateSP create()
    {
        ENTER();
        PrivateSP priv(new Private());
        if (priv) {
            INFO("private create success");
        }
        return priv;
    }

    mm_status_t init(AudioSinkCras *audioSink) {
        ENTER();
        mAudioSink = audioSink;

        if (mm_check_env_str("mm.audio.dump.pcm", "MM_AUDIO_DUMP_PCM", "1", false)) {
            mDumpFile = fopen("/tmp/audio_sink_cras.pcm","wb");
            INFO("dump audio pcm data to /tmp/audio_sink_cras.pcm");
        }
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    void uninit() {
        if (mDumpFile) {
            fclose(mDumpFile);
            mDumpFile = NULL;
        }
    }
    ~Private() {destroyAudioRender();}
    mm_status_t release();

    void clearSourceBuffers();
    mm_status_t resumeInternal();

    uint32_t formatSize(snd_format_t format);
    static void audioCallback(YunOSAudioNS::AudioRender::evt_t event, void *user, void *info);

    void onMoreData(YunOSAudioNS::AudioRender::evt_t event, void *info);
    void onStreamError(YunOSAudioNS::AudioRender::evt_t event, void *info);
    mm_status_t setAudioStreamType(int type);
    mm_status_t getAudioStreamType(int *type);

    mm_status_t ensureAudioRender();
    void destroyAudioRender();

    void audioRenderError();

    YunOSAudioNS::AudioRender *mRender;
	uint32_t mBytesPerFrame;

    int32_t mFormat;
    int32_t mSampleRate;
    int32_t mChannelCount;
    int32_t mBytesPerSample;
    int32_t mAudioStreamType;
    int64_t mDurationUs;
    int64_t mCurrentPositionUs;

    std::queue<MediaBufferSP> mAvailableSourceBuffers;

    ClockWrapperSP mClockWrapper;

    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    state_t mState;
    int32_t mTotalBuffersQueued;
    AudioSinkCras *mAudioSink;
    FILE* mDumpFile;
    int32_t mScaledPlayRate;
    uint32_t mOutFrameCnt;
    int32_t mBufferCount;
    CallFrequencyStatics mCrasCBStatics;

    Private()
        :
        mRender(NULL),
        mBytesPerFrame(0),
        mFormat(SND_FORMAT_PCM_16_BIT),
        mSampleRate(DEFAULT_SAMPLE_RATE),
        mChannelCount(DEFAULT_CHANNEL),
        mBytesPerSample(2),
        mAudioStreamType(AS_TYPE_MUSIC),
        mDurationUs(0),
        mCurrentPositionUs(0),
        mIsPaused(true),
        mCondition(mLock),
        mState(STATE_IDLE),
        mTotalBuffersQueued(0),
        mAudioSink(NULL),
        mDumpFile(NULL),
        mScaledPlayRate(SCALED_PLAY_RATE),
        mOutFrameCnt(0),
        mBufferCount(0),
        mCrasCBStatics("CrasCBStatics")
    {
        ENTER();
        mClockWrapper.reset(new ClockWrapper());
        EXIT();
    }

    MM_DISALLOW_COPY(Private);

};

#define ASC_MSG_prepare (msg_type)1
#define ASC_MSG_start (msg_type)2
#define ASC_MSG_resume (msg_type)3
#define ASC_MSG_pause (msg_type)4
#define ASC_MSG_stop (msg_type)5
#define ASC_MSG_flush (msg_type)6
#define ASC_MSG_reset (msg_type)7
#define ASC_MSG_write (msg_type)8
#define ASC_MSG_AudioRenderError (msg_type)9
#define ASC_MSG_ScheduleEOS (msg_type)10

BEGIN_MSG_LOOP(AudioSinkCras)
    MSG_ITEM(ASC_MSG_prepare, onPrepare)
    MSG_ITEM(ASC_MSG_start, onStart)
    MSG_ITEM(ASC_MSG_resume, onResume)
    MSG_ITEM(ASC_MSG_pause, onPause)
    MSG_ITEM(ASC_MSG_stop, onStop)
    MSG_ITEM(ASC_MSG_flush, onFlush)
    MSG_ITEM(ASC_MSG_reset, onReset)
    MSG_ITEM(ASC_MSG_write, onWrite)
    MSG_ITEM(ASC_MSG_AudioRenderError, onAudioRenderError)
    MSG_ITEM(ASC_MSG_ScheduleEOS, onScheduleEOS)
END_MSG_LOOP()

AudioSinkCras::AudioSinkCras(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME)
                                                                  , mComponentName(COMPONENT_NAME)
                                                                  , mVolume(VOLUME_NOT_SET)
                                                                  , mMute(DEFAULT_MUTE)
                                                                  , mCurrentPosition(-1ll)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no render");
}

AudioSinkCras::~AudioSinkCras()
{
    //release();
}

Component::WriterSP AudioSinkCras::getWriter(MediaType mediaType)
{
     ENTER();
     if ( (int)mediaType != Component::kMediaTypeAudio ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::WriterSP((Component::Writer*)NULL);
        }

    Component::WriterSP wd(new AudioSinkCras::AudioSinkWriter(this));
    return wd;

}

mm_status_t AudioSinkCras::init()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    int ret = mPriv->init(this);
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);

    ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkCras::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();

    EXIT();
}

const char * AudioSinkCras::name() const
{
    return mComponentName.c_str();
}


ClockSP AudioSinkCras::provideClock()
{
    ENTER();
    if (!mPriv)
        ERROR("no render");
    return mPriv->mClockWrapper->provideClock();
}

mm_status_t AudioSinkCras::prepare()
{

    postMsg(ASC_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::start()
{
    postMsg(ASC_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::resume()
{
    postMsg(ASC_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::stop()
{
    postMsg(ASC_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::pause()
{
    postMsg(ASC_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::reset()
{
    postMsg(ASC_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::flush()
{
    postMsg(ASC_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkCras::AudioSinkWriter::write(const MediaBufferSP & buf)
{
    ENTER();
    if (!mRender->mPriv)
        return MM_ERROR_NO_COMPONENT;
    AudioSinkCras::Private::QueueEntry *pEntry = new AudioSinkCras::Private::QueueEntry;
    pEntry->mBuffer = buf;
    ++mRender->mPriv->mTotalBuffersQueued;
    mRender->postMsg(ASC_MSG_write, 0, (param2_type)pEntry);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkCras::AudioSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    ENTER();
    if (!mRender->mPriv)
        return MM_ERROR_NO_COMPONENT;
    int ret = metaData->getInt32(MEDIA_ATTR_SAMPLE_RATE, mRender->mPriv->mSampleRate);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    ret = metaData->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, mRender->mPriv->mFormat);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_FORMAT);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    ret = metaData->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mRender->mPriv->mChannelCount);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    ret = metaData->getInt32(MEDIA_ATTR_BUFFER_LIST, mRender->mPriv->mBufferCount);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_BUFFER_LIST);
    }

    INFO("sampleRate %d, format %d, channel %d", mRender->mPriv->mSampleRate, mRender->mPriv->mFormat, mRender->mPriv->mChannelCount);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkCras::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    bool stopAudioRender = false;
    {
        MMAutoLock locker(mPriv->mLock);

        for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
            const MediaMeta::MetaItem & item = *i;
            if ( !strcmp(item.mName, MEDIA_ATTR_MUTE) ) {
                if ( item.mType != MediaMeta::MT_Int32 ) {
                    WARNING("invalid type for %s\n", item.mName);
                    continue;
                }
                mMute = item.mValue.ii;
                setMute(mMute);
                INFO("key: %s, value: %d\n", item.mName, mMute);
                continue;
            }
            if ( !strcmp(item.mName, MEDIA_ATTR_VOLUME) ) {
                if ( item.mType != MediaMeta::MT_Int64 ) {
                    WARNING("invalid type for %s\n", item.mName);
                    continue;
                }
                mVolume = item.mValue.ld;
                setVolume(mVolume);
                INFO("key: %s, value: %" PRId64 "\n", item.mName, mVolume);
                continue;
            }
            if ( !strcmp(item.mName, MEDIA_ATTR_PALY_RATE) ) {
                if ( item.mType != MediaMeta::MT_Int32 ) {
                    WARNING("invalid type for %s\n", item.mName);
                    continue;
                }

                if (mPriv->mScaledPlayRate == item.mValue.ii) {
                    DEBUG("play rate is already %d, just return\n", mPriv->mScaledPlayRate);
                    continue;
                }
                if (item.mValue.ii != SCALED_PLAY_RATE) {
                    stopAudioRender = true;
                } else {
                    mm_status_t ret = mPriv->ensureAudioRender();
                    if (ret != MM_ERROR_SUCCESS)
                        EXIT_AND_RETURN(ret);
                }

                mPriv->mScaledPlayRate = item.mValue.ii;
                INFO("key: %s, val: %d\n", item.mName, item.mValue.ii);
                continue;
            }
        }
    }
    if (stopAudioRender) {
        {
            MMAutoLock locker(mPriv->mLock);
            mPriv->mCondition.signal();
        }
        if (mPriv->mRender)
            mPriv->mRender->stop();
        mPriv->destroyAudioRender();
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t AudioSinkCras::getOutputSampleRate()
{
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    YunOSAudioNS::AudioManager* am = YunOSAudioNS::AudioManager::create();
    if (!am) {
        ERROR("Fail to create Audio Manager");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    } else {
        int ret = 0;
        ret = am->getOutputSampleRate(&mPriv->mSampleRate);
        INFO("mSampleRate : %d", mPriv->mSampleRate);
        if(am != NULL) {
            delete am;
            am = NULL;
        }
        if (ret) {
            ERROR("get output samplerate error");
            EXIT_AND_RETURN(ret);
        }
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
#else
    EXIT_AND_RETURN(MM_ERROR_UNSUPPORTED);
#endif
}

mm_status_t AudioSinkCras::getParameter(MediaMetaSP & meta) const
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;

    MMAutoLock locker(mPriv->mLock);

    getOutputSampleRate();
    meta->setInt32(MEDIA_ATTR_MUTE, mMute);
    meta->setInt64(MEDIA_ATTR_VOLUME, mVolume);
    meta->setInt32(MEDIA_ATTR_SAMPLE_RATE, mPriv->mSampleRate);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkCras::seek(int msec, int seekSequence)
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    MMAutoLock locker(mPriv->mLock);
    mCurrentPosition = msec * 1000;
    return MM_ERROR_SUCCESS;
}

int64_t AudioSinkCras::getCurrentPosition()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    MMAutoLock locker(mPriv->mLock);
    int64_t currentPosition = -1ll;
    if (mCurrentPosition > -1) {
        mPriv->mClockWrapper->setAnchorTime(mCurrentPosition, Clock::getNowUs(), mCurrentPosition);
        mCurrentPosition = -1;
    }
    if (mPriv->mClockWrapper && mPriv->mClockWrapper->getCurrentPosition(currentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        currentPosition = -1ll;
    }
    VERBOSE("getCurrentPosition %" PRId64 " ms", currentPosition/1000ll);
    if (mPriv->mDurationUs > 0 && currentPosition > mPriv->mDurationUs)
        currentPosition = mPriv->mDurationUs;
    return currentPosition;
}

mm_status_t AudioSinkCras::scheduleEOS(int64_t delayEOSUs)
{
    ENTER();
    DEBUG("delayEOSUs = %" PRId64 " us",delayEOSUs);
    postMsg(ASC_MSG_ScheduleEOS, 0, NULL, delayEOSUs);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkCras::onScheduleEOS(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    notify(kEventEOS, 0, 0, nilParam);
    EXIT();
}

void AudioSinkCras::onAudioRenderError(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mPriv->destroyAudioRender();
    EXIT();
}

void AudioSinkCras::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{

    ENTER1();
    mPriv->mOutFrameCnt = YunOSAudioNS::AudioRender::getDefaultPeriodSize(
        mPriv->mSampleRate,
        adev_get_out_mask_from_channel_count(mPriv->mChannelCount),
        (snd_format_t)mPriv->mFormat,
        (as_type_t)mPriv->mAudioStreamType);
    mPriv->mBytesPerSample = adev_bytes_per_sample((snd_format_t)mPriv->mFormat);
    INFO("outFrameCnt %d, bytesPerSample %d\n", mPriv->mOutFrameCnt, mPriv->mBytesPerSample);
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

mm_status_t AudioSinkCras::Private::resumeInternal()
{

    if (!mIsPaused/* || mIsDraining*/) {
        ERROR("Aready started\n");
        mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    if (mIsPaused && mClockWrapper) {
        mClockWrapper->resume();
    }
    mIsPaused = false;
    if (!mAvailableSourceBuffers.empty()) {
        mm_status_t ret = ensureAudioRender();
        if (ret != MM_ERROR_SUCCESS) {
            mAudioSink->notify(kEventStartResult, ret, 0, nilParam);
            EXIT_AND_RETURN(ret);
        }
    }
    setState(mState, STATE_STARTED);
    mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

uint32_t AudioSinkCras::Private::formatSize(snd_format_t format)
{
    uint32_t size = 0;
    uint32_t count = sizeof(SizeTable)/sizeof(SizeTable[0]);
    uint32_t i = 0;

    for (i = 0; i < count; i++) {
        if (SizeTable[i].format == format) {
            size = SizeTable[i].size;
            break;
        }
    }
    return size;
}

void AudioSinkCras::Private::audioCallback(YunOSAudioNS::AudioRender::evt_t event, void * user,void * info) {
    AudioSinkCras *me = static_cast<AudioSinkCras *>(user);

    if (YunOSAudioNS::AudioRender::EVT_MORE_DATA == event) {
        me->mPriv->onMoreData(event, info);
        } else if (YunOSAudioNS::AudioRender::EVT_STREAM_ERROR == event) {
            ERROR("audio callback error callback");
            me->notify(Component::kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
            me->audioRenderError();
        } else {
            ERROR("audio callback event %d not implement.", event);
        }
}

void AudioSinkCras::Private::onMoreData(YunOSAudioNS::AudioRender::evt_t event, void *info) {
    MediaBufferSP mediaBuffer;
    uint8_t *sourceBuf = NULL;
    int64_t pts = 0;
    int32_t offset = 0;
    int32_t size = 0;
    int32_t remainSize = 0;
    uint32_t lastLatency = 0;
    int32_t writePosition = 0;

    mCrasCBStatics.updateSample();
    YunOSAudioNS::AudioRender::Buffer *buffer = static_cast<YunOSAudioNS::AudioRender::Buffer *>(info);
    int mWriteableSize = buffer->mFrameCount * mChannelCount * mBytesPerSample;
    buffer->mFrameCount = 0; //FIXME: to set framecount even if push nothing
#if defined (__MM_YUNOS_CNTRHAL_BUILD__) && !(__PLATFORM_TV__)
#if MM_USE_AUDIO_VERSION>=20
    buffer->mIsEOS = false;
#endif
#endif
    if (mIsPaused || (mScaledPlayRate != SCALED_PLAY_RATE)) {
      return;
    }

    MMAutoLock locker(mLock);

    while(mWriteableSize > 0) {
        offset = 0;
        size = 0;
        if (mAvailableSourceBuffers.empty()) {
                int64_t waitTime = (int64_t)mOutFrameCnt * 1000000 * 2 / mSampleRate;
                mCondition.timedWait(waitTime);
                if (mAvailableSourceBuffers.empty()) {
                    WARNING("No buffer after wait: %" PRId64 " us\n", waitTime);
                    return;
                }
        }

        if (mIsPaused) {
          return;
        }
        mediaBuffer = mAvailableSourceBuffers.front();
        mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1);
        remainSize = size - offset;
        if (mediaBuffer->type() != MediaBuffer::MBT_RawAudio) {
           ERROR("wrong buffer type %d", mediaBuffer->type());
           mAvailableSourceBuffers.pop();
           return;
        }
#ifdef __MM_YUNOS_YUNHAL_BUILD__
        lastLatency = mRender->getCurLatency() + 213;//FIXME: need support the correct latency by audio render
#else
        lastLatency = mRender->getCurLatency();
#endif

        if (!sourceBuf || remainSize <= 0) {
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
//#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#if defined (__MM_YUNOS_CNTRHAL_BUILD__) && !(__PLATFORM_TV__)
#if MM_USE_AUDIO_VERSION>=20
                buffer->mIsEOS = true;
#endif
#endif
                mAudioSink->scheduleEOS(lastLatency * 1000);
                mDurationUs = mCurrentPositionUs;
            }
            {
                mAvailableSourceBuffers.pop();
            }
            return;
        }

        //sourceBuf += offset;
        VERBOSE("pcm buffer %p, offset %d, size %d, pts %" PRId64 " ms",
          sourceBuf, offset, remainSize, mediaBuffer->pts()/1000ll);
        pts = mediaBuffer->pts();

        INFO("mWriteableSize = %d,size = %d,writePosition = %d,offset = %d, pts %" PRId64 " ms",
            mWriteableSize,remainSize,writePosition,offset, mediaBuffer->pts()/1000ll);

        int32_t frameWritten = (mWriteableSize >= remainSize) ? remainSize : mWriteableSize;
        int64_t durationUs = frameWritten  * 1000*1000ll / formatSize((snd_format_t)mFormat) / mChannelCount / mSampleRate;
        mCurrentPositionUs = pts + durationUs;
        if (MM_LIKELY(!mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
            mClockWrapper->setAnchorTime(pts, Clock::getNowUs() + lastLatency, pts + durationUs);
            mAudioSink->mCurrentPosition = -1ll;
        } else {
            MMLOGV("eos, not set anchor\n");
        }
        memcpy(buffer->mBuffer + writePosition, sourceBuf + offset, frameWritten);
        buffer->mFrameCount += frameWritten / (mChannelCount * mBytesPerSample);
        if (mDumpFile)
            fwrite(sourceBuf+offset,1,frameWritten,mDumpFile);

        if (mWriteableSize >= remainSize) {
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
//#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#if defined (__MM_YUNOS_CNTRHAL_BUILD__) && !(__PLATFORM_TV__)
#if MM_USE_AUDIO_VERSION>=20
                buffer->mIsEOS = true;
#endif
#endif
                mAudioSink->scheduleEOS(lastLatency * 1000);
                mDurationUs = mCurrentPositionUs;
            }
            mAvailableSourceBuffers.pop();
            mWriteableSize -= remainSize;
            writePosition += remainSize;
        } else {
            offset += mWriteableSize;
            // remainSize -= mWriteableSize;
            pts += mWriteableSize  * 1000*1000ll / formatSize((snd_format_t)mFormat) / mChannelCount / mSampleRate;
            mWriteableSize = 0;
            mediaBuffer->setPts(pts);
            mediaBuffer->updateBufferOffset(&offset, 1);
            return;
        }

    }

}

mm_status_t AudioSinkCras::Private::setAudioStreamType(int type)
{
    mAudioStreamType = type;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
mm_status_t AudioSinkCras::Private::getAudioStreamType(int *type)
{
    *type = mAudioStreamType;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkCras::Private::ensureAudioRender()
{
//    MMLOGV("+\n");
    if (mRender || mAvailableSourceBuffers.empty()) {
        return MM_ERROR_SUCCESS;
    }
    int64_t bufferStart = mAvailableSourceBuffers.front()->pts();
    int64_t bufferEnd = mAvailableSourceBuffers.back()->pts();
    int bufferCount = mAvailableSourceBuffers.size();

    if (!mAvailableSourceBuffers.back()->isFlagSet(MediaBuffer::MBFT_EOS)) {
        if ((mBufferCount > 0 && bufferCount < mBufferCount - 1)) {
            return MM_ERROR_SUCCESS;
        }
    } else {
        if (bufferCount == 1) {
            mAudioSink->notify(kEventEOS, 0, 0, nilParam);
            return MM_ERROR_SUCCESS;
        }
    }

    mRender = YunOSAudioNS::AudioRender::create((as_type_t)mAudioStreamType,
        mSampleRate,
        (snd_format_t)mFormat,
        adev_get_out_mask_from_channel_count(mChannelCount),
        mOutFrameCnt,
        ADEV_OUTPUT_FLAG_NONE,
        audioCallback,
        mAudioSink);

    MMLOGV("mRender: %p\n", mRender);
    if (!mRender) {
        MMLOGE("failed to create audio track\n");
        mAudioSink->notify(kEventError, MM_ERROR_NO_AUDIORENDER, 0, nilParam);
        return MM_ERROR_NO_AUDIORENDER;
    }

    int ret = mRender->start();
    if (0 != ret) {
        MMLOGE("Failed to start AudioRender: %d.\n", ret);
        destroyAudioRender();
        return MM_ERROR_OP_FAILED;
    }

    if (VOLUME_NOT_SET != mAudioSink->mVolume) {
        mAudioSink->setVolume(mAudioSink->mVolume);
    }

    mAudioSink->setMute(mAudioSink->mMute);

    return MM_ERROR_SUCCESS;
}

void AudioSinkCras::Private::destroyAudioRender()
{
    MMLOGV("+\n");
    MM_RELEASE(mRender);
    MMLOGV("-\n");
}

void AudioSinkCras::Private::audioRenderError()
{
    MMLOGV("+\n");
    mAudioSink->audioRenderError();
    MMLOGV("-\n");
}

void AudioSinkCras::audioRenderError()
{
    postMsg(ASC_MSG_AudioRenderError, 0, 0);
}

void AudioSinkCras::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkCras::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkCras::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        mPriv->mCondition.signal();
    }
    if (mPriv->mRender)
        mPriv->mRender->stop();

    {
        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->flush();
        }
    }
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->destroyAudioRender();
        mPriv->clearSourceBuffers();
        if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED/* || mPriv->mIsDraining*/) {
            notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
            EXIT1();
        }
        setState(mPriv->mState, mPriv->STATE_STOPED);
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    }
    EXIT1();
}

void AudioSinkCras::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        mPriv->mCondition.signal();
    }

    if (mPriv->mRender) {
        mPriv->mRender->pause();
    }
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->destroyAudioRender();
        if (mPriv->mState == mPriv->STATE_PAUSED/* || mPriv->mIsDraining*/) {
            notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
            EXIT1();
        }

        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->pause();
        }
        setState(mPriv->mState, mPriv->STATE_PAUSED);
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    }
    EXIT1();
}

void AudioSinkCras::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->clearSourceBuffers();
    }
    {
        mPriv->mCondition.signal();
        if (mPriv->mRender)
            mPriv->mRender->flush();
        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->flush();
        }
    }

    MMAutoLock locker(mPriv->mLock);
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSinkCras::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        mPriv->mCondition.signal();
        mPriv->destroyAudioRender();
        mPriv->clearSourceBuffers();
    }
    mPriv->release();
    mPriv->mScaledPlayRate = SCALED_PLAY_RATE;
    setState(mPriv->mState, mPriv->STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSinkCras::onWrite(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    AudioSinkCras::Private::QueueEntry *pEntry = (AudioSinkCras::Private::QueueEntry *)param2;
    if (pEntry->mBuffer) {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mAvailableSourceBuffers.push(pEntry->mBuffer);
        mPriv->mCondition.signal();
        if (mPriv->mScaledPlayRate == SCALED_PLAY_RATE) {
            mPriv->ensureAudioRender();
        }
        #if DUMP_PCM
        {
            static FILE *fp = NULL;
            if (fp == NULL) {
                fp=fopen("/data/raw.pcm", "wb");
            }
            MediaBufferSP mediaBuffer = pEntry->mBuffer;
            int32_t offset = 0;
            int32_t size = 0;
            uint8_t *data = NULL;
            mediaBuffer->getBufferInfo((uintptr_t*)&data, &offset, &size, 1);
            fwrite(data,1,size, fp);
            fflush(fp);
        }
        #endif
    } else {
        WARNING("Write NULL buffer");
    }
    delete pEntry;
    EXIT();
}

mm_status_t AudioSinkCras::Private::release()
{
    ENTER();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkCras::Private::clearSourceBuffers()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

mm_status_t AudioSinkCras::setAudioStreamType(int type)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    ret = mPriv->setAudioStreamType(type);
    EXIT_AND_RETURN(ret);
}

mm_status_t AudioSinkCras::getAudioStreamType(int *type)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    ret = mPriv->getAudioStreamType(type);
    EXIT_AND_RETURN(ret);
}

mm_status_t AudioSinkCras::setVolume(double volume)
{
    ENTER();

    mVolume = volume;
    if (mPriv->mRender) {
        mPriv->mRender->setVolume(mVolume);
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

double AudioSinkCras::getVolume()
{
    ENTER();
    if (mPriv->mRender) {
        mVolume = mPriv->mRender->getVolume();
    }
    EXIT_AND_RETURN(mVolume);

}

mm_status_t AudioSinkCras::setMute(bool mute)
{
    ENTER();
    if (VOLUME_NOT_SET == mVolume && mPriv->mRender) {
        mVolume = mPriv->mRender->getVolume();
    }

    if (mute) {
        if (mPriv->mRender) {
            mPriv->mRender->setVolume(0.0f);
        }
    } else {
        if (mPriv->mRender) {
            if ((mVolume - VOLUME_NOT_SET) >= -EPSINON && (mVolume - VOLUME_NOT_SET) <= EPSINON) {
                mPriv->mRender->setVolume(1.0f);
            } else {
                mPriv->mRender->setVolume(mVolume);
            }
        }
    }
    mMute = mute;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

bool AudioSinkCras::getMute()
{
    ENTER();
    EXIT_AND_RETURN(mMute);
}


}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    //INFO("createComponent");
    YUNOS_MM::AudioSinkCras *sinkComponent = new YUNOS_MM::AudioSinkCras(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}


void releaseComponent(YUNOS_MM::Component *component)
{
    //INFO("createComponent");
    delete component;
}
}
