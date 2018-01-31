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
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <multimedia/av_ffmpeg_helper.h>

#include "external_capture_source.h"
#include <unistd.h>
#include <multimedia/av_buffer_helper.h>
#include <multimedia/media_attr_str.h>
#include <multimedia/mm_types.h>
#include "third_helper.h"

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>


namespace YUNOS_MM {

#define MSG_PREPARE (MMMsgThread::msg_type)1


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

#define SET_BUFFERING_STATE(_state) do {\
    MMLOGI("set buffering state from %d to %s\n", mBufferState, #_state);\
    mBufferState = _state;\
}while(0)

#define NOTIFY(_event, _param1, _param2, _obj) do {\
    MMLOGI("notify: %s, param1: %d, param2: %d\n", #_event, (_param1), (_param2));\
    notify(_event, _param1, _param2, _obj);\
}while(0)

#define NOTIFY_ERROR(_reason) do {\
    MMLOGV("notify: error: %d, %s\n", _reason, #_reason);\
    NOTIFY(kEventError, _reason, 0, nilParam);\
}while(0)

#define PREPARE_FAILED(_reason) do {\
        resetInternal();\
        NOTIFY(kEventPrepareResult, _reason, 0, nilParam);\
        return;\
}while(0)

#define EXIT_TMHANDLER(_exit) do { \
    MMLOGI("set timeouthandler exit state to: %d\n", _exit);\
    mInterruptHandler->exit(_exit);\
} while (0)

#define WAIT_PREPARE_OVER() do {\
    EXIT_TMHANDLER(true);\
    while ( 1 ) {\
        MMAutoLock locker(mLock);\
        if ( MM_LIKELY(mState != STATE_PREPARING)  ) {\
            break;\
        }\
        MMLOGI("preparing, waitting...\n");\
        usleep(30000);\
    }\
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

#define SETPARAM_BEGIN()\
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {\
        const MediaMeta::MetaItem & item = *i;

#define CHECK_TYPE(_val_type)\
        if ( item.mType != MediaMeta::_val_type ) {\
            WARNING("invalid type for %s, expect: %s, real: %d\n", item.mName, #_val_type, item.mType);\
            continue;\
        }

#define SETPARAM_I32(_key_name, _val)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int32);\
        _val = item.mValue.ii;\
        INFO("setparam key: %s, val: %d\n", item.mName, item.mValue.ii);\
        mMetaData->setInt32(_key_name, item.mValue.ii);\
        continue;\
    }

#define SETPARAM_I64(_key_name, _val)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int64);\
        _val = item.mValue.ld;\
        INFO("setparam key: %s, val: %" PRId64 "\n", item.mName, item.mValue.ld);\
        mMetaData->setInt32(_key_name, item.mValue.ld);\
        continue;\
    }

#define SETPARAM_END() }

#define FUNC_ENTER() MMLOGI("+\n")
#define FUNC_LEAVE() MMLOGV("-\n")

const char * MEDIABUFFER_CDS_NAME = "CSDPointer";
static const char * MMMSGTHREAD_NAME = "ExternalCaptureSource";
static const char * MMTHREAD_NAME = "ExternalCaptureSource::ReadThread";

static const int PREPARE_TIMEOUT_DEFUALT = 10000000;//us
static const int READ_TIMEOUT_DEFAULT = 10000000;

static const int WANT_TRACK_NONE = -1;
static const int WANT_TRACK_INDEX_DEFAULT = 0;

static const int64_t BUFFERING_TIME_DEFAULT = 2 * 1000 * 1000;
static const int64_t BUFFERING_TIME_LOCAL = 300 * 1000;


static const AVRational gTimeBase = {1,1000000};

static const size_t AVIO_BUFFER_SIZE = 128*1024;

static const float PRECISION_DIFF = 0.000001f;

// fixed point to double
#define CONV_FP(x) ((double) (x)) / (1 << 16)

// double to fixed point
#define CONV_DB(x) (int32_t) ((x) * (1 << 16))

static double av_display_rotation_get(const int32_t matrix[9])
{
    double rotation, scale[2];

    scale[0] = hypot(CONV_FP(matrix[0]), CONV_FP(matrix[3]));
    scale[1] = hypot(CONV_FP(matrix[1]), CONV_FP(matrix[4]));

    if (scale[0] == 0.0 || scale[1] == 0.0)
        return NAN;

    rotation = atan2(CONV_FP(matrix[1]) / scale[1],
                     CONV_FP(matrix[0]) / scale[0]) * 180 / M_PI;

    return rotation;
}


DEFINE_LOGTAG(ExternalCaptureSource)
DEFINE_LOGTAG(ExternalCaptureSource::InterruptHandler)
DEFINE_LOGTAG(ExternalCaptureSource::ReadThread)


ExternalCaptureSource::InterruptHandler::InterruptHandler()
                          : mIsTimeout(false),
                          mExit(false),
                          mActive(false)
{
    FUNC_ENTER();
    callback = handleTimeout;
    opaque = this;
    FUNC_LEAVE();
}

ExternalCaptureSource::InterruptHandler::~InterruptHandler()
{
    FUNC_ENTER();
    FUNC_LEAVE();
}

void ExternalCaptureSource::InterruptHandler::start(int64_t timeout)
{
    MMLOGV("+\n");
    mActive = true;
    mExit = false;
    mIsTimeout = false;
    mTimer.start(timeout);
    FUNC_LEAVE();
}

void ExternalCaptureSource::InterruptHandler::end()
{
    MMLOGV("+\n");
    mIsTimeout = mTimer.isExpired();
    mActive = false;
    FUNC_LEAVE();
}

void ExternalCaptureSource::InterruptHandler::exit(bool isExit)
{
    mExit = isExit;
}

bool ExternalCaptureSource::InterruptHandler::isExiting() const
{
    return mExit;
}

bool ExternalCaptureSource::InterruptHandler::isTimeout() const
{
    return mIsTimeout;
}

/*static */int ExternalCaptureSource::InterruptHandler::handleTimeout(void* obj)
{
    InterruptHandler* handler = static_cast<InterruptHandler*>(obj);
    if (!handler) {
        MMLOGE("InterruptHandler is null\n");
        return 1;
    }

    if (!handler->mActive) {
        MMLOGE("InterruptHandler not started\n");
        return 0;
    }

    if ( handler->isExiting() ) {
        MMLOGI("user exit\n");
        return 1;
    }

    if ( handler->mTimer.isExpired() ) {
        MMLOGE("timeout\n");
        return 1;
    }

//    MMLOGV("not timeout\n");
    return  0;
}


ExternalCaptureSource::StreamInfo::StreamInfo() : mMediaType(kMediaTypeUnknown),
                        mHasAttachedPic(false)
{
    FUNC_ENTER();
    memset(&mTimeBase, 0, sizeof(AVRational));
    FUNC_LEAVE();
}

ExternalCaptureSource::StreamInfo::~StreamInfo()
{
    FUNC_ENTER();
    reset();
    FUNC_LEAVE();
}

bool ExternalCaptureSource::StreamInfo::init()
{
    FUNC_ENTER();
    mMetaData = MediaMeta::create();
    return mMetaData;
}

void ExternalCaptureSource::StreamInfo::addSink(Component * component)
{
    MMLOGV("+\n");
    if (!component) {
        MMLOGE("invalid param");
        return;
    }
    WriterSP w = component->getWriter(mMediaType);
    if (!w) {
        MMLOGE("failed to get writer\n");
        return;
    }
    mWriter = w;
    mWriter->setMetaData(mMetaData);
}

void ExternalCaptureSource::StreamInfo::reset()
{
    FUNC_ENTER();
    mMediaType = kMediaTypeUnknown;
    mAllStreams.clear();
    mHasAttachedPic = false;
    memset(&mTimeBase, 0, sizeof(AVRational));
    mMetaData->clear();
    FUNC_LEAVE();
}

bool ExternalCaptureSource::StreamInfo::write(MediaBufferSP buffer)
{
    if (!mWriter) {
        MMLOGE("no sink installed\n");
        return false;
    }

    mm_status_t ret = mWriter->write(buffer);
    if (ret != MM_ERROR_SUCCESS) {
        MMLOGE("failed to write: %d\n", ret);
        return false;
    }

    return true;
}

ExternalCaptureSource::ReadThread::ReadThread(ExternalCaptureSource * watcher)
    : MMThread(MMTHREAD_NAME),
    mWatcher(watcher),
    mContinue(false),
    mCorded(true),
    mSignalEOSReq(false),
    mSignalEOSDone(false)
{
    FUNC_ENTER();
    sem_init(&mSem, 0, 0);
    FUNC_LEAVE();
}

ExternalCaptureSource::ReadThread::~ReadThread()
{
    FUNC_ENTER();
    sem_destroy(&mSem);
    FUNC_LEAVE();
}

