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
#include "audio_src_cras.h"
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_audio.h"
#include "multimedia/mm_audio_compat.h"

#include <util/Utils.h>

#include <stdio.h>

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("ASC")

static const char * COMPONENT_NAME = "AudioSrcCras";
//static const char * MMTHREAD_NAME = "AudioSrcCras::Private::OutputThread";

#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            false
#define MAX_VOLUME              10.0
#define DEFAULT_SAMPLE_RATE     48000
#define DEFAULT_CHANNEL         2
#define CLOCK_TIME_NONE         -1
#define DEFAULT_INPUT_SIZE      16384
#define SPECTRUM_HIGH 3000
#define SPECTRUM_LOW 10
#define DURATION_THRESHOLD      2000000

struct FormatSize {
    snd_format_t format;
    uint32_t size;
};
static const FormatSize SizeTable[] = {
    {SND_FORMAT_PCM_8_BIT, 1},
    {SND_FORMAT_PCM_16_BIT, 2},
    {SND_FORMAT_PCM_32_BIT, 4}
};

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


class AudioSrcCras::Private
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

    class AudioSrcReader : public Reader {
    public:
        AudioSrcReader(AudioSrcCras * src){
            mSrc = src;
            mContinue = true;
        }
        ~AudioSrcReader(){
            MMAutoLock locker(mSrc->mPriv->mLock);
            mContinue = false;
            mSrc->mPriv->mCondition.signal();
        }

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        AudioSrcCras *mSrc;
        bool mContinue;
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

    mm_status_t init(AudioSrcCras *audiosrc) {
        ENTER();
        mAudioSrc = audiosrc;
#ifdef DUMP_SRC_CRAS_DATA
        mDumpFile = fopen("/data/audio_sink_pulse.pcm","wb");
        mDumpFileSize = fopen("/data/audio_sink_pulse.size","wb");
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    void uninit() {
#ifdef DUMP_SRC_CRAS_DATA
        fclose(mDumpFile);
        fclose(mDumpFileSize);
#endif

    }
    ~Private() {}
    mm_status_t release();

    void clearSourceBuffers_l();
    int64_t bufferDurationUs_l();
    mm_status_t creatCrasClient();
    mm_status_t creatCrasStream();
    uint32_t formatSize(snd_format_t format);
    static void audioCallback(YunOSAudioNS::AudioCapture::evt_t evt, void* user, void *info);
    static bool releaseInputBuffer(MediaBuffer* mediaBuffer);

	uint32_t mBytesPerFrame;
    YunOSAudioNS::AudioCapture *mCapture;

    int32_t mFormat;
    int32_t mSampleRate;
    int32_t mChannelCount;
    int32_t mMaxInputSize;
    adev_source_t mAudioSource;
    int32_t mMusicSpectrum;
    uint64_t mSpectrumDuration;
    int32_t mMaxSpectrum;

    std::queue<MediaBufferSP> mAvailableSourceBuffers;

    bool mIsPaused;
    bool mIsEOS;
    bool mIsAudioError;
    Condition mCondition;
    Lock mLock;
    state_t mState;
    int32_t mTotalBuffersQueued;
    AudioSrcCras *mAudioSrc;
    MediaMetaSP mMetaData;
    uint64_t mDoubleDuration;
    uint64_t mPTS;
#ifdef DUMP_SRC_CRAS_DATA
    FILE* mDumpFile;
    FILE* mDumpFileSize;
#endif
    int64_t mStartTimeUs;

    Private()
        :
        mBytesPerFrame(0),
        mCapture(NULL),
        mFormat(SND_FORMAT_PCM_16_BIT),
        mSampleRate(DEFAULT_SAMPLE_RATE),
        mChannelCount(DEFAULT_CHANNEL),
        mMaxInputSize(DEFAULT_INPUT_SIZE),
        mAudioSource(ADEV_SOURCE_MIC),
        mMusicSpectrum(-1),
        mSpectrumDuration(0),
        mMaxSpectrum(0),
        mIsPaused(true),
        mIsEOS(false),
        mIsAudioError(false),
        mCondition(mLock),
        mState(STATE_IDLE),
        mTotalBuffersQueued(0),
        mAudioSrc(NULL),
        mDoubleDuration(0),
        mPTS(0),
        mStartTimeUs(-1)
    {
        ENTER();
        mMetaData = MediaMeta::create();
        EXIT();
    }

    MM_DISALLOW_COPY(Private);

};

#define ASC_MSG_prepare (msg_type)1
#define ASC_MSG_start (msg_type)2
#define ASC_MSG_stop (msg_type)5
#define ASC_MSG_flush (msg_type)6
#define ASC_MSG_reset (msg_type)8
#define ASC_MSG_signal_EOS (msg_type)9


BEGIN_MSG_LOOP(AudioSrcCras)
    MSG_ITEM(ASC_MSG_prepare, onPrepare)
    MSG_ITEM(ASC_MSG_start, onStart)
    MSG_ITEM(ASC_MSG_stop, onStop)
    MSG_ITEM(ASC_MSG_flush, onFlush)
    MSG_ITEM(ASC_MSG_reset, onReset)
    MSG_ITEM(ASC_MSG_signal_EOS, onSignalEOS)
END_MSG_LOOP()

AudioSrcCras::AudioSrcCras(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME)
                                                                  , mComponentName(COMPONENT_NAME)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no render");
}

