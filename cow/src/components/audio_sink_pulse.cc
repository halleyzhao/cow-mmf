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

#include <math.h>
#include "audio_sink_pulse.h"
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

MM_LOG_DEFINE_MODULE_NAME("MSP")

static const char * COMPONENT_NAME = "AudioSinkPulse";
static const char * MMTHREAD_NAME = "AudioSinkPulse::Private::OutputThread";

#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            false
#define MAX_VOLUME              10.0
#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_CHANNEL         2
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

class AudioSinkPulse::Private
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

/* start of outputthread*/
    class OutputThread;
    typedef MMSharedPtr <OutputThread> OutputThreadSP;
    class OutputThread : public MMThread {
      public:

        OutputThread(Private* render)
          : MMThread(MMTHREAD_NAME)
          , mRender(render)
          , mContinue(true)
        {
          ENTER();
          EXIT();
        }

        ~OutputThread()
        {
          ENTER();
          EXIT();
        }

        void signalExit()
        {
          ENTER();
          MMAutoLock locker(mRender->mLock);
          mContinue = false;
          mRender->mCondition.signal();
          EXIT();
        }

        void signalContinue()
        {
          ENTER();
          mRender->mCondition.signal();
          EXIT();
        }

        protected:

        // Write PCM data to pulseaudio
        void main()
        {
          ENTER();
          MediaBufferSP mediaBuffer;
          uint8_t *sourceBuf = NULL;
          int64_t pts = 0;
          int negative = 0;
          pa_usec_t latencyMicros = 0;

          int32_t offset = 0;
          int32_t size = 0;
          while(1) {
              {
                  MMAutoLock locker(mRender->mLock);
                  if (!mContinue) {
                      break;
                  }
                  mRender->mPAWriteableSize = pa_stream_writable_size (mRender->mPAStream);
                  if (mRender->mIsPaused || mRender->mAvailableSourceBuffers.empty()) {
                      VERBOSE("waitting condition\n");
                      mRender->mCondition.wait();
                      VERBOSE("wakeup condition\n");
                      continue;
                  }
                  mediaBuffer = mRender->mAvailableSourceBuffers.front();
                  mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1);
              }
              if (!sourceBuf || size == 0) {
                  if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                      PAMMAutoLock paLoop(mRender->mPALoop);
                      mRender->streamDrain();
                      mRender->mAudioSink->notify(kEventEOS, 0, 0, nilParam);
                  }
                  {
                      MMAutoLock locker(mRender->mLock);
                      mRender->mAvailableSourceBuffers.pop();
                  }
                  continue;
              }
              {
                  MMAutoLock locker(mRender->mLock);
                  if (mediaBuffer->type() != MediaBuffer::MBT_RawAudio) {
                     ERROR("wrong buffer type %d", mediaBuffer->type());
                     mRender->mAvailableSourceBuffers.pop();
                     continue;
                  }
              }
              sourceBuf += offset;
              VERBOSE("pcm buffer %p, offset %d, size %d, pts %" PRId64 " ms",
                  sourceBuf, offset, size, mediaBuffer->pts()/1000ll);

#ifdef DUMP_SINK_PULSE_DATA
              fwrite(sourceBuf,1,size,mRender->mDumpFile);
              fwrite(&size,4,1,mRender->mDumpFileSize);
#endif
              pts = mediaBuffer->pts();

              while (size > 0) {
                  if ((mRender->mPAWriteableSize > 0) && (mRender->mScaledPlayRate == SCALED_PLAY_RATE)) {
                      {
                          PAMMAutoLock paLoop(mRender->mPALoop);
                          int32_t writeSize = mRender->mPAWriteableSize < size ? mRender->mPAWriteableSize:size;

                          if (pa_stream_get_latency(mRender->mPAStream, &latencyMicros, &negative) != 0)
                              ERROR("get latency error");

                          pa_sample_format paFormat = mRender->convertFormatToPulse((snd_format_t)mRender->mFormat);
                              pa_sample_spec sample_spec = {
                                  .format = paFormat,
                                  .rate = (uint32_t)mRender->mSampleRate,
                                  .channels = (uint8_t)mRender->mChannelCount
                              };
                          int64_t duration = pa_bytes_to_usec((uint64_t)writeSize, &sample_spec);
                          if (MM_LIKELY(!mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
                              // Some packet->pts is -1 for TS file. So DO NOT set anchro time when pts is invalid.
                              if (pts >= 0) {
                                mRender->mClockWrapper->setAnchorTime(pts, Clock::getNowUs() + latencyMicros, pts + duration);
                              }
                          }


                          pa_stream_write(mRender->mPAStream, sourceBuf, writeSize, NULL, 0LL, PA_SEEK_RELATIVE);
                          size -= writeSize;

                          if (size > 0) {
                              sourceBuf += writeSize;
                              mRender->mPAWriteableSize = 0;
                              pts += duration/1000ll;
                          }
                      }
                      if (size <= 0) {
                          if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                              PAMMAutoLock paLoop(mRender->mPALoop);
                              mRender->streamDrain();
                              mRender->mAudioSink->notify(kEventEOS, 0, 0, nilParam);
                          }
                          MMAutoLock locker(mRender->mLock);
                          mRender->mAvailableSourceBuffers.pop();
                          break;
                      }
                  } else {
                      MMAutoLock locker(mRender->mLock);
                      VERBOSE("wait for writeable");
                      mRender->mCondition.wait();
                      VERBOSE("wait for writeable wakeup ");
                      if (!mContinue || mRender->mAvailableSourceBuffers.empty()) {
                          break;
                      }
                      if (!mRender->mIsPaused)
                          mRender->mPAWriteableSize = pa_stream_writable_size (mRender->mPAStream);
                      continue;
                  }
              }

          }

          INFO("Output thread exited\n");
          EXIT();
        }

      private:
        AudioSinkPulse::Private *mRender;
        bool mContinue;
    };
