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

#include "av_muxer.h"
#include <unistd.h>
#include <multimedia/av_buffer_helper.h>
#include <multimedia/media_attr_str.h>
#include <cow_util.h>


#ifdef __mm_debug_H
#error who included mm debug?
#endif

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

#define SET_STATE(_state) do {\
    MMLOGI("state changed from %d to %s\n", mState, #_state);\
    mState = _state;\
}while(0)

#define CHECK_STATE(_expected_state) do {\
    if ( mState != _expected_state ) {\
        MMLOGE("invalid state(%d). expect %s\n", mState, #_expected_state);\
        return MM_ERROR_INVALID_STATE;\
    }\
}while(0)

#define RET_IF_ALREADY_STATE(_state, _ret) do {\
    if ( mState == (_state) ) {\
        MMLOGE("already in state(%s). ret %s\n", #_state, #_ret);\
        return (_ret);\
    }\
}while(0)

#define NOTIFY(_event, _param1, _param2, _obj) do {\
    MMLOGI("notify: %s, param1: %d, param2: %d\n", #_event, (_param1), (_param2));\
    notify(_event, _param1, _param2, _obj);\
}while(0)

#define NOTIFY_ERROR(_reason) do {\
    MMLOGV("notify: error: %d, %s\n", _reason, #_reason);\
    NOTIFY(kEventError, _reason, 0, nilParam);\
}while(0)

#define DUMP_BUFFER(_buf, _size, _info) do {\
    char * dump = new char[_size * 3 + 1];\
    if ( !dump ) {\
        MMLOGE("no mem for dump\n");\
        break;\
    }\
    char * dump_p = dump;\
    const uint8_t * buf_p = _buf;\
    for ( size_t i = 0; i < (size_t)_size; ++i ) {\
        sprintf(dump_p, "%02x ", *buf_p);\
        ++buf_p;\
        dump_p += 3;\
    }\
    *dump_p = '\0';\
    MMLOGV("%s: %s\n", _info, dump);\
    MM_RELEASE_ARRAY(dump);\
} while (0)

#define FUNC_ENTER() MMLOGI("+\n")
#define FUNC_LEAVE() MMLOGV("-\n")

#define ADDSTREAM_GET_META_INT32(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    if ( mCodecMeta->getInt32(_attr, _val) ) {\
        MMLOGI("media_%d got meta: %s -> %d\n", mMediaType, _attr, _val);\
    } else {\
        MMLOGI("media_%d meta %s not provided\n", mMediaType, _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_INT64(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    if ( mCodecMeta->getInt64(_attr, _val) ) {\
        MMLOGI("media_%d got meta: %s -> %d\n", mMediaType, _attr, _val);\
    } else {\
        MMLOGI("media_%d meta %s not provided\n", mMediaType, _attr);\
    }\
}while(0)



#define ADDSTREAM_GET_META_INT32_TO_FRACTION(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    int32_t _i32;\
    if ( mCodecMeta->getInt32(_attr, _i32) ) {\
        MMLOGI("media_%d got meta: %s -> %d\n", mMediaType, _attr, _i32);\
        _val.num = _i32;\
        _val.den = 1;\
    } else {\
        MMLOGI("media_%d meta %s not provided\n", mMediaType, _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_INT32_TO_UINT32(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    int32_t _i32;\
    if ( mCodecMeta->getInt32(_attr, _i32) ) {\
        MMLOGI("media_%d got meta: %s -> %d\n", mMediaType, _attr, _i32);\
        _val = _i32;\
    } else {\
        MMLOGI("media_%d meta %s not provided\n", mMediaType, _attr);\
    }\
}while(0)

//#define DUMP_MUX_DATA
#ifdef DUMP_MUX_DATA
static const char* muxDataDumpFile = "/tmp/mux.h264";
static DataDump muxDataDump(muxDataDumpFile);
#endif


#define MSG_STOP (MMMsgThread::msg_type)1
#define MSG_RESET (MMMsgThread::msg_type)2

#define COPY_TO_SINK_BUFFER

struct AVLogger {
    static void av_log_callback(void *ptr, int level, const char *fmt, va_list vl)
    {
        MMLogLevelType mmLevel = convertLevel(level);
        if (mmLevel > mm_log_get_level()) {
            return;
        }

        char * buf = new char[1024];
        if ( buf ) {
            int ret = vsnprintf(buf, 1023, fmt, vl);
            buf[ret] = '\0';
            MMLOGI("%s\n", buf);
            delete [] buf;
        }
    }

private:
    static MMLogLevelType convertLevel(int level) {
        switch (level) {
            case AV_LOG_PANIC:
            case AV_LOG_FATAL:
            case AV_LOG_ERROR:
                return MM_LOG_ERROR;
            case AV_LOG_WARNING:
                return MM_LOG_WARN;
            case AV_LOG_INFO:
                return MM_LOG_INFO;
            case AV_LOG_VERBOSE:
            case AV_LOG_DEBUG:
            case AV_LOG_TRACE:
                return MM_LOG_VERBOSE;
        }
        return MM_LOG_VERBOSE;
    }

    DECLARE_LOGTAG()
};


DEFINE_LOGTAG(AVMuxer)
DEFINE_LOGTAG(AVMuxer::AVMuxWriter)
DEFINE_LOGTAG(AVMuxer::MuxThread)
DEFINE_LOGTAG(AVLogger)

static const size_t AVIO_BUFFER_SIZE = 32768;
static const AVRational TIMEBASE_DEF = {1, 1000000};

static const char * META_SI = "AV-META-SI";
static const char * META_ME = "AV-META-ME";
static const char * MMTHREAD_NAME = "AVMuxer::MuxThread";
static const char * MMMSGTHREAD_NAME = "AVMuxer";

AVMuxer::StreamInfo::StreamInfo(AVMuxer *muxer, MediaType mediaType) :
                    mComponent(muxer),
                    mMediaType(mediaType),
                    mCodecId(kCodecIDNONE),
                    mStreamEOS(false),
                    mFormerDts(0),
                    mFormerDtsAbs(0),
                    mStartTimeUs(-1ll),
                    mTrackDurationUs(0),
                    mFrameDurationUs(-1ll),
                    mPausedDurationUs(-1ll),
                    mPaused(false),
                    mResumeFirstFrameDone(false),
                    mStream(NULL),
                    mSeq(0),
                    mExtraDataDetermined(false)
{
    FUNC_ENTER();
    memset(&mTimeBase, 0, sizeof(mTimeBase));
    FUNC_LEAVE();
}

AVMuxer::StreamInfo::~StreamInfo()
{
    FUNC_ENTER();
    FUNC_LEAVE();
}

/*static */AVSampleFormat AVMuxer::StreamInfo::convertAudioFormat(snd_format_t sampleFormat)
{
#undef item
#define item(_av, _audio) \
    case _audio:\
        MMLOGI("%s -> %s\n", #_audio, #_av);\
        return _av

    switch ( sampleFormat ) {
        item(AV_SAMPLE_FMT_U8, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32, SND_FORMAT_PCM_32_BIT);
        //item(AV_SAMPLE_FMT_FLT, AUDIO_SAMPLE_FLOAT32LE);
        default:
            MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", sampleFormat);
            return AV_SAMPLE_FMT_NONE;
    }
}

bool AVMuxer::StreamInfo::addStream()
{
    MMLOGI("adding stream: %d\n", mMediaType);
    if ( !mCodecMeta) {
        MMLOGE("stream: %d meta not provided\n", mMediaType);
        return false;
    }

    int32_t codecid;
    if ( !mCodecMeta->getInt32(MEDIA_ATTR_CODECID, codecid) ) {
        MMLOGE("no codec id provided\n");
        return false;
    }
    mCodecId = (CowCodecID)codecid;
    MMLOGI("codecid: %d\n", mCodecId);
    mStream = avformat_new_stream(mComponent->mAVFormatContext, 0);
    if (!mStream) {
        MMLOGE("failed to new stream\n");
        return false;
    }

    if ( !mCodecMeta->getFraction(MEDIA_ATTR_TIMEBASE, mTimeBase.num, mTimeBase.den) ) {
        MMLOGE("timebase not provided, set to default.\n");
        mTimeBase = TIMEBASE_DEF;
    }
    MMLOGI("encoder timebase: %d, %d\n", mTimeBase.num, mTimeBase.den);

    mStream->id = mComponent->mAVFormatContext->nb_streams - 1;
    mStream->time_base = mTimeBase;

    char t[128] = {0};
    sprintf(t, "%d", mComponent->mRotation);
    av_dict_set(&mStream->metadata, "rotate", t, 0);
    MMLOGI("set rotation %d", mComponent->mRotation);


    AVCodecContext *c = nullptr;
    void *ptr = nullptr;
    if (mCodecMeta->getPointer("AVCodecContext", ptr)) {
        MMLOGD("find AVCodecContext pointer in CodecMeta");
        c = static_cast<AVCodecContext*>(ptr);
        if (!c) {
            MMLOGE("null pointer in AVCodecContext");
            return false;
        }

        if (mComponent->mAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
            c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_parameters_from_context(mStream->codecpar, c);
        if (ret < 0) {
            MMLOGE("Could not copy the stream parameters\n");
            return false;
        }
        hexDump(mStream->codecpar->extradata, mStream->codecpar->extradata_size, 16);
        MMLOGI("adding stream: %d, over\n", mMediaType);
        return true;
    }

    // Using Stream Codec member if "AVCodecContext" pointer is not set.
    c = mStream->codec;

    switch ( mMediaType ) {
        case kMediaTypeVideo:
            c->codec_type =  AVMEDIA_TYPE_VIDEO;
            break;
        case kMediaTypeAudio:
            c->codec_type =  AVMEDIA_TYPE_AUDIO;
            break;
        default:
            break;
    }

    if (mComponent->mAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;


    c->codec_id = (AVCodecID)mCodecId;
    c->time_base = mStream->time_base;

//    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_COLOR_FORMAT, c->pix_fmt);
//    ADDSTREAM_GET_META_INT32(, c->bits_per_raw_sample);

    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_CODECPROFILE, c->profile);

    int32_t tmp;
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_BIT_RATE, tmp);
    c->bit_rate = tmp;

    if (c->codec_type == AVMEDIA_TYPE_AUDIO) {
        int32_t i32 = 0;
        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_SAMPLE_RATE, c->sample_rate);
        i32 = 0;
        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_SAMPLE_FORMAT, i32);
        c->sample_fmt = convertAudioFormat((snd_format_t)i32);
        c->frame_size = 1024;

        int64_t i64 = 0;
        ADDSTREAM_GET_META_INT64(MEDIA_ATTR_CHANNEL_LAYOUT, i64);
        c->channel_layout = (uint64_t)i64;

        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_CHANNEL_COUNT, c->channels);
        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_BLOCK_ALIGN, c->block_align);

    } else if (c->codec_type == AVMEDIA_TYPE_VIDEO) {

        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_WIDTH, c->width);
        ADDSTREAM_GET_META_INT32(MEDIA_ATTR_HEIGHT, c->height);

        ADDSTREAM_GET_META_INT32_TO_FRACTION(MEDIA_ATTR_AVG_FRAMERATE, mStream->avg_frame_rate);
    }

    MMLOGI("adding stream: %d, over\n", mMediaType);
    return true;
}