mm_status_t ExternalCaptureSource::ReadThread::prepare()
{
    FUNC_ENTER();
    mCorded = true;
    mContinue = true;
    if ( create() ) {
        MMLOGE("failed to create thread\n");
        return MM_ERROR_NO_MEM;
    }

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::reset()
{
    FUNC_ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::start()
{
    FUNC_ENTER();
    mSignalEOSReq = mSignalEOSDone = false;
    mCorded = false;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::pause()
{
    FUNC_ENTER();
    mCorded = true;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::resume()
{
    FUNC_ENTER();
    mCorded = false;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::stop()
{
    FUNC_ENTER();
    mCorded = true;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::read()
{
    MMLOGV("+\n");
    sem_post(&mSem);
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::ReadThread::signalEOS()
{
    MMLOGV("+\n");
    mSignalEOSReq = true;
    mCorded = 0;
    return read();
}

void ExternalCaptureSource::ReadThread::main()
{
    MMLOGI("started\n");
    while ( MM_LIKELY(mContinue) ) {
        MMLOGI("waitting sem\n");
        sem_wait(&mSem);
        if ( MM_UNLIKELY(!mContinue) ) {
            MMLOGI("not conitnue\n");
            break;
        }
        if ( MM_UNLIKELY(mSignalEOSReq) ) {
            MMLOGI("SignalEOS\n");
            if (!mSignalEOSDone) {
                mWatcher->doSignalEOS();
                mSignalEOSDone = true;
            }
            continue;
        }

        mm_status_t ret = -1;
        MMLOGD("resume reading to get more data");
        while ( MM_LIKELY(mContinue) ) {
            if ( MM_UNLIKELY(mCorded) ) {
                MMLOGI("mCorded\n");
                break;
            }

            if ( MM_UNLIKELY(mSignalEOSReq) ) {
                MMLOGI("SignalEOS\n");
                if (!mSignalEOSDone) {
                    mWatcher->doSignalEOS();
                    mSignalEOSDone = true;
                }
                break;
            }

            MMLOGV("reading...\n");
            ret = mWatcher->readFrame();
            MMLOGV("reading...ret: %d\n", ret);
            if ( ret == MM_ERROR_SUCCESS ) {
                usleep(0);
                continue;
            }

            if ( ret == MM_ERROR_TIMED_OUT ||
                  ret == MM_ERROR_IO ) {
                MMLOGE("read timeout or I/O error\n");
                usleep(100000);
                continue;
            }

            // other:
            break;
        }
        MMLOGD("pause reading with ret(%d)", ret);
    }

    MMLOGI("exited\n");
}


ExternalCaptureSource::ExternalCaptureSource() :
                MMMsgThread(MMMSGTHREAD_NAME),
                mState(STATE_NONE),
                mAVFormatContext(NULL),
                mAVInputFormat(NULL),
                mAVIOContext(NULL),
                mPrepareTimeout(PREPARE_TIMEOUT_DEFUALT),
                mReadTimeout(READ_TIMEOUT_DEFAULT),
                mInterruptHandler(NULL),
                mReadThread(NULL)
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

    FUNC_LEAVE();
}

ExternalCaptureSource::~ExternalCaptureSource()
{
    FUNC_ENTER();

    FUNC_LEAVE();
}

mm_status_t ExternalCaptureSource::init()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_IDLE, MM_ERROR_SUCCESS);
    CHECK_STATE(STATE_NONE);

    if ( !(mMetaData = MediaMeta::create()) ) {
        MMLOGE("no mem\n");
        SET_STATE(STATE_ERROR);
        return MM_ERROR_NO_MEM;
    }

    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        if ( !mStreamInfoArray[i].init() ) {
            MMLOGE("failed to init si %d\n", i);
            SET_STATE(STATE_ERROR);
            return MM_ERROR_NO_MEM;
        }
    }

    mInterruptHandler = new InterruptHandler();
    if ( !mInterruptHandler ) {
        MMLOGE("failed to alloc InterruptHandler\n");
        SET_STATE(STATE_ERROR);
        return MM_ERROR_NO_MEM;
    }

    int ret = run();
    if ( ret ) {
        MMLOGE("failed to run, ret: %d\n", ret);
        MM_RELEASE(mInterruptHandler);
        SET_STATE(STATE_ERROR);
        return MM_ERROR_NO_MEM;
    }

    mReadThread = new ReadThread(this);
    if ( !mReadThread ) {
        MMLOGE("no mem\n");
        MM_RELEASE(mInterruptHandler);
        exit();
        SET_STATE(STATE_ERROR);
        return MM_ERROR_NO_MEM;
    }

    SET_STATE(STATE_IDLE);
    MMLOGV("success\n");
    return MM_ERROR_SUCCESS;
}

void ExternalCaptureSource::uninit()
{
    FUNC_ENTER();
    reset();
    exit();
    MMLOGV("thread exited\n");
    MM_RELEASE(mReadThread);
    MMLOGV("freeing InterruptHandler\n");
    MM_RELEASE(mInterruptHandler);
    FUNC_LEAVE();
}

bool ExternalCaptureSource::hasMedia(MediaType mediaType)
{
    MMAutoLock lock(mLock);
    return hasMediaInternal(mediaType);
}

bool ExternalCaptureSource::hasMediaInternal(MediaType mediaType)
{
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return false;
    }

    MMLOGV("mediatype: %d\n", mStreamInfoArray[mediaType].mMediaType);
    return mStreamInfoArray[mediaType].mMediaType != kMediaTypeUnknown;
}

MediaMetaSP ExternalCaptureSource::getMetaData()
{
    return mMetaData;
}

MMParamSP ExternalCaptureSource::getTrackInfo()
{
    MMAutoLock lock(mLock);
    if ( mState < STATE_PREPARED || mState > STATE_STARTED ) {
        MMLOGE("invalid state(%d)\n", mState);
        return nilParam;
    }

    int streamCount = 0;
    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        if ( mStreamInfoArray[i].mMediaType != kMediaTypeUnknown ) {
            ++streamCount;
        }
    }

    if ( streamCount == 0 ) {
        MMLOGI("no stream found\n");
        return nilParam;
    }

    MMParamSP param(new MMParam());
    if ( !param ) {
        MMLOGE("no mem\n");
        return nilParam;
    }

    // stream count
    param->writeInt32(streamCount);
    MMLOGV("stream count: %d\n", streamCount);

    // every stream
    const StreamInfo * si = mStreamInfoArray;
    for ( int i = 0; i < kMediaTypeCount; ++i, ++si ) {
        if ( si->mMediaType == kMediaTypeUnknown ) {
            MMLOGV("%d: type: %d not supported, ignore\n", i, si->mMediaType);
            continue;
        }

        // track type
        MMLOGV("track type: %d\n", i);
        param->writeInt32(i);

        // track count
        MMLOGV("track count: %d\n", si->mAllStreams.size());
        param->writeInt32(si->mAllStreams.size());
        std::vector<int>::const_iterator it;
        int idx;
        for ( it = si->mAllStreams.begin(), idx = 0; it != si->mAllStreams.end(); ++it, ++idx ) {
            MMLOGV("track id: %d\n", idx);
            param->writeInt32(idx); // id
            AVStream * stream = mAVFormatContext->streams[*it];
            // codec
            CowCodecID codecId;
            const char * codecName;
            int32_t width = 0, height = 0;
            AVCodecParameters * codecParams = stream->codecpar;
            if (codecParams && codecParams->codec_id) {
                MMLOGV("has param: %p\n", codecParams);
                width = codecParams->width;
                height = codecParams->height;
                codecId = AVCodecId2CodecId(codecParams->codec_id);
                codecName = avcodec_descriptor_get(codecParams->codec_id)->name;
                MMLOGV("codecId: %d, codecName: %s\n", codecId, codecName);
            } else {
                MMLOGI("unknown codec\n");
                codecId = kCodecIDNONE;
                codecName = "";
            }
            param->writeInt32(codecId);
            param->writeCString(codecName);
            const char* name = codecId2Mime((CowCodecID)codecId);
            if (name)
                param->writeCString(name);
            else {
                MMLOGW("cannot find codec name for id %d", codecId);
                param->writeCString("");
            }
            MMLOGV("getting title\n");
            // title
            AVDictionaryEntry * tagTitle = av_dict_get(stream->metadata, "title", NULL, 0);
#define WRITE_TAG(_tag) do {\
        if (_tag)\
            param->writeCString(_tag->value);\
        else\
            param->writeCString("");\
    }while(0)
            WRITE_TAG(tagTitle);

            // lang
            MMLOGV("getting lang\n");
            AVDictionaryEntry * tagLang = av_dict_get(stream->metadata, "language", NULL, 0);
            if (!tagLang)
                tagLang = av_dict_get(stream->metadata, "lang", NULL, 0);
            WRITE_TAG(tagLang);

            MMLOGD("id: %d(%d), title: %s, lang: %s, codecId: %d, codecName: %s\n",
                idx, *it, tagTitle ? tagTitle->value : "", tagLang ? tagLang->value : "", codecId, codecName);
            if (i == kMediaTypeVideo) {
                param->writeInt32(width);
                param->writeInt32(height);
                MMLOGD("resolution: %dx%d\n", width, height);
            }
        }
    }

    return param;
}

mm_status_t ExternalCaptureSource::selectTrack(MediaType mediaType, int index)
{
    MMLOGI("mediaType: %d, index: %d\n", mediaType, index);
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return MM_ERROR_INVALID_PARAM;
    }

    MMAutoLock lock(mLock);
    return selectTrackInternal(mediaType, index);
}

