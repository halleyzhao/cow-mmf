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

#include "audio_sink_LPA.h"
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"

//#include "cras_client.h"
#include <audio/AudioManager.h>
#include <audio/AudioRender.h>

#include <stdio.h>

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("ASC")

static const char * COMPONENT_NAME = "AudioSinkLPA";
static const char * MMTHREAD_NAME = "AudioSinkLPA::ReadThread";

#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            false
#define MAX_VOLUME              10.0
#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_CHANNEL         2
#define CLOCK_TIME_NONE         -1
#define RESAMPLE_AUDIO_SIZE     8192*4
#define DEFAULT_BIT_RATE        192000
#define PAUSEMAXUS              10*1000*1000

const int TrafficControlLowBar = 1;
const int TrafficControlHighBar = 10;

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ADEV_MAKE_OFFLOAD_INFO_VERSION(maj,min) \
             ((((maj) & 0xff) << 8) | ((min) & 0xff))

#define ADEV_OFFLOAD_INFO_VERSION_0_1 ADEV_MAKE_OFFLOAD_INFO_VERSION(0, 1)
#define ADEV_OFFLOAD_INFO_VERSION_CURRENT ADEV_OFFLOAD_INFO_VERSION_0_1

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

struct mime_conv_t {
    const char* mime;
    snd_format_t format;
};

static const struct mime_conv_t mimeLookup[] = {
    { MEDIA_MIMETYPE_AUDIO_MPEG,        SND_FORMAT_MP3 },
    { MEDIA_MIMETYPE_AUDIO_RAW,         SND_FORMAT_PCM_16_BIT },
    { MEDIA_MIMETYPE_AUDIO_AMR_NB,      SND_FORMAT_AMR_NB },
    { MEDIA_MIMETYPE_AUDIO_AMR_WB,      SND_FORMAT_AMR_WB },
    { MEDIA_MIMETYPE_AUDIO_AAC,         SND_FORMAT_AAC },
    { MEDIA_MIMETYPE_AUDIO_VORBIS,      SND_FORMAT_VORBIS },
    { MEDIA_MIMETYPE_AUDIO_OPUS,        SND_FORMAT_OPUS},
    { MEDIA_MIMETYPE_AUDIO_AC3,         SND_FORMAT_AC3},
    { MEDIA_MIMETYPE_AUDIO_EC3,         SND_FORMAT_E_AC3},
    { 0, SND_FORMAT_INVALID }
};

mm_status_t mapMimeToAudioFormat( snd_format_t& format, const char* mime )
{
const struct mime_conv_t* p = &mimeLookup[0];
    while (p->mime != NULL) {
        if (!strcmp(mime, p->mime)) {
            format = p->format;
            return MM_ERROR_SUCCESS;
        }
        ++p;
    }

    return MM_ERROR_INVALID_PARAM;
}

// ////////////////////// ReadThread
AudioSinkLPA::ReadThread::ReadThread(AudioSinkLPA *sink)
    : MMThread(MMTHREAD_NAME)
    , mAudioSink(sink)
    , mContinue(true)
{
    ENTER();
    EXIT();
}

AudioSinkLPA::ReadThread::~ReadThread()
{
    ENTER();
    EXIT();
}

void AudioSinkLPA::ReadThread::signalExit()
{
    ENTER();
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mAudioSink->mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    MMAutoLock locker(mAudioSink->mLock);
    mContinue = false;
    mAudioSink->mCondition.signal();
    EXIT();
}