mm_status_t AVMuxer::StreamInfo::pause()
{
    if (!mPaused) {
        mResumeFirstFrameDone = false;
        mPaused = true;
        return MM_ERROR_SUCCESS;
    } else {
        return MM_ERROR_INVALID_STATE;
    }

}

mm_status_t AVMuxer::StreamInfo::resume()
{
    if (mPaused)  {
        mResumeFirstFrameDone = true;
        mPaused = false;
        return MM_ERROR_SUCCESS;
    } else {
        return MM_ERROR_INVALID_STATE;
    }
}

mm_status_t AVMuxer::StreamInfo::stop_l()
{
    DEBUG("%s buffer size %d", mMediaType == kMediaTypeAudio? "Audio" : "Video", mEncodedBuffers.size());
    mEncodedBuffers.clear();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::StreamInfo::write_l(MediaBufferSP buffer)
{
    mEncodedBuffers.push_back(buffer);
    return MM_ERROR_SUCCESS;
}

MediaBufferSP AVMuxer::StreamInfo::read_l()
{
    MediaBufferSP buffer;
    if (!mEncodedBuffers.empty()) {
        buffer = mEncodedBuffers.front();
        mEncodedBuffers.pop_front();
        return buffer;
    }

    return buffer;
}

int64_t AVMuxer::StreamInfo::bufferedDurationUs_l() const
{
    if (mEncodedBuffers.empty()) {
        return -1;
    }

    int64_t diff = mEncodedBuffers.back()->dts() - mEncodedBuffers.front()->dts();
    return av_rescale_q(diff, mTimeBase, TIMEBASE_DEF);

}

int64_t AVMuxer::StreamInfo::bufferFirstUs_l() const
{
    bool emtpy = mEncodedBuffers.empty();
#if 0
    if (mMediaType == kMediaTypeAudio) {
#ifdef HAVE_EIS_AUDIO_DELAY
#define DROP_AUDIO_DURATION_MS 1200
#endif

#ifdef HAVE_EIS_AUDIO_DELAY
        emtpy = bufferedDurationUs_l() / 1000LL < (int64_t)DROP_AUDIO_DURATION_MS;
        emtpy = mComponent->mIsAudioDelay ? emtpy : mEncodedBuffers.empty();
#else
        emtpy = mEncodedBuffers.empty();
#endif
     }
#endif
    if (emtpy)
        return -1;

    return av_rescale_q(mEncodedBuffers.front()->dts(), mTimeBase, TIMEBASE_DEF);
}


AVMuxer::AVMuxWriter::AVMuxWriter(AVMuxer * muxer, StreamInfo * si)
                    : mComponent(muxer),
                    mStreamInfo(si)
{
    FUNC_ENTER();

    const char* monitorName = "TCAVMuxOtherWriter";
    if (si->mMediaType == kMediaTypeAudio)
        monitorName = "TCAVMuxAudioWriter";
    else if (si->mMediaType == kMediaTypeVideo)
        monitorName = "TCAVMuxVideoWriter";
    mTimeCostWriter.reset(new TimeCostStatics(monitorName, 1500)); // 1.5ms fluctuate isn't important

    FUNC_LEAVE();
}

AVMuxer::AVMuxWriter::~AVMuxWriter()
{
    FUNC_ENTER();
    FUNC_LEAVE();
}

mm_status_t AVMuxer::AVMuxWriter::write(const MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    if (mComponent->mForceDisableMuxer) {
        // write eos buffer to filesink, otherwise recorder pipeline cannot exit
        if (buffer && !buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
            return MM_ERROR_SUCCESS;
        }
    }
    mTimeCostWriter->sampleBegin();
    mm_status_t status = mComponent->write(buffer, mStreamInfo);
    mTimeCostWriter->sampleEnd();
    return status;
}

mm_status_t AVMuxer::AVMuxWriter::setMetaData(const MediaMetaSP & metaData)
{
    FUNC_ENTER();
    mStreamInfo->mCodecMeta = metaData;
    MMLOGD("media meta(%d) dump:\n", mStreamInfo->mMediaType);
    mStreamInfo->mCodecMeta->dump();
    return MM_ERROR_SUCCESS;
}

AVMuxer::MuxThread::MuxThread(AVMuxer * muxer)
                    : MMThread(MMTHREAD_NAME),
                    mMuxer(muxer),
                    mContinue(false)
{
    FUNC_ENTER();
    sem_init(&mSem, 0, 0);
    FUNC_LEAVE();
}

AVMuxer::MuxThread::~MuxThread()
{
    FUNC_ENTER();
    sem_destroy(&mSem);
    FUNC_LEAVE();
}

mm_status_t AVMuxer::MuxThread::prepare()
{
    FUNC_ENTER();
    mContinue = true;
    if ( create() ) {
        MMLOGE("failed to create thread\n");
        return MM_ERROR_NO_MEM;
    }

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::MuxThread::reset()
{
    FUNC_ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::MuxThread::mux()
{
    MMLOGV("+\n");
    sem_post(&mSem);
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void AVMuxer::MuxThread::main()
{
    FUNC_ENTER();
    while ( MM_LIKELY(mContinue) ) {
        MMLOGV("waitting sem\n");
        sem_wait(&mSem);
        if ( MM_UNLIKELY(!mContinue) ) {
            MMLOGV("not conitnue\n");
            break;
        }

        mm_status_t ret;
        while ( MM_LIKELY(mContinue) ) {
            MMLOGV("muxing...\n");
            ret = mMuxer->mux();
            MMLOGV("muxing...ret: %d\n", ret);
            if ( ret == MM_ERROR_SUCCESS ) {
                usleep(0);
                continue;
            }

            if ( MM_UNLIKELY(ret == MM_ERROR_EOS) ) {
                MMLOGI("EOS\n");
                mContinue = false;
                break;
            }

            if ( ret == MM_ERROR_NO_MORE
                || ret == MM_ERROR_INVALID_STATE ) {
                MMLOGV("no more or undetermined\n");
                break;
            }

            if (ret == MM_ERROR_MALFORMED) {
                MMLOGE("media may not recorgnized\n");
                return;
            }

            // other:
            MMLOGE("other error\n");
            usleep(100000);
            continue;
        }
    }

    MMLOGI("will exiting, write all\n");

    mm_status_t r;
    while ( (r = mMuxer->mux()) == MM_ERROR_SUCCESS ) {
        usleep(0);
    }

    for ( size_t i = 0; i < mMuxer->mStreamInfoArray.size(); ++i ) {
        StreamInfo * si = mMuxer->mStreamInfoArray[i];
        DEBUG("%s buffer size %d",
            si->mMediaType == kMediaTypeAudio ? "audio" : "video", si->mEncodedBuffers.size());
    }

    if (MM_UNLIKELY(r == MM_ERROR_MALFORMED)) {
        MMLOGE("MALFORMED, not write trailer\n");
    } else {
        mMuxer->writeTrailer();
    }

    MMLOGI("-\n");
}


BEGIN_MSG_LOOP(AVMuxer)
    MSG_ITEM(MSG_STOP, onStop)
    MSG_ITEM(MSG_RESET, onReset)
END_MSG_LOOP()

AVMuxer::AVMuxer() : MMMsgThread(MMMSGTHREAD_NAME),
                        mState(STATE_NONE),
                        mRotation(0),
                        mLatitude(-360.0f),
                        mLongitude(-360.0f),
                        mMaxDuration(0),
                        mMaxFileSize(0),
                        mCurFileSize(0),
                        mAVFormatContext(NULL),
                        mAVIOContext(NULL),
                        mWriter((Writer*)NULL),
                        mOutputSeekOffset(-1),
                        mOutputSeekWhence(-1),
                    #ifdef HAVE_EIS_AUDIO_DELAY
                        mIsAudioDelay(false),
                    #endif
                        mSinkBufferReleased(true),
                        mAllMediaExtraDataDetermined(false),
                        mEOS(false),
                        mMuxThread(NULL),
                        mCurrentDts(-1ll),
                        mCheckVideoKeyFrame(false),
                        mTimeCostMux("TCAVMuxer", 1500),
                        mForceDisableMuxer(false),
                        mStreamDriftMax(-1)
{
    FUNC_ENTER();
    class AVInitializer {
    public:
        AVInitializer() {
            MMLOGI("use log level converter\n");
            av_log_set_callback(AVLogger::av_log_callback);
            av_log_set_level(AV_LOG_ERROR);
            avcodec_register_all();
            av_register_all();
            avformat_network_init();
        }
        ~AVInitializer() {
            av_log_set_callback(NULL);
            avformat_network_deinit();
        }
    };
    static AVInitializer sAVInit;
    mForceDisableMuxer = mm_check_env_str("mm.force.disable.mux", "MM_FORCE_DISABLE_MUX", "1", false);
    mConvertH264ByteStreamToAvcc = mm_check_env_str("mm.mux.2.avcc", "MM_MUX_2_AVCC", "1", false);
    FUNC_LEAVE();
}

AVMuxer::~AVMuxer()
{
    FUNC_ENTER();
    FUNC_LEAVE();
}

mm_status_t AVMuxer::init()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_IDLE, MM_ERROR_SUCCESS);
    CHECK_STATE(STATE_NONE);

    int ret = run();
    if ( ret ) {
        MMLOGE("failed to run, ret: %d\n", ret);
        SET_STATE(STATE_ERROR);
        return MM_ERROR_NO_MEM;
    }

    mState = STATE_IDLE;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void AVMuxer::uninit()
{
    FUNC_ENTER();
    exit();
    mState = STATE_NONE;
    FUNC_LEAVE();
}

mm_status_t AVMuxer::setParameter(const MediaMetaSP & meta)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_OUTPUT_FORMAT) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mOutputFormat = item.mValue.str;
            if (!strcmp(item.mValue.str, "m4a")) {
                mOutputFormat = "mp4";
            }
            MMLOGI("key: %s, value: %s\n", item.mName, mOutputFormat.c_str());
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_ROTATION) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mRotation = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mRotation);
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_LOCATION_LATITUDE) ) {
            if ( item.mType != MediaMeta::MT_Float ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mLatitude = item.mValue.f;
            MMLOGI("key: %s, value: %0.4f\n", item.mName, mLatitude);
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_LOCATION_LONGITUDE) ) {
            if ( item.mType != MediaMeta::MT_Float ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mLongitude = item.mValue.f;
            MMLOGI("key: %s, value: %0.4f\n", item.mName, mLongitude);
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_MAX_DURATION) ) {
            if ( item.mType != MediaMeta::MT_Int64 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mMaxDuration = item.mValue.ld;
            MMLOGI("key: %s, value: %" PRId64 "\n", item.mName, mMaxDuration);
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_MAX_FILE_SIZE) ) {
            if ( item.mType != MediaMeta::MT_Int64 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mMaxFileSize = item.mValue.ld;
            MMLOGI("key: %s, value: %" PRId64 "\n", item.mName, mMaxFileSize);
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_MUXER_STREAM_DRIFT_MAX) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }

            mStreamDriftMax = item.mValue.ii;
            MMLOGI("key: %s, value: %d ms" , item.mName, mStreamDriftMax);
            continue;
        }
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::getParameter(MediaMetaSP & meta) const
{
    FUNC_ENTER();
    meta->setString(MEDIA_ATTR_OUTPUT_FORMAT, mOutputFormat.c_str());
    return MM_ERROR_UNSUPPORTED;
}