mm_status_t ExternalCaptureSource::selectTrackInternal(MediaType mediaType, int index)
{
    MMLOGV("mediaType: %d, index: %d\n", mediaType, index);
    if ( index < 0 ) {
        MMLOGV("<0, set to default: %d\n", WANT_TRACK_INDEX_DEFAULT);
        index = WANT_TRACK_INDEX_DEFAULT;
    }

    MMASSERT(mediaType < kMediaTypeCount && mediaType > kMediaTypeUnknown);
    StreamInfo * si = &mStreamInfoArray[mediaType];
    if ( si->mMediaType == kMediaTypeUnknown ) {
        MMLOGE("no such media(%d)\n", mediaType);
        return MM_ERROR_INVALID_PARAM;
    }

    if ( (size_t)index >= si->mAllStreams.size() ) {
        MMLOGE("no such index(%d)\n", index);
        return MM_ERROR_INVALID_PARAM;
    }

    int id = si->mAllStreams[index];
    AVStream * stream = mAVFormatContext->streams[id];
    stream->discard = AVDISCARD_DEFAULT;
    memcpy(&si->mTimeBase, &stream->time_base, sizeof(AVRational));
    si->mHasAttachedPic = stream->disposition & AV_DISPOSITION_ATTACHED_PIC ? true : false;
    MMLOGI("mediaType: %d, index: %d, id: %d, info: %p, HasAttachedPic: %d, num: %d, den: %d\n", mediaType, index, id, si, si->mHasAttachedPic, si->mTimeBase.num, si->mTimeBase.den);

    AVCodecParameters * codecParams = mAVFormatContext->streams[id]->codecpar;
    MMLOGI("codecParams: codec_type: %d, codec_id: 0x%x, codec_tag: 0x%x, stream_codec_tag: 0x%x, profile: %d, width: %d, height: %d, extradata: %p, extradata_size: %d, channels: %d, sample_rate: %d, channel_layout: %" PRId64 ", bit_rate: %d, block_align: %d, avg_frame_rate: (%d, %d)\n",
                    codecParams->codec_type,
                    codecParams->codec_id,
                    codecParams->codec_tag,
                    codecParams->codec_tag,
                    codecParams->profile,
                    codecParams->width,
                    codecParams->height,
                    codecParams->extradata,
                    codecParams->extradata_size,
                    codecParams->channels,
                    codecParams->sample_rate,
                    codecParams->channel_layout,
                    codecParams->bit_rate,
                    codecParams->block_align,
                    mAVFormatContext->streams[id]->avg_frame_rate.num,
                    mAVFormatContext->streams[id]->avg_frame_rate.den);

    if ( mediaType == kMediaTypeVideo ) {
        if ( codecParams->extradata && codecParams->extradata_size > 0 ) {
            si->mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecParams->extradata, codecParams->extradata_size);
        }
        AVRational * avr = &mAVFormatContext->streams[id]->avg_frame_rate;
        if ( avr->num > 0 && avr->den > 0 ) {
            MMLOGD("has avg fps: %d\n", avr->num / avr->den);
            si->mMetaData->setInt32(MEDIA_ATTR_AVG_FRAMERATE, avr->num / avr->den);
        }
        si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecParams->codec_id);
        si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecParams->codec_id));
        si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecParams->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecParams->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecParams->profile);
        si->mMetaData->setInt32(MEDIA_ATTR_WIDTH, codecParams->width);
        si->mMetaData->setInt32(MEDIA_ATTR_HEIGHT, codecParams->height);

        if (stream->nb_side_data) {
            for (int i = 0; i < stream->nb_side_data; i++) {
                AVPacketSideData sd = stream->side_data[i];
                if (sd.type == AV_PKT_DATA_DISPLAYMATRIX) {
                    double degree = av_display_rotation_get((int32_t *)sd.data);
                    MMLOGD("degree %0.2f\n", degree);

                    int32_t degreeInt = (int32_t)degree;
                    if (degreeInt < 0)
                        degreeInt += 360;
                    si->mMetaData->setInt32(MEDIA_ATTR_ROTATION, (int32_t)degreeInt);
                    NOTIFY(kEventVideoRotationDegree, int(degreeInt), 0, nilParam);
                }
                else {
                    MMLOGD("ignore type %d\n", sd.type);
                    continue;
                }
            }
        }

    } else if ( mediaType == kMediaTypeAudio ) {
        mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, codecParams->bit_rate);
        if ( codecParams->extradata && codecParams->extradata_size > 0 ) {
            si->mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecParams->extradata, codecParams->extradata_size);
        }
        si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecParams->codec_id);
        si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecParams->codec_id));
        si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecParams->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecParams->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecParams->profile);
        si->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, codecParams->format);
        si->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, codecParams->sample_rate);
        si->mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, codecParams->channels);
        si->mMetaData->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, codecParams->channel_layout);
        si->mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, codecParams->bit_rate);
        si->mMetaData->setInt32(MEDIA_ATTR_BLOCK_ALIGN, codecParams->block_align);


        if (mAVInputFormat == av_find_input_format("aac") ||
           mAVInputFormat == av_find_input_format("mpegts") ||
           mAVInputFormat == av_find_input_format("hls,applehttp")) {
           //Set kKeyIsADTS to ture as default as mpegts2Extractor/aacExtractor did
           si->mMetaData->setInt32(MEDIA_ATTR_IS_ADTS, 1);
        }
    }

    si->mMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecParams->profile);
    si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecParams->codec_tag);
    si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecParams->codec_id));
    si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecParams->codec_id);

    NOTIFY(kEventInfo, kEventMetaDataUpdate, 0, nilParam);

    return MM_ERROR_SUCCESS;
}

int ExternalCaptureSource::getSelectedTrack(MediaType mediaType)
{
    MMLOGV("mediaType: %d\n", mediaType);
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return -1;
    }

    MMAutoLock lock(mLock);
    // ?? mSelectedStream is packet stream_index, not track index
    return mStreamInfoArray[mediaType].mSelectedStream;
}

BEGIN_MSG_LOOP(ExternalCaptureSource)
    MSG_ITEM(MSG_PREPARE, onPrepare)
END_MSG_LOOP()


void ExternalCaptureSource::resetInternal()
{
    EXIT_TMHANDLER(true);
    mReadThread->reset();
    releaseContext();

    mUri = "";

    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        mStreamInfoArray[i].reset();
    }
    mStreamIdx2Info.clear();
    mMetaData->clear();

    SET_STATE(STATE_IDLE);
}

mm_status_t ExternalCaptureSource::setParameter(const MediaMetaSP & meta)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    SETPARAM_BEGIN()
        SETPARAM_I32(PARAM_KEY_PREPARE_TIMEOUT, mPrepareTimeout)
        SETPARAM_I32(PARAM_KEY_READ_TIMEOUT, mReadTimeout)
    SETPARAM_END()

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::getParameter(MediaMetaSP & meta) const
{
    MMAutoLock lock(mLock);
    meta.reset();
    meta = mMetaData;

    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::addSink(Component * component, MediaType mediaType)
{
    MMLOGI("component: %p, mediaType: %d\n", component, mediaType);
    MMAutoLock lock(mLock);
    if (!component || mediaType <= kMediaTypeUnknown || mediaType >= kMediaTypeCount) {
        MMLOGE("invalid params: component: %p, mediaType: %d\n", component, mediaType);
        return MM_ERROR_INVALID_PARAM;
    }

    selectTrackInternal(mediaType, -1);

    mStreamInfoArray[mediaType].addSink(component);
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL)
{
    MMLOGI("uri: %s\n", uri);
    MMAutoLock lock(mLock);
    CHECK_STATE(STATE_IDLE);
    mUri = uri;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::prepare()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_PREPARING, MM_ERROR_ASYNC);
    RET_IF_ALREADY_STATE(STATE_PREPARED, MM_ERROR_SUCCESS);
    CHECK_STATE(STATE_IDLE);

    if (mUri.empty()) {
        MMLOGE("uri not set\n");
        return MM_ERROR_INVALID_URI;
    }

    SET_STATE(STATE_PREPARING);
    if ( postMsg(MSG_PREPARE, 0, 0, 0) ) {
        MMLOGE("failed to post\n");
        SET_STATE(STATE_IDLE);
        return MM_ERROR_NO_MEM;
    }

    FUNC_LEAVE();
    return MM_ERROR_ASYNC;
}