AudioSrcCras::~AudioSrcCras()
{
    //release();
}

Component::ReaderSP AudioSrcCras::getReader(MediaType mediaType)
{
     ENTER();
     if ( (int)mediaType != Component::kMediaTypeAudio ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::ReaderSP((Component::Reader*)NULL);
        }

    Component::ReaderSP rsp(new AudioSrcCras::Private::AudioSrcReader(this));
    return rsp;
}

mm_status_t AudioSrcCras::init()
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

void AudioSrcCras::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();

    EXIT();
}

const char * AudioSrcCras::name() const
{
    return mComponentName.c_str();
}

mm_status_t AudioSrcCras::signalEOS()
{
    MMAutoLock locker(mPriv->mLock);
    //mPriv->mIsPaused = true;
    mPriv->mIsEOS = true;
    if (mPriv->mIsAudioError) {
        notify(kEventEOS, 0, 0, nilParam);
        mPriv->mIsAudioError = false;
    }
    postMsg(ASC_MSG_signal_EOS, 0, NULL, 50*1000);
    return MM_ERROR_SUCCESS;
}

void AudioSrcCras::onSignalEOS(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
    mediaBuf->setSize(0);
    mediaBuf->setPts(0);
    mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    mPriv->mAvailableSourceBuffers.push(mediaBuf);

    // maybe call mCapture->stop here. check it in future
    EXIT1();
}

mm_status_t AudioSrcCras::prepare()
{
    postMsg(ASC_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcCras::start()
{
    postMsg(ASC_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcCras::stop()
{
    postMsg(ASC_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t AudioSrcCras::reset()
{
    postMsg(ASC_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcCras::flush()
{
    postMsg(ASC_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcCras::Private::AudioSrcReader::read(MediaBufferSP & buffer)
{
    ENTER();
    MMAutoLock locker(mSrc->mPriv->mLock);
    if (!mSrc->mPriv->mAvailableSourceBuffers.size()) {
        mSrc->mPriv->mCondition.timedWait(mSrc->mPriv->mDoubleDuration);
    }
    if (!mSrc->mPriv->mAvailableSourceBuffers.size()) {
        EXIT_AND_RETURN(MM_ERROR_AGAIN);
    }

    buffer = mSrc->mPriv->mAvailableSourceBuffers.front();
    mSrc->mPriv->mAvailableSourceBuffers.pop();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

MediaMetaSP AudioSrcCras::Private::AudioSrcReader::getMetaData()
{
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, mSrc->mPriv->mFormat);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, mSrc->mPriv->mSampleRate);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, mSrc->mPriv->mChannelCount);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_MAX_INPUT_SIZE, mSrc->mPriv->mMaxInputSize);

    mSrc->mPriv->mMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    return mSrc->mPriv->mMetaData;
}

mm_status_t AudioSrcCras::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    MMAutoLock locker(mPriv->mLock);
    int ret = MM_ERROR_SUCCESS;

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mSampleRate = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mSampleRate);
            ret = MM_ERROR_SUCCESS;
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_CHANNEL_COUNT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mChannelCount = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mChannelCount);
            ret = MM_ERROR_SUCCESS;
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_AUDIO_SOURCE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mAudioSource = (adev_source_t)item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mAudioSource);
            ret = MM_ERROR_SUCCESS;
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_FORMAT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mFormat = (snd_format_t)item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mFormat);
            ret = MM_ERROR_SUCCESS;
        }
        else if ( !strcmp(item.mName, MEDIA_ATTR_MUSIC_SPECTRUM) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mMusicSpectrum = (snd_format_t)item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mMusicSpectrum);
            ret = MM_ERROR_SUCCESS;
        }
        else if (!strcmp(item.mName, MEDIA_ATTR_START_TIME)) {
            if ( item.mType != MediaMeta::MT_Int64 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mPriv->mStartTimeUs = item.mValue.ld;
            DEBUG("mStartTimeUs %" PRId64 "", mPriv->mStartTimeUs);
        }
    }

    EXIT_AND_RETURN(ret);

}