Component::WriterSP AVMuxer::getWriter(MediaType mediaType)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return Component::WriterSP((Component::Writer*)NULL);
    }

    StreamInfo * si = new StreamInfo(this, mediaType);
    if ( !si ) {
        MMLOGE("no mem\n");
        return Component::WriterSP((Component::Writer*)NULL);
    }

    WriterSP writer(new AVMuxWriter(this, si));
    if ( !writer ) {
        MMLOGE("no mem\n");
        MM_RELEASE(si);
        return Component::WriterSP((Component::Writer*)NULL);
    }

    mStreamInfoArray.push_back(si);

    FUNC_LEAVE();
    return writer;
}

mm_status_t AVMuxer::addSink(Component * component, MediaType mediaType)
{
    FUNC_ENTER();
    MMASSERT(component != NULL);
    mWriter = component->getWriter(mediaType);
    if ( !mWriter ) {
        MMLOGE("failed to get writer\n");
        return MM_ERROR_INVALID_PARAM;
    }

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::prepare()
{
    FUNC_ENTER();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

bool AVMuxer::writeHeader()
{
    MMLOGI("writting header\n");

    //check latitude and longitude
    bool valid = fabs(mLatitude) < 90.00001f && fabs(mLongitude) < 180.00001f;
    if (valid) {
        char t[128] = {0};
        char sign1 = int32_t(mLatitude * 10000) >= 0 ? '+' : '-';
        char sign2 = int32_t(mLongitude * 10000) >= 0 ? '+' : '-';
        sprintf(t, "%c%2.4f%c%2.4f", sign1, mLatitude, sign2, mLongitude);

        av_dict_set(&mAVFormatContext->metadata, "location", t, 0);
        MMLOGI("set [%0.4f %0.4f], location %s", mLatitude, mLongitude, t);
    } else {
        MMLOGD("%2.4f*%2.4f", mLatitude, mLongitude);
    }

    // set metadata before calling avformat_write_header, for ffmpeg will check this metadata in write_header
    if (mMaxFileSize > 0) {
        //notify ffmpeg to calculate moov_size
        av_dict_set(&mAVFormatContext->metadata, "need_moov_size", "1", 0);
    }

    //AVStream.time_base will be overwritten by muxer
    if ( avformat_write_header(mAVFormatContext, NULL) ) {
        MMLOGE("failed to write header\n");
        return false;
    }
    MMLOGV("writting header over\n");
    return true;
}

bool AVMuxer::writeTrailer()
{
    MMLOGI("writting trailer\n");

    //Do not write trailer if no frame is written
    for ( size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
        StreamInfo * si = mStreamInfoArray[i];
        if ( !si->mExtraDataDetermined) {
            MMLOGI("checking extra data: stream_%d not determined\n", si->mMediaType);
            // write eos buffer to file sink, in which will notify kEventEos to pipeline
            signalEOS2Sink();
            return false;
        }
    }

    // If av_write_header is not called, av_write_trailer should not be called
    if (!mAllMediaExtraDataDetermined) {
        signalEOS2Sink();
        return false;
    }

    if ( av_write_trailer(mAVFormatContext) ) {
        MMLOGE("failed to write trailer\n");
        // return false;
    }

    signalEOS2Sink();
    MMLOGD("writting trailer over\n");
    return true;
}

mm_status_t AVMuxer::start()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_STARTED, MM_ERROR_SUCCESS);
    CHECK_STATE(STATE_IDLE);

#ifdef HAVE_EIS_AUDIO_DELAY
    mIsAudioDelay = mm_check_env_str("persist.camera.morphoeis.en", "PERSIST_CAMERA_MORPHOEIS_EN", "1", false);
#endif

    mm_status_t ret = createContext();
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to create context\n");
        return ret;
    }

    StreamInfoArray::iterator i;
    for ( i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
        StreamInfo * si = *i;
        if ( !si->addStream() ) {
            MMLOGE("failed to add stream for %d\n", si->mMediaType);
            return MM_ERROR_OP_FAILED;
        }
        si->mStreamEOS = false;
    }

    mAllMediaExtraDataDetermined = false;
    mEOS = false;

    mMuxThread = new MuxThread(this);
    if ( !mMuxThread ) {
        MMLOGE("failed to new thread\n");
        releaseContext();
        return MM_ERROR_NO_MEM;
    }

    if ( mMuxThread->prepare() != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to start thread\n");
        releaseContext();
        MM_RELEASE(mMuxThread);
        return MM_ERROR_NO_MEM;
    }

    SET_STATE(STATE_STARTED);
    MMLOGI("success\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::createContext()
{
    FUNC_ENTER();
    MMASSERT(mAVFormatContext == NULL);
    MMASSERT(mAVIOContext == NULL);

    if ( mOutputFormat.empty() ) {
        MMLOGE("output format not set\n");
        return MM_ERROR_INVALID_PARAM;
    }

    AVOutputFormat * outputFormat = av_guess_format(mOutputFormat.c_str(), NULL, NULL);
    if ( !outputFormat ) {
        MMLOGE("format(%s) not supported.\n", mOutputFormat.c_str());
        return MM_ERROR_INVALID_PARAM;
    }

    mAVFormatContext = avformat_alloc_context();
    if ( !mAVFormatContext ) {
        MMLOGE("failed to create avcontext\n");
        return MM_ERROR_INVALID_PARAM;
    }
    mAVFormatContext->oformat = outputFormat;

    unsigned char * ioBuf = (unsigned char*)av_malloc(AVIO_BUFFER_SIZE);
    if ( !ioBuf ) {
        MMLOGE("no mem\n");
        avformat_free_context(mAVFormatContext);
        mAVFormatContext = NULL;
        return MM_ERROR_NO_MEM;
    }

    mAVIOContext = avio_alloc_context(ioBuf,
                    AVIO_BUFFER_SIZE,
                    1,
                    this,
                    NULL,
                    avWrite,
                    avSeek);

    if ( !mAVIOContext ) {
        MMLOGE("no mem\n");
        avformat_free_context(mAVFormatContext);
        mAVFormatContext = NULL;
        av_free(ioBuf);
        ioBuf = NULL;
        return MM_ERROR_NO_MEM;
    }

    mAVFormatContext->pb = mAVIOContext;
    mAVFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

    MMLOGI("success\n");
    return MM_ERROR_SUCCESS;
}

void AVMuxer::releaseContext()
{
    FUNC_ENTER();
    for ( auto i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
        StreamInfo * si = *i;
        void *ptr = nullptr;
        if (!si->mCodecMeta->getPointer("AVCodecContext", ptr)) {
            // Using Stream Codec member if "AVCodecContext" pointer is not set.
            AVCodecContext *c = si->mStream->codec;
            if (si->mStream->codec->extradata &&
                si->mStream->codec->extradata_size) {
                av_free(c->extradata);
                c->extradata = NULL;
                c->extradata_size = 0;
            }
        }
    }

    if ( mAVFormatContext ) {
        mAVFormatContext->pb = NULL;
        avformat_free_context(mAVFormatContext);
        mAVFormatContext = NULL;
    }

    if ( mAVIOContext ) {
        if ( mAVIOContext->buffer ) {
            av_free(mAVIOContext->buffer);
            mAVIOContext->buffer = NULL;
        }
        av_free(mAVIOContext);
        mAVIOContext = NULL;
    }
}

bool AVMuxer::hasMedia(MediaType mediaType)
{
    MMAutoLock lock(mLock);
    return hasMediaInternal(mediaType);
}

bool AVMuxer::hasMediaInternal(MediaType mediaType)
{
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return false;
    }

    StreamInfoArray::iterator i;
    for ( i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
        StreamInfo * si = *i;
        if ( si->mMediaType == mediaType) {
            MMLOGV("mediaType %d found in track", mediaType);
            return true;
        }
    }
    return false;
}