void AudioSinkLPA::ReadThread::signalContinue()
{
    ENTER();
    mAudioSink->mCondition.signal();
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

// read Buffer
void AudioSinkLPA::ReadThread::main()
{
    ENTER();

    while(1) {

        MMAutoLock locker(mAudioSink->mLock);
        if (!mContinue) {
            break;
        }
        if (mAudioSink->mIsPaused || mAudioSink->mIsEOS) {
            INFO("pause wait");
            mAudioSink->mCondition.wait();
            INFO("pause wait wakeup");
            continue;
        }
        MediaBufferSP mediaBuffer;
        mAudioSink->mReader->read(mediaBuffer);

        if (mediaBuffer) {
#ifdef DECODE_BEFORE_RENDER
            int64_t targetTime = -1ll;
            if (mediaBuffer->getMediaMeta()->getInt64(MEDIA_ATTR_TARGET_TIME, targetTime)) {
                mAudioSink->mTargetTimeUs = targetTime;
                INFO("mTargetTimeUs %0.3f\n", mAudioSink->mTargetTimeUs/1000000.0f);
            }

            int gotFrame = 0;
            uint8_t *pktData = NULL;
            int pktSize = 0;
            uint8_t *buffer = NULL;
            int bufferSize = 0;
            int decodedSize = 0;

            AVBufferHelper::convertToAVPacket(mediaBuffer, &mAudioSink->mAVPacket);
            // AVDemuxer ensures that EOS mediaBuffer comes w/o data
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
                mAudioSink->mIsPaused = true;
                if (!mAudioSink->mAVPacket || mediaBuffer->size() == 0) {
                    mediaBuf->setSize(0);
                    if (mAudioSink->write(mediaBuf) != MM_ERROR_SUCCESS) {
                        ERROR("fail to write Sink");
                        EXIT();
                    }
                    continue;
                }
            }
            // If flush flag is set, discard all the decoded frames and flush internel buffers in codecs.
            if (mAudioSink->mNeedFlush) {
                DEBUG("need flush old buffer in codec\n");
                avcodec_flush_buffers(mAudioSink->mAVCodecContext);
                mAudioSink->mNeedFlush = false;
            }

            pktData = mAudioSink->mAVPacket->data;
            pktSize = mAudioSink->mAVPacket->size;
            while(pktSize > 0) {
                // ensure an AVFrame to accommodate output frame
                if (!mAudioSink->mAVFrame)
                    mAudioSink->mAVFrame = av_frame_alloc();
                if (!mAudioSink->mAVFrame) {
                    ERROR("fail to av_frame_alloc");
                    break;
                }

                int len = avcodec_decode_audio4(mAudioSink->mAVCodecContext, mAudioSink->mAVFrame, &gotFrame, mAudioSink->mAVPacket);
                if (len < 0) {
                    INFO("Error while decoding.\n");
                    break;
                }
                if (gotFrame > 0) {
                    MediaBufferSP mediaBuf;
                    if (mAudioSink->mHasResample) {
                        bufferSize = RESAMPLE_AUDIO_SIZE;
                        buffer = new uint8_t[bufferSize];
                        decodedSize = swr_convert(mAudioSink->mAVResample, &buffer,
                            bufferSize/mAudioSink->mAVCodecContext->channels/av_get_bytes_per_sample(AV_SAMPLE_FMT_S16),
                            mAudioSink->mAVFrame->data,
                            mAudioSink->mAVFrame->nb_samples);
                        decodedSize = decodedSize*mAudioSink->mAVCodecContext->channels*av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                        mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                        mediaBuf->setBufferInfo((uintptr_t *)&buffer, NULL, &decodedSize, 1);
                        mediaBuf->setPts(mAudioSink->mAVFrame->pkt_pts);
                        mediaBuf->addReleaseBufferFunc(releaseOutputBuffer);
                    } else {
                        decodedSize = mAudioSink->mAVFrame->linesize[0];
                        mediaBuf = AVBufferHelper::createMediaBuffer(mAudioSink->mAVFrame, true, true);
                        buffer = mAudioSink->mAVFrame->data[0];
                        mAudioSink->mAVFrame = NULL; // transfer the AVFrame ownership to MediaBuffer
                    }

                    if (mediaBuf->pts() < mAudioSink->mTargetTimeUs) {
                        VERBOSE("ignore this frame, timeUs %0.3f, mTargetTimeUs %0.3f",
                            mediaBuf->pts()/1000000.0f, mAudioSink->mTargetTimeUs/1000000.0f);
                    } else {
                        mAudioSink->mTargetTimeUs = -1ll;

                        mediaBuf->setMonitor(mAudioSink->mMonitorWrite);

                        if (mAudioSink->write(mediaBuf) != MM_ERROR_SUCCESS) {
                            ERROR("fail to write buffer");
                            EXIT();
                        }

                        TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mAudioSink->mMonitorWrite.get());
                        if (trafficControlWrite)
                            trafficControlWrite->waitOnFull();
                    }
                }
                pktSize -= len;
                pktData += len;
            }
#else
            mediaBuffer->setMonitor(mAudioSink->mMonitorWrite);
            if (mAudioSink->write(mediaBuffer) != MM_ERROR_SUCCESS) {
                ERROR("fail to write buffer");
                EXIT();
            }

            TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mAudioSink->mMonitorWrite.get());
            if (trafficControlWrite)
                trafficControlWrite->waitOnFull();
#endif
        }else {
            VERBOSE("read NULL buffer from demuxer\n");
            usleep(10*1000);
        }

    }

    INFO("audio Output thread exited\n");
    EXIT();
}

// /////////////////////////////////////