void AudioSrcCras::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    uint32_t inBufSize = YunOSAudioNS::AudioCapture::getDefaultPeriodSize(
        mPriv->mSampleRate,
        adev_get_in_mask_from_channel_count(mPriv->mChannelCount),
        (snd_format_t)mPriv->mFormat
        );

    mPriv->mMaxInputSize = inBufSize;
    mPriv->mCapture = YunOSAudioNS::AudioCapture::create(
        mPriv->mAudioSource,
        mPriv->mSampleRate,
        (snd_format_t)mPriv->mFormat,
        adev_get_in_mask_from_channel_count(mPriv->mChannelCount),
        inBufSize,
        mPriv->audioCallback,
        this
        );
    if (!mPriv->mCapture) {
        MMLOGE("failed to create audio capture\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT1();
    }
    mPriv->mDoubleDuration = (1000*1000ll / mPriv->mSampleRate)* 1000ll;
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

/*static*/ bool AudioSrcCras::Private::releaseInputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

uint32_t AudioSrcCras::Private::formatSize(snd_format_t format)
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

void AudioSrcCras::Private::audioCallback(YunOSAudioNS::AudioCapture::evt_t evt, void* user, void *info)
{
    AudioSrcCras *me = static_cast<AudioSrcCras *>(user);
    if (!me) {
        return;
    }
    if (evt == YunOSAudioNS::AudioCapture::EVT_STREAM_ERROR) {
        me->mPriv->mIsAudioError = true;
        me->mPriv->mAudioSrc->notify(Component::kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
        return;
    }

    int64_t timeUs = MMMsgThread::getTimeUs();
    if (timeUs < me->mPriv->mStartTimeUs) {
        DEBUG("drop audio frame, %" PRId64 " < %" PRId64 "", timeUs, me->mPriv->mStartTimeUs);
        return;
    }
    if (evt == YunOSAudioNS::AudioCapture::EVT_MORE_DATA) {
        MMAutoLock locker(me->mPriv->mLock);
        if(me->mPriv->mIsPaused) return;
        YunOSAudioNS::AudioCapture::Buffer *bufferReaded = static_cast<YunOSAudioNS::AudioCapture::Buffer *>(info);
        MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
        uint8_t *buffer = NULL;
        size_t readSize = 0;
        readSize= bufferReaded->size;
        buffer = new uint8_t[readSize];
        memcpy(buffer, bufferReaded->mData, readSize);
#ifdef DUMP_SRC_CRAS_DATA
            fwrite(bufferReaded->mData, 1, readSize, me->mPriv->mDumpFile);
#endif
        mediaBuf->setBufferInfo((uintptr_t *)&buffer, NULL, (int32_t *)&readSize, 1);
        mediaBuf->setSize((int64_t)readSize);
        mediaBuf->addReleaseBufferFunc(releaseInputBuffer);
        mediaBuf->setPts(me->mPriv->mPTS);
        if(me->mPriv->mIsEOS) {
            mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
            me->mPriv->mIsPaused = true;
            INFO("set EOS buffer");
        }
        uint64_t duration = (((readSize / (adev_bytes_per_sample((snd_format_t)me->mPriv->mFormat) *
            me->mPriv->mChannelCount)) * 1000*1000ll) / me->mPriv->mSampleRate);
        mediaBuf->setDuration(duration);
        me->mPriv->mPTS += duration;
        me->mPriv->mAvailableSourceBuffers.push(mediaBuf);
        if (me->mPriv->mAvailableSourceBuffers.size() == 1) {
            me->mPriv->mCondition.signal();
        }
        me->mPriv->mSpectrumDuration += duration;
        if (me->mPriv->mMusicSpectrum > 0) {
            int16_t *bufferSpectrum = (int16_t *) bufferReaded->mData;
            for (int i = 0; i < bufferReaded->frameCount; i++) {
                int16_t value = *bufferSpectrum++;
                if (value < 0)value = -value;
                if (value > me->mPriv->mMaxSpectrum)
                    me->mPriv->mMaxSpectrum = value;
            }
        }
        if (me->mPriv->mMusicSpectrum > 0 && (me->mPriv->mSpectrumDuration >= (1000*1000ll / me->mPriv->mMusicSpectrum))) {
            if (me->mPriv->mMaxSpectrum > SPECTRUM_HIGH)
                me->mPriv->mMaxSpectrum = 100;
            else
                me->mPriv->mMaxSpectrum = me->mPriv->mMaxSpectrum * 100 / SPECTRUM_HIGH;
            me->mPriv->mAudioSrc->notify(Component::kEventMusicSpectrum, me->mPriv->mMaxSpectrum, 0, nilParam);
            me->mPriv->mSpectrumDuration = 0;
            me->mPriv->mMaxSpectrum = 0;
        }

        int64_t bufferDuation = me->mPriv->bufferDurationUs_l();
        if (bufferDuation > DURATION_THRESHOLD) {
            WARNING("read size: %u, pts %0.3f, buffer duration %0.3f, buffer size %d\n",
                readSize, me->mPriv->mPTS/1000000.0f, bufferDuation/1000000.0f,
                me->mPriv->mAvailableSourceBuffers.size());
        }
    }
}

void AudioSrcCras::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);

    if (!mPriv->mIsPaused) {
        ERROR("Aready started\n");
        notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }
    if (!mPriv->mCapture) {
        ERROR("no audio capture\n");
        mPriv->mIsAudioError = true;
        notify(Component::kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
        EXIT1();
    } else {
        int ret = mPriv->mCapture->start();
        if (0 != ret) {
            if (ret == -EBUSY) {
                ERROR("audio device is occupied, ret %d", ret);
                // use MM_ERROR_RESOURCE_LIMIT instead of EBUSY in future
                notify(kEventStartResult, ret, 0, nilParam);
            } else if (ret == STATUS_PERMISSION_DENIED) {
                MMLOGE("no permission for audio capture: %d.\n", ret);
                notify(kEventStartResult, MM_ERROR_PERMISSION_DENIED, 0, nilParam);
            } else {
                MMLOGE("Failed to start AudioCapture: %d.\n", ret);
                notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, nilParam);
            }
            mPriv->mIsAudioError = true;
            EXIT1();
        }
    }

    mPriv->mIsPaused = false;
    setState(mPriv->mState, mPriv->STATE_STARTED);
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcCras::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    if (mPriv->mCapture) {
        mPriv->mCapture->stop();
    }
    mPriv->mIsPaused = true;

    MMAutoLock locker(mPriv->mLock);

    if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED) {
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }


    setState(mPriv->mState, mPriv->STATE_STOPED);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcCras::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    MMAutoLock locker(mPriv->mLock);
    //mPriv->mCapture->flush();//FIXME:need supported by libaudio
    mPriv->clearSourceBuffers_l();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcCras::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        if(mPriv->mCapture)
            delete mPriv->mCapture;
        mPriv->clearSourceBuffers_l();
    }
    mPriv->release();
    setState(mPriv->mState, mPriv->STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

mm_status_t AudioSrcCras::Private::release()
{
    ENTER();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSrcCras::Private::clearSourceBuffers_l()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

int64_t AudioSrcCras::Private::bufferDurationUs_l()
{
    int64_t header = mAvailableSourceBuffers.front()->pts();
    int64_t tailer = mAvailableSourceBuffers.back()->pts();
    return tailer - header > 0 ? tailer - header : 0;
}


}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    //INFO("createComponent");
    YUNOS_MM::AudioSrcCras *sinkComponent = new YUNOS_MM::AudioSrcCras(mimeType, isEncoder);
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
