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


#include "audio_src_pulse.h"
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_audio.h"

#include <pulse/sample.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pthread.h>
#include <stdio.h>
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
#include <multimedia/mm_amhelper.h>
#endif

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MSRCP")

static const char * COMPONENT_NAME = "AudioSrcPulse";
static const char * MMTHREAD_NAME = "AudioSinkPulse::Private::InputThread";

#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            false
#define MAX_VOLUME              10.0
#define DEFAULT_SAMPLE_RATE     48000
#define DEFAULT_CHANNEL         2
#define DEFAULT_FORMAT          SND_FORMAT_PCM_16_BIT
#define CLOCK_TIME_NONE         -1

#define PA_ERROR(_retcode, _info, _pa_err_no) do {\
        ERROR("%s, retcode: %d, pa: %s\n", _info, _retcode, pa_strerror(_pa_err_no));\
        return _retcode;\
}while(0)

static const struct format_entry {
    snd_format_t spformat;
    pa_sample_format_t pa;
} format_map[] = {
    {SND_FORMAT_PCM_16_BIT, PA_SAMPLE_S16LE},
    {SND_FORMAT_PCM_32_BIT, PA_SAMPLE_S32LE},
    {SND_FORMAT_PCM_8_BIT, PA_SAMPLE_U8},
    {SND_FORMAT_INVALID, PA_SAMPLE_INVALID}
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

class PAMMAutoLock
{
  public:
    explicit PAMMAutoLock(pa_threaded_mainloop* loop) : mPALoop(loop)
    {
        pa_threaded_mainloop_lock (mPALoop);
    }
    ~PAMMAutoLock()
    {
        pa_threaded_mainloop_unlock (mPALoop);
    }
    pa_threaded_mainloop* mPALoop;
  private:
    MM_DISALLOW_COPY(PAMMAutoLock);
};

class AudioSrcPulse::Private
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
        AudioSrcReader(AudioSrcPulse * src){
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
        AudioSrcPulse *mSrc;
        bool mContinue;
    };

/* start of inputthread*/
    class InputThread;
    typedef MMSharedPtr <InputThread> InputThreadSP;
    class InputThread : public MMThread {
    public:

        InputThread(Private* render)
          : MMThread(MMTHREAD_NAME)
          , mPriv(render)
          , mContinue(true)
        {
          ENTER();
          EXIT();
        }

        ~InputThread()
        {
          ENTER();
          EXIT();
        }

        void signalExit()
        {
          ENTER();
          MMAutoLock locker(mPriv->mLock);
          mContinue = false;
          mPriv->mCondition.signal();
          EXIT();
        }

        void signalContinue()
        {
          ENTER();
          mPriv->mCondition.signal();
          EXIT();
        }

        static bool releaseInputBuffer(MediaBuffer* mediaBuffer)
        {
            uint8_t *buffer = NULL;
            if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
                WARNING("error in release mediabuffer");
                return false;
            }
            MM_RELEASE_ARRAY(buffer);
            return true;
        }

        protected:

        // Read PCM data from pulseaudio
        void main()
        {
            ENTER();
            int toRead = 0;
            size_t readSize = 0;
            while(1) {
                {
                    MMAutoLock locker(mPriv->mLock);
                    if (!mContinue) {
                        MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                        mediaBuf->setSize((int64_t)0);
                        mediaBuf->setPts(mPriv->mPTS);
                        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
                        mPriv->mAvailableSourceBuffers.push(mediaBuf);
                        mPriv->mCondition.signal();
                        break;
                    }
                    if (mPriv->mIsPaused || mPriv->mPAReadSize == 0) {
                        VERBOSE("waitting condition\n");
                        mPriv->mCondition.wait();
                        VERBOSE("wakeup condition\n");
                        continue;
                    }
                    toRead = mPriv->mPAReadSize;
                    mPriv->mPAReadSize = 0;
                    readSize = 0;
                }

                mPriv->mPAReadData = NULL;
                while(toRead > 0) {
                    MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                    uint8_t *buffer = NULL;
                    {
                        PAMMAutoLock paLoop(mPriv->mPALoop);

                        int ret = pa_stream_peek(mPriv->mPAStream, (const void**)&mPriv->mPAReadData, &readSize);

                        if (ret != 0) {
                            ERROR("read error: %s\n", pa_strerror(ret));
                            continue;
                        }
                        if (readSize <= 0) {
                            ERROR("no data\n");
                            break;
                        }
                        if (!mPriv->mPAReadData) {
                            pa_stream_drop(mPriv->mPAStream);
                            toRead -= readSize;
                            mPriv->mPAReadData = NULL;
                            readSize = 0;
                            ERROR("there is a hole.\n");
                            continue;
                        }
                        VERBOSE("read size: %u", readSize);
                        buffer = new uint8_t[readSize];
                        memcpy(buffer, mPriv->mPAReadData, readSize);
#ifdef DUMP_SRC_PULSE_DATA
                        fwrite(buffer, 1, readSize, mPriv->mDumpFile);
#endif
                        ret = pa_stream_drop(mPriv->mPAStream);
                        if (ret != 0) {
                            MMLOGE("drop error: %d\n", ret);
                        }
                        mediaBuf->setPts(mPriv->mPTS);
                        pa_sample_spec sample_spec = {
                            .format = mPriv->convertFormatToPulse((snd_format_t)mPriv->mFormat),
                            .rate = (uint32_t)mPriv->mSampleRate,
                            .channels = (uint8_t)mPriv->mChannelCount
                        };
                        mPriv->mPTS += pa_bytes_to_usec((uint64_t)readSize, &sample_spec);
                        mediaBuf->setBufferInfo((uintptr_t *)&buffer, NULL, (int32_t *)&readSize, 1);
                        mediaBuf->setSize((int64_t)readSize);
                        mediaBuf->addReleaseBufferFunc(releaseInputBuffer);
                        toRead -= readSize;
                        mPriv->mPAReadData = NULL;
                        readSize = 0;
                    }
                    {
                        MMAutoLock locker(mPriv->mLock);
                        mPriv->mAvailableSourceBuffers.push(mediaBuf);
                        mPriv->mCondition.signal();
                    }
                }

            }

            INFO("Input thread exited\n");
            EXIT();
        }

      private:
        AudioSrcPulse::Private *mPriv;
        bool mContinue;
    };