class AudioSinkLPA::Private
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

    mm_status_t init(AudioSinkLPA *audioSink) {
        ENTER();
        mAudioSink = audioSink;
#ifdef DUMP_SINK_CRAS_DATA
        mDumpFile = fopen("/data/audio_sink_LPA.mp3","wb");
#endif
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    void uninit() {
#ifdef DUMP_SINK_CRAS_DATA
        fclose(mDumpFile);
#endif

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
    int32_t mSampleFormat;
    int32_t mSampleRate;
    int32_t mChannelCount;
    int32_t mBytesPerSample;
    int32_t mAudioStreamType;
    uint64_t mPosition;
    uint64_t mTimeStamp;
    int64_t mDurationUs;
    int32_t mBitRate;

    std::queue<MediaBufferSP> mAvailableSourceBuffers;

    ClockWrapperSP mClockWrapper;

    bool mIsPaused;
    Condition mCondition;
    Lock mLock;
    state_t mState;
    int32_t mTotalBuffersQueued;
    AudioSinkLPA *mAudioSink;
#ifdef DUMP_SINK_CRAS_DATA
    FILE* mDumpFile;
#endif
    int32_t mScaledPlayRate;
    uint32_t mOutFrameCnt;

    Private()
        :
        mRender(NULL),
        mBytesPerFrame(0),
        mFormat(SND_FORMAT_INVALID),
        mSampleFormat(SND_FORMAT_PCM_16_BIT),
        mSampleRate(DEFAULT_SAMPLE_RATE),
        mChannelCount(DEFAULT_CHANNEL),
        mBytesPerSample(2),
        mAudioStreamType(AS_TYPE_MUSIC),
        mPosition(0),
        mTimeStamp(0),
        mDurationUs(-1),
        mBitRate(DEFAULT_BIT_RATE),
        mIsPaused(true),
        mCondition(mLock),
        mState(STATE_IDLE),
        mTotalBuffersQueued(0),
        mAudioSink(NULL),
        mScaledPlayRate(SCALED_PLAY_RATE),
        mOutFrameCnt(0)
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
#define ASC_MSG_setParameters (msg_type)8
#define ASC_MSG_getParameters (msg_type)9
#define ASC_MSG_write (msg_type)10
#define ASC_MSG_AudioRenderError (msg_type)11
#define ASC_MSG_ScheduleEOS (msg_type)12
#define ASC_MSG_ScheduleEOS2Audio (msg_type)13
#define ASC_MSG_StartPauseTimeout (msg_type)14
#define ASC_MSG_ScheduleNewAudioRender (msg_type)15

BEGIN_MSG_LOOP(AudioSinkLPA)
    MSG_ITEM(ASC_MSG_prepare, onPrepare)
    MSG_ITEM(ASC_MSG_start, onStart)
    MSG_ITEM(ASC_MSG_resume, onResume)
    MSG_ITEM(ASC_MSG_pause, onPause)
    MSG_ITEM(ASC_MSG_stop, onStop)
    MSG_ITEM(ASC_MSG_flush, onFlush)
    MSG_ITEM(ASC_MSG_reset, onReset)
    MSG_ITEM(ASC_MSG_setParameters, onSetParameters)
    MSG_ITEM(ASC_MSG_getParameters, onGetParameters)
    MSG_ITEM(ASC_MSG_write, onWrite)
    MSG_ITEM(ASC_MSG_AudioRenderError, onAudioRenderError)
    MSG_ITEM(ASC_MSG_ScheduleEOS, onScheduleEOS)
    MSG_ITEM(ASC_MSG_ScheduleEOS2Audio, onScheduleEOS2Audio)
    MSG_ITEM(ASC_MSG_StartPauseTimeout, onStartPauseTimeout)
    MSG_ITEM(ASC_MSG_ScheduleNewAudioRender, onScheduleNewAudioRender)
END_MSG_LOOP()

AudioSinkLPA::AudioSinkLPA(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME),
                                                                   mComponentName(COMPONENT_NAME),
                                                                   mVolume(VOLUME_NOT_SET),
                                                                   mMute(DEFAULT_MUTE),
                                                                   mIsLPA(true),
                                                                   mCurrentPosition(0),
                                                                   mSeekPosition(0),
                                                                   mNeedSeekForPauseTimeout(true),
#ifdef DECODE_BEFORE_RENDER
                                                                   mNeedFlush(false),
                                                                   mAVCodecContext(NULL),
                                                                   mAVCodec(NULL),
                                                                   mAVPacket(NULL),
                                                                   mAVFrame(NULL),
                                                                   mCodecID(0),
                                                                   mAVResample(NULL),
                                                                   mHasResample(true),
                                                                   mTargetTimeUs(-1ll),
#endif
                                                                   mIsPaused(true),
                                                                   mIsEOS(false),
                                                                   mCBCount(0),
                                                                   mCondition(mLock),
                                                                   mPauseTimeoutGeneration(0)
{
    mPriv = Private::create();
    if (!mPriv)
        ERROR("no render");
}

AudioSinkLPA::~AudioSinkLPA()
{
    //release();
}

mm_status_t AudioSinkLPA::init()
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

void AudioSinkLPA::uninit()
{
    ENTER();
    mPriv->uninit();
    MMMsgThread::exit();

    EXIT();
}

const char * AudioSinkLPA::name() const
{
    return mComponentName.c_str();
}


ClockSP AudioSinkLPA::provideClock()
{
    ENTER();
    if (!mPriv)
        ERROR("no render");
    return mPriv->mClockWrapper->provideClock();
}

mm_status_t AudioSinkLPA::prepare()
{

    postMsg(ASC_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::start()
{
    postMsg(ASC_MSG_start, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::resume()
{
    postMsg(ASC_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::stop()
{
    postMsg(ASC_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::pause()
{
    postMsg(ASC_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::seek(int msec, int seekSequence)
{
    MMAutoLock locker(mPriv->mLock);
    mSeekPosition = msec;
    mCurrentPosition = msec * 1000ll;
    mNeedSeekForPauseTimeout = false;
    return MM_ERROR_SUCCESS;
}

mm_status_t AudioSinkLPA::reset()
{
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    postMsg(ASC_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::flush()
{
    postMsg(ASC_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t AudioSinkLPA::write(const MediaBufferSP & buf)
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    AudioSinkLPA::Private::QueueEntry *pEntry = new AudioSinkLPA::Private::QueueEntry;
    pEntry->mBuffer = buf;
    ++mPriv->mTotalBuffersQueued;
    postMsg(ASC_MSG_write, 0, (param2_type)pEntry);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

#if 0
mm_status_t AudioSinkLPA::AudioSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    ENTER();
    if (!mRender->mPriv)
        return MM_ERROR_NO_COMPONENT;
    int ret = metaData->getInt32(MEDIA_ATTR_SAMPLE_RATE, mRender->mPriv->mSampleRate);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    ret = metaData->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, mRender->mPriv->mSampleFormat);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_FORMAT);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }
    ret = metaData->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mRender->mPriv->mChannelCount);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    INFO("sampleRate %d, format %d, channel %d", mRender->mPriv->mSampleRate, mRender->mPriv->mSampleFormat, mRender->mPriv->mChannelCount);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
#endif

mm_status_t AudioSinkLPA::setParameter(const MediaMetaSP & meta)
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
#ifdef DECODE_BEFORE_RENDER
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
                    mPriv->ensureAudioRender();
                }

                mPriv->mScaledPlayRate = item.mValue.ii;
                INFO("key: %s, val: %d\n", item.mName, item.mValue.ii);
                continue;
            }
#endif
        }
    }
#ifdef DECODE_BEFORE_RENDER
    if (stopAudioRender) {
        {
            MMAutoLock locker(mPriv->mLock);
            mPriv->mCondition.signal();
        }
        if (mPriv->mRender)
            mPriv->mRender->stop();
        mPriv->destroyAudioRender();
    }
#endif
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t AudioSinkLPA::getParameter(MediaMetaSP & meta) const
{
    ENTER();
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;

    MMAutoLock locker(mPriv->mLock);

    meta->setInt32(MEDIA_ATTR_MUTE, mMute);
    meta->setInt64(MEDIA_ATTR_VOLUME, mVolume);

    EXIT_AND_RETURN(MM_ERROR_UNSUPPORTED);
}

int64_t AudioSinkLPA::getCurrentPositionInternal()
{
    int64_t currentPosition = mCurrentPosition;
    uint64_t nowMs;
    struct timespec t;
    AudioRender::AudioTimestamp timestamp;
    if (mPriv->mRender) {
        mPriv->mRender->getTimestamp(&timestamp);
        mPriv->mTimeStamp = timestamp.mTime.tv_sec * 1000ll + timestamp.mTime.tv_nsec / 1000000ll;
        mPriv->mPosition = timestamp.mPosition;

        clock_gettime(CLOCK_MONOTONIC, &t);
        nowMs = t.tv_sec * 1000ll + t.tv_nsec / 1000000ll;
        currentPosition = (mPriv->mPosition * 1000ll / mPriv->mSampleRate + (nowMs - mPriv->mTimeStamp) + mSeekPosition) * 1000ll;

        if (mPriv->mDurationUs > 0 && currentPosition > mPriv->mDurationUs) {
            currentPosition = mPriv->mDurationUs;
        }
    }
    return currentPosition;
}

int64_t AudioSinkLPA::getCurrentPosition()
{
    if (!mPriv)
        return MM_ERROR_NO_COMPONENT;
    MMAutoLock locker(mPriv->mLock);
    int64_t currentPosition = -1ll;
#ifdef DECODE_BEFORE_RENDER
    if (mPriv->mClockWrapper && mPriv->mClockWrapper->getCurrentPosition(currentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        currentPosition = -1ll;
    }
#else
    if (mPriv->mTimeStamp == 0) {
        currentPosition = mCurrentPosition;
    }
    else {
        if (mIsPaused) {
            currentPosition = mCurrentPosition;
        } else {
            mCurrentPosition = currentPosition = getCurrentPositionInternal();
        }
    }
#endif
    VERBOSE("getCurrentPosition %" PRId64 " ms", currentPosition/1000ll);
    return currentPosition;
}


void AudioSinkLPA::onStartPauseTimeout(param1_type param1, param2_type param2, uint32_t rspId)
{
    MMAutoLock locker(mPriv->mLock);
    if ((int32_t)param1 == mPauseTimeoutGeneration) {
        INFO("Timeout to delete audio render and seek internal");
        canclePauseTimeout();
        mCBCount = 0;
        mPriv->destroyAudioRender();
        if (mCurrentPosition < mPriv->mDurationUs && mNeedSeekForPauseTimeout) {
            notify(kEventSeekRequire, int(mCurrentPosition), 0, MMParamSP((MMParam*)NULL));
        }
    }
}

void AudioSinkLPA::startPauseTimeout()
{
    mNeedSeekForPauseTimeout = true;
    postMsg(ASC_MSG_StartPauseTimeout, mPauseTimeoutGeneration, NULL, PAUSEMAXUS);
}

void AudioSinkLPA::canclePauseTimeout()
{
    ++mPauseTimeoutGeneration;
}

mm_status_t AudioSinkLPA::scheduleEOS(int64_t delayEOSUs)
{
    ENTER();
    DEBUG("delayEOSUs = %" PRId64 " us",delayEOSUs);
    postMsg(ASC_MSG_ScheduleEOS, 0, NULL, delayEOSUs);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkLPA::onScheduleEOS(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    notify(kEventEOS, 0, 0, MMParamSP((MMParam*)NULL));
    EXIT();
}

mm_status_t AudioSinkLPA::scheduleNewAudioRender()
{
    ENTER();
    postMsg(ASC_MSG_ScheduleNewAudioRender, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkLPA::onScheduleNewAudioRender(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        MMAutoLock locker(mLock);
        if (mPriv->mRender) {
            mPriv->mRender->stop();
        }
        mPriv->destroyAudioRender();
        mPriv->mIsPaused = true;
        mPriv->clearSourceBuffers();
        mPriv->mTimeStamp = 0;
        mPriv->mPosition = 0;
        notify(kEventSeekRequire, int(mCurrentPosition), 0, MMParamSP((MMParam*)NULL));
    }
    mPriv->resumeInternal();
    EXIT();
}

mm_status_t AudioSinkLPA::scheduleEOS2Audio()
{
    ENTER();
    postMsg(ASC_MSG_ScheduleEOS2Audio, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkLPA::onScheduleEOS2Audio(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    if (mPriv->mRender) {
        mPriv->mRender->drain();
    }
    EXIT();
}

void AudioSinkLPA::onAudioRenderError(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mPriv->destroyAudioRender();
    EXIT();
}

#ifdef DECODE_BEFORE_RENDER
mm_status_t AudioSinkLPA::release()
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

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}
#endif

bool AudioSinkLPA::isLPASupport(const MediaMetaSP & meta)
{
    bool isSupport = true;
    int64_t durationMs;

    int ret = meta->getInt32(MEDIA_ATTR_SAMPLE_RATE, mPriv->mSampleRate);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
    }
    ret = meta->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, mPriv->mSampleFormat);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_FORMAT);
    }
    ret = meta->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mPriv->mChannelCount);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
    }

    ret = meta->getInt64(MEDIA_ATTR_DURATION, durationMs);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_DURATION);
    }
    mPriv->mDurationUs = durationMs * 1000;

    ret = meta->getInt32(MEDIA_ATTR_BIT_RATE, mPriv->mBitRate);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_BIT_RATE);
    }

    const char *mime = NULL;
    ret = meta->getString(MEDIA_ATTR_MIME, mime);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_MIME);
    }

    if (mapMimeToAudioFormat((snd_format_t&)mPriv->mFormat, mime) != MM_ERROR_SUCCESS) {
        ERROR("fail to map mime type %s to audio_format.", mime);
        return false;
    } else {
        VERBOSE("Mime type %s mapped to audio_format %d .", mime, mPriv->mFormat);
    }

    if (SND_FORMAT_INVALID == mPriv->mFormat) {
        ERROR("mime type %s not a known audio format", mime);
        return false;
    }

    adev_offload_info_t offloadInfo = {
         version: ADEV_OFFLOAD_INFO_VERSION_CURRENT,
         size: sizeof(adev_offload_info_t),
         sample_rate: mPriv->mSampleRate,
         channel_mask: adev_get_out_mask_from_channel_count(mPriv->mChannelCount),
         format: (snd_format_t)mPriv->mFormat,
         stream_type: (as_type_t)mPriv->mAudioStreamType,
         bit_rate: mPriv->mBitRate,
         duration_us: mPriv->mDurationUs,
         has_video: false,
         is_streaming: false
    };

    YunOSAudioNS::AudioManager* am = YunOSAudioNS::AudioManager::create();
    if (!am) {
        ERROR("Fail to create Audio Manager");
        EXIT_AND_RETURN(false);
    }
    isSupport = am->isCompressedAudioEnabled(&offloadInfo);
    INFO("LPA isSupport : %d", isSupport);
    if(am != NULL) {
        delete am;
        am = NULL;
    }

    EXIT_AND_RETURN(isSupport ? true : false);
}