/* end of outputthread*/

    static PrivateSP create()
    {
        ENTER();
        PrivateSP priv(new Private());
        if (priv) {
            INFO("private create success");
        }
        return priv;
    }

    mm_status_t init(AudioSinkPulse *audioSink) {
        ENTER();
        mAudioSink = audioSink;
#ifdef DUMP_SINK_PULSE_DATA
        mDumpFile = fopen("/data/audio_sink_pulse.pcm","wb");
        mDumpFileSize = fopen("/data/audio_sink_pulse.size","wb");
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    void uninit() {
#ifdef DUMP_SINK_PULSE_DATA
        fclose(mDumpFile);
        fclose(mDumpFileSize);
#endif

    }
    ~Private() {
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
       ENSURE_AUDIO_DEF_CONNECTION_CLEAN();
#endif
    }
    snd_format_t convertFormatFromPulse(pa_sample_format paFormat);
    pa_sample_format convertFormatToPulse(snd_format_t format);
    static void contextStateCallback(pa_context *c, void *userdata);
    static void contextSinkinputInfoCallback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata);
    static void contextSubscribeCallback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata);
    static void contextSinkInfoCallback(pa_context *c, const pa_sink_input_info *i, int is_last, void *userdata);
    static void streamStateCallback(pa_stream *s, void *userdata);
    static void streamLatencyUpdateCallback(pa_stream *s, void *userdata);
    static void streamUnderflowCallback(pa_stream *s, void *userdata);
    static void streamOverflowCallback(pa_stream *s, void *userdata);
    static void streamWriteCallback(pa_stream *s, size_t length, void *userdata);
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
    mm_status_t streamDrain();
    static void streamFlushCallback(pa_stream*s, int success, void *userdata);
    static void streamDrainCallback(pa_stream*s, int success, void *userdata);
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
    //pa_sink_input_info mPASinkInfo;
    int32_t mPAWriteableSize;

    double mVolume;
    bool mMute;
    int32_t mFormat;
    int32_t mSampleRate;
    int32_t mChannelCount;
    int mCorkResult;
    int mFlushResult;
    int mDrainResult;
    bool mIsDraining;

    std::queue<MediaBufferSP> mAvailableSourceBuffers;

    ClockWrapperSP mClockWrapper;

    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    state_t mState;
    int32_t mTotalBuffersQueued;
    OutputThreadSP mOutputThread;
    AudioSinkPulse *mAudioSink;
#ifdef DUMP_SINK_PULSE_DATA
    FILE* mDumpFile;
    FILE* mDumpFileSize;
#endif
    int32_t mScaledPlayRate;
    std::string mAudioConnectionId;