/* end of InputThread*/

    static PrivateSP create()
    {
        ENTER();
        PrivateSP priv(new Private());
        if (priv) {
            INFO("private create success");
        }
        return priv;
    }

    mm_status_t init(AudioSrcPulse *audioSource) {
        ENTER();
        mAudioSource = audioSource;
#ifdef DUMP_SRC_PULSE_DATA
        mDumpFile = fopen("/data/audio_src_pulse.pcm","wb");
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    mm_status_t uninit() {
#ifdef DUMP_SRC_PULSE_DATA
        fclose(mDumpFile);
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    ~Private() {
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
       ENSURE_AUDIO_DEF_CONNECTION_CLEAN();
#endif
    }
    snd_format_t convertFormatFromPulse(pa_sample_format paFormat);
    pa_sample_format convertFormatToPulse(snd_format_t format);
    static void contextStateCallback(pa_context *c, void *userdata);
    static void contextSourceOutputInfoCallback(pa_context *c, const pa_source_output_info *i, int eol, void *userdata);
    static void contextSubscribeCallback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata);
    static void contextSourceInfoCallback(pa_context *c, const pa_source_output_info *i, int is_last, void *userdata);
    static void streamStateCallback(pa_stream *s, void *userdata);
    static void streamLatencyUpdateCallback(pa_stream *s, void *userdata);
    static void streamUnderflowCallback(pa_stream *s, void *userdata);
    static void streamOverflowCallback(pa_stream *s, void *userdata);
    static void streamReadCallback(pa_stream *s, size_t length, void *userdata);
    static void streamSuccessCallback(pa_stream*s, int success, void *userdata);
    static void streamSuspendedCallback (pa_stream * s, void *userdata);
    static void streamStartedCallback (pa_stream * s, void *userdata);
    static void streamEventCallback (pa_stream * s, const char *name, pa_proplist * pl, void *userdata);
    mm_status_t creatPAContext();
    mm_status_t creatPAStream();
    mm_status_t release();
    mm_status_t freePAContext();
    mm_status_t freePASteam();
    mm_status_t freePALoop();
    mm_status_t streamFlush();
    static void streamFlushCallback(pa_stream*s, int success, void *userdata);
    mm_status_t cork(int b);
    static void streamCorkCallback(pa_stream*s, int success, void *userdata);
    void clearPACallback();
    void clearSourceBuffers();
    mm_status_t setVolume(double volume);
    double getVolume();
    mm_status_t setMute(bool mute);
    bool getMute();
    bool waitPAOperation(pa_operation *op);
    mm_status_t resumeInternal();

    pa_threaded_mainloop *mPALoop;
    pa_context *mPAContext;
    pa_stream *mPAStream;
    size_t mPAReadSize;

    double mVolume;
    bool mMute;
    int32_t mFormat;
    int32_t mSampleRate;
    int32_t mChannelCount;
    int mCorkResult;
    int mFlushResult;

    std::queue<MediaBufferSP> mAvailableSourceBuffers;
    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    state_t mState;
    int32_t mTotalBuffersQueued;
    InputThreadSP mInputThread;
    AudioSrcPulse *mAudioSource;
    MediaMetaSP mMetaData;
    const uint8_t * mPAReadData;
    uint64_t mDoubleDuration;
    uint64_t mPTS;
#ifdef DUMP_SRC_PULSE_DATA
            FILE* mDumpFile;
#endif
    std::string mAudioConnectionId;
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
    ENSURE_AUDIO_DEF_CONNECTION_DECLARE()
#endif

    Private()
        :mPALoop(NULL),
        mPAContext(NULL),
        mPAStream(NULL),
        mPAReadSize(0),
        mVolume(DEFAULT_VOLUME),
        mMute(DEFAULT_MUTE),
        mFormat(DEFAULT_FORMAT),
        mSampleRate(DEFAULT_SAMPLE_RATE),
        mChannelCount(DEFAULT_CHANNEL),
        mCorkResult(0),
        mFlushResult(0),
        mIsPaused(true),
        mCondition(mLock),
        mState(STATE_IDLE),
        mTotalBuffersQueued(0),
        mPAReadData(NULL),
        mDoubleDuration(0),
        mPTS(0)

    {
        ENTER();
        mMetaData = MediaMeta::create();
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
       ENSURE_AUDIO_DEF_CONNECTION_INIT();
#endif
        EXIT();
    }

    MM_DISALLOW_COPY(Private);

};