mm_status_t AVMuxer::stop()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_IDLE, MM_ERROR_SUCCESS);
    RET_IF_ALREADY_STATE(STATE_STOPPING, MM_ERROR_ASYNC);

    if ( mState != STATE_STARTED) {
        MMLOGE("invalid sate(%d)\n", mState);
        return MM_ERROR_INVALID_STATE;
    }

    SET_STATE(STATE_STOPPING);
    if ( postMsg(MSG_STOP, 0, 0, 0) ) {
        MMLOGE("failed to post\n");
        return MM_ERROR_NO_MEM;
    }

    FUNC_LEAVE();
    return MM_ERROR_ASYNC;
}

void AVMuxer::stopInternal()
{
    FUNC_ENTER();
    mMuxThread->reset();

    {
        MMAutoLock lock(mBufferLock);
        MMLOGI("thread exit");

        for ( auto i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
            StreamInfo * si = *i;
            if (si->mMediaType == kMediaTypeAudio) {
#ifdef HAVE_EIS_AUDIO_DELAY
                if (mIsAudioDelay) {
                    MMLOGI("thread exit, timeDiff %" PRId64 "ms\n",
                        si->bufferedDurationUs_l());
                }
#endif
            }
            si->stop_l();
        }
    }



    MM_RELEASE(mMuxThread);
    releaseContext();

    SET_STATE(STATE_IDLE);
    FUNC_LEAVE();
}