mm_status_t AudioSinkLPA::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeAudio) {
        mReader = component->getReader(kMediaTypeAudio);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (!isLPASupport(metaData)) {
                EXIT_AND_RETURN(MM_ERROR_LPA_NOT_SUPPORT);
            }
#ifdef DECODE_BEFORE_RENDER
            if (metaData) {
                mInputMetaData = metaData->copy();
                mOutputMetaData = metaData->copy();
                mOutputMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, SND_FORMAT_PCM_16_BIT);
                mOutputMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, DEFAULT_SAMPLE_RATE);
                mHasResample = true;//FIXME:whether need resample
            }
#endif
            EXIT_AND_RETURN(MM_ERROR_SUCCESS);
        }
    }
    EXIT_AND_RETURN(MM_ERROR_LPA_NOT_SUPPORT);
}

#ifdef DECODE_BEFORE_RENDER
/*static */AVSampleFormat AudioSinkLPA::convertAudioFormat(snd_format_t formt)
{
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
#endif

void AudioSinkLPA::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{

    ENTER1();

#ifdef DECODE_BEFORE_RENDER
    mIsLPA = false;
    int64_t wanted_channel_layout = 0;
    av_register_all();

    int ret = mOutputMetaData->getInt32(MEDIA_ATTR_SAMPLE_RATE, mPriv->mSampleRate);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_RATE);
    }
    ret = mOutputMetaData->getInt32(MEDIA_ATTR_SAMPLE_FORMAT, mPriv->mSampleFormat);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_SAMPLE_FORMAT);
    }
    ret = mOutputMetaData->getInt32(MEDIA_ATTR_CHANNEL_COUNT, mPriv->mChannelCount);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
    }

    void* ptr = NULL;
    ret = mOutputMetaData->getPointer(MEDIA_ATTR_CODEC_CONTEXT, ptr);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_CHANNEL_COUNT);
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, MMParamSP((MMParam*)NULL));
        EXIT();
    }
    mAVCodecContext = static_cast<AVCodecContext*> (ptr);

    //mAVCodecContext = avcodec_alloc_context3(mAVCodec);
    //mAVCodecContext->channels = mPriv->mChannelCount;
    //mAVCodecContext->sample_rate = mPriv->mSampleRate;
    //mAVCodecContext->sample_fmt = convertAudioFormat(mPriv->mSampleFormat);

    mOutputMetaData->getInt32(MEDIA_ATTR_CODECID, mCodecID);

    mAVFrame = av_frame_alloc();

    if (mHasResample) {
        mAVResample = swr_alloc();
        wanted_channel_layout =
            (mAVCodecContext->channel_layout &&
            mAVCodecContext->channels ==
            av_get_channel_layout_nb_channels(mAVCodecContext->channel_layout)) ? mAVCodecContext->channel_layout : av_get_default_channel_layout(mAVCodecContext->channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;

        av_opt_set_int(mAVResample, "in_channel_layout",  wanted_channel_layout, 0);
        av_opt_set_int(mAVResample, "in_sample_fmt",      mAVCodecContext->sample_fmt,0);
        av_opt_set_int(mAVResample, "in_sample_rate",     mAVCodecContext->sample_rate, 0);
        av_opt_set_int(mAVResample, "out_channel_layout", wanted_channel_layout, 0);
        av_opt_set_int(mAVResample, "out_sample_fmt",     AV_SAMPLE_FMT_S16, 0);
        av_opt_set_int(mAVResample, "out_sample_rate",    DEFAULT_SAMPLE_RATE,0);
        if ( swr_init(mAVResample) < 0) {
            ERROR("error initializing libswresample\n");
            notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, MMParamSP((MMParam*)NULL));
            EXIT();
        }
    }