#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
    ENSURE_AUDIO_DEF_CONNECTION_DECLARE()
#endif

    Private()
        :mPALoop(NULL),
        mPAContext(NULL),
        mPAStream(NULL),
        mPAWriteableSize(0),
        mVolume(DEFAULT_VOLUME),
        mMute(DEFAULT_MUTE),
        mFormat(SND_FORMAT_PCM_16_BIT),
        mSampleRate(DEFAULT_SAMPLE_RATE),
        mChannelCount(DEFAULT_CHANNEL),
        mCorkResult(0),
        mFlushResult(0),
        mDrainResult(0),
        mIsDraining(false),
        mIsPaused(true),
        mCondition(mLock),
        mState(STATE_IDLE),
        mTotalBuffersQueued(0),
        mAudioSink(NULL),
        mScaledPlayRate(SCALED_PLAY_RATE)

    {
        ENTER();
        mClockWrapper.reset(new ClockWrapper());
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
#define ASP_MSG_write (msg_type)11
#define ASP_MSG_setMetaData (msg_type)12

BEGIN_MSG_LOOP(AudioSinkPulse)
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
    MSG_ITEM(ASP_MSG_write, onWrite)
    MSG_ITEM(ASP_MSG_setMetaData, onSetMetaData)
END_MSG_LOOP()

AudioSinkPulse::AudioSinkPulse(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME)
                                                                  , mComponentName(COMPONENT_NAME)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no render");
}

AudioSinkPulse::~AudioSinkPulse()
{
    //release();
}

Component::WriterSP AudioSinkPulse::getWriter(MediaType mediaType)
{
     ENTER();
     if ( (int)mediaType != Component::kMediaTypeAudio ) {
            ERROR("not supported mediatype: %d\n", mediaType);
            return Component::WriterSP((Component::Writer*)NULL);
        }

    Component::WriterSP wd(new AudioSinkPulse::AudioSinkWriter(this));
    return wd;

}

mm_status_t AudioSinkPulse::init()
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

void AudioSinkPulse::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();
    EXIT();
}

const char * AudioSinkPulse::name() const
{
    return mComponentName.c_str();
}


ClockSP AudioSinkPulse::provideClock()
{
    ENTER();
    if (!mPriv)
        ERROR("no render");
    return mPriv->mClockWrapper->provideClock();
}