void AVMuxer::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    stopInternal();
    NOTIFY(kEventStopped, 0, 0, nilParam);
    FUNC_LEAVE();
}

mm_status_t AVMuxer::pause()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    StreamInfoArray::iterator i;
    for ( i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
        StreamInfo * si = *i;
        if ( si->pause() == MM_ERROR_INVALID_STATE) {
            return MM_ERROR_SUCCESS;
        }
    }
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVMuxer::resume()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    StreamInfoArray::iterator i;
    for ( i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
        StreamInfo * si = *i;
        if ( si->resume() == MM_ERROR_INVALID_STATE ) {
            return MM_ERROR_SUCCESS;  //not need to reset mCheckVideoKeyFrame when resume is repeatedly called
        }
    }

    mCheckVideoKeyFrame = hasMediaInternal(kMediaTypeVideo);
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void AVMuxer::resetInternal()
{
    FUNC_ENTER();

    {
        MMAutoLock lock(mBufferLock);
        for ( auto i = mStreamInfoArray.begin(); i != mStreamInfoArray.end(); ++i ) {
            StreamInfo * si = *i;
            si->stop_l();
        }
    }

    mWriter.reset();
    for ( size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
        StreamInfo * si = mStreamInfoArray[i];
        delete si;
    }
    mStreamInfoArray.clear();
    SET_STATE(STATE_IDLE);
    FUNC_LEAVE();
}

mm_status_t AVMuxer::reset()
{
    FUNC_ENTER();
    if ( postMsg(MSG_RESET, 0, 0, 0) ) {
        MMLOGE("failed to post\n");
        return MM_ERROR_NO_MEM;
    }

    FUNC_LEAVE();
    return MM_ERROR_ASYNC;
}

void AVMuxer::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    switch ( mState ) {
        case STATE_STARTED:
            stopInternal();
        case STATE_IDLE:
            resetInternal();
            break;
        case STATE_NONE:
        case STATE_ERROR:
        default:
            MMLOGI("state now is %d\n", mState);
    }
    NOTIFY(kEventResetComplete, 0, 0, nilParam);
    FUNC_LEAVE();
}