void ExternalCaptureSource::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    MMASSERT(rspId == 0);
    MMAutoLock lock(mLock);
    MMASSERT(mState == STATE_PREPARING);

    EXIT_TMHANDLER(false);
    mm_status_t ret = createContext();
    if ( ret !=  MM_ERROR_SUCCESS) {
        MMLOGE("failed to createcontext: %d\n", ret);
        PREPARE_FAILED(MM_ERROR_NOT_SUPPORTED_FILE);
    }

    mAVInputFormat = mAVFormatContext->iformat;
    MMLOGV("name: %s, long_name: %s, flags: %d, mime: %s\n",
        mAVInputFormat->name, mAVInputFormat->long_name, mAVInputFormat->flags, mAVInputFormat->mime_type);

    bool hasAudio = false;
    bool hasVideo = false;
    int32_t audioCodecId = 0;
    for (unsigned int i = 0; i < mAVFormatContext->nb_streams; ++i) {
        AVStream * s = mAVFormatContext->streams[i];
        AVMediaType type = s->codecpar->codec_type;
        MMLOGV("steramid: %d, codec_type: %d, start_time: %" PRId64 ", duration: %" PRId64 ", nb_frames: %" PRId64 "\n",
            i,
            s->codecpar->codec_type,
            s->start_time == (int64_t)AV_NOPTS_VALUE ? -1 : s->start_time,
            s->duration,
            s->nb_frames);
        StreamInfo * si;
        switch ( type ) {
            case AVMEDIA_TYPE_VIDEO:
                if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                    MMLOGI("ignore the video\n");
                    AVPacket * pkt = &s->attached_pic;
                    mMetaData->setString(MEDIA_ATTR_HAS_COVER, MEDIA_ATTR_YES);
                    mMetaData->setPointer(MEDIA_ATTR_ATTACHEDPIC, pkt->data);
                    mMetaData->setInt32(MEDIA_ATTR_ATTACHEDPIC_SIZE, (int32_t)pkt->size);
                    mMetaData->setInt32(MEDIA_ATTR_ATTACHEDPIC_CODECID, (int32_t)s->codecpar->codec_id);
                    mMetaData->setString(MEDIA_ATTR_ATTACHEDPIC_MIME, codecId2Mime((CowCodecID)s->codecpar->codec_id));
                    continue;
                }
                si = &mStreamInfoArray[kMediaTypeVideo];
                si->mMediaType = kMediaTypeVideo;
                hasVideo = true;
                MMLOGI("found video stream(%d)\n", i);
                break;
            case AVMEDIA_TYPE_AUDIO:
                si = &mStreamInfoArray[kMediaTypeAudio];
                si->mMediaType = kMediaTypeAudio;
                hasAudio = true;
                audioCodecId = s->codecpar->codec_id;
                MMLOGI("found audio stream(%d)\n", i);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                si = &mStreamInfoArray[kMediaTypeSubtitle];
                si->mMediaType = kMediaTypeSubtitle;
                MMLOGI("found subtitle stream(%d)\n", i);
                break;
            default:
                MMLOGI("not supported mediatype: %d\n", type);
                continue;
        }

        si->mAllStreams.push_back(i);
        mStreamIdx2Info.insert(std::pair<int, StreamInfo*>(i, si));
    }

    if ( !hasAudio && !hasVideo ) {
        MMLOGE("no any streams\n");
        PREPARE_FAILED(MM_ERROR_NOT_SUPPORTED_FILE);
    }

    ret = mReadThread->prepare();
    if ( ret != MM_ERROR_SUCCESS ) {
        PREPARE_FAILED(MM_ERROR_NO_MEM);
    }

    AVDictionaryEntry *tag = NULL;
    if (NULL != mAVFormatContext->metadata) {
        while ((tag = av_dict_get(mAVFormatContext->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            MMLOGI("got meta: %s : %s\n", tag->key, tag->value);
            if ( !strcmp(tag->key, "album_artist") ) {
                mMetaData->setString(MEDIA_ATTR_ALBUM_ARTIST, tag->value);
            } else {
                mMetaData->setString(tag->key, tag->value);
            }
        }
    } else {
        MMLOGW("metadata in avformat is NULL\n");
    }

    for (uint32_t i = 0; i < mAVFormatContext->nb_streams; i++)
        mAVFormatContext->streams[i]->discard = AVDISCARD_ALL;

    if ( !strcmp(mAVInputFormat->long_name, "QuickTime / MOV") ) {
        const char * major_brand = NULL;
        if ( hasAudio && !hasVideo ) {
            if ( mMetaData->getString(MEDIA_ATTR_FILE_MAJOR_BAND, major_brand) ) {
                if ( strstr(major_brand, "3gp") ) {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_AUDIO_3GPP);
                } else if ( strstr(major_brand, "qt") ) {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_AUDIO_QUICKTIME);
                } else {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_AUDIO_MP4);
                }
            } else {
                mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_AUDIO_MP4);
            }
        } else {
            if ( mMetaData->getString(MEDIA_ATTR_FILE_MAJOR_BAND, major_brand) ) {
                if ( strstr(major_brand, "3gp") ) {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_VIDEO_3GPP);
                } else if ( strstr(major_brand, "qt") ) {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_VIDEO_QUICKTIME);
                } else {
                    mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_VIDEO_MP4);
                }
            } else {
                mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_ATTR_MIME_VIDEO_MP4);
            }
        }
    } else if (!strcmp(mAVInputFormat->name, "ogg")) {
        MMLOGV("ogg container type\n");
        mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_CONTAINER_OGG);
    } else if (!strcmp(mAVInputFormat->name, "amr")) {
        if (audioCodecId == AV_CODEC_ID_AMR_NB) {
            MMLOGV("amr container type\n");
            mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_AUDIO_AMR_NB);
        } else {
            MMLOGV("amr wb container type\n");
            mMetaData->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_AUDIO_AMR_WB);
        }
    } else {
        int idx = hasVideo ? kMediaTypeVideo : kMediaTypeAudio;
        const char * mime = NULL;
        // FIXME, redo it after user selectTrack
        if ( mStreamInfoArray[idx].mMetaData->getString(MEDIA_ATTR_MIME, mime) ) {
            mMetaData->setString(MEDIA_ATTR_MIME, mime);
        } else {
            mMetaData->setString(MEDIA_ATTR_MIME, "unknown");
        }
    }

    SET_STATE(STATE_PREPARED);
    NOTIFY(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    FUNC_LEAVE();
}

mm_status_t ExternalCaptureSource::createContext()
{
    FUNC_ENTER();
    MMASSERT(mAVFormatContext == NULL);
    MMASSERT(mAVIOContext == NULL);

    if (mUri.empty()) {
        MMLOGE("no uri provided\n");
        return MM_ERROR_INVALID_STATE;
    }

    mAVFormatContext = avformat_alloc_context();
    if ( !mAVFormatContext ) {
        MMLOGE("failed to create avcontext\n");
        return MM_ERROR_NO_MEM;
    }

    mAVFormatContext->interrupt_callback = *mInterruptHandler;

    mInterruptHandler->start(mPrepareTimeout);

    AVDictionary *options = NULL;

    mLock.release();
    int ret = avformat_open_input(&mAVFormatContext, mUri.c_str(), NULL, &options);
    mLock.acquire();
    if ( ret < 0 ) {
        MMLOGE("failed to open uri: %s, err: %d(%s)\n", mUri.c_str(), ret, strerror(ret));
        mInterruptHandler->end();
        releaseContext();
        return ret;
    }


    mAVFormatContext->flags |= AVFMT_FLAG_GENPTS;
    if (mUri.empty()) {
        mAVFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
    }

    MMLOGV("finding stream info\n");
    ret = avformat_find_stream_info(mAVFormatContext, NULL);
    mInterruptHandler->end();
    if ( ret < 0 ) {
        MMLOGE("failed to find stream info: %d\n", ret);
        releaseContext();
        return ret;
    }

    return MM_ERROR_SUCCESS;
}