#endif

    mPriv->mOutFrameCnt = YunOSAudioNS::AudioRender::getDefaultPeriodSize(
        mPriv->mSampleRate,
        adev_get_out_mask_from_channel_count(mPriv->mChannelCount),
        mIsLPA ? (snd_format_t)mPriv->mFormat : (snd_format_t)mPriv->mSampleFormat,
        (as_type_t)mPriv->mAudioStreamType,
        mIsLPA);
    if (!mIsLPA) {
        mPriv->mBytesPerSample = adev_bytes_per_sample((snd_format_t)mPriv->mSampleFormat);
        INFO("outFrameCnt %d, bytesPerSample %d\n", mPriv->mOutFrameCnt, mPriv->mBytesPerSample);
    }

    if (!mReadThread) {
        // create thread to decode buffer
        mReadThread.reset (new ReadThread(this), MMThread::releaseHelper);
        mReadThread->create();
    }
    setState(mPriv->mState, mPriv->STATE_PREPARED);
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    EXIT1();

}

mm_status_t AudioSinkLPA::Private::resumeInternal()
{
    {
        MMAutoLock locker(mLock);
        if (!mIsPaused/* || mIsDraining*/) {
            ERROR("Aready started\n");
            mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
            EXIT_AND_RETURN(MM_ERROR_SUCCESS);
        }
        if (mIsPaused && mClockWrapper) {
            mClockWrapper->resume();
        }
        mAudioSink->mIsPaused = false;
        mAudioSink->canclePauseTimeout();
    }
    mIsPaused = false;
    mAudioSink->mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "AudioSinkLPAWrite"));
    mAudioSink->mReadThread->signalContinue();

    MMAutoLock locker(mLock);
    ensureAudioRender();

    if (mAudioSink->mIsLPA) {
        if (mRender) {
            int ret = mRender->start();
            if (0 != ret) {
                MMLOGE("Failed to start AudioRender: %d.\n", ret);
                destroyAudioRender();
                return MM_ERROR_OP_FAILED;
            }
        } else {
            mAudioSink->notify(kEventStartResult, MM_ERROR_SOUND_POLICY, 0, MMParamSP((MMParam*)NULL));
            EXIT_AND_RETURN(MM_ERROR_SOUND_POLICY);
        }
    }

    setState(mState, STATE_STARTED);
    mAudioSink->notify(kEventStartResult, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

uint32_t AudioSinkLPA::Private::formatSize(snd_format_t format)
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

void AudioSinkLPA::Private::audioCallback(YunOSAudioNS::AudioRender::evt_t event, void * user,void * info) {
    AudioSinkLPA *me = static_cast<AudioSinkLPA *>(user);

    if (YunOSAudioNS::AudioRender::EVT_MORE_DATA == event) {
        me->mPriv->onMoreData(event, info);
        } else if (YunOSAudioNS::AudioRender::EVT_STREAM_ERROR == event) {
            ERROR("audio callback error callback");
            me->notify(Component::kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
            me->audioRenderError();
        } else if (YunOSAudioNS::AudioRender::EVT_NEW_AUDIORENDER == event) {
            ERROR("audiostreamer restarted");
            if (me->mIsLPA)
                me->scheduleNewAudioRender();
        }else if (YunOSAudioNS::AudioRender::EVT_DRAIN_OVER == event) {
            if (me->mIsLPA)
                me->scheduleEOS(0);
        } else {
            ERROR("audio callback event %d not implement.", event);
        }
}

void AudioSinkLPA::Private::onMoreData(YunOSAudioNS::AudioRender::evt_t event, void *info) {
    MediaBufferSP mediaBuffer;
    uint8_t *sourceBuf = NULL;
    int64_t pts = 0;
    int32_t offset = 0;
    int32_t size = 0;
    int32_t remainSize = 0;
    uint32_t lastLatency = 0;
    int32_t writePosition = 0;

    YunOSAudioNS::AudioRender::Buffer *buffer = static_cast<YunOSAudioNS::AudioRender::Buffer *>(info);
    int mWriteableSize = mAudioSink->mIsLPA ? (buffer->mFrameCount *sizeof(int8_t)) : (buffer->mFrameCount * mChannelCount * mBytesPerSample);
    buffer->mFrameCount = 0; //FIXME: to set framecount even if push nothing
    if (mIsPaused || (mScaledPlayRate != SCALED_PLAY_RATE) || mAudioSink->mIsEOS) {
      return;
    }

    MMAutoLock locker(mLock);
    if (mAudioSink->mIsLPA) {
        AudioRender::AudioTimestamp timestamp;
        mRender->getTimestamp(&timestamp);
        mTimeStamp = timestamp.mTime.tv_sec * 1000ll + timestamp.mTime.tv_nsec / 1000000ll;
        mPosition = timestamp.mPosition;
    }
    while(mWriteableSize > 0) {

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
        mAudioSink->mCBCount++;
        if (mAudioSink->mCBCount == 10) {
            //copy first 10 buffers to play as soon as possible
            return;
        }
        mediaBuffer = mAvailableSourceBuffers.front();
        mediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, &offset, &size, 1);
        remainSize = mAudioSink->mIsLPA ? mediaBuffer->size() : (size - offset);
        if (mediaBuffer->type() != (mAudioSink->mIsLPA ? MediaBuffer::MBT_ByteBuffer : MediaBuffer::MBT_RawAudio)) {
           ERROR("wrong buffer type %d", mediaBuffer->type());
           mAvailableSourceBuffers.pop();
           return;
        }
#ifdef DECODE_BEFORE_RENDER
        lastLatency = mRender->getCurLatency();
#endif

        if (!sourceBuf || remainSize <= 0) {
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
#ifdef DECODE_BEFORE_RENDER
                mAudioSink->scheduleEOS(lastLatency);
#endif
                mAudioSink->scheduleEOS2Audio();
                mAudioSink->mIsEOS = true;
            }
            {
                mAvailableSourceBuffers.pop();
            }
            return;
        }
        pts = mediaBuffer->pts();

        INFO("mWriteableSize = %d,size = %d,writePosition = %d,offset = %d, pts %" PRId64 " ms",
            mWriteableSize,remainSize,writePosition,offset, mediaBuffer->pts()/1000ll);
#ifdef DECODE_BEFORE_RENDER
        int32_t frameWritten = (mWriteableSize >= remainSize) ? remainSize : mWriteableSize;
        int64_t durationUs = ((frameWritten / (formatSize((snd_format_t)mSampleFormat) * mChannelCount)) * 1000*1000ll) / mSampleRate;
        if (MM_LIKELY(!mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
            mClockWrapper->setAnchorTime(pts, Clock::getNowUs() + lastLatency, pts + durationUs);
        } else {
            MMLOGV("eos, not set anchor\n");
        }
#endif
#ifdef DUMP_SINK_CRAS_DATA
            fwrite(sourceBuf,1,remainSize,mDumpFile);
#endif
        if (mWriteableSize >= remainSize) {
            memcpy(buffer->mBuffer + writePosition, sourceBuf + offset, remainSize);
            buffer->mFrameCount += mAudioSink->mIsLPA ? (remainSize/sizeof(int8_t)) : (remainSize / (mChannelCount * mBytesPerSample));
            if (mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
#ifdef DECODE_BEFORE_RENDER
                mAudioSink->scheduleEOS(lastLatency);
#endif
            }
            mAvailableSourceBuffers.pop();
            mWriteableSize -= remainSize;
            writePosition += remainSize;
        } else {
#ifdef DECODE_BEFORE_RENDER
            memcpy(buffer->mBuffer + writePosition, sourceBuf + offset, mWriteableSize);
            buffer->mFrameCount += mAudioSink->mIsLPA ? (mWriteableSize/sizeof(int8_t)) : (mWriteableSize / (mChannelCount * mBytesPerSample));
            offset += mWriteableSize;
            remainSize -= mWriteableSize;

            pts += (((remainSize / (formatSize((snd_format_t)mSampleFormat) * mChannelCount)) * 1000*1000ll) / mSampleRate);
            mediaBuffer->setPts(pts);
            mediaBuffer->setSize(remainSize);
            mediaBuffer->updateBufferOffset(&offset, 1);
#endif
            mWriteableSize = 0;
            return;
        }

    }

}

mm_status_t AudioSinkLPA::Private::setAudioStreamType(int type)
{
    mAudioStreamType = type;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkLPA::Private::getAudioStreamType(int *type)
{
    *type = mAudioStreamType;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t AudioSinkLPA::Private::ensureAudioRender()
{
//    MMLOGV("+\n");
#ifdef DECODE_BEFORE_RENDER
    if (mAvailableSourceBuffers.empty()) {
        return MM_ERROR_SUCCESS;
    }
#endif

    if (mRender) {
        return MM_ERROR_SUCCESS;
    }
    adev_offload_info_t offloadInfo = {
         version: ADEV_OFFLOAD_INFO_VERSION_CURRENT,
         size: sizeof(adev_offload_info_t),
         sample_rate: mSampleRate,
         channel_mask: adev_get_out_mask_from_channel_count(mChannelCount),
         format: (snd_format_t)mFormat,
         stream_type: (as_type_t)mAudioStreamType,
         bit_rate: mBitRate,
         duration_us: mDurationUs,
         has_video: false,
         is_streaming: false
    };

    mRender = YunOSAudioNS::AudioRender::create((as_type_t)mAudioStreamType,
        mSampleRate,
        (snd_format_t)mFormat,
        adev_get_out_mask_from_channel_count(mChannelCount),
        mOutFrameCnt,
        mAudioSink->mIsLPA ? ADEV_OUTPUT_FLAG_COMPRESS_OFFLOAD : ADEV_OUTPUT_FLAG_NONE,
        audioCallback,
        mAudioSink,
        0,
        TRANSFER_CALLBACK,
        mAudioSink->mIsLPA ? &offloadInfo : NULL);

    if (!mRender) {
        MMLOGE("failed to create audio track\n");
        mAudioSink->notify(kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
        return MM_ERROR_SOUND_POLICY;
    }

#ifdef DECODE_BEFORE_RENDER
    int ret = mRender->start();
    if (0 != ret) {
        MMLOGE("Failed to start AudioRender: %d.\n", ret);
        destroyAudioRender();
        mAudioSink->notify(kEventError, MM_ERROR_SOUND_POLICY, 0, nilParam);
        return MM_ERROR_SOUND_POLICY;
    }
#endif
    return MM_ERROR_SUCCESS;
}

void AudioSinkLPA::Private::destroyAudioRender()
{
    MMLOGV("+\n");
    MM_RELEASE(mRender);
    MMLOGV("-\n");
}

void AudioSinkLPA::Private::audioRenderError()
{
    MMLOGV("+\n");
    mAudioSink->audioRenderError();
    MMLOGV("-\n");
}

void AudioSinkLPA::audioRenderError()
{
    postMsg(ASC_MSG_AudioRenderError, 0, 0);
}

void AudioSinkLPA::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
#ifdef DECODE_BEFORE_RENDER
    if (mAVCodec == NULL) {
        int ret = -1;
        mAVCodec = avcodec_find_decoder((AVCodecID)mCodecID);
        if (mAVCodec == NULL) {
            ERROR("error no Codec found\n");
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, MMParamSP((MMParam*)NULL));
            EXIT();
        }
        ret = avcodec_open2(mAVCodecContext, mAVCodec, NULL) ;
        if (ret < 0) {
            ERROR("error avcodec_open failed.\n");
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, MMParamSP((MMParam*)NULL));
            EXIT();
        }
    }

#endif
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkLPA::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    mPriv->resumeInternal();
    EXIT1();
}

void AudioSinkLPA::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    {
#ifdef DECODE_BEFORE_RENDER
        MMAutoLock locker(mLock);
        mNeedFlush = true;
        if (mAVCodecContext) {
            avcodec_close(mAVCodecContext);
            mAVCodec = NULL;
        }
#endif
    }
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        mPriv->mCondition.signal();
        canclePauseTimeout();
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
        mCBCount = 0;
        if (mPriv->mState == mPriv->STATE_IDLE || mPriv->mState == mPriv->STATE_STOPED/* || mPriv->mIsDraining*/) {
            notify(kEventStopped, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
            EXIT1();
        }
        setState(mPriv->mState, mPriv->STATE_STOPED);
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    }
    EXIT1();
}