mm_status_t AVMuxer::write(const MediaBufferSP & buffer, StreamInfo * si)
{
    size_t i = 0;
    if ( !si ) {
        MMLOGE("no si\n");
        return MM_ERROR_INVALID_PARAM;
    }
    MMAutoLock lock(mBufferLock);
    if ( mState != STATE_STARTED ) {
        MMLOGE("invalid state(%d). expect STATE_STARTED\n", mState);
        return MM_ERROR_AGAIN;
    }

    if (!buffer) {
        MMLOGE("buffer is invalid\n");
        return MM_ERROR_INVALID_PARAM;
    }
    if ( MM_UNLIKELY(si->mStreamEOS) ) {
        MMLOGW("media %d already eos\n", si->mMediaType);
        return MM_ERROR_INVALID_STATE;
    }

    if (mStreamDriftMax > 0) {
        // if current stream is too early than others, reject current buffer and inform upstream component to send the buffer again
        for ( i = 0; i < mStreamInfoArray.size(); ++i ) {
            if (buffer->dts() - mStreamInfoArray[i]->mFormerDtsAbs > mStreamDriftMax) {
                DEBUG("bufer->dts: %" PRId64 " mFormerDtsAbs[%d]: %" PRId64, buffer->dts(), i, mStreamInfoArray[i]->mFormerDtsAbs);
                return MM_ERROR_AGAIN;
            }
        }
    }

    MediaMetaSP meta = buffer->getMediaMeta();

    // generate avcC (extradata) once
    if ( MM_UNLIKELY(!si->mExtraDataDetermined) ) {
        do {
            // amr-nb/wb have no extradata
            if (si->mCodecId == kCodecIDAMR_NB ||
                si->mCodecId == kCodecIDAMR_WB) {
                si->mExtraDataDetermined = true;
                break;
            }
            // get codecdata of SPS/PPS info
            uint8_t * data;
            int32_t size;
            // codecdata usually comes with first buffer (encoder), it can be set during pipeline-graph-configuration as well(avdemuxer)
            if ( !meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size) && !si->mCodecMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) {
                MMLOGI("media_%d no codec data\n", si->mMediaType);
                break;
            } else {
                MMLOGI("got codec data");
                hexDump(data, size, 16);
            }

            MediaBufferSP mediaBuffer;
            if (!strcmp(mOutputFormat.c_str(), "mp4") && (si->mCodecId == kCodecIDH264 && mConvertH264ByteStreamToAvcc)) {
                if (isAnnexBByteStream(data, size)) {
                    // convert ByteStream to avcC for codecdata
                    DEBUG("h264 video got ByteStream codec data");
                    mediaBuffer = MakeAVCCodecExtradata(data, (size_t)size);
                    if (!mediaBuffer) {
                        MMLOGE("make avcc extradata failed\n");
                        return MM_ERROR_MALFORMED;
                    }

                    // retrieve (updated) codec data from mediaBuffer
                    uint8_t *buffers = NULL;
                    int32_t offsets = 0;
                    int32_t strides = 0;
                    if ( !mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
                        MMLOGE("failed to get bufferinfo(media: %d)\n", si->mMediaType);
                        return MM_ERROR_INVALID_PARAM;
                    }
                    data = buffers + offsets;
                    size = strides - offsets;
                } else {
                    DEBUG("h264 video got avcC codecdata");
                }
            } else if (!strcmp(mOutputFormat.c_str(), "mp4") && (si->mCodecId == kCodecIDHEVC || si->mCodecId == kCodecIDH264)) {
                if (isAnnexBByteStream(data, size))
                    INFO("get %s csd data in annex B format", si->mCodecId == kCodecIDHEVC ? "hevc" : "avc");
                else if (data[0] == 1)
                    INFO("get %s csd data in avcC-formatted", si->mCodecId == kCodecIDHEVC ? "hevc" : "avc");
                else {
                    ERROR("invalid %s csd data", si->mCodecId == kCodecIDHEVC ? "hevc" : "avc");
                    size = 0;
                    data = NULL;
                }
            }

            void *ptr = nullptr;
            // AVCodecContext.extradata field is Set/allocated/freed by libavcodec.
            // So it's better to use AVCodecContext.codecparas when encoder is ffmepg.
            // when there is AVCodecContext already, we needn't fill extradata (supposed it is already there).
            if (!si->mCodecMeta->getPointer("AVCodecContext", ptr)) {
                // Using Stream Codec member if "AVCodecContext" pointer is not set.
                AVCodecContext *c = si->mStream->codec;
                if (data && size) {
                    c->extradata = (uint8_t*)av_malloc(size);
                    if ( !c->extradata ) {
                        MMLOGE("media_%d getting codec data, no mem, need: %d\n", si->mMediaType, size);
                        c->extradata_size = 0;
                        break;
                    }

                    MMLOGI("media_%d got codec data, size: %d\n", si->mMediaType, size);
                    memcpy(c->extradata, data, size);
                    c->extradata_size = size;
                } else {
                    c->extradata = NULL;
                    c->extradata_size = 0;
                }
            }
            si->mExtraDataDetermined = true;
        } while(0);

        if ( buffer->isFlagSet(MediaBuffer::MBFT_CodecData) ) {
            MMLOGI("codec data only\n");
            return MM_ERROR_SUCCESS;
        }
    }

    meta->setPointer(META_SI, si);

    // "strip start code" doesn't work for hevc
    if (!strcmp(mOutputFormat.c_str(), "mp4") && (si->mCodecId == kCodecIDH264 && mConvertH264ByteStreamToAvcc)) {
        uint8_t *buffers = NULL;
        int32_t offsets = 0;
        int32_t strides = 0;
        if ( buffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) &&
            strides - offsets != 0) {
            buffers += offsets;
            strides -= offsets;
            // hexDump(buffers, 64, 16);
            if (isAnnexBByteStream(buffers, strides)) {
                VERBOSE("convert byte stream to avcc");
                // convert ByteStream 0001 to nal size
                uint8_t *nalStart = nullptr;
                size_t nalSize = 0;
                const uint8_t *srcBuffer = buffers;
                size_t srcSize = (size_t)strides;
                while (getNextNALUnit(&srcBuffer, &srcSize, (const uint8_t**)&nalStart, &nalSize, true) == MM_ERROR_SUCCESS) {
                    if (nalSize > 0) {
                        //The length field includes the size of
                        //both the one byte NAL header and the EBSP payload
                        //but does not include the length field itself

                        // Make sure the startcode is 0x00000001, nalsizelength is 4
                        nalStart[-4] = nalSize >> 24;
                        nalStart[-3] = (nalSize >> 16) & 0xff;
                        nalStart[-2] = (nalSize >> 8) & 0xff;
                        nalStart[-1] = nalSize & 0xff;

                        VERBOSE("nalsize %d", nalSize);
                    }
                }
                // hexDump(buffers, 64, 16);
            }
        } else {
            if ( buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
                MMLOGI("get eos buffer %d from mWriter, queue to buffer list\n", si->mMediaType);
            } else {
                // if buffer size is 0 and not set eos flag, just discard this buffer
                return MM_ERROR_SUCCESS;
            }
        }
    }

    si->mSeq++;
    MMLOGV("write to list: media: %d, seq: %d, pts: %" PRId64 ", dts: %" PRId64 ", size: %" PRId64 ", steamid: %d\n",
        si->mMediaType,
        si->mSeq,
        buffer->pts(),
        buffer->dts(),
        buffer->size(),
        si->mStream->id);

    if (buffer->pts() < 0 ) {
        buffer->setPts(0);
    }
    if (buffer->dts() < 0) {
        buffer->setDts(0);
    }
    si->write_l(buffer);
    if ( buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        MMLOGI("get eos buffer %d from mWriter, queue to buffer list\n", si->mMediaType);
    }
    mMuxThread->mux();
    MMLOGV("-\n");
    return MM_ERROR_SUCCESS;
}

void AVMuxer::avWrite(uint8_t *buf, int buf_size)
{
    MMLOGV("+\n");
    MediaBufferSP mb = createSinkBuffer(buf, buf_size);
    if ( !mb ) {
        MMLOGE("failed to create sink buffer\n");
        return;
    }
#ifndef COPY_TO_SINK_BUFFER
    mSinkBufferReleased = false;
#endif
    mWriter->write(mb);
    MMLOGV("-\n");
}

/*static */int AVMuxer::avWrite(void *opaque, uint8_t *buf, int buf_size)
{
    AVMuxer * me = static_cast<AVMuxer*>(opaque);
    if ( !me ) {
        MMLOGE("invalid cb\n");
        return -1;
    }

    me->avWrite(buf, buf_size);
    return buf_size;
}

#if 0
//protected by mBufferLock
MediaBufferSP AVMuxer::getMinFirstBuffer_l()
{
    int64_t audioPts = -1;
    int64_t videoPts = -1;
    MediaBufferSP buffer;

    StreamInfo *audioInfo = NULL;
    StreamInfo *videoInfo = NULL;
    for ( size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
        StreamInfo * si = mStreamInfoArray[i];
        if (si->mMediaType == kMediaTypeAudio) {
            audioPts =  si->bufferFirstUs_l();
            audioInfo = si;
        } else if (si->mMediaType == kMediaTypeVideo) {
            videoPts =  si->bufferFirstUs_l();
            videoInfo = si;
        }
    }

    if (videoInfo && !audioInfo) {
        // no audio
        return videoInfo->read_l();
    } else if (!videoInfo && audioInfo) {
        // no video
        return audioInfo->read_l();
    }


    if (audioPts == -1 && videoPts == -1) {
        // both buffer lists are empty
    } else if (audioPts == -1 && videoPts >= 0) {
        // video stream has one more buffers
        return videoInfo->read_l();
    } else if (audioPts >= 0 && videoPts == -1){
        // audio stream has one more buffers
        return audioInfo->read_l();
    } else {
        if (audioPts < videoPts) {
            return audioInfo->read_l();
        } else {
            return videoInfo->read_l();
        }
    }

    return buffer;
}
#else
//protected by mBufferLock
MediaBufferSP AVMuxer::getMinFirstBuffer_l()
{
    MediaBufferSP buffer;

    int64_t minPts = 0x7fffffffffffffff;
    int32_t selectIndex = -1;

    for ( size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
        StreamInfo * si = mStreamInfoArray[i];
        if (si->mMediaType == kMediaTypeAudio || si->mMediaType == kMediaTypeVideo) {
            int64_t fisrtPts = si->bufferFirstUs_l();
            if (fisrtPts == -1) {
                // empty buffer list
                if (si->mStreamEOS) {
                    // if stream got eos flag, continue to mux the other stream
                    continue;
                } else {
                    return buffer;;
                }
            }

            if (minPts >= fisrtPts) {
                minPts = fisrtPts;
                selectIndex = i;
            }
        }
    }
    if (selectIndex >= 0) {
        return mStreamInfoArray[selectIndex]->read_l();
    }

    return buffer;
}
#endif