#define ASP_MSG_prepare (msg_type)1
#define ASP_MSG_start (msg_type)2
#define ASP_MSG_resume (msg_type)3
#define ASP_MSG_pause (msg_type)4
#define ASP_MSG_stop (msg_type)5
#define ASP_MSG_flush (msg_type)6
#define ASP_MSG_seek (msg_type)7
#define ASP_MSG_reset (msg_type)8
#define ASP_MSG_setParameters (msg_type)9
#define ASP_MSG_getParameters (msg_type)10

BEGIN_MSG_LOOP(AudioSrcPulse)
    MSG_ITEM(ASP_MSG_prepare, onPrepare)
    MSG_ITEM(ASP_MSG_start, onStart)
    MSG_ITEM(ASP_MSG_resume, onResume)
    MSG_ITEM(ASP_MSG_pause, onPause)
    MSG_ITEM(ASP_MSG_stop, onStop)
    MSG_ITEM(ASP_MSG_flush, onFlush)
    MSG_ITEM(ASP_MSG_seek, onSeek)
    MSG_ITEM(ASP_MSG_reset, onReset)
    MSG_ITEM(ASP_MSG_setParameters, onSetParameters)
    MSG_ITEM(ASP_MSG_getParameters, onGetParameters)
END_MSG_LOOP()

AudioSrcPulse::AudioSrcPulse(const char *mimeType, bool isEncoder) :MMMsgThread(COMPONENT_NAME)
                                                                ,mComponentName(COMPONENT_NAME)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no render");
}

AudioSrcPulse::~AudioSrcPulse()
{
    //release();
}

Component::ReaderSP AudioSrcPulse::getReader(MediaType mediaType)
{
     ENTER();
     if ( (int)mediaType != Component::kMediaTypeAudio ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::ReaderSP((Component::Reader*)NULL);
        }

    Component::ReaderSP rsp(new AudioSrcPulse::Private::AudioSrcReader(this));
    return rsp;
}

mm_status_t AudioSrcPulse::init()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    int ret = mPriv->init(this); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSrcPulse::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();
    EXIT();
}

const char * AudioSrcPulse::name() const
{
    return mComponentName.c_str();
}

mm_status_t AudioSrcPulse::signalEOS() {
    mPriv->mIsPaused = true;
    if (mPriv->mInputThread) {
        mPriv->mInputThread->signalExit();
        mPriv->mInputThread.reset();
    }
    return MM_ERROR_SUCCESS;
}

mm_status_t AudioSrcPulse::setAudioConnectionId(const char * connectionId)
{
    mPriv->mAudioConnectionId = connectionId;
}

const char * AudioSrcPulse::getAudioConnectionId() const
{
    return mPriv->mAudioConnectionId.c_str();
}