void AudioSinkLPA::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        MMAutoLock locker(mPriv->mLock);
        mCurrentPosition = getCurrentPositionInternal();
    }
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    {
        MMAutoLock locker(mPriv->mLock);
        mPriv->mIsPaused = true;
        mPriv->mCondition.signal();
    }
    if (mPriv->mRender) {
        INFO("audio render pause start");
        mPriv->mRender->pause();
        INFO("audio render pause end");
    }

    {
        MMAutoLock locker(mPriv->mLock);
#ifdef DECODE_BEFORE_RENDER
        mPriv->destroyAudioRender();
#endif

        if (mPriv->mState == mPriv->STATE_PAUSED/* || mPriv->mIsDraining*/) {
            notify(kEventPaused, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
            EXIT1();
        }

        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->pause();
        }
        setState(mPriv->mState, mPriv->STATE_PAUSED);
        startPauseTimeout();
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    }
    EXIT1();
}

void AudioSinkLPA::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
#ifdef DECODE_BEFORE_RENDER
    mNeedFlush = true;
#endif
    {
        mPriv->mCondition.signal();
        if (mPriv->mRender)
            mPriv->mRender->flush();
        if (mPriv->mClockWrapper) {
            mPriv->mClockWrapper->flush();
        }
    }

    MMAutoLock locker(mPriv->mLock);
    mPriv->clearSourceBuffers();
    mIsEOS = false;
    mPriv->mTimeStamp = 0;
    mPriv->mPosition = 0;
    mCBCount = 0;
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    EXIT1();
}