void ExternalCaptureSource::releaseContext()
{
    FUNC_ENTER();
    if (mUri.empty()) {
        if ( mAVFormatContext ) {
            mAVFormatContext->interrupt_callback = {.callback = NULL, .opaque = NULL};
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
    } else {
        if ( mAVFormatContext ) {
            mAVFormatContext->interrupt_callback = {.callback = NULL, .opaque = NULL};
            avformat_close_input(&mAVFormatContext);
            mAVFormatContext = NULL;
        }
    }
}


mm_status_t ExternalCaptureSource::start()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_STARTED, MM_ERROR_SUCCESS);
    EXIT_TMHANDLER(false);
    mReadThread->start();
    SET_STATE(STATE_STARTED);
    mReadThread->read();
    MMLOGI("success\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::stop()
{
    FUNC_ENTER();
    WAIT_PREPARE_OVER();

    MMAutoLock lock(mLock);
    stopInternal();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void ExternalCaptureSource::stopInternal()
{
    MMLOGI("+\n");
    EXIT_TMHANDLER(true);
    mReadThread->stop();
}

mm_status_t ExternalCaptureSource::pause()
{
    MMLOGV("ignore\n");
    MMAutoLock lock(mLock);
    EXIT_TMHANDLER(true);
    mReadThread->pause();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::resume()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    mReadThread->resume();
    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::reset()
{
    FUNC_ENTER();
    WAIT_PREPARE_OVER();

    MMAutoLock lock(mLock);
    switch ( mState ) {
        case STATE_PREPARING:
            break;
        case STATE_STARTED:
        case STATE_PAUSED:
            stopInternal();
        case STATE_PREPARED:
            MMLOGV("reseting\n");
            resetInternal();
            break;
        case STATE_NONE:
        case STATE_ERROR:
        case STATE_IDLE:
        default:
            MMLOGI("state now is %d\n", mState);
            return MM_ERROR_SUCCESS;
    }

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void ExternalCaptureSource::doSignalEOS()
{
    for (int i = 0; i < kMediaTypeCount; ++i) {
        StreamInfo *s = &mStreamInfoArray[i];
        if (s->peerInstalled()) {
            MediaBufferSP buf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
            if ( !buf ) {
                MMLOGE("failed to createMediaBuffer\n");
                NOTIFY_ERROR(MM_ERROR_NO_MEM);
                return;
            }
            MMLOGI("write eos to media: %d\n", i);
            buf->setFlag(MediaBuffer::MBFT_EOS);
            buf->setSize(0);
            s->write(buf);
        }
    }
}


#define FREE_AVPACKET(_pkt) do {\
    if ( _pkt ) {\
        av_free_packet(_pkt);\
        free(_pkt);\
        (_pkt) = NULL;\
    }\
}while(0)

mm_status_t ExternalCaptureSource::readFrame()
{
    MMLOGV("+\n");

    AVPacket * packet = NULL;
    do {
        packet = (AVPacket*)malloc(sizeof(AVPacket));
        if ( !packet ) {
            MMLOGE("no mem\n");
            NOTIFY_ERROR(MM_ERROR_NO_MEM);
            return MM_ERROR_NO_MEM;
        }
        av_init_packet(packet);

        mInterruptHandler->start(READ_TIMEOUT_DEFAULT);
        int ret = av_read_frame(mAVFormatContext, packet); //0: ok, <0: error/end
        mInterruptHandler->end();

        if ( ret < 0 ) {
            free(packet);
            packet = NULL;

            char errorBuf[256] = {0};
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            MMLOGW("read_frame failed: %d(%s)\n", ret, errorBuf);
            if ( ret == AVERROR_EOF ) {
                MMLOGI("eof\n");
                for ( int i = 0; i < kMediaTypeCount; ++i ) {
                    StreamInfo * s = &mStreamInfoArray[i];
                    if ( s->mMediaType == kMediaTypeUnknown )
                        continue;

                    MediaBufferSP buf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
                    if ( !buf ) {
                        MMLOGE("failed to createMediaBuffer\n");
                        NOTIFY_ERROR(MM_ERROR_NO_MEM);
                        return MM_ERROR_NO_MEM;
                    }
                    MMLOGI("write eos to media: %d\n", i);
                    buf->setFlag(MediaBuffer::MBFT_EOS);
                    buf->setSize(0);
                    s->write(buf);
                }

                return MM_ERROR_NO_MORE;
            }

            if ( mInterruptHandler->isTimeout() ) {
                MMLOGE("read timeout\n");
                NOTIFY_ERROR(MM_ERROR_TIMED_OUT);
                return MM_ERROR_TIMED_OUT;
            }
            if ( mInterruptHandler->isExiting() ) {
                MMLOGE("user exit\n");
                return MM_ERROR_SUCCESS;
            }

            NOTIFY_ERROR(MM_ERROR_IO);
            return MM_ERROR_IO;
        }

        std::map<int, StreamInfo*>::iterator it = mStreamIdx2Info.find(packet->stream_index);
        if ( it == mStreamIdx2Info.end() ) {
            WARNING("stream index %d not found, try next\n", packet->stream_index);
            FREE_AVPACKET(packet);
            usleep(0);
            continue;
        }

        StreamInfo * si = it->second;
        if (!si->peerInstalled()) {
            MMLOGV("stream %d peer not installed, ignore\n", si->mMediaType);
            FREE_AVPACKET(packet);
            usleep(0);
            continue;
        }


        MediaBufferSP buf = AVBufferHelper::createMediaBuffer(packet, true);
        if ( !buf ) {
            MMLOGE("failed to createMediaBuffer\n");
            FREE_AVPACKET(packet);
            NOTIFY_ERROR(MM_ERROR_NO_MEM);
            return MM_ERROR_NO_MEM;
        }

        MMLOGD("packet: media: %s, pts: %0.3f , dts: %0.3f, data: %p, size: %d, stream_index: %d, flags: %d, duration: %" PRId64 ", pos: %" PRId64 ", num: %d, den: %d\n",
            si->mMediaType == kMediaTypeVideo ? "Video" : "Audio",
            packet->pts/1000000.0f,
            packet->dts/1000000.0f,
            packet->data,
            packet->size,
            packet->stream_index,
            packet->flags,
            packet->duration,
            packet->pos,
            mAVFormatContext->streams[packet->stream_index]->time_base.num,
            mAVFormatContext->streams[packet->stream_index]->time_base.den
            );
        if (si->mMediaType == kMediaTypeVideo && packet->data && packet->data[0] != 0) {
            MMLOGW("video buffer size error");
        }

        if (!si->write(buf)) {
            MMLOGE("failed to write\n");
        }
    } while(0);

    return MM_ERROR_SUCCESS;
}

mm_status_t ExternalCaptureSource::signalEOS() {
    MMLOGV("+\n");
    mReadThread->signalEOS();
    return MM_ERROR_SUCCESS;
}


/*static */snd_format_t ExternalCaptureSource::convertAudioFormat(AVSampleFormat avFormat)
{
#undef item
#define item(_av, _audio) \
    case _av:\
        MMLOGI("%s -> %s\n", #_av, #_audio);\
        return _audio

    switch ( avFormat ) {
        item(AV_SAMPLE_FMT_U8, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLT, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBL, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_U8P, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16P, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32P, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLTP, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBLP, SND_FORMAT_PCM_32_BIT);
        default:
            MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", avFormat);
            return SND_FORMAT_INVALID;
    }
}

CowCodecID ExternalCaptureSource::AVCodecId2CodecId(AVCodecID id)
{
#undef item
#define item(_avid, _cowid) \
        case _avid:\
            MMLOGI("%s -> %s\n", #_avid, #_cowid);\
            return _cowid

        switch ( id ) {
            item(AV_CODEC_ID_NONE, kCodecIDNONE);
            item(AV_CODEC_ID_MPEG1VIDEO, kCodecIDMPEG1VIDEO);
            item(AV_CODEC_ID_MPEG2VIDEO, kCodecIDMPEG2VIDEO);
            item(AV_CODEC_ID_MPEG2VIDEO_XVMC, kCodecIDMPEG2VIDEO_XVMC);
            item(AV_CODEC_ID_H261, kCodecIDH261);
            item(AV_CODEC_ID_H263, kCodecIDH263);
            item(AV_CODEC_ID_RV10, kCodecIDRV10);
            item(AV_CODEC_ID_RV20, kCodecIDRV20);
            item(AV_CODEC_ID_MJPEG, kCodecIDMJPEG);
            item(AV_CODEC_ID_MJPEGB, kCodecIDMJPEGB);
            item(AV_CODEC_ID_LJPEG, kCodecIDLJPEG);
            item(AV_CODEC_ID_SP5X, kCodecIDSP5X);
            item(AV_CODEC_ID_JPEGLS, kCodecIDJPEGLS);
            item(AV_CODEC_ID_MPEG4, kCodecIDMPEG4);
            item(AV_CODEC_ID_RAWVIDEO, kCodecIDRAWVIDEO);
            item(AV_CODEC_ID_MSMPEG4V1, kCodecIDMSMPEG4V1);
            item(AV_CODEC_ID_MSMPEG4V2, kCodecIDMSMPEG4V2);
            item(AV_CODEC_ID_MSMPEG4V3, kCodecIDMSMPEG4V3);
            item(AV_CODEC_ID_WMV1, kCodecIDWMV1);
            item(AV_CODEC_ID_WMV2, kCodecIDWMV2);
            item(AV_CODEC_ID_H263P, kCodecIDH263P);
            item(AV_CODEC_ID_H263I, kCodecIDH263I);
            item(AV_CODEC_ID_FLV1, kCodecIDFLV1);
            item(AV_CODEC_ID_SVQ1, kCodecIDSVQ1);
            item(AV_CODEC_ID_SVQ3, kCodecIDSVQ3);
            item(AV_CODEC_ID_DVVIDEO, kCodecIDDVVIDEO);
            item(AV_CODEC_ID_HUFFYUV, kCodecIDHUFFYUV);
            item(AV_CODEC_ID_CYUV, kCodecIDCYUV);
            item(AV_CODEC_ID_H264, kCodecIDH264);
            item(AV_CODEC_ID_INDEO3, kCodecIDINDEO3);
            item(AV_CODEC_ID_VP3, kCodecIDVP3);
            item(AV_CODEC_ID_THEORA, kCodecIDTHEORA);
            item(AV_CODEC_ID_ASV1, kCodecIDASV1);
            item(AV_CODEC_ID_ASV2, kCodecIDASV2);
            item(AV_CODEC_ID_FFV1, kCodecIDFFV1);
            item(AV_CODEC_ID_4XM, kCodecID4XM);
            item(AV_CODEC_ID_VCR1, kCodecIDVCR1);
            item(AV_CODEC_ID_CLJR, kCodecIDCLJR);
            item(AV_CODEC_ID_MDEC, kCodecIDMDEC);
            item(AV_CODEC_ID_ROQ, kCodecIDROQ);
            item(AV_CODEC_ID_INTERPLAY_VIDEO, kCodecIDINTERPLAY_VIDEO);
            item(AV_CODEC_ID_XAN_WC3, kCodecIDXAN_WC3);
            item(AV_CODEC_ID_XAN_WC4, kCodecIDXAN_WC4);
            item(AV_CODEC_ID_RPZA, kCodecIDRPZA);
            item(AV_CODEC_ID_CINEPAK, kCodecIDCINEPAK);
            item(AV_CODEC_ID_WS_VQA, kCodecIDWS_VQA);
            item(AV_CODEC_ID_MSRLE, kCodecIDMSRLE);
            item(AV_CODEC_ID_MSVIDEO1, kCodecIDMSVIDEO1);
            item(AV_CODEC_ID_IDCIN, kCodecIDIDCIN);
            item(AV_CODEC_ID_8BPS, kCodecID8BPS);
            item(AV_CODEC_ID_SMC, kCodecIDSMC);
            item(AV_CODEC_ID_FLIC, kCodecIDFLIC);
            item(AV_CODEC_ID_TRUEMOTION1, kCodecIDTRUEMOTION1);
            item(AV_CODEC_ID_VMDVIDEO, kCodecIDVMDVIDEO);
            item(AV_CODEC_ID_MSZH, kCodecIDMSZH);
            item(AV_CODEC_ID_ZLIB, kCodecIDZLIB);
            item(AV_CODEC_ID_QTRLE, kCodecIDQTRLE);
            item(AV_CODEC_ID_TSCC, kCodecIDTSCC);
            item(AV_CODEC_ID_ULTI, kCodecIDULTI);
            item(AV_CODEC_ID_QDRAW, kCodecIDQDRAW);
            item(AV_CODEC_ID_VIXL, kCodecIDVIXL);
            item(AV_CODEC_ID_QPEG, kCodecIDQPEG);
            item(AV_CODEC_ID_PNG, kCodecIDPNG);
            item(AV_CODEC_ID_PPM, kCodecIDPPM);
            item(AV_CODEC_ID_PBM, kCodecIDPBM);
            item(AV_CODEC_ID_PGM, kCodecIDPGM);
            item(AV_CODEC_ID_PGMYUV, kCodecIDPGMYUV);
            item(AV_CODEC_ID_PAM, kCodecIDPAM);
            item(AV_CODEC_ID_FFVHUFF, kCodecIDFFVHUFF);
            item(AV_CODEC_ID_RV30, kCodecIDRV30);
            item(AV_CODEC_ID_RV40, kCodecIDRV40);
            item(AV_CODEC_ID_VC1, kCodecIDVC1);
            item(AV_CODEC_ID_WMV3, kCodecIDWMV3);
            item(AV_CODEC_ID_LOCO, kCodecIDLOCO);
            item(AV_CODEC_ID_WNV1, kCodecIDWNV1);
            item(AV_CODEC_ID_AASC, kCodecIDAASC);
            item(AV_CODEC_ID_INDEO2, kCodecIDINDEO2);
            item(AV_CODEC_ID_FRAPS, kCodecIDFRAPS);
            item(AV_CODEC_ID_TRUEMOTION2, kCodecIDTRUEMOTION2);
            item(AV_CODEC_ID_BMP, kCodecIDBMP);
            item(AV_CODEC_ID_CSCD, kCodecIDCSCD);
            item(AV_CODEC_ID_MMVIDEO, kCodecIDMMVIDEO);
            item(AV_CODEC_ID_ZMBV, kCodecIDZMBV);
            item(AV_CODEC_ID_AVS, kCodecIDAVS);
            item(AV_CODEC_ID_SMACKVIDEO, kCodecIDSMACKVIDEO);
            item(AV_CODEC_ID_NUV, kCodecIDNUV);
            item(AV_CODEC_ID_KMVC, kCodecIDKMVC);
            item(AV_CODEC_ID_FLASHSV, kCodecIDFLASHSV);
            item(AV_CODEC_ID_CAVS, kCodecIDCAVS);
            item(AV_CODEC_ID_JPEG2000, kCodecIDJPEG2000);
            item(AV_CODEC_ID_VMNC, kCodecIDVMNC);
            item(AV_CODEC_ID_VP5, kCodecIDVP5);
            item(AV_CODEC_ID_VP6, kCodecIDVP6);
            item(AV_CODEC_ID_VP6F, kCodecIDVP6F);
            item(AV_CODEC_ID_TARGA, kCodecIDTARGA);
            item(AV_CODEC_ID_DSICINVIDEO, kCodecIDDSICINVIDEO);
            item(AV_CODEC_ID_TIERTEXSEQVIDEO, kCodecIDTIERTEXSEQVIDEO);
            item(AV_CODEC_ID_TIFF, kCodecIDTIFF);
            item(AV_CODEC_ID_GIF, kCodecIDGIF);
            item(AV_CODEC_ID_DXA, kCodecIDDXA);
            item(AV_CODEC_ID_DNXHD, kCodecIDDNXHD);
            item(AV_CODEC_ID_THP, kCodecIDTHP);
            item(AV_CODEC_ID_SGI, kCodecIDSGI);
            item(AV_CODEC_ID_C93, kCodecIDC93);
            item(AV_CODEC_ID_BETHSOFTVID, kCodecIDBETHSOFTVID);
            item(AV_CODEC_ID_PTX, kCodecIDPTX);
            item(AV_CODEC_ID_TXD, kCodecIDTXD);
            item(AV_CODEC_ID_VP6A, kCodecIDVP6A);
            item(AV_CODEC_ID_AMV, kCodecIDAMV);
            item(AV_CODEC_ID_VB, kCodecIDVB);
            item(AV_CODEC_ID_PCX, kCodecIDPCX);
            item(AV_CODEC_ID_SUNRAST, kCodecIDSUNRAST);
            item(AV_CODEC_ID_INDEO4, kCodecIDINDEO4);
            item(AV_CODEC_ID_INDEO5, kCodecIDINDEO5);
            item(AV_CODEC_ID_MIMIC, kCodecIDMIMIC);
            item(AV_CODEC_ID_RL2, kCodecIDRL2);
            item(AV_CODEC_ID_ESCAPE124, kCodecIDESCAPE124);
            item(AV_CODEC_ID_DIRAC, kCodecIDDIRAC);
            item(AV_CODEC_ID_BFI, kCodecIDBFI);
            item(AV_CODEC_ID_CMV, kCodecIDCMV);
            item(AV_CODEC_ID_MOTIONPIXELS, kCodecIDMOTIONPIXELS);
            item(AV_CODEC_ID_TGV, kCodecIDTGV);
            item(AV_CODEC_ID_TGQ, kCodecIDTGQ);
            item(AV_CODEC_ID_TQI, kCodecIDTQI);
            item(AV_CODEC_ID_AURA, kCodecIDAURA);
            item(AV_CODEC_ID_AURA2, kCodecIDAURA2);
            item(AV_CODEC_ID_V210X, kCodecIDV210X);
            item(AV_CODEC_ID_TMV, kCodecIDTMV);
            item(AV_CODEC_ID_V210, kCodecIDV210);
            item(AV_CODEC_ID_DPX, kCodecIDDPX);
            item(AV_CODEC_ID_MAD, kCodecIDMAD);
            item(AV_CODEC_ID_FRWU, kCodecIDFRWU);
            item(AV_CODEC_ID_FLASHSV2, kCodecIDFLASHSV2);
            item(AV_CODEC_ID_CDGRAPHICS, kCodecIDCDGRAPHICS);
            item(AV_CODEC_ID_R210, kCodecIDR210);
            item(AV_CODEC_ID_ANM, kCodecIDANM);
            item(AV_CODEC_ID_BINKVIDEO, kCodecIDBINKVIDEO);
            item(AV_CODEC_ID_IFF_ILBM, kCodecIDIFF_ILBM);
            item(AV_CODEC_ID_KGV1, kCodecIDKGV1);
            item(AV_CODEC_ID_YOP, kCodecIDYOP);
            item(AV_CODEC_ID_VP8, kCodecIDVP8);
            item(AV_CODEC_ID_PICTOR, kCodecIDPICTOR);
            item(AV_CODEC_ID_ANSI, kCodecIDANSI);
            item(AV_CODEC_ID_A64_MULTI, kCodecIDA64_MULTI);
            item(AV_CODEC_ID_A64_MULTI5, kCodecIDA64_MULTI5);
            item(AV_CODEC_ID_R10K, kCodecIDR10K);
            item(AV_CODEC_ID_MXPEG, kCodecIDMXPEG);
            item(AV_CODEC_ID_LAGARITH, kCodecIDLAGARITH);
            item(AV_CODEC_ID_PRORES, kCodecIDPRORES);
            item(AV_CODEC_ID_JV, kCodecIDJV);
            item(AV_CODEC_ID_DFA, kCodecIDDFA);
            item(AV_CODEC_ID_WMV3IMAGE, kCodecIDWMV3IMAGE);
            item(AV_CODEC_ID_VC1IMAGE, kCodecIDVC1IMAGE);
            item(AV_CODEC_ID_UTVIDEO, kCodecIDUTVIDEO);
            item(AV_CODEC_ID_BMV_VIDEO, kCodecIDBMV_VIDEO);
            item(AV_CODEC_ID_VBLE, kCodecIDVBLE);
            item(AV_CODEC_ID_DXTORY, kCodecIDDXTORY);
            item(AV_CODEC_ID_V410, kCodecIDV410);
            item(AV_CODEC_ID_XWD, kCodecIDXWD);
            item(AV_CODEC_ID_CDXL, kCodecIDCDXL);
            item(AV_CODEC_ID_XBM, kCodecIDXBM);
            item(AV_CODEC_ID_ZEROCODEC, kCodecIDZEROCODEC);
            item(AV_CODEC_ID_MSS1, kCodecIDMSS1);
            item(AV_CODEC_ID_MSA1, kCodecIDMSA1);
            item(AV_CODEC_ID_TSCC2, kCodecIDTSCC2);
            item(AV_CODEC_ID_MTS2, kCodecIDMTS2);
            item(AV_CODEC_ID_CLLC, kCodecIDCLLC);
            item(AV_CODEC_ID_MSS2, kCodecIDMSS2);
            item(AV_CODEC_ID_VP9, kCodecIDVP9);
            item(AV_CODEC_ID_AIC, kCodecIDAIC);
            item(AV_CODEC_ID_ESCAPE130, kCodecIDESCAPE130);
            item(AV_CODEC_ID_G2M, kCodecIDG2M);
            item(AV_CODEC_ID_WEBP, kCodecIDWEBP);
            item(AV_CODEC_ID_HNM4_VIDEO, kCodecIDHNM4_VIDEO);
            item(AV_CODEC_ID_HEVC, kCodecIDHEVC);
            item(AV_CODEC_ID_FIC, kCodecIDFIC);
            item(AV_CODEC_ID_ALIAS_PIX, kCodecIDALIAS_PIX);
            item(AV_CODEC_ID_BRENDER_PIX, kCodecIDBRENDER_PIX);
            item(AV_CODEC_ID_PAF_VIDEO, kCodecIDPAF_VIDEO);
            item(AV_CODEC_ID_EXR, kCodecIDEXR);
            item(AV_CODEC_ID_VP7, kCodecIDVP7);
            item(AV_CODEC_ID_SANM, kCodecIDSANM);
            item(AV_CODEC_ID_SGIRLE, kCodecIDSGIRLE);
            item(AV_CODEC_ID_MVC1, kCodecIDMVC1);
            item(AV_CODEC_ID_MVC2, kCodecIDMVC2);
            item(AV_CODEC_ID_HQX, kCodecIDHQX);
            item(AV_CODEC_ID_TDSC, kCodecIDTDSC);
            item(AV_CODEC_ID_HQ_HQA, kCodecIDHQ_HQA);
            item(AV_CODEC_ID_HAP, kCodecIDHAP);
            item(AV_CODEC_ID_DDS, kCodecIDDDS);
            item(AV_CODEC_ID_DXV, kCodecIDDXV);
            item(AV_CODEC_ID_SCREENPRESSO, kCodecIDSCREENPRESSO);
            item(AV_CODEC_ID_RSCC, kCodecIDRSCC);
            item(AV_CODEC_ID_Y41P, kCodecIDY41P);
            item(AV_CODEC_ID_AVRP, kCodecIDAVRP);
            item(AV_CODEC_ID_012V, kCodecID012V);
            item(AV_CODEC_ID_AVUI, kCodecIDAVUI);
            item(AV_CODEC_ID_AYUV, kCodecIDAYUV);
            item(AV_CODEC_ID_TARGA_Y216, kCodecIDTARGA_Y216);
            item(AV_CODEC_ID_V308, kCodecIDV308);
            item(AV_CODEC_ID_V408, kCodecIDV408);
            item(AV_CODEC_ID_YUV4, kCodecIDYUV4);
            item(AV_CODEC_ID_AVRN, kCodecIDAVRN);
            item(AV_CODEC_ID_CPIA, kCodecIDCPIA);
            item(AV_CODEC_ID_XFACE, kCodecIDXFACE);
            item(AV_CODEC_ID_SNOW, kCodecIDSNOW);
            item(AV_CODEC_ID_SMVJPEG, kCodecIDSMVJPEG);
            item(AV_CODEC_ID_APNG, kCodecIDAPNG);
            item(AV_CODEC_ID_DAALA, kCodecIDDAALA);
            item(AV_CODEC_ID_CFHD, kCodecIDCFHD);
            item(AV_CODEC_ID_TRUEMOTION2RT, kCodecIDTRUEMOTION2RT);
            item(AV_CODEC_ID_M101, kCodecIDM101);
            item(AV_CODEC_ID_MAGICYUV, kCodecIDMAGICYUV);
            item(AV_CODEC_ID_SHEERVIDEO, kCodecIDSHEERVIDEO);
            item(AV_CODEC_ID_YLC, kCodecIDYLC);
            item(AV_CODEC_ID_PCM_S16LE, kCodecIDPCM_S16LE);
            item(AV_CODEC_ID_PCM_S16BE, kCodecIDPCM_S16BE);
            item(AV_CODEC_ID_PCM_U16LE, kCodecIDPCM_U16LE);
            item(AV_CODEC_ID_PCM_U16BE, kCodecIDPCM_U16BE);
            item(AV_CODEC_ID_PCM_S8, kCodecIDPCM_S8);
            item(AV_CODEC_ID_PCM_U8, kCodecIDPCM_U8);
            item(AV_CODEC_ID_PCM_MULAW, kCodecIDPCM_MULAW);
            item(AV_CODEC_ID_PCM_ALAW, kCodecIDPCM_ALAW);
            item(AV_CODEC_ID_PCM_S32LE, kCodecIDPCM_S32LE);
            item(AV_CODEC_ID_PCM_S32BE, kCodecIDPCM_S32BE);
            item(AV_CODEC_ID_PCM_U32LE, kCodecIDPCM_U32LE);
            item(AV_CODEC_ID_PCM_U32BE, kCodecIDPCM_U32BE);
            item(AV_CODEC_ID_PCM_S24LE, kCodecIDPCM_S24LE);
            item(AV_CODEC_ID_PCM_S24BE, kCodecIDPCM_S24BE);
            item(AV_CODEC_ID_PCM_U24LE, kCodecIDPCM_U24LE);
            item(AV_CODEC_ID_PCM_U24BE, kCodecIDPCM_U24BE);
            item(AV_CODEC_ID_PCM_S24DAUD, kCodecIDPCM_S24DAUD);
            item(AV_CODEC_ID_PCM_ZORK, kCodecIDPCM_ZORK);
            item(AV_CODEC_ID_PCM_S16LE_PLANAR, kCodecIDPCM_S16LE_PLANAR);
            item(AV_CODEC_ID_PCM_DVD, kCodecIDPCM_DVD);
            item(AV_CODEC_ID_PCM_F32BE, kCodecIDPCM_F32BE);
            item(AV_CODEC_ID_PCM_F32LE, kCodecIDPCM_F32LE);
            item(AV_CODEC_ID_PCM_F64BE, kCodecIDPCM_F64BE);
            item(AV_CODEC_ID_PCM_F64LE, kCodecIDPCM_F64LE);
            item(AV_CODEC_ID_PCM_BLURAY, kCodecIDPCM_BLURAY);
            item(AV_CODEC_ID_PCM_LXF, kCodecIDPCM_LXF);
            item(AV_CODEC_ID_S302M, kCodecIDS302M);
            item(AV_CODEC_ID_PCM_S8_PLANAR, kCodecIDPCM_S8_PLANAR);
            item(AV_CODEC_ID_PCM_S24LE_PLANAR, kCodecIDPCM_S24LE_PLANAR);
            item(AV_CODEC_ID_PCM_S32LE_PLANAR, kCodecIDPCM_S32LE_PLANAR);
            item(AV_CODEC_ID_PCM_S16BE_PLANAR, kCodecIDPCM_S16BE_PLANAR);
            item(AV_CODEC_ID_PCM_S64LE, kCodecIDPCM_S64LE);
            item(AV_CODEC_ID_PCM_S64BE, kCodecIDPCM_S64BE);
            item(AV_CODEC_ID_ADPCM_IMA_QT, kCodecIDADPCM_IMA_QT);
            item(AV_CODEC_ID_ADPCM_IMA_WAV, kCodecIDADPCM_IMA_WAV);
            item(AV_CODEC_ID_ADPCM_IMA_DK3, kCodecIDADPCM_IMA_DK3);
            item(AV_CODEC_ID_ADPCM_IMA_DK4, kCodecIDADPCM_IMA_DK4);
            item(AV_CODEC_ID_ADPCM_IMA_WS, kCodecIDADPCM_IMA_WS);
            item(AV_CODEC_ID_ADPCM_IMA_SMJPEG, kCodecIDADPCM_IMA_SMJPEG);
            item(AV_CODEC_ID_ADPCM_MS, kCodecIDADPCM_MS);
            item(AV_CODEC_ID_ADPCM_4XM, kCodecIDADPCM_4XM);
            item(AV_CODEC_ID_ADPCM_XA, kCodecIDADPCM_XA);
            item(AV_CODEC_ID_ADPCM_ADX, kCodecIDADPCM_ADX);
            item(AV_CODEC_ID_ADPCM_EA, kCodecIDADPCM_EA);
            item(AV_CODEC_ID_ADPCM_G726, kCodecIDADPCM_G726);
            item(AV_CODEC_ID_ADPCM_CT, kCodecIDADPCM_CT);
            item(AV_CODEC_ID_ADPCM_SWF, kCodecIDADPCM_SWF);
            item(AV_CODEC_ID_ADPCM_YAMAHA, kCodecIDADPCM_YAMAHA);
            item(AV_CODEC_ID_ADPCM_SBPRO_4, kCodecIDADPCM_SBPRO_4);
            item(AV_CODEC_ID_ADPCM_SBPRO_3, kCodecIDADPCM_SBPRO_3);
            item(AV_CODEC_ID_ADPCM_SBPRO_2, kCodecIDADPCM_SBPRO_2);
            item(AV_CODEC_ID_ADPCM_THP, kCodecIDADPCM_THP);
            item(AV_CODEC_ID_ADPCM_IMA_AMV, kCodecIDADPCM_IMA_AMV);
            item(AV_CODEC_ID_ADPCM_EA_R1, kCodecIDADPCM_EA_R1);
            item(AV_CODEC_ID_ADPCM_EA_R3, kCodecIDADPCM_EA_R3);
            item(AV_CODEC_ID_ADPCM_EA_R2, kCodecIDADPCM_EA_R2);
            item(AV_CODEC_ID_ADPCM_IMA_EA_SEAD, kCodecIDADPCM_IMA_EA_SEAD);
            item(AV_CODEC_ID_ADPCM_IMA_EA_EACS, kCodecIDADPCM_IMA_EA_EACS);
            item(AV_CODEC_ID_ADPCM_EA_XAS, kCodecIDADPCM_EA_XAS);
            item(AV_CODEC_ID_ADPCM_EA_MAXIS_XA, kCodecIDADPCM_EA_MAXIS_XA);
            item(AV_CODEC_ID_ADPCM_IMA_ISS, kCodecIDADPCM_IMA_ISS);
            item(AV_CODEC_ID_ADPCM_G722, kCodecIDADPCM_G722);
            item(AV_CODEC_ID_ADPCM_IMA_APC, kCodecIDADPCM_IMA_APC);
            item(AV_CODEC_ID_ADPCM_VIMA, kCodecIDADPCM_VIMA);
            item(AV_CODEC_ID_ADPCM_AFC, kCodecIDADPCM_AFC);
            item(AV_CODEC_ID_ADPCM_IMA_OKI, kCodecIDADPCM_IMA_OKI);
            item(AV_CODEC_ID_ADPCM_DTK, kCodecIDADPCM_DTK);
            item(AV_CODEC_ID_ADPCM_IMA_RAD, kCodecIDADPCM_IMA_RAD);
            item(AV_CODEC_ID_ADPCM_G726LE, kCodecIDADPCM_G726LE);
            item(AV_CODEC_ID_ADPCM_THP_LE, kCodecIDADPCM_THP_LE);
            item(AV_CODEC_ID_ADPCM_PSX, kCodecIDADPCM_PSX);
            item(AV_CODEC_ID_ADPCM_AICA, kCodecIDADPCM_AICA);
            item(AV_CODEC_ID_ADPCM_IMA_DAT4, kCodecIDADPCM_IMA_DAT4);
            item(AV_CODEC_ID_ADPCM_MTAF, kCodecIDADPCM_MTAF);
            item(AV_CODEC_ID_AMR_NB, kCodecIDAMR_NB);
            item(AV_CODEC_ID_AMR_WB, kCodecIDAMR_WB);
            item(AV_CODEC_ID_RA_144, kCodecIDRA_144);
            item(AV_CODEC_ID_RA_288, kCodecIDRA_288);
            item(AV_CODEC_ID_ROQ_DPCM, kCodecIDROQ_DPCM);
            item(AV_CODEC_ID_INTERPLAY_DPCM, kCodecIDINTERPLAY_DPCM);
            item(AV_CODEC_ID_XAN_DPCM, kCodecIDXAN_DPCM);
            item(AV_CODEC_ID_SOL_DPCM, kCodecIDSOL_DPCM);
            item(AV_CODEC_ID_SDX2_DPCM, kCodecIDSDX2_DPCM);
            item(AV_CODEC_ID_MP2, kCodecIDMP2);
            item(AV_CODEC_ID_MP3, kCodecIDMP3);
            item(AV_CODEC_ID_AAC, kCodecIDAAC);
            item(AV_CODEC_ID_AC3, kCodecIDAC3);
            item(AV_CODEC_ID_DTS, kCodecIDDTS);
            item(AV_CODEC_ID_VORBIS, kCodecIDVORBIS);
            item(AV_CODEC_ID_DVAUDIO, kCodecIDDVAUDIO);
            item(AV_CODEC_ID_WMAV1, kCodecIDWMAV1);
            item(AV_CODEC_ID_WMAV2, kCodecIDWMAV2);
            item(AV_CODEC_ID_MACE3, kCodecIDMACE3);
            item(AV_CODEC_ID_MACE6, kCodecIDMACE6);
            item(AV_CODEC_ID_VMDAUDIO, kCodecIDVMDAUDIO);
            item(AV_CODEC_ID_FLAC, kCodecIDFLAC);
            item(AV_CODEC_ID_MP3ADU, kCodecIDMP3ADU);
            item(AV_CODEC_ID_MP3ON4, kCodecIDMP3ON4);
            item(AV_CODEC_ID_SHORTEN, kCodecIDSHORTEN);
            item(AV_CODEC_ID_ALAC, kCodecIDALAC);
            item(AV_CODEC_ID_WESTWOOD_SND1, kCodecIDWESTWOOD_SND1);
            item(AV_CODEC_ID_GSM, kCodecIDGSM);
            item(AV_CODEC_ID_QDM2, kCodecIDQDM2);
            item(AV_CODEC_ID_COOK, kCodecIDCOOK);
            item(AV_CODEC_ID_TRUESPEECH, kCodecIDTRUESPEECH);
            item(AV_CODEC_ID_TTA, kCodecIDTTA);
            item(AV_CODEC_ID_SMACKAUDIO, kCodecIDSMACKAUDIO);
            item(AV_CODEC_ID_QCELP, kCodecIDQCELP);
            item(AV_CODEC_ID_WAVPACK, kCodecIDWAVPACK);
            item(AV_CODEC_ID_DSICINAUDIO, kCodecIDDSICINAUDIO);
            item(AV_CODEC_ID_IMC, kCodecIDIMC);
            item(AV_CODEC_ID_MUSEPACK7, kCodecIDMUSEPACK7);
            item(AV_CODEC_ID_MLP, kCodecIDMLP);
            item(AV_CODEC_ID_GSM_MS, kCodecIDGSM_MS);
            item(AV_CODEC_ID_ATRAC3, kCodecIDATRAC3);
            item(AV_CODEC_ID_VOXWARE, kCodecIDVOXWARE);
            item(AV_CODEC_ID_NELLYMOSER, kCodecIDNELLYMOSER);
            item(AV_CODEC_ID_MUSEPACK8, kCodecIDMUSEPACK8);
            item(AV_CODEC_ID_SPEEX, kCodecIDSPEEX);
            item(AV_CODEC_ID_WMAVOICE, kCodecIDWMAVOICE);
            item(AV_CODEC_ID_WMAPRO, kCodecIDWMAPRO);
            item(AV_CODEC_ID_WMALOSSLESS, kCodecIDWMALOSSLESS);
            item(AV_CODEC_ID_ATRAC3P, kCodecIDATRAC3P);
            item(AV_CODEC_ID_EAC3, kCodecIDEAC3);
            item(AV_CODEC_ID_SIPR, kCodecIDSIPR);
            item(AV_CODEC_ID_MP1, kCodecIDMP1);
            item(AV_CODEC_ID_TWINVQ, kCodecIDTWINVQ);
            item(AV_CODEC_ID_TRUEHD, kCodecIDTRUEHD);
            item(AV_CODEC_ID_MP4ALS, kCodecIDMP4ALS);
            item(AV_CODEC_ID_ATRAC1, kCodecIDATRAC1);
            item(AV_CODEC_ID_BINKAUDIO_RDFT, kCodecIDBINKAUDIO_RDFT);
            item(AV_CODEC_ID_BINKAUDIO_DCT, kCodecIDBINKAUDIO_DCT);
            item(AV_CODEC_ID_AAC_LATM, kCodecIDAAC_LATM);
            item(AV_CODEC_ID_QDMC, kCodecIDQDMC);
            item(AV_CODEC_ID_CELT, kCodecIDCELT);
            item(AV_CODEC_ID_G723_1, kCodecIDG723_1);
            item(AV_CODEC_ID_G729, kCodecIDG729);
            item(AV_CODEC_ID_8SVX_EXP, kCodecID8SVX_EXP);
            item(AV_CODEC_ID_8SVX_FIB, kCodecID8SVX_FIB);
            item(AV_CODEC_ID_BMV_AUDIO, kCodecIDBMV_AUDIO);
            item(AV_CODEC_ID_RALF, kCodecIDRALF);
            item(AV_CODEC_ID_IAC, kCodecIDIAC);
            item(AV_CODEC_ID_ILBC, kCodecIDILBC);
            item(AV_CODEC_ID_OPUS, kCodecIDOPUS);
            item(AV_CODEC_ID_COMFORT_NOISE, kCodecIDCOMFORT_NOISE);
            item(AV_CODEC_ID_TAK, kCodecIDTAK);
            item(AV_CODEC_ID_METASOUND, kCodecIDMETASOUND);
            item(AV_CODEC_ID_PAF_AUDIO, kCodecIDPAF_AUDIO);
            item(AV_CODEC_ID_ON2AVC, kCodecIDON2AVC);
            item(AV_CODEC_ID_DSS_SP, kCodecIDDSS_SP);
            item(AV_CODEC_ID_FFWAVESYNTH, kCodecIDFFWAVESYNTH);
            item(AV_CODEC_ID_SONIC, kCodecIDSONIC);
            item(AV_CODEC_ID_SONIC_LS, kCodecIDSONIC_LS);
            item(AV_CODEC_ID_EVRC, kCodecIDEVRC);
            item(AV_CODEC_ID_SMV, kCodecIDSMV);
            item(AV_CODEC_ID_DSD_LSBF, kCodecIDDSD_LSBF);
            item(AV_CODEC_ID_DSD_MSBF, kCodecIDDSD_MSBF);
            item(AV_CODEC_ID_DSD_LSBF_PLANAR, kCodecIDDSD_LSBF_PLANAR);
            item(AV_CODEC_ID_DSD_MSBF_PLANAR, kCodecIDDSD_MSBF_PLANAR);
            item(AV_CODEC_ID_4GV, kCodecID4GV);
            item(AV_CODEC_ID_INTERPLAY_ACM, kCodecIDINTERPLAY_ACM);
            item(AV_CODEC_ID_XMA1, kCodecIDXMA1);
            item(AV_CODEC_ID_XMA2, kCodecIDXMA2);
            item(AV_CODEC_ID_DST, kCodecIDDST);
            item(AV_CODEC_ID_DVD_SUBTITLE, kCodecIDDVD_SUBTITLE);
            item(AV_CODEC_ID_DVB_SUBTITLE, kCodecIDDVB_SUBTITLE);
            item(AV_CODEC_ID_TEXT, kCodecIDTEXT);
            item(AV_CODEC_ID_XSUB, kCodecIDXSUB);
            item(AV_CODEC_ID_SSA, kCodecIDSSA);
            item(AV_CODEC_ID_MOV_TEXT, kCodecIDMOV_TEXT);
            item(AV_CODEC_ID_HDMV_PGS_SUBTITLE, kCodecIDHDMV_PGS_SUBTITLE);
            item(AV_CODEC_ID_DVB_TELETEXT, kCodecIDDVB_TELETEXT);
            item(AV_CODEC_ID_SRT, kCodecIDSRT);
            item(AV_CODEC_ID_MICRODVD, kCodecIDMICRODVD);
            item(AV_CODEC_ID_EIA_608, kCodecIDEIA_608);
            item(AV_CODEC_ID_JACOSUB, kCodecIDJACOSUB);
            item(AV_CODEC_ID_SAMI, kCodecIDSAMI);
            item(AV_CODEC_ID_REALTEXT, kCodecIDREALTEXT);
            item(AV_CODEC_ID_STL, kCodecIDSTL);
            item(AV_CODEC_ID_SUBVIEWER1, kCodecIDSUBVIEWER1);
            item(AV_CODEC_ID_SUBVIEWER, kCodecIDSUBVIEWER);
            item(AV_CODEC_ID_SUBRIP, kCodecIDSUBRIP);
            item(AV_CODEC_ID_WEBVTT, kCodecIDWEBVTT);
            item(AV_CODEC_ID_MPL2, kCodecIDMPL2);
            item(AV_CODEC_ID_VPLAYER, kCodecIDVPLAYER);
            item(AV_CODEC_ID_PJS, kCodecIDPJS);
            item(AV_CODEC_ID_ASS, kCodecIDASS);
            item(AV_CODEC_ID_HDMV_TEXT_SUBTITLE, kCodecIDHDMV_TEXT_SUBTITLE);
            item(AV_CODEC_ID_TTF, kCodecIDTTF);
            item(AV_CODEC_ID_BINTEXT, kCodecIDBINTEXT);
            item(AV_CODEC_ID_XBIN, kCodecIDXBIN);
            item(AV_CODEC_ID_IDF, kCodecIDIDF);
            item(AV_CODEC_ID_OTF, kCodecIDOTF);
            item(AV_CODEC_ID_SMPTE_KLV, kCodecIDSMPTE_KLV);
            item(AV_CODEC_ID_DVD_NAV, kCodecIDDVD_NAV);
            item(AV_CODEC_ID_TIMED_ID3, kCodecIDTIMED_ID3);
            item(AV_CODEC_ID_BIN_DATA, kCodecIDBIN_DATA);
            item(AV_CODEC_ID_PROBE, kCodecIDPROBE);
            item(AV_CODEC_ID_MPEG2TS, kCodecIDMPEG2TS);
            item(AV_CODEC_ID_MPEG4SYSTEMS, kCodecIDMPEG4SYSTEMS);
            item(AV_CODEC_ID_FFMETADATA, kCodecIDFFMETADATA);
            item(AV_CODEC_ID_WRAPPED_AVFRAME, kCodecIDWRAPPED_AVFRAME);
            default:
                MMLOGV("%d -> AV_CODEC_ID_NONE\n", id);
                return kCodecIDNONE;
        }

}


extern "C" {

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("ExternalCaptureSourceCreater");

Component * createComponent(const char* mimeType, bool isEncoder)
{
    FUNC_ENTER();
    ExternalCaptureSource * component = new ExternalCaptureSource();
    if ( !component ) {
        MMLOGE("no mem\n");
        return NULL;
    }

    MMLOGI("ret: %p\n", component);
    return component;
}

void releaseComponent(Component * component)
{
    if ( component ) {
        ExternalCaptureSource * c = DYNAMIC_CAST<ExternalCaptureSource*>(component);
        MM_RELEASE(c);
    }
}
}

}