mm_status_t AudioSrcPulse::prepare()
{
    postMsg(ASP_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::start()
{
    postMsg(ASP_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::resume()
{
    postMsg(ASP_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::stop()
{
    postMsg(ASP_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::pause()
{
    postMsg(ASP_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::seek(int msec, int seekSequence)
{
    postMsg(ASP_MSG_seek, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::reset()
{
    postMsg(ASP_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::flush()
{
    postMsg(ASP_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSrcPulse::Private::AudioSrcReader::read(MediaBufferSP & buffer)
{
    ENTER();
    MMAutoLock locker(mSrc->mPriv->mLock);
    if (mSrc->mPriv->mAvailableSourceBuffers.empty()) {
        mSrc->mPriv->mCondition.timedWait(mSrc->mPriv->mDoubleDuration);
        EXIT_AND_RETURN(MM_ERROR_AGAIN);
    } else {
        buffer = mSrc->mPriv->mAvailableSourceBuffers.front();
        mSrc->mPriv->mAvailableSourceBuffers.pop();
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
}

MediaMetaSP AudioSrcPulse::Private::AudioSrcReader::getMetaData()
{
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, mSrc->mPriv->mFormat);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, mSrc->mPriv->mSampleRate);
    mSrc->mPriv->mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, mSrc->mPriv->mChannelCount);

    mSrc->mPriv->mMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    return mSrc->mPriv->mMetaData;
}

mm_status_t AudioSrcPulse::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    if (mPriv->mPALoop)
        PAMMAutoLock paLoop(mPriv->mPALoop);

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_MUTE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mMute = item.mValue.ii;
            mPriv->setMute(mPriv->mMute);
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mMute);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_VOLUME) ) {
            if ( item.mType != MediaMeta::MT_Int64 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mVolume = item.mValue.ld;
            mPriv->setVolume(mPriv->mVolume);
            MMLOGI("key: %s, value: %" PRId64 "\n", item.mName, mPriv->mVolume);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_FORMAT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mFormat = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mFormat);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mSampleRate = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mSampleRate);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_CHANNEL_COUNT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mChannelCount = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPriv->mChannelCount);
            continue;
        }
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t AudioSrcPulse::getParameter(MediaMetaSP & meta) const
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    if (!mPriv->mPALoop)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    PAMMAutoLock paLoop(mPriv->mPALoop);
    meta->setInt64(MEDIA_ATTR_VOLUME, mPriv->getVolume());
    meta->setInt32(MEDIA_ATTR_MUTE, mPriv->getMute());

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSrcPulse::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    mPriv->mPAReadSize = 0;
    mm_status_t ret = mPriv->creatPAContext();
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create context\n");
        notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT1();
    }

    ret = mPriv->creatPAStream();
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create stream\n");
        notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
        EXIT1();
    }
    pa_sample_spec sample_spec = {
        .format = mPriv->convertFormatToPulse((snd_format_t)mPriv->mFormat),
        .rate = (uint32_t)mPriv->mSampleRate,
        .channels = (uint8_t)mPriv->mChannelCount
    };
    size_t frameSize = pa_frame_size(&sample_spec);
    mPriv->mDoubleDuration = pa_bytes_to_usec((uint64_t)frameSize, &sample_spec) * 1000ll;
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

mm_status_t AudioSrcPulse::Private::resumeInternal()
{
    if (!mIsPaused) {
        ERROR("Aready started\n");
        mAudioSource->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    mm_status_t ret = cork(0);
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create stream\n");
        mAudioSource->notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    mIsPaused = false;
    setState(mState, STATE_STARTED);
    mAudioSource->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mInputThread->signalContinue();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

void AudioSrcPulse::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    // create thread to handle output buffer
    if (!mPriv->mInputThread) {
        mPriv->mInputThread.reset (new AudioSrcPulse::Private::InputThread(mPriv.get()),MMThread::releaseHelper);
        mPriv->mInputThread->create();
    }
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSrcPulse::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSrcPulse::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        mPriv->mIsPaused = true;
        if (mPriv->mInputThread) {
            mPriv->mInputThread->signalExit();
            mPriv->mInputThread.reset();
        }
    }
    PAMMAutoLock paLoop(mPriv->mPALoop);
    if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED) {
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }
    if (mPriv->mState == mPriv->STATE_STARTED) {
        mm_status_t ret = mPriv->streamFlush();
        if (MM_ERROR_SUCCESS != ret) {
            ERROR("flush fail");
            notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT1();
        }

        ret = mPriv->cork(1);
        if (MM_ERROR_SUCCESS != ret) {
            ERROR("cork fail");
            notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT1();
        }
    }
    mPriv->clearPACallback();
    setState(mPriv->mState, mPriv->STATE_STOPED);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcPulse::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    mPriv->mIsPaused = true;
    if (mPriv->mState == mPriv->STATE_PAUSED) {
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }

    mm_status_t ret = mPriv->cork(1);
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create stream\n");
        notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    setState(mPriv->mState, mPriv->STATE_PAUSED);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcPulse::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        PAMMAutoLock paLoop(mPriv->mPALoop);
        mm_status_t ret = mPriv->streamFlush();
        if (MM_ERROR_SUCCESS != ret) {
            ERROR("flush fail");
            notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT1();
        }
    }

    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}
void AudioSrcPulse::onSeek(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        PAMMAutoLock paLoop(mPriv->mPALoop);
        mm_status_t ret = mPriv->streamFlush();
        if (MM_ERROR_SUCCESS != ret) {
            ERROR("flush fail");
            notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT1();
        }
    }
    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    notify(kEventSeekComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcPulse::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        mPriv->mIsPaused = true;
        if (mPriv->mInputThread) {
            mPriv->mInputThread->signalExit();
            mPriv->mInputThread.reset();
        }
    }
    {
        PAMMAutoLock paLoop(mPriv->mPALoop);
        if (mPriv->mState == mPriv->STATE_STARTED) {
            mm_status_t ret = mPriv->streamFlush();
            if (MM_ERROR_SUCCESS != ret) {
                ERROR("flush fail");
                notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
                EXIT1();
            }
            ret = mPriv->cork(1);
            if (MM_ERROR_SUCCESS != ret) {
                ERROR("cork fail");
                notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
                EXIT1();
            }
        }
        mPriv->clearPACallback();
    }
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->clearSourceBuffers();
    }
    mPriv->release();
    setState(mPriv->mState, mPriv->STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSrcPulse::onSetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    /*
    if (!strcmp((char *)param1, "setVolume")) {
        setVolume((double)param2);
    } else if (!strcmp((char *)param1, "setMute")) {
        setMute((bool)param2);
    } else if (!strcmp((char *)param1, "sampleRate")) {
        mSampleRate = (uint32_t)param2;
    } else if (!strcmp((char *)param1, "format")) {
        mFormat = (snd_format_t)param2;
    } else if (!strcmp((char *)param1, "channel")) {
        mChannelCount = (uint8_t)param2;
    }
    */
    //notify(EVENT_SETPARAMETERSCOMPLETE, MM_ERROR_SUCCESS, 0, NULL);
    EXIT();
}

void AudioSrcPulse::onGetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //if (!strcmp((char *)param1, "getVolume")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getVolume(), 0, NULL);
    //} else if (!strcmp((char *)param1, "getMute")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getMute(), 0, NULL);
    //}
    EXIT();
}

bool AudioSrcPulse::Private::waitPAOperation(pa_operation *op) {
    if (!op) {
        return false;
    }
    pa_operation_state_t state = pa_operation_get_state(op);
    while (state == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(mPALoop);
        state = pa_operation_get_state(op);
    }
    pa_operation_unref(op);
    return state == PA_OPERATION_DONE;
}

snd_format_t AudioSrcPulse::Private::convertFormatFromPulse(pa_sample_format paFormat)
{
    ENTER();
    for (int i = 0; format_map[i].spformat != SND_FORMAT_INVALID; ++i) {
        if (format_map[i].pa == paFormat)
            EXIT_AND_RETURN(format_map[i].spformat);
    }
    EXIT_AND_RETURN(SND_FORMAT_INVALID);
}

pa_sample_format AudioSrcPulse::Private::convertFormatToPulse(snd_format_t format)
{
    ENTER();
    for (int i = 0; format_map[i].spformat != SND_FORMAT_INVALID; ++i) {
        if (format_map[i].spformat == format)
            EXIT_AND_RETURN(format_map[i].pa);
    }
    EXIT_AND_RETURN(PA_SAMPLE_INVALID);
}

void AudioSrcPulse::Private::contextStateCallback(pa_context *c, void *userdata)
{
    ENTER();
    if (c == NULL) {
        ERROR("invalid param\n");
        return;
    }

    AudioSrcPulse::Private * me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            pa_threaded_mainloop_signal (me->mPALoop, 0);
            break;
        default:
            break;
    }
    EXIT();
}

void AudioSrcPulse::Private::contextSourceOutputInfoCallback(pa_context *c, const pa_source_output_info *i, int eol, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private *me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    if (!i)
        goto done;

    if (!me->mPAStream)
        goto done;

    if (i->index == pa_stream_get_index (me->mPAStream)) {
        me->mVolume = pa_sw_volume_to_linear (pa_cvolume_max (&i->volume));
        me->mMute = i->mute;
    }

done:
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();

}


void AudioSrcPulse::Private::contextSubscribeCallback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private *me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t t = pa_subscription_event_type_t(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            if (me->mPAStream && idx == pa_stream_get_index(me->mPAStream )) {
                    switch (t) {
                        case PA_SUBSCRIPTION_EVENT_REMOVE:
                            INFO("PulseAudio source killed");
                            break;
                        default:
                            pa_operation *op = pa_context_get_source_output_info(c, idx, contextSourceOutputInfoCallback, me);
                            if (!op) {
                                ERROR("failed to get pa source output info");
                            }
                            break;
                    }
               }
            break;
        case  PA_SUBSCRIPTION_EVENT_CARD:
            INFO("PA_SUBSCRIPTION_EVENT_CARD");
            break;
        default:
            break;
    }
    EXIT();
}

void AudioSrcPulse::Private::contextSourceInfoCallback(pa_context *c, const pa_source_output_info *i, int is_last, void *userdata)
{
    ENTER();
    INFO("context source info");
    EXIT();
}

void AudioSrcPulse::Private::streamStateCallback(pa_stream *s, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private *me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    switch (pa_stream_get_state(s)) {
    case PA_STREAM_FAILED:
        INFO("pa stream failed");
        pa_threaded_mainloop_signal(me->mPALoop, 0);
        break;
    case PA_STREAM_READY:
        INFO("pa stream ready");
        pa_threaded_mainloop_signal(me->mPALoop, 0);
        break;
    case PA_STREAM_TERMINATED:
        INFO("pa stream terminated");
        pa_threaded_mainloop_signal(me->mPALoop, 0);
        break;
    default:
        break;
    }
    EXIT();
}

void AudioSrcPulse::Private::streamLatencyUpdateCallback(pa_stream *s, void *userdata)
{
    ENTER();
#if 0
    AudioSrcPulse *me = static_cast<AudioSrcPulse*>(userdata);
    MMASSERT(me);
    const pa_timing_info *info;
    pa_usec_t sink_usec;
    info = pa_stream_get_timing_info (s);
    if (!info) {
      return;
    }
    sink_usec = info->configured_sink_usec;
    VERBOSE("write_index_corrupt = %d write_index = %llu  read_index_corrupt = %d read_index = %d info->sink_usec = %llu configured_sink_usec = %llu \n",
        info->write_index_corrupt,
        info->write_index,
        info->read_index_corrupt,
        info->read_index,
        info->sink_usec,
        sink_usec);
#endif
    EXIT();
}

void AudioSrcPulse::Private::streamUnderflowCallback(pa_stream *s, void *userdata)
{
    ENTER();
    INFO("under flow");
    EXIT();
}

void AudioSrcPulse::Private::streamOverflowCallback(pa_stream *s, void *userdata)
{
    ENTER();
    INFO("over flow");
    EXIT();
}

void AudioSrcPulse::Private::streamReadCallback(pa_stream *s, size_t length, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private *me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    MMAutoLock locker(me->mLock);
    if (!me->mIsPaused) {
        me->mPAReadSize = length;
        me->mCondition.signal();
        VERBOSE("stream readable length = %d", length);
    }

    EXIT();
}

void AudioSrcPulse::Private::streamSuspendedCallback (pa_stream * s, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private *me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    if (pa_stream_is_suspended (s))
        INFO ("stream suspended");
    else
        INFO ("stream resumed");
    EXIT();
}

void AudioSrcPulse::Private::streamStartedCallback (pa_stream * s, void *userdata)
{
    ENTER();
    INFO ("stream started");
    EXIT();
}

void AudioSrcPulse::Private::streamEventCallback (pa_stream * s, const char *name, pa_proplist * pl, void *userdata)
{
    ENTER();
    INFO ("stream event name = %s",name);
    EXIT();
}

mm_status_t AudioSrcPulse::Private::creatPAContext()
{
    ENTER();
    int ret;
    pa_mainloop_api *api;

    /* Set up a new main loop */
    MMLOGV("newing pa thread main loop\n");
    mPALoop = pa_threaded_mainloop_new();
    if (mPALoop == NULL){
        PA_ERROR(MM_ERROR_NO_MEM, "failed to get pa api", pa_context_errno(mPAContext));
    }

    api = pa_threaded_mainloop_get_api(mPALoop);
    pa_proplist *proplist = pa_proplist_new();
    if ( !proplist ) {
        PA_ERROR(MM_ERROR_NO_MEM, "failed to new proplist", pa_context_errno(mPAContext));
    }
    ret = pa_proplist_sets(proplist, "log-backtrace", "10");
    if ( ret < 0 ) {
        pa_proplist_free(proplist);
        PA_ERROR(MM_ERROR_NO_MEM, "failed to set proplist", ret);
    }

    mPAContext = pa_context_new_with_proplist(api, COMPONENT_NAME, proplist);
    pa_proplist_free(proplist);
    if (mPAContext == NULL) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa api", pa_context_errno(mPAContext));
    }

    pa_context_set_state_callback(mPAContext, contextStateCallback, this);

    /* Connect the context */
    INFO("connecting pa context\n");
    ret = pa_context_connect(mPAContext, "127.0.0.1", PA_CONTEXT_NOFLAGS, NULL);
    if ( ret < 0) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to connect to context", ret);
    }

    INFO("starting pa mainloop\n");
    ret = pa_threaded_mainloop_start(mPALoop);
    if(ret != 0){
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to start mainloop", ret);
    }

    mm_status_t result;
    INFO("waitting context ready\n");
    PAMMAutoLock paLoop(mPALoop);
    while ( 1 ) {
        pa_context_state_t state = pa_context_get_state (mPAContext);
        INFO("now state: %d\n", state);

        if ( state == PA_CONTEXT_READY ) {
            INFO("ready\n");
            result = MM_ERROR_SUCCESS;
            break;
        } else if ( state == PA_CONTEXT_TERMINATED || state == PA_CONTEXT_FAILED ) {
            INFO("terminated or failed\n");
            result = MM_ERROR_OP_FAILED;
            break;
        }

        INFO("not expected state, wait\n");
        pa_threaded_mainloop_wait (mPALoop);
    }

    EXIT_AND_RETURN(result);

}
mm_status_t AudioSrcPulse::Private::creatPAStream()
{
    ENTER();

    uint32_t flags = PA_STREAM_AUTO_TIMING_UPDATE |
                                                  PA_STREAM_START_CORKED;
#ifdef USE_PA_STREAM_INTERPOLATE_TIMING
        flags |= PA_STREAM_INTERPOLATE_TIMING;
#endif
#ifdef USE_PA_STREAM_ADJUST_LATENCY
        flags |= PA_STREAM_ADJUST_LATENCY;
#endif

    pa_sample_spec ss = {
        .format = convertFormatToPulse((snd_format_t)mFormat),
        .rate = (uint32_t)mSampleRate,
        .channels = (uint8_t)mChannelCount
    };

    MMLOGI("format: format: %d, rate: %d, channels: %d\n", ss.format, ss.rate, ss.channels);

    pa_proplist *pl = pa_proplist_new();
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
    ENSURE_AUDIO_DEF_CONNECTION_ENSURE(mAudioConnectionId, MMAMHelper::recordChnnelMic());
#endif
    MMLOGI("connection_id: %s\n", mAudioConnectionId.c_str());
    pa_proplist_sets(pl, "connection_id", mAudioConnectionId.c_str());
    mPAStream = pa_stream_new_with_proplist(mPAContext, COMPONENT_NAME, &ss, NULL, pl);
    //mPAStream = pa_stream_new(mPAContext, COMPONENT_NAME, &ss, NULL);
    if (!mPAStream) {
        ERROR("PulseAudio: failed to create a stream");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    pa_buffer_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.maxlength = (uint32_t) -1;;
    attr.tlength = (uint32_t) -1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t) -1;
    attr.fragsize = (uint32_t) -1;
    int ret = pa_stream_connect_record(mPAStream, NULL, &attr, (pa_stream_flags_t)flags);
    if( ret != 0 ){
        ERROR("PulseAudio: failed to connect record stream");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    /* install essential callbacks */
    pa_stream_set_read_callback(mPAStream, streamReadCallback, this);
    pa_stream_set_state_callback(mPAStream, streamStateCallback, this);
    pa_stream_set_underflow_callback (mPAStream, streamUnderflowCallback, this);
    pa_stream_set_overflow_callback (mPAStream, streamOverflowCallback, this);
    //pa_stream_set_latency_update_callback (mPAStream, streamLatencyUpdateCallback, this);
    pa_stream_set_suspended_callback (mPAStream, streamSuspendedCallback, this);
    pa_stream_set_started_callback (mPAStream, streamStartedCallback, this);
    pa_stream_set_event_callback (mPAStream, streamEventCallback, this);

    PAMMAutoLock paLoop(mPALoop);
    while (true) {
        const pa_stream_state_t st = pa_stream_get_state(mPAStream);
        if (st == PA_STREAM_READY)
            break;
        if (!PA_STREAM_IS_GOOD(st)) {
            ERROR("PulseAudio stream init failed");
            EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
        }
        pa_threaded_mainloop_wait(mPALoop);
    }
    if (pa_stream_is_suspended(mPAStream)) {
        ERROR("PulseAudio stream is suspende");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    INFO("over\n");
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}
mm_status_t AudioSrcPulse::Private::freePAContext()
{
    PAMMAutoLock paLoop(mPALoop);

    if (mPAContext) {
        pa_context_disconnect (mPAContext);
        pa_context_unref (mPAContext);
        mPAContext = NULL;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
mm_status_t AudioSrcPulse::Private::freePASteam()
{
    ENTER();
    PAMMAutoLock paLoop(mPALoop);
    if (mPAStream) {
        pa_stream_disconnect (mPAStream);
        pa_stream_unref(mPAStream);
        mPAStream = NULL;
    }
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
    ENSURE_AUDIO_DEF_CONNECTION_CLEAN();
#endif
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
mm_status_t AudioSrcPulse::Private::freePALoop()
{
    ENTER();
    if (mPALoop) {
        pa_threaded_mainloop_stop(mPALoop);
        pa_threaded_mainloop_free(mPALoop);
        mPALoop = NULL;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSrcPulse::Private::streamFlush()
{
    ENTER();
    mFlushResult = 0;
    if (!waitPAOperation(pa_stream_flush(mPAStream, streamFlushCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    }

    INFO("result: %d\n", mFlushResult);

    EXIT_AND_RETURN(mFlushResult > 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSrcPulse::Private::streamFlushCallback(pa_stream*s, int success, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private * me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    me->mFlushResult = success ? 1 : -1;
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();
}

mm_status_t AudioSrcPulse::Private::cork(int b)
{
    ENTER();
    mCorkResult = 0;
    if (!waitPAOperation(pa_stream_cork(mPAStream, b, streamCorkCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    }

    INFO("result: %d\n", mCorkResult);

    EXIT_AND_RETURN(mCorkResult > 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSrcPulse::Private::streamCorkCallback(pa_stream*s, int success, void *userdata)
{
    ENTER();
    AudioSrcPulse::Private * me = static_cast<AudioSrcPulse::Private*>(userdata);
    MMASSERT(me);
    me->mCorkResult = success ? 1 : -1;
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();
}

mm_status_t AudioSrcPulse::Private::release()
{
    ENTER();
    mm_status_t ret;
    ret = freePASteam();
    ret = freePAContext();
    ret = freePALoop();

    EXIT_AND_RETURN(ret);
}

void AudioSrcPulse::Private::clearPACallback()
{
    if (mPAContext) {
        /* Make sure we don't get any further callbacks */
        pa_context_set_state_callback (mPAContext, NULL, NULL);
        pa_context_set_subscribe_callback (mPAContext, NULL, NULL);
    }
    if (mPAStream) {
        /* Make sure we don't get any further callbacks */
        pa_stream_set_state_callback(mPAStream, NULL, NULL);
        pa_stream_set_write_callback(mPAStream, NULL, NULL);
        pa_stream_set_underflow_callback(mPAStream, NULL, NULL);
        pa_stream_set_overflow_callback(mPAStream, NULL, NULL);
        pa_stream_set_latency_update_callback (mPAStream, NULL, NULL);
        pa_stream_set_suspended_callback (mPAStream, NULL, NULL);
        pa_stream_set_started_callback (mPAStream, NULL, NULL);
        pa_stream_set_event_callback (mPAStream, NULL, NULL);
    }
}

void AudioSrcPulse::Private::clearSourceBuffers()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

mm_status_t AudioSrcPulse::Private::setVolume(double volume)
{
    ENTER();
    pa_cvolume vol;
    pa_operation *o = NULL;
    uint32_t idx;

    mVolume = volume;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    pa_cvolume_reset(&vol, mChannelCount);
    pa_cvolume_set(&vol, mChannelCount, pa_volume_t(volume*double(PA_VOLUME_NORM)));
    if (!(o = pa_context_set_source_output_volume (mPAContext, idx, &vol, NULL, NULL)))
        goto volume_failed;

unlock:
    if (o)
        pa_operation_unref (o);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

  /* ERRORS */
no_index:
  {
    INFO ("we don't have a stream index");
    goto unlock;
  }
volume_failed:
  {
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    goto unlock;
  }
}
double AudioSrcPulse::Private::getVolume()
{
    ENTER();
    double v = DEFAULT_VOLUME;
    pa_operation *o = NULL;
    uint32_t idx;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!waitPAOperation(pa_context_get_source_output_info(mPAContext, idx, contextSourceOutputInfoCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
        goto volume_failed;
    }

unlock:
    v = mVolume;
    if (o)
        pa_operation_unref (o);
    EXIT_AND_RETURN(v);

  /* ERRORS */
no_index:
  {
    INFO ("we don't have a stream index");
    goto unlock;
  }
volume_failed:
  {
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    goto unlock;
  }

}
mm_status_t AudioSrcPulse::Private::setMute(bool mute)
{
    ENTER();
    pa_operation *o = NULL;
    uint32_t idx;

    mMute = mute;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!(o = pa_context_set_source_output_mute (mPAContext, idx, mute, NULL, NULL)))
        goto mute_failed;

unlock:
    if (o)
        pa_operation_unref (o);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

  /* ERRORS */
no_index:
  {
    INFO ("we don't have a stream index");
    goto unlock;
  }
mute_failed:
  {
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    goto unlock;
  }
}

bool AudioSrcPulse::Private::getMute()
{
    ENTER();
    pa_operation *o = NULL;
    uint32_t idx;
    bool mute = mMute;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!waitPAOperation(pa_context_get_source_output_info(mPAContext, idx, contextSourceOutputInfoCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
        goto mute_failed;
    }

unlock:
    mute = mMute;
    if (o)
        pa_operation_unref (o);
    EXIT_AND_RETURN(mute);

  /* ERRORS */
no_index:
  {
    INFO ("we don't have a stream index");
    goto unlock;
  }
mute_failed:
  {
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa source output info", pa_context_errno(mPAContext));
    goto unlock;
  }

}


}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    //INFO("createComponent");
    YUNOS_MM::AudioSrcPulse *sourceComponent = new YUNOS_MM::AudioSrcPulse(mimeType, isEncoder);
    if (sourceComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sourceComponent);
}


void releaseComponent(YUNOS_MM::Component *component)
{
    //INFO("createComponent");
    delete component;
}
}