mm_status_t AVMuxer::mux()
{
    MMLOGV("+\n");
    AutoTimeCost tc(mTimeCostMux);

#ifndef COPY_TO_SINK_BUFFER
    if ( !mSinkBufferReleased ) {
        MMLOGV("sink buffer not released\n");
        return MM_ERROR_INVALID_STATE;
    }
#endif

    MediaBufferSP buffer;
    {
        MMAutoLock lock(mBufferLock);
        buffer = getMinFirstBuffer_l();
        if (!buffer) {
            MMLOGV("no more\n");
            if ( mEOS ) {
                MMLOGI("eos\n");
                return MM_ERROR_EOS;
            }
            return MM_ERROR_NO_MORE;
        }
    }

    MediaMetaSP meta = buffer->getMediaMeta();
    StreamInfo * si;
    void * p;
    if ( !meta->getPointer(META_SI, p) ) {
        MMLOGE("no stream info\n");
        return MM_ERROR_INVALID_PARAM;
    }

    si = static_cast<StreamInfo*>(p);
    if ( !si ) {
        MMLOGE("invalid si\n");
        return MM_ERROR_INVALID_PARAM;
    }

    uint8_t * buffers;
    int32_t offsets;
    int32_t strides;

    if (MM_UNLIKELY(buffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
        si->mStreamEOS = true;
        MMLOGI("media eos: %d\n", si->mMediaType);
        for (size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
            if ( !mStreamInfoArray[i]->mStreamEOS ) {
                MMLOGI("media not eos: %d\n", mStreamInfoArray[i]->mMediaType);
                mMuxThread->mux();
                return MM_ERROR_SUCCESS;
            }
        }

        MMLOGI("all media eos\n");
        mEOS = true;
        mMuxThread->mux();
        return MM_ERROR_EOS;
    }

    if ( MM_UNLIKELY(!mAllMediaExtraDataDetermined) ) {
        MMLOGV("checking extra data\n");
        for ( size_t i = 0; i < mStreamInfoArray.size(); ++i ) {
            StreamInfo * si = mStreamInfoArray[i];
            if ( !si->mExtraDataDetermined ) {
                MMLOGI("checking extra data: stream_%d not determined\n", si->mMediaType);
                return MM_ERROR_INVALID_STATE;
            }
        }
        if (!writeHeader()) {
            MMLOGE("failed to write header\n");
            return MM_ERROR_MALFORMED;
        }
        MMLOGV("checking extra data: all extra data determined\n");
        mAllMediaExtraDataDetermined = true;
        // return MM_ERROR_SUCCESS;
    }

    if ( !buffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
        MMLOGE("failed to get bufferinfo(media: %d)\n", si->mMediaType);
        return MM_ERROR_INVALID_PARAM;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = (uint8_t*)buffers + offsets;
    pkt.size = (uint32_t)buffer->size() - offsets;

    pkt.pts = buffer->pts();
    pkt.dts = buffer->dts();
    pkt.duration = buffer->duration();
    si->mFormerDtsAbs = pkt.dts; //time based on src timebase

    // convert src timebase to muxer timebase
    av_packet_rescale_ts(&pkt, si->mTimeBase,si->mStream->time_base);
    VERBOSE("encoder timebase [%d, %d], mux timebase [%d, %d]",
        si->mTimeBase.num, si->mTimeBase.den,
        si->mStream->time_base.num, si->mStream->time_base.den);
    pkt.stream_index = si->mStream->id;
    switch ( si->mMediaType ) {
        case kMediaTypeVideo:
            if ( buffer->isFlagSet(MediaBuffer::MBFT_KeyFrame) ) {
                pkt.flags |= AV_PKT_FLAG_KEY;
            } else {
                pkt.flags &= ~AV_PKT_FLAG_KEY;
            }
            break;
        case kMediaTypeAudio:
            pkt.flags |= AV_PKT_FLAG_KEY;
            break;
        default:
            break;
    }

    if (MM_UNLIKELY(si->mPaused)) {
        MMLOGV("discard frame in Paused state");
        return MM_ERROR_SUCCESS;
    }

    if (MM_UNLIKELY(mCheckVideoKeyFrame)) {
        if (si->mMediaType == kMediaTypeVideo &&
            buffer->isFlagSet(MediaBuffer::MBFT_KeyFrame)) {
            MMLOGD("get I frame after resume");
            mCheckVideoKeyFrame = false;
        } else {
            MMLOGD("skip %s frame before got first key video frame\n",
                si->mMediaType == kMediaTypeVideo ? "Video" : "Audio");
            return MM_ERROR_SUCCESS;
        }
    }

    // note: mFrameDurationUs/mFormerDts/mFrameDurationUs are based on muxer timebase
    if (si->mResumeFirstFrameDone) {

        si->mPausedDurationUs = pkt.dts - si->mTrackDurationUs - si->mFrameDurationUs;

        if (si->mStartTimeUs != -1)
            si->mPausedDurationUs -= si->mStartTimeUs;

        si->mResumeFirstFrameDone = false;
    }

    if (si->mPausedDurationUs != -1) {
        pkt.dts -= si->mPausedDurationUs;
        pkt.pts = pkt.dts;
    }
    if (si->mFormerDts != 0 &&
        pkt.dts >= si->mFormerDts) {
        si->mFrameDurationUs = pkt.dts - si->mFormerDts;//based on muxer timebase
    }

    if (pkt.dts < si->mFormerDts) {
        // bugs in libyami encoder, output buffer with 0 timestamp, which should be fixed in future
        // just work round in muxer
        pkt.dts = si->mFormerDts + 1;
        pkt.pts = pkt.dts;
    }

    si->mTrackDurationUs += si->mFrameDurationUs;
    if (pkt.duration <= 0 && si->mFrameDurationUs > 0) {
        pkt.duration = si->mFrameDurationUs; //pkt.duration should based on muxer timebase
    }
    MMLOGD("writting frame, media: %d, dts: %" PRId64 ", pts: %" PRId64 ", duration: %" PRId64 ", size: %d, stream_index: %d, flags: %d, mFrameDurationUs %" PRId64 ", mStartTimeUs %" PRId64 ", mTrackDurationUs %" PRId64 ", mPausedDurationUs %" PRId64 "\n",
        si->mMediaType,
        av_rescale_q(pkt.dts, si->mStream->time_base, TIMEBASE_DEF),
        av_rescale_q(pkt.pts, si->mStream->time_base, TIMEBASE_DEF),
        pkt.duration,
        pkt.size,
        pkt.stream_index,
        pkt.flags,
        si->mFrameDurationUs,
        si->mStartTimeUs,
        si->mTrackDurationUs,
        si->mPausedDurationUs);


    si->mFormerDts = pkt.dts;
    if (si->mStartTimeUs == -1ll) {
        si->mStartTimeUs = pkt.dts;
    }

    // pkt.size will be cleared in av_interleaved_write_frame
    int pktSize = pkt.size;
    // pkt is based on mStream->time_base
    // convert to pts based on {1,1000000}
    mCurrentDts = av_rescale_q(pkt.pts, si->mStream->time_base, TIMEBASE_DEF);
#ifdef DUMP_MUX_DATA
    muxDataDump.dump(pkt.data, pkt.size);
#endif

    if ( av_interleaved_write_frame(mAVFormatContext, &pkt) ) {
        MMLOGE("failed to write to avformat(media: %d)\n", si->mMediaType);
        return MM_ERROR_OP_FAILED;
    }

    MMLOGV("av_interleaved_write_frame() done, mCurrentDts %lld", mCurrentDts/1000LL);
    //moov_size
    if (mMaxFileSize > 0) {
        mCurFileSize += pktSize;
        if (!strcmp(mOutputFormat.c_str(), "mp4") ||
            !strcmp(mOutputFormat.c_str(), "3gp")) {
            AVDictionaryEntry * aMoovSize = av_dict_get(mAVFormatContext->metadata, "moov_size", NULL, 0);
            if (aMoovSize) {
                MMLOGV("mCurFileSize %" PRId64 ", pkt.size %d, key %s, value %s\n",
                    mCurFileSize, pktSize, aMoovSize->key, aMoovSize->value);
                int64_t iMoovSize = atoi(aMoovSize->value);

                //add 1024 bytes as tolerance
                if (mCurFileSize + iMoovSize + 1024 >  mMaxFileSize * 95 / 100) {
                    NOTIFY(kEventInfo, Component::kEventMaxFileSizeReached, 0, nilParam);
                    mEOS = true;
                    return MM_ERROR_EOS;
                }
            } else {
                MMLOGW("can not found moov_size in mp4 format");
            }
        } else if (!strcmp(mOutputFormat.c_str(), "amr")) {
            //arm header size 9
            if (mCurFileSize + 32 + 9 >  mMaxFileSize) {
                NOTIFY(kEventInfo, Component::kEventMaxFileSizeReached, 0, nilParam);
                mEOS = true;
                return MM_ERROR_EOS;
            }
        }
    }

    if (mMaxDuration > 0) {
        if (mMaxDuration <= mCurrentDts / 1000) {
            NOTIFY(kEventInfo, Component::kEventMaxDurationReached, 0, nilParam);
            mEOS = true;
            return MM_ERROR_EOS;
        }
    }

    MMLOGV("-success(media: %d)\n", si->mMediaType);
    return MM_ERROR_SUCCESS;
}

MediaBufferSP AVMuxer::createSinkBuffer(const uint8_t * buf, size_t size)
{
    MMLOGV("+\n");
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer) {
        MMLOGE("no mem\n");
        return buffer;
    }

    MediaMetaSP meta = buffer->getMediaMeta();
    meta->setPointer(META_ME, this);
    if ( mOutputSeekOffset >= 0 ) {
        MMLOGV("seek: offset: %" PRId64 ", whence: %d\n", mOutputSeekOffset, mOutputSeekWhence);
        meta->setInt64(MEDIA_ATTR_SEEK_OFFSET, mOutputSeekOffset);
        meta->setInt32(MEDIA_ATTR_SEEK_WHENCE, mOutputSeekWhence);
        mOutputSeekOffset = -1;
    }

    //buffer->setFlag(MediaBuffer::MBFT_CodecData);
#ifdef COPY_TO_SINK_BUFFER
    uint8_t * data = (uint8_t*)malloc(size);
    if ( !data ) {
        NOTIFY_ERROR(MM_ERROR_NO_MEM);
        MMLOGE("no mem, need %zu\n", size);
        return MediaBufferSP((MediaBuffer*)NULL);
    }
    memcpy(data, buf, size);
    buffer->setBufferInfo((uintptr_t*)&data, NULL, NULL, 1);
#else
    buffer->setBufferInfo((uintptr_t*)&buf, NULL, NULL, 1);
#endif
    buffer->setSize((int64_t)size);
    buffer->setPts(mCurrentDts);
    buffer->setDts(mCurrentDts);
    DEBUG("mCurrentDts %lld", mCurrentDts/1000LL);

    buffer->addReleaseBufferFunc(releaseSinkBuffer);
    MMLOGV("-\n");
    return buffer;
}

/*static */bool AVMuxer::releaseSinkBuffer(MediaBuffer* mediaBuffer)
{
    MMLOGV("+\n");
    MediaMetaSP meta = mediaBuffer->getMediaMeta();
#ifdef COPY_TO_SINK_BUFFER
    uint8_t * data;
    int32_t offsets;
    int32_t strides;
    if ( mediaBuffer->getBufferInfo((uintptr_t*)&data, &offsets, &strides, 1) ) {
        free(data);
    } else {
        MMLOGE("no data\n");
    }
#else
    void * p;
    if ( !meta->getPointer(META_ME, p) ) {
        MMLOGE("no me\n");
        return false;
    }

    AVMuxer * me = static_cast<AVMuxer*>(p);
    if ( !me ) {
        MMLOGE("invalid me\n");
        return false;
    }

    me->sinkBufferReleased();
#endif

    MMLOGV("-\n");
    return true;
}

void AVMuxer::sinkBufferReleased()
{
    MMLOGV("+\n");
#ifndef COPY_TO_SINK_BUFFER
    mSinkBufferReleased = true;
    mMuxThread->mux();
#endif
    MMLOGV("-\n");
}

/*static */int64_t AVMuxer::avSeek(void *opaque, int64_t offset, int whence)
{
    AVMuxer * me = static_cast<AVMuxer*>(opaque);
    if ( !me ) {
        MMLOGE("invalid cb\n");
        return -1;
    }

    me->avSeek(offset, whence);
    return 0;
}

int64_t AVMuxer::avSeek(int64_t offset, int whence)
{
    if ( whence == SEEK_SET && offset < 0 ) {
        MMLOGE("seek from start, but offset < 0\n");
        return -1;
    }

    if ( whence == AVSEEK_SIZE ) {
        MMLOGE("AVSEEK_SIZE not supported\n");
        return -1;
    }

    MMAutoLock lock(mBufferLock);
    if ( mOutputSeekOffset >= 0 ) {
        MMLOGW("cur seek not completed(%" PRId64 ")\n", mOutputSeekOffset);
    }

    mOutputSeekOffset = offset;
    mOutputSeekWhence = whence;
    MMLOGV("av_seek: req: offset: %" PRId64 ", whence: %d", mOutputSeekOffset, mOutputSeekWhence);
    return offset;
}

void AVMuxer::signalEOS2Sink()
{
    MMLOGV("+\n");
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer) {
        MMLOGE("no mem\n");
        return;
    }

    buffer->setFlag(MediaBuffer::MBFT_EOS);
    mWriter->write(buffer);
    MMLOGV("-\n");
}

}

extern "C" {

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("AVMuxerCreater");

Component * createComponent(const char* mimeType, bool isEncoder)
{
    FUNC_ENTER();
    return new AVMuxer();
}

void releaseComponent(Component * component)
{
    MMLOGI("%p\n", component);
    if ( component ) {
        AVMuxer * c = DYNAMIC_CAST<AVMuxer*>(component);
        MMASSERT(c != NULL);
        MM_RELEASE(c);
    }
}
}