mm_status_t AudioSinkPulse::prepare()
{
    postMsg(ASP_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::start()
{
    postMsg(ASP_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::resume()
{
    postMsg(ASP_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::stop()
{
    postMsg(ASP_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::pause()
{
    postMsg(ASP_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::seek(int msec, int seekSequence)
{
    postMsg(ASP_MSG_seek, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::reset()
{
    postMsg(ASP_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::flush()
{
    postMsg(ASP_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkPulse::AudioSinkWriter::write(const MediaBufferSP & buf)
{
    ENTER();
    if (!mRender->mPriv)
        return MM_ERROR_NO_COMPONENT;
    AudioSinkPulse::Private::QueueEntry *pEntry = new AudioSinkPulse::Private::QueueEntry;
    pEntry->mBuffer = buf;
    ++mRender->mPriv->mTotalBuffersQueued;
    mRender->postMsg(ASP_MSG_write, 0, (param2_type)pEntry);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkPulse::AudioSinkWriter::setMetaData(const MediaMetaSP & metaData)
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

    INFO("sampleRate %d, format %d, channel %d", mRender->mPriv->mSampleRate, mRender->mPriv->mFormat, mRender->mPriv->mChannelCount);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkPulse::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    //if (!mPriv->mPALoop)
    //    EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    //PAMMAutoLock paLoop(mPriv->mPALoop);//remove these code to setVolume and setMute method.

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_MUTE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mMute = item.mValue.ii;
            mPriv->setMute(mPriv->mMute);
            INFO("key: %s, value: %d\n", item.mName, mPriv->mMute);
        }

        if ( !strcmp(item.mName, MEDIA_ATTR_VOLUME) ) {
            if ( item.mType != MediaMeta::MT_Int64 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            mPriv->mVolume = item.mValue.ld;
            mPriv->setVolume(mPriv->mVolume);
            INFO("key: %s, value: %" PRId64 "\n", item.mName, mPriv->mMute);
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

            mPriv->mScaledPlayRate = item.mValue.ii;
            DEBUG("key: %s, val: %d\n", item.mName, item.mValue.ii);

            continue;
        }

    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t AudioSinkPulse::getParameter(MediaMetaSP & meta) const
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;

    meta->setInt32(MEDIA_ATTR_MUTE, mPriv->mMute);
    meta->setInt64(MEDIA_ATTR_VOLUME, mPriv->mVolume);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

int64_t AudioSinkPulse::getCurrentPosition()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    PAMMAutoLock paLoop(mPriv->mPALoop);
    int64_t currentPosition = -1ll;
    if (mPriv->mClockWrapper && mPriv->mClockWrapper->getCurrentPosition(currentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        currentPosition = -1ll;
    }
    VERBOSE("getCurrentPosition %" PRId64 " ms", currentPosition/1000ll);
    return currentPosition;
}

void AudioSinkPulse::onSetMetaData(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    EXIT();

}

void AudioSinkPulse::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    mPriv->mPAWriteableSize = 0;
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
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();

}

mm_status_t AudioSinkPulse::Private::resumeInternal()
{

    if (!mIsPaused || mIsDraining) {
        ERROR("Aready started\n");
        mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    mm_status_t ret = cork(0);
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create stream\n");
        mAudioSink->notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    if (mIsPaused && mClockWrapper) {
        mClockWrapper->resume();
    }
    mIsPaused = false;
    setState(mState, STATE_STARTED);
    mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mOutputThread->signalContinue();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

void AudioSinkPulse::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    // create thread to handle output buffer
    if (!mPriv->mOutputThread) {
        mPriv->mOutputThread.reset (new AudioSinkPulse::Private::OutputThread(mPriv.get()), MMThread::releaseHelper);
        mPriv->mOutputThread->create();
    }
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkPulse::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkPulse::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        mPriv->mIsPaused = true;
        if (mPriv->mOutputThread) {
            mPriv->mOutputThread->signalExit();
            mPriv->mOutputThread.reset();
        }
    }
    PAMMAutoLock paLoop(mPriv->mPALoop);
    if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED || mPriv->mIsDraining) {
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

void AudioSinkPulse::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    PAMMAutoLock paLoop(mPriv->mPALoop);
    mPriv->mIsPaused = true;
    if (mPriv->mState == mPriv->STATE_PAUSED || mPriv->mIsDraining) {
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
        EXIT1();
    }

    mm_status_t ret = mPriv->cork(1);
    if ( ret != MM_ERROR_SUCCESS ) {
        ERROR("failed to create stream\n");
        notify(kEventError, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    if (mPriv->mClockWrapper) {
        mPriv->mClockWrapper->pause();
    }
    setState(mPriv->mState, mPriv->STATE_PAUSED);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSinkPulse::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
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
        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->flush();
        }
    }

    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}
void AudioSinkPulse::onSeek(param1_type param1, param2_type param2, uint32_t rspId)
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
        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->flush();
        }
    }
    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    notify(kEventSeekComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSinkPulse::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        mPriv->mIsPaused = true;
        if (mPriv->mOutputThread) {
            mPriv->mOutputThread->signalExit();
            mPriv->mOutputThread.reset();
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
            if (mPriv->mClockWrapper) {
                mPriv->mClockWrapper->flush();
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
        mPriv->mScaledPlayRate = SCALED_PLAY_RATE;
        mPriv->clearSourceBuffers();
    }
    mPriv->release();
    setState(mPriv->mState, mPriv->STATE_IDLE);
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT1();
}

void AudioSinkPulse::onSetParameters(param1_type param1, param2_type param2, uint32_t rspId)
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

void AudioSinkPulse::onGetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //if (!strcmp((char *)param1, "getVolume")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getVolume(), 0, NULL);
    //} else if (!strcmp((char *)param1, "getMute")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getMute(), 0, NULL);
    //}
    EXIT();
}

void AudioSinkPulse::onWrite(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    AudioSinkPulse::Private::QueueEntry *pEntry = (AudioSinkPulse::Private::QueueEntry *)param2;
    if (pEntry->mBuffer) {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mAvailableSourceBuffers.push(pEntry->mBuffer);
        mPriv->mCondition.signal();
    } else {
        WARNING("Write NULL buffer");
    }
    delete pEntry;
    EXIT();
}

bool AudioSinkPulse::Private::waitPAOperation(pa_operation *op) {
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

snd_format_t AudioSinkPulse::Private::convertFormatFromPulse(pa_sample_format paFormat)
{
    ENTER();
    for (int i = 0; format_map[i].spformat != SND_FORMAT_INVALID; ++i) {
        if (format_map[i].pa == paFormat)
            EXIT_AND_RETURN(format_map[i].spformat);
    }
    EXIT_AND_RETURN(SND_FORMAT_INVALID);
}

pa_sample_format AudioSinkPulse::Private::convertFormatToPulse(snd_format_t format)
{
    ENTER();
    for (int i = 0; format_map[i].spformat != SND_FORMAT_INVALID; ++i) {
        if (format_map[i].spformat == format)
            EXIT_AND_RETURN(format_map[i].pa);
    }
    EXIT_AND_RETURN(PA_SAMPLE_INVALID);
}

void AudioSinkPulse::Private::contextStateCallback(pa_context *c, void *userdata)
{
    ENTER();
    if (c == NULL) {
        ERROR("invalid param\n");
        return;
    }

    AudioSinkPulse::Private * me = static_cast<AudioSinkPulse::Private*>(userdata);
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

void AudioSinkPulse::Private::contextSinkinputInfoCallback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private *me = static_cast<AudioSinkPulse::Private*>(userdata);
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


void AudioSinkPulse::Private::contextSubscribeCallback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private *me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t t = pa_subscription_event_type_t(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            break;
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            if (me->mPAStream && idx == pa_stream_get_index(me->mPAStream )) {
                    switch (t) {
                        case PA_SUBSCRIPTION_EVENT_REMOVE:
                            INFO("PulseAudio sink killed");
                            break;
                        default:
                            pa_operation *op = pa_context_get_sink_input_info(c, idx, contextSinkinputInfoCallback, me);
                            if (!op) {
                                ERROR("failed to get pa sink input info");
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

void AudioSinkPulse::Private::contextSinkInfoCallback(pa_context *c, const pa_sink_input_info *i, int is_last, void *userdata)
{
    ENTER();
    INFO("context sink info");
    EXIT();
}

void AudioSinkPulse::Private::streamStateCallback(pa_stream *s, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private *me = static_cast<AudioSinkPulse::Private*>(userdata);
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

void AudioSinkPulse::Private::streamLatencyUpdateCallback(pa_stream *s, void *userdata)
{
    ENTER();
#if 0
    AudioSinkPulse *me = static_cast<AudioSinkPulse*>(userdata);
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

void AudioSinkPulse::Private::streamUnderflowCallback(pa_stream *s, void *userdata)
{
    ENTER();
    INFO("under flow");
    EXIT();
}

void AudioSinkPulse::Private::streamOverflowCallback(pa_stream *s, void *userdata)
{
    ENTER();
    INFO("over flow");
    EXIT();
}

void AudioSinkPulse::Private::streamWriteCallback(pa_stream *s, size_t length, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private *me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    if (!me->mIsPaused) {
        MMAutoLock locker(me->mLock);
        if (!me->mAvailableSourceBuffers.empty()) {
            me->mCondition.signal();
        }
        VERBOSE("stream write length = %d",length);
    }

    EXIT();
}

void AudioSinkPulse::Private::streamSuspendedCallback (pa_stream * s, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private *me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    if (pa_stream_is_suspended (s))
        INFO ("stream suspended");
    else
        INFO ("stream resumed");
    EXIT();
}

void AudioSinkPulse::Private::streamStartedCallback (pa_stream * s, void *userdata)
{
    ENTER();
    INFO ("stream started");
    EXIT();
}

void AudioSinkPulse::Private::streamEventCallback (pa_stream * s, const char *name, pa_proplist * pl, void *userdata)
{
    ENTER();
    INFO ("stream event name = %s",name);
    EXIT();
}

mm_status_t AudioSinkPulse::Private::creatPAContext()
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
mm_status_t AudioSinkPulse::Private::creatPAStream()
{
    ENTER();

    pa_format_info *format = pa_format_info_new();
    format->encoding = PA_ENCODING_PCM;
    pa_sample_format paFormat = convertFormatToPulse((snd_format_t)mFormat);

    if (paFormat == PA_SAMPLE_INVALID) {
        ERROR("PulseAudio: invalid format");
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }
    pa_format_info_set_sample_format(format, paFormat);
    pa_format_info_set_channels(format, mChannelCount);
    pa_format_info_set_rate(format, mSampleRate);
    // pa_format_info_set_channel_map(fi, NULL);
    if (!pa_format_info_valid(format)) {
        ERROR("PulseAudio: invalid format");
        pa_format_info_free(format);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    pa_sample_spec ss;
    ss.channels = mChannelCount;
    ss.format = convertFormatToPulse((snd_format_t)mFormat);
    ss.rate = mSampleRate;

    pa_proplist *pl = pa_proplist_new();
    if (pl) {
        pa_proplist_sets(pl, "log-backtrace", "10");
    }

#ifdef ENABLE_DEFAULT_AUDIO_CONNECTION
   ENSURE_AUDIO_DEF_CONNECTION_ENSURE(mAudioConnectionId, MMAMHelper::playChnnelMain());
#endif
    MMLOGI("device: %s\n", mAudioConnectionId.c_str());
    pa_proplist_sets(pl, "connection_id", mAudioConnectionId.c_str());
    mPAStream = pa_stream_new_with_proplist(mPAContext, "audio stream", &ss, NULL, pl);
    //mPAStream = pa_stream_new_extended(mPAContext, "audio stream", &format, 1, pl);
    if (!mPAStream) {
        pa_format_info_free(format);
        pa_proplist_free(pl);
        ERROR("PulseAudio: failed to create a stream");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    pa_format_info_free(format);
    pa_proplist_free(pl);

    /* install essential callbacks */
    pa_stream_set_write_callback(mPAStream, streamWriteCallback, this);
    pa_stream_set_state_callback(mPAStream, streamStateCallback, this);
    pa_stream_set_underflow_callback (mPAStream, streamUnderflowCallback, this);
    pa_stream_set_overflow_callback (mPAStream, streamOverflowCallback, this);
    //pa_stream_set_latency_update_callback (mPAStream, streamLatencyUpdateCallback, this);
    pa_stream_set_suspended_callback (mPAStream, streamSuspendedCallback, this);
    pa_stream_set_started_callback (mPAStream, streamStartedCallback, this);
    pa_stream_set_event_callback (mPAStream, streamEventCallback, this);

    pa_sample_spec sample_spec = {
        .format = paFormat,
        .rate = (uint32_t)mSampleRate,
        .channels = (uint8_t)mChannelCount
    };
    pa_buffer_attr wanted;
    wanted.maxlength = (uint32_t)-1; // max buffer size on the server
    wanted.tlength = (uint32_t) pa_usec_to_bytes(PA_USEC_PER_MSEC * 200/*DEFAULT_TLENGTH_MSEC*/, &sample_spec);
    //wanted.tlength = (uint32_t)-1; // ?
    wanted.prebuf = 1;//(uint32_t)-1; // play as soon as possible
    wanted.minreq = (uint32_t)-1;
    wanted.fragsize = (uint32_t)-1;
    // PA_STREAM_NOT_MONOTONIC?
    pa_stream_flags_t flags = pa_stream_flags_t(PA_STREAM_NOT_MONOTONIC|PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_AUTO_TIMING_UPDATE);
    if (pa_stream_connect_playback(mPAStream, NULL, &wanted, flags, NULL, NULL) < 0) {
        ERROR("PulseAudio failed: pa_stream_connect_playback");
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
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
mm_status_t AudioSinkPulse::Private::freePAContext()
{
    PAMMAutoLock paLoop(mPALoop);

    if (mPAContext) {
        pa_context_disconnect (mPAContext);
        pa_context_unref (mPAContext);
        mPAContext = NULL;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
mm_status_t AudioSinkPulse::Private::freePASteam()
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
mm_status_t AudioSinkPulse::Private::freePALoop()
{
    ENTER();
    if (mPALoop) {
        pa_threaded_mainloop_stop(mPALoop);
        pa_threaded_mainloop_free(mPALoop);
        mPALoop = NULL;
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkPulse::Private::streamFlush()
{
    ENTER();
    mFlushResult = 0;
    if (!waitPAOperation(pa_stream_flush(mPAStream, streamFlushCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    }

    INFO("result: %d\n", mFlushResult);

    EXIT_AND_RETURN(mFlushResult > 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSinkPulse::Private::streamFlushCallback(pa_stream*s, int success, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private * me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    me->mFlushResult = success ? 1 : -1;
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();
}

mm_status_t AudioSinkPulse::Private::cork(int b)
{
    ENTER();
    mCorkResult = 0;
    if (!waitPAOperation(pa_stream_cork(mPAStream, b, streamCorkCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    }

    INFO("result: %d\n", mCorkResult);

    EXIT_AND_RETURN(mCorkResult > 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSinkPulse::Private::streamCorkCallback(pa_stream*s, int success, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private * me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    me->mCorkResult = success ? 1 : -1;
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();
}

mm_status_t AudioSinkPulse::Private::streamDrain()
{
    ENTER();
    mDrainResult = 0;
    mIsDraining = true;
    if (!waitPAOperation(pa_stream_drain(mPAStream, streamDrainCallback, this))) {
        ERROR("fail to drain stream");
    }
    INFO("mDrainResult= %d",mDrainResult);
    mIsDraining = false;

    EXIT_AND_RETURN(mFlushResult > 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSinkPulse::Private::streamDrainCallback(pa_stream*s, int success, void *userdata)
{
    ENTER();
    AudioSinkPulse::Private * me = static_cast<AudioSinkPulse::Private*>(userdata);
    MMASSERT(me);
    me->mDrainResult = success ? 1 : -1;
    pa_threaded_mainloop_signal (me->mPALoop, 0);
    EXIT();
}

mm_status_t AudioSinkPulse::Private::release()
{
    ENTER();
    mm_status_t ret;
    ret = freePASteam();
    ret = freePAContext();
    ret = freePALoop();

    EXIT_AND_RETURN(ret);
}

void AudioSinkPulse::Private::clearPACallback()
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

void AudioSinkPulse::Private::clearSourceBuffers()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

mm_status_t AudioSinkPulse::Private::setVolume(double volume)
{
    ENTER();

    if (mPALoop)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    PAMMAutoLock paLoop(mPALoop);

    pa_cvolume vol;
    pa_operation *o = NULL;
    uint32_t idx;

    mVolume = volume;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    pa_cvolume_reset(&vol, mChannelCount);
    pa_cvolume_set(&vol, mChannelCount, pa_volume_t(volume*double(PA_VOLUME_NORM)));
    if (!(o = pa_context_set_sink_input_volume (mPAContext, idx, &vol, NULL, NULL)))
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
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    goto unlock;
  }
}
double AudioSinkPulse::Private::getVolume()
{
    ENTER();
    double v = DEFAULT_VOLUME;
    pa_operation *o = NULL;
    uint32_t idx;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!waitPAOperation(pa_context_get_sink_input_info(mPAContext, idx, contextSinkinputInfoCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
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
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    goto unlock;
  }

}
mm_status_t AudioSinkPulse::Private::setMute(bool mute)
{
    ENTER();

    if (!mPALoop)
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);

    PAMMAutoLock paLoop(mPALoop);

    pa_operation *o = NULL;
    uint32_t idx;

    mMute = mute;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!(o = pa_context_set_sink_input_mute (mPAContext, idx, mute, NULL, NULL)))
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
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    goto unlock;
  }
}

bool AudioSinkPulse::Private::getMute()
{
    ENTER();
    pa_operation *o = NULL;
    uint32_t idx;
    bool mute = mMute;

    if ((idx = pa_stream_get_index (mPAStream)) == PA_INVALID_INDEX)
        goto no_index;

    if (!waitPAOperation(pa_context_get_sink_input_info(mPAContext, idx, contextSinkinputInfoCallback, this))) {
        PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
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
    PA_ERROR(MM_ERROR_OP_FAILED, "failed to get pa sink input info", pa_context_errno(mPAContext));
    goto unlock;
  }

}

mm_status_t AudioSinkPulse::setAudioConnectionId(const char * connectionId)
{
    mPriv->mAudioConnectionId = connectionId;
    return MM_ERROR_SUCCESS;
}

const char * AudioSinkPulse::getAudioConnectionId() const
{
    return mPriv->mAudioConnectionId.c_str();
}


}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    //INFO("createComponent");
    YUNOS_MM::AudioSinkPulse *sinkComponent = new YUNOS_MM::AudioSinkPulse(mimeType, isEncoder);
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