void AudioSinkLPA::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER1();
    {
        mIsPaused = true;
        if (mReadThread) {
            mReadThread->signalExit();
            mReadThread.reset();
        }
        MMAutoLock locker(mLock);
#ifdef DECODE_BEFORE_RENDER
        release();
#endif

        mReader.reset();
        mMonitorWrite.reset();
    }

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
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    EXIT1();
}

void AudioSinkLPA::onSetParameters(param1_type param1, param2_type param2, uint32_t rspId)
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
        mFormat = (audio_sample_format_t)param2;
    } else if (!strcmp((char *)param1, "channel")) {
        mChannelCount = (uint8_t)param2;
    }
    */
    //notify(EVENT_SETPARAMETERSCOMPLETE, MM_ERROR_SUCCESS, 0, NULL);
    EXIT();
}

void AudioSinkLPA::onGetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //if (!strcmp((char *)param1, "getVolume")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getVolume(), 0, NULL);
    //} else if (!strcmp((char *)param1, "getMute")) {
    //    notify(EVENT_GETPARAMETERSCOMPLETE, getMute(), 0, NULL);
    //}
    EXIT();
}

void AudioSinkLPA::onWrite(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    AudioSinkLPA::Private::QueueEntry *pEntry = (AudioSinkLPA::Private::QueueEntry *)param2;
    if (pEntry->mBuffer) {
        MMAutoLock locker(mPriv->mLock);
#ifdef DECODE_BEFORE_RENDER
        if (mPriv->mScaledPlayRate == SCALED_PLAY_RATE) {
            mPriv->ensureAudioRender();
        }
#endif
        mPriv->mAvailableSourceBuffers.push(pEntry->mBuffer);
        mPriv->mCondition.signal();
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

mm_status_t AudioSinkLPA::Private::release()
{
    ENTER();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void AudioSinkLPA::Private::clearSourceBuffers()
{
    while(!mAvailableSourceBuffers.empty()) {
        mAvailableSourceBuffers.pop();
    }
}

mm_status_t AudioSinkLPA::setAudioStreamType(int type)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    ret = mPriv->setAudioStreamType(type);
    EXIT_AND_RETURN(ret);
}

mm_status_t AudioSinkLPA::getAudioStreamType(int *type)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    ret = mPriv->getAudioStreamType(type);
    EXIT_AND_RETURN(ret);
}

mm_status_t AudioSinkLPA::setVolume(double volume)
{
    ENTER();

    mVolume = volume;
    if (mPriv->mRender) {
        mPriv->mRender->setVolume(mVolume);
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

double AudioSinkLPA::getVolume()
{
    ENTER();
    if (mPriv->mRender) {
        mVolume = mPriv->mRender->getVolume();
    }
    EXIT_AND_RETURN(mVolume);

}

mm_status_t AudioSinkLPA::setMute(bool mute)
{
    ENTER();
    if (mute != mMute) {
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
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

bool AudioSinkLPA::getMute()
{
    ENTER();
    EXIT_AND_RETURN(mMute);
}


}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::AudioSinkLPA *sinkComponent = new YUNOS_MM::AudioSinkLPA(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}


void releaseComponent(YUNOS_MM::Component *component)
{
    delete component;
}
}
