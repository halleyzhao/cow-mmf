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
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
#ifdef USE_RESMANAGER
#include <data/ResManager.h>
#include <file/FilePath.h>
#else
#include <ResourceLocator.h>
#endif
#include <uri/Uri.h>
#endif

#include <multimedia/av_ffmpeg_helper.h>

#include "av_demuxer.h"
#include <unistd.h>
#include <multimedia/av_buffer_helper.h>
#include <multimedia/media_attr_str.h>
#include <multimedia/mm_types.h>
#include "third_helper.h"

#ifdef __USING_SYSCAP_MM__
#include <yunhal/VideoSysCapability.h>
using yunos::VideoSysCapability;
using yunos::VideoSysCapabilitySP;
#endif
#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
MM_LOG_DEFINE_MODULE_NAME("Demuxer")
#ifdef USE_CONNECTIVITY_PROXY_INFO
#include "LinkClient.h"
#include "looper/Looper.h"

static void useProxyFromConnectivity()
{
    MMLOGD("[proxy_debug] check proxy setting from connectivity");
    if (YUNOS_MM::mm_check_env_str("mm.skip.proxy","MM_SKIP_PROXY", "1", false)) {
        MMLOGD("[proxy_debug] skip proxy setting");
        return;
    }
    yunos::Looper looper;
    yunos::SharedPtr<yunos::LinkClient> lc =
        yunos::LinkClient::getInstance();
    if (!lc) {
      MMLOGD("link client is null");
      return;
    }
    yunos::SharedPtr<yunos::NetStatus> status = lc->getActiveNetworkStatus();
    if (!status) {
      MMLOGD("Network status is null");
      return;
    }
    int net_id = status->mNetId;
    yunos::SharedPtr<yunos::ConnectInfo> ci = lc->getConnectionInfoById(net_id);
    MMLOGD("ci->mNetProxy: %p", ci->mNetProxy.pointer());
    if (ci && ci->mNetProxy) {
      MMLOGD("ci->mNetProxy: %p", ci->mNetProxy.pointer());
      if (!ci->mNetProxy->mLocalHost.isNull()) {
        char buf[32] = {0};
        int port = ci->mNetProxy->mLocalPort;
        sprintf(buf, ":%d", port > 0 ? port : 80);

        MMLOGD("host: %s, port: %d", ci->mNetProxy->mLocalHost.c_str(), ci->mNetProxy->mLocalPort);
        std::string http_proxy_key = "http_proxy";
        std::string http_proxy_value = "http://";
        http_proxy_value += ci->mNetProxy->mLocalHost.c_str();
        http_proxy_value += buf;
        const char* curr_value = getenv(http_proxy_key.c_str());
        if (!curr_value || strcasecmp(http_proxy_value.c_str(), curr_value)) {
            setenv(http_proxy_key.c_str(), http_proxy_value.c_str(), 1);
        }
        MMLOGD("[proxy_debug] proxy from connectivity is %s", http_proxy_value.c_str());
        std::string https_proxy_key = "https_proxy";
        std::string https_proxy_value = "https://";
        https_proxy_value += ci->mNetProxy->mLocalHost.c_str();
        https_proxy_value += buf;
        curr_value = getenv(https_proxy_key.c_str());
        if (!curr_value || strcasecmp(https_proxy_value.c_str(), curr_value)) {
            setenv(https_proxy_key.c_str(), https_proxy_value.c_str(), 1);
        }
      }
    }
}
#else
static void useProxyFromConnectivity() {}
#endif

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

#define SETPARAM_STRING(_key_name, _val)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_String);\
        _val = item.mValue.str;\
        INFO("setparam key: %s, val: %s\n", item.mName, item.mValue.str);\
        mMetaData->setString(_key_name, item.mValue.str);\
        continue;\
    }

#define SETPARAM_END() }

#define FREE_AVPACKET(_pkt) do {\
    if ( _pkt ) {\
        av_free_packet(_pkt);\
        free(_pkt);\
        (_pkt) = NULL;\
    }\
}while(0)


#define FUNC_ENTER() MMLOGI("+\n")
#define FUNC_LEAVE() MMLOGV("-\n")

const char * MEDIABUFFER_CDS_NAME = "CSDPointer";
static const char * MMMSGTHREAD_NAME = "AVDemuxer";
static const char * MMTHREAD_NAME = "AVDemuxer::ReadThread";

static const int PREPARE_TIMEOUT_DEFUALT = 10000000;//us
static const int READ_TIMEOUT_DEFAULT = 10000000;
static const int SEEK_TIMEOUT_DEFAULT = 10000000;

static const int REPORTED_PERCENT_NONE = -1;
static const int WANT_TRACK_NONE = -1;
static const int WANT_TRACK_INDEX_DEFAULT = 0;

static const int64_t SEEK_NONE = -1;

static const int64_t BUFFERING_TIME_DEFAULT = 2 * 1000 * 1000;
static const int64_t BUFFERING_TIME_LOCAL = 300 * 1000;
static const char * BUFFERING_TIME_CFG_KEY = "mm.avdemuxer.buffer";
static const char * BUFFERING_TIME_CFG_ENV = "MM_AVDEMUXER_BUFFER";

static const int BUFFER_HIGH_FACTOR = 2;

static const int64_t MAX_BUFFER_TIME = 10 * 1000 * 1000;

static const int64_t MAX_BUFFER_SEEK = 5 * 1000 * 1000; // used for internet streaming only, see #10079483

static const AVRational gTimeBase = {1,1000000};

static const size_t AVIO_BUFFER_SIZE = 128*1024;

static const float PRECISION_DIFF = 0.000001f;

#ifdef DUMP_INPUT
static const char* sourceDataDump = "/data/input.raw";
static DataDump encodedDataDump(sourceDataDump);
#endif

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




DEFINE_LOGTAG(AVDemuxer)
DEFINE_LOGTAG(AVDemuxer::InterruptHandler)
DEFINE_LOGTAG(AVDemuxer::AVDemuxReader)
DEFINE_LOGTAG(AVDemuxer::ReadThread)


AVDemuxer::InterruptHandler::InterruptHandler(AVDemuxer* demuxer)
                          : mIsTimeout(false),
                          mExit(false),
                          mActive(false)
{
    FUNC_ENTER();
    callback = handleTimeout;
    opaque = this;
    FUNC_LEAVE();
}

AVDemuxer::InterruptHandler::~InterruptHandler()
{
    FUNC_ENTER();
    FUNC_LEAVE();
}

void AVDemuxer::InterruptHandler::start(int64_t timeout)
{
    MMLOGV("+\n");
    mActive = true;
    mExit = false;
    mIsTimeout = false;
    mTimer.start(timeout);
    FUNC_LEAVE();
}

void AVDemuxer::InterruptHandler::end()
{
    MMLOGV("+\n");
    mIsTimeout = mTimer.isExpired();
    mActive = false;
    FUNC_LEAVE();
}

void AVDemuxer::InterruptHandler::exit(bool isExit)
{
    mExit = isExit;
}

bool AVDemuxer::InterruptHandler::isExiting() const
{
    return mExit;
}

bool AVDemuxer::InterruptHandler::isTimeout() const
{
    return mIsTimeout;
}

/*static */int AVDemuxer::InterruptHandler::handleTimeout(void* obj)
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


AVDemuxer::StreamInfo::StreamInfo() : mMediaType(kMediaTypeUnknown),
                        mSelectedStream(WANT_TRACK_NONE),
                        mSelectedStreamPending(WANT_TRACK_NONE),
                        mCurrentIndex(0),
                        mBeginWritePendingList(false),
                        mHasAttachedPic(false),
                        mNotCheckBuffering(false),
                        mPeerInstalled(false),
                        mPacketCount(0),
                        mLastDts(0),
                        mLastPts(0),
                        mTargetTimeUs(SEEK_NONE),
                        mStartTimeUs(0)
{
    FUNC_ENTER();
    memset(&mTimeBase, 0, sizeof(AVRational));
    FUNC_LEAVE();
}

AVDemuxer::StreamInfo::~StreamInfo()
{
    FUNC_ENTER();
    reset();
    FUNC_LEAVE();
}

bool AVDemuxer::StreamInfo::init()
{
    MMAutoLock locker(mLock);
    FUNC_ENTER();
    mMetaData = MediaMeta::create();
    return mMetaData;
}

void AVDemuxer::StreamInfo::reset()
{
    // MMAutoLock locker(mLock); // reset() is called by ~StreamInfo()
    FUNC_ENTER();
    mMediaType = kMediaTypeUnknown;
    mSelectedStream = WANT_TRACK_NONE;
    mSelectedStreamPending = WANT_TRACK_NONE;
    mBeginWritePendingList =false;
    mAllStreams.clear();
    mBufferList[0].clear();
    mBufferList[1].clear();
    mHasAttachedPic = false;
    mCurrentIndex = 0;
    mNotCheckBuffering = false;
    mPeerInstalled = false;
    memset(&mTimeBase, 0, sizeof(AVRational));
    mPacketCount = 0;
    mLastDts = 0;
    mLastPts = 0;
    mMetaData->clear();
    mStartTimeUs = 0;
    FUNC_LEAVE();
}
bool AVDemuxer::StreamInfo::shortCircuitSelectTrack(int trackIndex)
{
    MMAutoLock locker(mLock);
    int streamIndex = mAllStreams[trackIndex];

    if (streamIndex == mSelectedStreamPending)
        return true;

    if (streamIndex == mSelectedStream) {
        mBufferList[!mCurrentIndex].clear();
        mSelectedStreamPending = WANT_TRACK_NONE;
        return true;
    }

    if (mSelectedStream == WANT_TRACK_NONE) {
        mSelectedStream = streamIndex;
        return false;
    }

    if (mSelectedStreamPending != WANT_TRACK_NONE) {
        mBufferList[!mCurrentIndex].clear();
    }
    mSelectedStreamPending = streamIndex;
    mBeginWritePendingList =false;

    return false;
}
int64_t AVDemuxer::StreamInfo::bufferedTime() const
{
    MMAutoLock locker(mLock);
    if ( mBufferList[mCurrentIndex].size() <= 1 ) {
        MMLOGV("size: %d\n", mBufferList[mCurrentIndex].size());
        return 0;
    }

    int64_t ft = mBufferList[mCurrentIndex].front()->dts();
    int64_t et = mBufferList[mCurrentIndex].back()->dts();
    MMLOGV("size: %u, ft: %" PRId64 ", et: %" PRId64 "\n", mBufferList[mCurrentIndex].size(), ft, et);
    int64_t dur = et - ft;
    if ( dur < 0 ) {
        int64_t all = 0;
        int64_t former;
        if ( mBufferList[mCurrentIndex].size() == 2 ) {
            return ft - et;
        }
        former = ft;
        BufferList::const_iterator i = mBufferList[mCurrentIndex].begin();
        ++i;
        for ( ; i != mBufferList[mCurrentIndex].end(); ++i ) {
            int64_t cur = (*i)->dts();
            if ( cur < former ) {
                former = cur;
                break;
            }

            all += (cur - former);
            former = cur;
        }

        if ( i != mBufferList[mCurrentIndex].end() ) {
            ++i;
            for ( ; i != mBufferList[mCurrentIndex].end(); ++i ) {
                int64_t cur = (*i)->dts();
                all += (cur - former);
                former = cur;
            }
        }

        return all;
    }

    return dur;
}

int64_t AVDemuxer::StreamInfo::bufferedFirstTs(bool isPendingList) const
{
    MMAutoLock locker(mLock);
    return bufferedFirstTs_l(isPendingList);
}
int64_t AVDemuxer::StreamInfo::bufferedFirstTs_l(bool isPendingList) const
{
    uint32_t index = mCurrentIndex;

    if (isPendingList)
        index = !mCurrentIndex;
    ASSERT(index<2);

    if ( mBufferList[index].size() == 0 ) {
        return -1;
    }

    return mBufferList[index].front()->dts();
}
int64_t AVDemuxer::StreamInfo::bufferedLastTs(bool isPendingList) const
{
    MMAutoLock locker(mLock);
    return bufferedLastTs_l(isPendingList);
}
int64_t AVDemuxer::StreamInfo::bufferedLastTs_l(bool isPendingList) const
{
    uint32_t index = mCurrentIndex;

    if (isPendingList)
        index = !mCurrentIndex;
    ASSERT(index<2);

    if ( mBufferList[index].size() == 0 ) {
        return -1;
    }

    return mBufferList[index].back()->dts();
}

bool AVDemuxer::StreamInfo::removeUntilTs(int64_t ts, int64_t * realTs)
{
    MMAutoLock locker(mLock);
    MMLOGD("media: %d, before remove, size: %u, req ts: %" PRId64 "\n", mMediaType, mBufferList[mCurrentIndex].size(), ts);
    MMASSERT(!mBufferList[mCurrentIndex].empty() );
    if ( MM_UNLIKELY(mBufferList[mCurrentIndex].empty()) ) {
        MMLOGE("no data to remove\n");
        return false;
    }

    if ( ts > bufferedLastTs_l() ) {
        mBufferList[mCurrentIndex].clear();
        MMLOGV("media: %d, req larger than last\n");
        return false;
    }

    if ( mMediaType == kMediaTypeVideo ) {
        MMLOGD("removing to previous-nearest-key frame");
        // find previous-nearest-key frame
        MediaBufferSP buf;
        BufferList::iterator preKeyPkt = mBufferList[mCurrentIndex].begin();
        BufferList::iterator it = mBufferList[mCurrentIndex].end();
        while (it!= mBufferList[mCurrentIndex].begin()) {
            buf = *it;
            if (buf->isFlagSet(MediaBuffer::MBFT_KeyFrame) && buf->dts() <= ts) {
                preKeyPkt = it;
                break;
            }
            it--;
        }

        if (it == mBufferList[mCurrentIndex].begin()) {
            MMLOGI("buffer has no key frame ahead of seek time %" PRId64 "", ts);
            return false;
        }

        // remove until previous-nearest-key frame
        mBufferList[mCurrentIndex].erase(mBufferList[mCurrentIndex].begin(), preKeyPkt);
        //update realTs
        buf = mBufferList[mCurrentIndex].front();
        *realTs = buf->dts();
    } else {
        while ( !mBufferList[mCurrentIndex].empty() ) {
            if ( ts <= mBufferList[mCurrentIndex].front()->dts() ) {
                *realTs = mBufferList[mCurrentIndex].front()->dts();
                break;
            }
            mBufferList[mCurrentIndex].pop_front();
        }
   }

    if ( mBufferList[mCurrentIndex].empty() ) {
        MMLOGV("media: %d, after remove, no data\n");
        return false;
    }
    MMLOGD("media: %d, after remove, size: %u, req ts: %" PRId64 ", realts: %" PRId64 "\n", mMediaType, mBufferList[mCurrentIndex].size(), ts, *realTs);
    return true;
}

mm_status_t AVDemuxer::StreamInfo::read(MediaBufferSP & buffer)
{
    MMAutoLock locker(mLock);
    if (mMediaType == kMediaTypeSubtitle) {
        if (mBufferList[0].empty()) {
            MMLOGE("no more\n");
            return MM_ERROR_EOS;
        }
        buffer = mBufferList[0].front();
        mBufferList[0].pop_front();
        return MM_ERROR_SUCCESS;
    }
    if ( mBufferList[mCurrentIndex].size() == 0 ) {
        MMLOGV("no more\n");
        return MM_ERROR_EOS;
    }

    MMLOGV("write_buffer_size_%d: (current, %d), (pending, %d)\n",
        mMediaType, mBufferList[mCurrentIndex].size(), mBufferList[!mCurrentIndex].size());
    if (mSelectedStreamPending != WANT_TRACK_NONE ) {
        // FIXME, how about immediate seek after select track?
        if (bufferedFirstTs_l(true) > 0  && bufferedFirstTs_l(true) <= bufferedFirstTs_l()) {
            mBufferList[mCurrentIndex].clear();
            mCurrentIndex = !mCurrentIndex;
            INFO("update_stream_index from mSelectedStream: %d to mSelectedStreamPending: %d",
                mSelectedStream, mSelectedStreamPending);
            mSelectedStream = mSelectedStreamPending;
            mSelectedStreamPending = WANT_TRACK_NONE;
            // there is no good place to clean up mStreamIdx2Info after track switch
            // so we keep all tracks in mStreamIdx2Info and filter out uninterested buffer in checkPacketWritable()
        }
        ASSERT(mBufferList[mCurrentIndex].size());
    }

    buffer = mBufferList[mCurrentIndex].front();
    mBufferList[mCurrentIndex].pop_front();

    // set mTargetTimeUs on mBufferList.front(), in case checkBufferSeek() is true
    if ((mTargetTimeUs > 0) && (buffer->pts() < mTargetTimeUs)) {
        int64_t time = mTargetTimeUs;
        if (mStartTimeUs != 0)
            time -= mStartTimeUs;
        buffer->getMediaMeta()->setInt64(MEDIA_ATTR_TARGET_TIME, time);
        MMLOGD("set TargetTimeUs %0.3f\n", time/1000000.0f);
        mTargetTimeUs = SEEK_NONE;
    }

    return MM_ERROR_SUCCESS;
}

bool AVDemuxer::StreamInfo::write(MediaBufferSP buffer, int streamIndex)
{
    MMAutoLock locker(mLock);
    if (mMediaType == kMediaTypeSubtitle) {
        mBufferList[0].push_back(buffer);
        return true;
    }
    MMLOGV("write_buffer_size_%d: (current, %d), (pending, %d)\n",
        mMediaType, mBufferList[mCurrentIndex].size(), mBufferList[!mCurrentIndex].size());

    uint32_t index = -1;
    if (streamIndex == mSelectedStream)
        index = mCurrentIndex;
    else if (streamIndex == mSelectedStreamPending) {
        if (!mBeginWritePendingList) {
            if (buffer->isFlagSet(MediaBuffer::MBFT_KeyFrame) && buffer->dts() >= bufferedFirstTs_l()) {
                // during track switch, begin to cache new track data from a future key frame
                mBeginWritePendingList = true;
            } else {
                return true;
            }
        }
        index = !mCurrentIndex;
    } else
        ASSERT(0 && "internal error, incorrect streamIndex");

    mBufferList[index].push_back(buffer);
    return true;
}


AVDemuxer::AVDemuxReader::AVDemuxReader(AVDemuxer * component, StreamInfo * si)
                    : mComponent(component),
                    mStreamInfo(si)
{
    FUNC_ENTER();
#ifdef DUMP_OUTPUT
#define OPENF(_f, _path) do {\
    _f = fopen(_path, "w");\
    if ( !_f ) {\
        MMLOGW("failed to open file for dump\n");\
    }\
}while(0)

    OPENF(mOutputDumpFpAudio, "/data/avdoa.bin");
    OPENF(mOutputDumpFpVideo, "/data/avdov.bin");
#endif
    FUNC_LEAVE();
}

AVDemuxer::AVDemuxReader::~AVDemuxReader()
{
    FUNC_ENTER();
#ifdef DUMP_OUTPUT
#define RELEASEF(_f) do {\
    if ( _f ) {\
        fclose(_f);\
        _f = NULL;\
    }\
}while(0)
    RELEASEF(mOutputDumpFpAudio);
    RELEASEF(mOutputDumpFpVideo);
#endif
    FUNC_LEAVE();
}

mm_status_t AVDemuxer::AVDemuxReader::read(MediaBufferSP & buffer)
{
    MMLOGV("+\n");
#ifdef DUMP_OUTPUT
    mm_status_t ret = mComponent->read(buffer, mStreamInfo);
    if ( ret == MM_ERROR_SUCCESS ) {
        FILE * fp;
        if ( mStreamInfo->mMediaType == kMediaTypeAudio ) {
            fp = mOutputDumpFpAudio;
        } else if ( mStreamInfo->mMediaType == kMediaTypeVideo ) {
            fp = mOutputDumpFpVideo;
        } else {
            fp = NULL;
        }
        if ( fp ) {
            if ( buffer->isFlagSet(MediaBuffer::MBFT_CodecData) ) {
                uint8_t * csdBuf;
                int64_t csdBufSize = buffer->size();
                buffer->getBufferInfo((uintptr_t*)&csdBuf, 0, 0, 1);
                if ( csdBuf && csdBufSize > 0 ) {
                    fwrite(csdBuf, 1, csdBufSize, fp);
                    fflush(fp);
                }
            } else if ( buffer->isFlagSet(MediaBuffer::MBFT_AVPacket) ) {
                uintptr_t buf;
                int64_t size = buffer->size();
                buffer->getBufferInfo((uintptr_t*)&buf, 0, 0, 1);
                if ( buf && size > 0 ) {
                    fwrite((unsigned char *)buf, 1, size, fp);
                    fflush(fp);
                }
            } else {
                MMLOGW("invalid outgoing data\n");
            }
        } else {
            MMLOGE("no fp to dump\n");
        }
    }
    return ret;
#else
    return mComponent->read(buffer, mStreamInfo);
#endif
}

MediaMetaSP AVDemuxer::AVDemuxReader::getMetaData()
{
    return mStreamInfo->mMetaData;
}


AVDemuxer::ReadThread::ReadThread(AVDemuxer * demuxer) : MMThread(MMTHREAD_NAME),
                                mDemuxer(demuxer),
                                mContinue(false),
                                mCorded(true)
{
    FUNC_ENTER();
    sem_init(&mSem, 0, 0);
    FUNC_LEAVE();
}

AVDemuxer::ReadThread::~ReadThread()
{
    FUNC_ENTER();
    sem_destroy(&mSem);
    FUNC_LEAVE();
}

mm_status_t AVDemuxer::ReadThread::prepare()
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

mm_status_t AVDemuxer::ReadThread::reset()
{
    FUNC_ENTER();
    mContinue = false;
    sem_post(&mSem);
    destroy();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::ReadThread::cork(int ck)
{
    FUNC_ENTER();
    mCorded = ck;

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::ReadThread::read()
{
    MMLOGV("+\n");
    sem_post(&mSem);
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void AVDemuxer::ReadThread::main()
{
    MMLOGI("started\n");
    while ( MM_LIKELY(mContinue) ) {
        MMLOGI("waitting sem\n");
        sem_wait(&mSem);
        if ( MM_UNLIKELY(!mContinue) ) {
            MMLOGI("not conitnue\n");
            break;
        }

        mm_status_t ret = -1;
        MMLOGD("resume reading to get more data");
        while ( MM_LIKELY(mContinue) ) {
            mDemuxer->checkSeek();

            if ( MM_UNLIKELY(mCorded) ) {
                MMLOGI("mCorded\n");
                break;
            }

            MMLOGV("reading...\n");
            ret = mDemuxer->readFrame();
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


AVDemuxer::AVDemuxer() :
                MMMsgThread(MMMSGTHREAD_NAME),
                mState(STATE_NONE),
                mAVFormatContext(NULL),
                mAVInputFormat(NULL),
                mAVIOContext(NULL),
                mPrepareTimeout(PREPARE_TIMEOUT_DEFUALT),
                mSeekTimeout(SEEK_TIMEOUT_DEFAULT),
                mReadTimeout(READ_TIMEOUT_DEFAULT),
                mScaledPlayRate(SCALED_PLAY_RATE),
                mScaledThresholdRate(SCALED_PLAY_RATE * 2),
                mScaledPlayRateCur(SCALED_PLAY_RATE),
                mInterruptHandler(NULL),
                mEOF(false),
                mReportedBufferingPercent(REPORTED_PERCENT_NONE),
                mBufferState(kBufferStateNone),
                mSeekUs(SEEK_NONE),
                mCheckVideoKeyFrame(false),
                mReadThread(NULL),
                mFd(-1),
                mLength(-1),
                mOffset(-1),
                mBufferSeekExtra(0)
#ifdef DUMP_INPUT
                , mInputFile(NULL)
#endif
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

    mBufferingTime = getConfigBufferingTime();
    if (mBufferingTime <= 0) {
        mBufferingTime = BUFFERING_TIME_DEFAULT;
    } else {
        mBufferingTime *= (1000 * 1000);
    }
    mBufferingTimeHigh = mBufferingTime * BUFFER_HIGH_FACTOR;
    MMLOGV("BufferingTime: %" PRId64 ", BufferingTimeHigh: %" PRId64 "\n", mBufferingTime, mBufferingTimeHigh);

    FUNC_LEAVE();
}

AVDemuxer::~AVDemuxer()
{
    FUNC_ENTER();

#ifdef DUMP_INPUT
    encodedDataDump.dump(NULL, 0);
#endif

    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
    FUNC_LEAVE();
}

int AVDemuxer::getConfigBufferingTime()
{
    std::string result = mm_get_env_str(BUFFERING_TIME_CFG_KEY, BUFFERING_TIME_CFG_ENV);
    const char * bufferingTime = result.c_str();
    if (!bufferingTime || bufferingTime[0] == '\0') {
        MMLOGI("buffering time not configured\n");
        return -1;
    }

    MMLOGI("buffering time configured: %s\n", bufferingTime);
    return atoi(bufferingTime);
}

mm_status_t AVDemuxer::init()
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
            MMLOGE("failed to prepare si %d\n", i);
            SET_STATE(STATE_ERROR);
            return MM_ERROR_NO_MEM;
        }
    }

    mInterruptHandler = new InterruptHandler(this);
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

void AVDemuxer::uninit()
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

const std::list<std::string> & AVDemuxer::supportedProtocols() const
{
    return getSupportedProtocols();
}

/*static */const std::list<std::string> & AVDemuxer::getSupportedProtocols()
{
    static std::list<std::string> protocols;
    if (!protocols.empty()) {
        MMLOGE("no supported protocol\n");
        return protocols;
    }

    av_register_all();
    void* opq = NULL;
    const char* protocol;
    while ((protocol = avio_enum_protocols(&opq, 0))) {
        MMLOGV("supported protocol: %s\n", protocol);
        std::string str(protocol);
        protocols.push_back(str);
    }
    return protocols;
}

bool AVDemuxer::isSeekable()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    return isSeekableInternal();
}

mm_status_t AVDemuxer::getDuration(int64_t & durationMs)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    return duration(durationMs);
}

mm_status_t AVDemuxer::duration(int64_t & durationMs)
{
    if ( !mAVFormatContext ) {
        MMLOGE("not loaded\n");
        return MM_ERROR_IVALID_OPERATION;
    }

    if ( mAVFormatContext->duration == (int64_t)AV_NOPTS_VALUE ) {
        MMLOGI("AV_NOPTS_VALUE\n");
        return MM_ERROR_UNSUPPORTED;
    }

    MMLOGI("dur: %" PRId64 " ms\n", ( mAVFormatContext->duration + 500 )/1000);
    durationMs = ( mAVFormatContext->duration + 500 ) / 1000LL;
    return MM_ERROR_SUCCESS;
}

bool AVDemuxer::isSeekableInternal()
{
    if ( !mAVFormatContext ) {
        MMLOGV("not inited\n");
        return false;
    }

    if ( mAVFormatContext->duration == (int64_t)AV_NOPTS_VALUE ) {
        MMLOGV("duration not determined, false\n");
        return false;
    }

    if ( mAVFormatContext->pb && mAVFormatContext->pb->seekable ) {
        MMLOGV("pb exists, ret: %d\n", mAVFormatContext->pb->seekable);
        return mAVFormatContext->pb->seekable;
    }

    MMLOGV("read_seek: %p, read_seek2: %p\n", mAVFormatContext->iformat->read_seek, mAVFormatContext->iformat->read_seek2);
    return mAVFormatContext->iformat->read_seek || mAVFormatContext->iformat->read_seek2;
}

bool AVDemuxer::hasMedia(MediaType mediaType)
{
    MMAutoLock lock(mLock);
    return hasMediaInternal(mediaType);
}

bool AVDemuxer::hasMediaInternal(MediaType mediaType)
{
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return false;
    }

    MMLOGV("mediatype: %d\n", mStreamInfoArray[mediaType].mMediaType);
    return mStreamInfoArray[mediaType].mMediaType != kMediaTypeUnknown;
}

MediaMetaSP AVDemuxer::getMetaData()
{
    return mMetaData;
}

MMParamSP AVDemuxer::getTrackInfo()
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
            AVCodecContext *ctx = stream->codec;
            if (ctx && ctx->codec_id) {
                MMLOGV("has ctx: %p\n", ctx);
                width = ctx->width;
                height = ctx->height;
                codecId = AVCodecId2CodecId(ctx->codec_id);
                codecName = avcodec_descriptor_get(ctx->codec_id)->name;
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

mm_status_t AVDemuxer::selectTrack(MediaType mediaType, int index)
{
    MMLOGI("mediaType: %d, index: %d\n", mediaType, index);
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return MM_ERROR_INVALID_PARAM;
    }

    // MMAutoLock lock(mLock);
    // CHECK_STATE(STATE_PREPARED);
    mm_status_t status = selectTrackInternal(mediaType, index);
    if (status != MM_ERROR_SUCCESS)
        return status;

    if (mediaType == kMediaTypeVideo) {
        /*
         * 1. for m3u8/ts, it is assumed to change audio track as well after video track changes
         *     then audio/video tracks come from the same ts file, it reduce bandwidth consumption
         * 2. for mkv with multiple track, there is usually one video + multiple audio.
         *     then this branch is not reachable -- user has no way to change video track
         */
        WARNING("assume we select audio track with same index: %d", index);
        int audio_track_index = index;
    #if 0
        // basing on the above assumption, the final audio_track_index should be equal to (video track) index
        // the above selectTrackInternal has run succes; we needn't validata the input
        StreamInfo * si = &mStreamInfoArray[kMediaTypeVideo];
        int video_stream_index = si->mAllStreams[index];
        AVStream * stream = mAVFormatContext->streams[video_stream_index];
        int programId = stream->id;

        uint32_t i = 0;
        int audio_stream_index = -1;
        for (i=0; i<mAVFormatContext->nb_streams; i++) {
            AVStream * stream = mAVFormatContext->streams[i];
            if (stream->id == programId && stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
                audio_stream_index = i;
        }

        si = &mStreamInfoArray[kMediaTypeAudio];
        for (i=0; i<si->mAllStreams.size(); i++) {
            if (si->mAllStreams[i] == audio_stream_index)
                audio_track_index = i;

        }
    #endif
        selectTrackInternal(kMediaTypeAudio, audio_track_index);
    }

    return status;
}

mm_status_t AVDemuxer::selectTrackInternal(MediaType mediaType, int index)
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


    if (si->shortCircuitSelectTrack(index)) {
        MMLOGI("short circuit select track", mediaType, index);
        return MM_ERROR_SUCCESS;
    }

    int id = si->mAllStreams[index];
    AVStream * stream = mAVFormatContext->streams[id];
    stream->discard = AVDISCARD_DEFAULT;
    memcpy(&si->mTimeBase, &stream->time_base, sizeof(AVRational));
    si->mHasAttachedPic = stream->disposition & AV_DISPOSITION_ATTACHED_PIC ? true : false;
    MMLOGI("mediaType: %d, index: %d, id: %d, info: %p, HasAttachedPic: %d, num: %d, den: %d\n", mediaType, index, id, si, si->mHasAttachedPic, si->mTimeBase.num, si->mTimeBase.den);

    AVCodecContext * codecContext = mAVFormatContext->streams[id]->codec;
    MMLOGI("codecContext: codec_type: %d, codec_id: 0x%x, codec_tag: 0x%x, stream_codec_tag: 0x%x, profile: %d, width: %d, height: %d, extradata: %p, extradata_size: %d, channels: %d, sample_rate: %d, channel_layout: %" PRId64 ", bit_rate: %d, block_align: %d, avg_frame_rate: (%d, %d)\n",
                    codecContext->codec_type,
                    codecContext->codec_id,
                    codecContext->codec_tag,
                    codecContext->stream_codec_tag,
                    codecContext->profile,
                    codecContext->width,
                    codecContext->height,
                    codecContext->extradata,
                    codecContext->extradata_size,
                    codecContext->channels,
                    codecContext->sample_rate,
                    codecContext->channel_layout,
                    codecContext->bit_rate,
                    codecContext->block_align,
                    mAVFormatContext->streams[id]->avg_frame_rate.num,
                    mAVFormatContext->streams[id]->avg_frame_rate.den);

#ifdef __USING_SYSCAP_MM__
    VideoSysCapabilitySP cap = VideoSysCapability::create();
    ASSERT(cap);
    int32_t maxWidth = cap->getMaxWidthSupportedByPlayer();
    int32_t maxHeight = cap->getMaxHeightSupportedByPlayer();
    DEBUG("maxWidth %d, maxHeight %d", maxWidth, maxHeight);
#else
#if defined(MM_VIDEO_MAX_WIDTH) && defined(MM_VIDEO_MAX_HEIGHT)
    int32_t maxWidth = MM_VIDEO_MAX_WIDTH;
    int32_t maxHeight = MM_VIDEO_MAX_HEIGHT;
#endif
#endif
    if (maxWidth > 0 && maxHeight > 0) {
        if (codecContext->width > maxWidth ||
            codecContext->height > maxHeight) {
            MMLOGE("unsupported %dx%d", codecContext->width, codecContext->height);
            return MM_ERROR_UNSUPPORTED;
        }
    }

    si->mCodecId = AVCodecId2CodecId(codecContext->codec_id);
    DEBUG("mCodecId %d", si->mCodecId);
    if ( mediaType == kMediaTypeVideo ) {
        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            si->mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        AVRational * avr = &mAVFormatContext->streams[id]->avg_frame_rate;
        if ( avr->num > 0 && avr->den > 0 ) {
            MMLOGD("has avg fps: %d\n", avr->num / avr->den);
            si->mMetaData->setInt32(MEDIA_ATTR_AVG_FRAMERATE, avr->num / avr->den);
        }
        si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);

        si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        si->mMetaData->setInt32(MEDIA_ATTR_WIDTH, codecContext->width);
        si->mMetaData->setInt32(MEDIA_ATTR_HEIGHT, codecContext->height);
        si->mMetaData->setInt32(MEDIA_ATTR_HEIGHT, codecContext->height);
        if (mAVFormatContext->iformat && mAVFormatContext->iformat->name)
            si->mMetaData->setString(MEDIA_ATTR_CONTAINER, mAVFormatContext->iformat->name);

        //set threshold rate according to dimesion
        if (codecContext->width > 1280 && codecContext->height > 720) {
            mScaledThresholdRate = SCALED_PLAY_RATE * 2;
        } else if (codecContext->width > 640 && codecContext->height > 480) {
            mScaledThresholdRate = SCALED_PLAY_RATE * 3;
        } else if (codecContext->width > 0 && codecContext->height > 0){
            mScaledThresholdRate = SCALED_PLAY_RATE * 4;
        }
        MMLOGD("width %d, height %d, mScaledThresholdRate %d\n", codecContext->width, codecContext->height, mScaledThresholdRate);


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
        mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            si->mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);
        si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        si->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, codecContext->sample_fmt);
        si->mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, codecContext->sample_rate);
        si->mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, codecContext->channels);
        si->mMetaData->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, codecContext->channel_layout);
        si->mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        si->mMetaData->setInt32(MEDIA_ATTR_BLOCK_ALIGN, codecContext->block_align);
        int64_t durationMs;
        if ( duration(durationMs) == MM_ERROR_SUCCESS )
            si->mMetaData->setInt64(MEDIA_ATTR_DURATION, durationMs);

        if (mAVInputFormat == av_find_input_format("aac") ||
           mAVInputFormat == av_find_input_format("mpegts") ||
           mAVInputFormat == av_find_input_format("hls,applehttp")) {
           //Set kKeyIsADTS to ture as default as mpegts2Extractor/aacExtractor did
           si->mMetaData->setInt32(MEDIA_ATTR_IS_ADTS, 1);
        }
    }

    si->mMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    si->mMetaData->setPointer(MEDIA_ATTR_CODEC_CONTEXT, codecContext);
    si->mMetaData->setPointer(MEDIA_ATTR_CODEC_CONTEXT_MUTEX, &mAVLock);
    si->mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
    si->mMetaData->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
    si->mMetaData->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
    si->mMetaData->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);

    NOTIFY(kEventInfo, kEventMetaDataUpdate, 0, nilParam);

    return MM_ERROR_SUCCESS;
}

void AVDemuxer::checkHighWater(int64_t readCosts, int64_t dur)
{
    int64_t allCosts = (readCosts * mBufferingTimeHigh) / dur;
    if ( MM_LIKELY(allCosts <= mBufferingTimeHigh) ) {
        /*MMLOGV("Hightwater %" PRId64 " not need change, readCosts: %" PRId64 ", dur: %" PRId64 "\n",
            mBufferingTimeHigh,
            readCosts,
            dur);*/
        return;
    }

    if ( allCosts > MAX_BUFFER_TIME ) {
        MMLOGW("read too slow, %" PRId64 ", readCosts: %" PRId64 ", dur: %" PRId64 "\n",
            mBufferingTimeHigh,
            readCosts,
            dur);
        return;
    }

    mBufferingTimeHigh = allCosts + 1000000;
    MMLOGI("Hightwater set to %" PRId64 ", readCosts: %" PRId64 ", dur: %" PRId64 "\n",
        mBufferingTimeHigh,
        readCosts,
        dur);
}

int AVDemuxer::getSelectedTrack(MediaType mediaType)
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

BEGIN_MSG_LOOP(AVDemuxer)
    MSG_ITEM(MSG_PREPARE, onPrepare)
END_MSG_LOOP()


void AVDemuxer::resetInternal()
{
    EXIT_TMHANDLER(true);
    mReadThread->reset();
    releaseContext();

    mUri = "";

    mEOF = false;
    mScaledThresholdRate = SCALED_PLAY_RATE * 2;
    mScaledPlayRate = SCALED_PLAY_RATE;

    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        mStreamInfoArray[i].reset();
    }
    mStreamIdx2Info.clear();
    mReportedBufferingPercent = REPORTED_PERCENT_NONE;
    SET_BUFFERING_STATE(kBufferStateNone);
    mMetaData->clear();
    MMAutoLock lock2(mBufferLock);
    while (!mSeekSequence.empty()) {
        mSeekSequence.pop();
    }
    mSeekUs = SEEK_NONE;
    mDownloadPath.clear();

    SET_STATE(STATE_IDLE);
}

Component::ReaderSP AVDemuxer::getReader(MediaType mediaType)
{
    MMAutoLock lock(mLock);
    if ( mediaType >= kMediaTypeCount || mediaType <= kMediaTypeUnknown ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return Component::ReaderSP((Component::Reader*)NULL);
    }

    StreamInfo * si = &mStreamInfoArray[mediaType];
    si->mPeerInstalled = true;
    si->mStartTimeUs = startTimeUs();

    return ReaderSP(new AVDemuxReader(this, si));
}

mm_status_t AVDemuxer::read(MediaBufferSP & buffer, StreamInfo * si)
{
    if ( !si ) {
        MMLOGE("invalid params\n");
        return MM_ERROR_INVALID_PARAM;
    }

    MMAutoLock lock(mBufferLock);
    if ( mBufferState == kBufferStateBuffering ) {
        MMLOGV("buffering\n");
        return MM_ERROR_AGAIN;
    }

    mm_status_t ret = si->read(buffer);
    MMLOGV("read_%d, ret: %d\n", si->mMediaType, ret);

    if ( MM_UNLIKELY(mBufferState == kBufferStateEOS) ) {
        MMLOGI("read_%d, eos, ret: %d\n", si->mMediaType, ret);
        return ret;
    }

    if ( MM_UNLIKELY(ret != MM_ERROR_SUCCESS) ) {
        MMLOGV("not success\n");
        return MM_ERROR_AGAIN;
    }

    int64_t min = INT64_MAX;
    if ( MM_UNLIKELY(!checkBuffer(min)) ) {
        MMLOGI("%d_not check buffer\n", si->mMediaType);
        mReadThread->read();
        return MM_ERROR_SUCCESS;
    }

    if ( MM_UNLIKELY(min <= 0) ) {
        SET_BUFFERING_STATE(kBufferStateBuffering);
        mReadThread->read();
        return MM_ERROR_SUCCESS;
    }

    if ( min < mBufferingTimeHigh / 2 && mBufferState != kBufferStateNormal ) {
        MMLOGD("%d, need start read: min: %" PRId64 ", buffering time high: %" PRId64 "\n", si->mMediaType, min, mBufferingTimeHigh);
        SET_BUFFERING_STATE(kBufferStateNormal);
        mReadThread->read();
        return MM_ERROR_SUCCESS;
    }

    return MM_ERROR_SUCCESS;
}


mm_status_t AVDemuxer::setParameter(const MediaMetaSP & meta)
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    SETPARAM_BEGIN()
        SETPARAM_I32(PARAM_KEY_PREPARE_TIMEOUT, mPrepareTimeout)
        SETPARAM_I32(PARAM_KEY_READ_TIMEOUT, mReadTimeout)
        SETPARAM_I64(PARAM_KEY_BUFFERING_TIME, mBufferingTime)
        SETPARAM_I32(MEDIA_ATTR_PALY_RATE, mScaledPlayRateCur)
        SETPARAM_STRING(MEDIA_ATTR_FILE_DOWNLOAD_PATH, mDownloadPath)
    SETPARAM_END()
    mBufferingTimeHigh = mBufferingTime * BUFFER_HIGH_FACTOR;

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::getParameter(MediaMetaSP & meta) const
{
    meta.reset();
    meta = mMetaData;

    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::setUri(const char * uri,
                            const std::map<std::string, std::string> * headers/* = NULL*/)
{
    MMLOGI("uri: %s\n", uri);
    MMAutoLock lock(mLock);
    CHECK_STATE(STATE_IDLE);
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
    if (uri) {
        if (!strncasecmp(uri, "page://", 7)) {
#ifdef USE_RESMANAGER
            yunos::FilePath filePath = yunos::ResManager::searchFile(yunos::Uri(uri));
            yunos::String path = filePath.toString();
#else
            yunos::String path = yunos::ResourceLocator::getInstance()->search(yunos::Uri(uri));
#endif
            if (path.size() == 0) {
                MMLOGE("failed to locate uri: %s\n", uri);
                return MM_ERROR_INVALID_PARAM;
            }
            MMLOGI("abs uri: %s\n", path.c_str());
            mUri = path.c_str();
            FUNC_LEAVE();
            return MM_ERROR_SUCCESS;
        }
        if (!strncasecmp(uri, "http://", 7) ||!strncasecmp(uri, "https://", 8)) {
            // add extra time of buffer seek for internet streaming
            mBufferSeekExtra = MAX_BUFFER_SEEK;
            if (!mDownloadPath.empty()) {
                mm_set_env_str(NULL, "MM_DOWNLOAD_FILE", NULL);
                if (access(mDownloadPath.c_str(), F_OK) == 0) {
                    INFO("%s already downloaded!", mDownloadPath.c_str());
                    mUri = mDownloadPath.c_str();
                    FUNC_LEAVE();
                    return MM_ERROR_SUCCESS;
                } else {
                    mm_set_env_str(NULL, "MM_DOWNLOAD_FILE", mDownloadPath.c_str());
                }
            }
        }
    }
#endif
    mUri = uri;
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::setUri(int fd, int64_t offset, int64_t length)
{
    mFd = dup(fd);
    mOffset = offset;
    mLength = length;

    int64_t pos1 = lseek(fd, mOffset, SEEK_SET);
    MMLOGI("fd %d, offset %" PRId64 " length %" PRId64 ", pos1 %" PRId64 "\n",
        fd, offset, length, pos1);
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::prepare()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_PREPARING, MM_ERROR_ASYNC);
    RET_IF_ALREADY_STATE(STATE_PREPARED, MM_ERROR_SUCCESS);
    CHECK_STATE(STATE_IDLE);

    if ( mUri.empty() && (mFd == -1)) {
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

void AVDemuxer::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    MMASSERT(rspId == 0);
    MMAutoLock lock(mLock);
    MMASSERT(mState == STATE_PREPARING);

    EXIT_TMHANDLER(false);
    mm_status_t ret = createContext();
    if ( ret !=  MM_ERROR_SUCCESS) {
        MMLOGE("failed to find stream info: %d\n", ret);
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
        AVMediaType type = s->codec->codec_type;
        MMLOGV("steramid: %d, codec_type: %d, start_time: %" PRId64 ", duration: %" PRId64 ", nb_frames: %" PRId64 "\n",
            i,
            s->codec->codec_type,
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
                    mMetaData->setInt32(MEDIA_ATTR_ATTACHEDPIC_CODECID, (int32_t)s->codec->codec_id);
                    mMetaData->setString(MEDIA_ATTR_ATTACHEDPIC_MIME, codecId2Mime((CowCodecID)s->codec->codec_id));
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
                audioCodecId = s->codec->codec_id;
                MMLOGI("found audio stream(%d)\n", i);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                si = &mStreamInfoArray[kMediaTypeSubtitle];
                si->mMediaType = kMediaTypeSubtitle;
                si->mNotCheckBuffering = true;
                MMLOGI("found subtitle stream(%d), set not check buffering\n", i);
                break;
            default:
                MMLOGI("not supported mediatype: %d\n", type);
                continue;
        }

        si->mAllStreams.push_back(i);
        mStreamIdx2Info.insert(std::pair<int, StreamInfo*>(i, si));
    }

    if ( !hasAudio && !hasVideo ) {
        MMLOGE("no streams for play\n");
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

    int64_t durationMs;
    if ( duration(durationMs) == MM_ERROR_SUCCESS )
        mMetaData->setInt64(MEDIA_ATTR_DURATION, durationMs);
    else
        durationMs = -1;

    mReportedBufferingPercent = REPORTED_PERCENT_NONE;
    SET_BUFFERING_STATE(kBufferStateBuffering);
    SET_STATE(STATE_PREPARED);
    NOTIFY(kEventInfo, kEventInfoSeekable, isSeekableInternal(), nilParam);
    // FIXME, int(durationMs)
    NOTIFY(kEventInfoDuration, int(durationMs), 0, nilParam);
    NOTIFY(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    // disable all track by default, then we can enable the selected track in selectTrackInternal
    for (uint32_t i = 0; i < mAVFormatContext->nb_streams; i++)
        mAVFormatContext->streams[i]->discard = AVDISCARD_ALL;

    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        StreamInfo * si = &mStreamInfoArray[i];
        if ( si->mMediaType != kMediaTypeUnknown ) {
            mm_status_t status = selectTrackInternal((MediaType)i, WANT_TRACK_INDEX_DEFAULT);
            if (status != MM_ERROR_SUCCESS) {
                NOTIFY(kEventPrepareResult, status, 0, nilParam);
                return;
            }
        }
    }

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

    FUNC_LEAVE();
}

mm_status_t AVDemuxer::createContext()
{
    FUNC_ENTER();
    MMASSERT(mAVFormatContext == NULL);
    MMASSERT(mAVIOContext == NULL);

    if (mUri.empty()) {
        MMLOGD("mUri is NULL\n");
        mAVFormatContext = avformat_alloc_context();
        if ( !mAVFormatContext ) {
            MMLOGE("failed to create avcontext\n");
            return MM_ERROR_INVALID_PARAM;
        }

        unsigned char * ioBuf = (unsigned char*)av_malloc(AVIO_BUFFER_SIZE);
        if ( !ioBuf ) {
            MMLOGE("no mem\n");
            avformat_free_context(mAVFormatContext);
            mAVFormatContext = NULL;
            return MM_ERROR_NO_MEM;
        }

        mAVIOContext = avio_alloc_context(ioBuf,
                        AVIO_BUFFER_SIZE,
                        0,
                        this,
                        avRead,
                        NULL,
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
    }

    if (!mAVFormatContext) {
        mAVFormatContext = avformat_alloc_context();
        if ( !mAVFormatContext ) {
            MMLOGE("failed to create avcontext\n");
            return MM_ERROR_INVALID_PARAM;
        }
    }

    mAVFormatContext->interrupt_callback = *mInterruptHandler;

    mInterruptHandler->start(mPrepareTimeout);
    const char *path = mUri.empty() ? NULL :  mUri.c_str();
    DEBUG("url: %s", PRINTABLE_STR(path));

    AVDictionary *options = NULL; //not used for now
    if (path && (!strncasecmp(path, "http://", 7) ||!strncasecmp(path, "https://", 8))) {
        useProxyFromConnectivity();
        // set network read/write timeout, refer to libavformat/tcp.c for detail info
        // av_dict_set(&options, "timeout", "1000000", 0);
    } else {
        mBufferingTime = BUFFERING_TIME_LOCAL;
    }

    mLock.release();
    int ret = avformat_open_input(&mAVFormatContext, path, NULL, &options);
    mLock.acquire();
    if (options) {
        av_dict_free(&options);
    }
    if ( ret < 0 ) {
        MMLOGE("failed to open input: %d(%s)\n", ret, strerror(-ret));
        mInterruptHandler->end();
        SET_STATE(STATE_IDLE);
        return ret;
    }


    if (mAVFormatContext->iformat && mAVFormatContext->iformat->name)
        DEBUG("av input format name %s", mAVFormatContext->iformat->name);

    mAVFormatContext->flags |= AVFMT_FLAG_GENPTS;
    if (mUri.empty()) {
        mAVFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
    }

    MMLOGV("finding stream info\n");
    ret = avformat_find_stream_info(mAVFormatContext, NULL);
    mInterruptHandler->end();
    if ( ret < 0 ) {
        MMLOGE("failed to find stream info: %d\n", ret);
        return ret;
    }

    return MM_ERROR_SUCCESS;
}

void AVDemuxer::releaseContext()
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


mm_status_t AVDemuxer::start()
{
    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_STARTED, MM_ERROR_SUCCESS);
    if ( mState != STATE_PREPARED/* &&
        mState != STATE_PAUSED*/ ) {
        MMLOGE("invalid sate(%d)\n", mState);
        return MM_ERROR_INVALID_STATE;
    }

    EXIT_TMHANDLER(false);
    if ( mState == STATE_PREPARED )
        mCheckVideoKeyFrame = hasMediaInternal(kMediaTypeVideo);
    MMAutoLock lock2(mBufferLock);
    mReadThread->cork(0);
    SET_STATE(STATE_STARTED);
    mReadThread->read();
    MMLOGI("success\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::stop()
{
    FUNC_ENTER();
    WAIT_PREPARE_OVER();

    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_PREPARED, MM_ERROR_SUCCESS);

    if ( mState != STATE_STARTED/* &&
        mState != STATE_PAUSED*/) {
        MMLOGE("invalid sate(%d)\n", mState);
        return MM_ERROR_INVALID_STATE;
    }

    stopInternal();
    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

void AVDemuxer::stopInternal()
{
    FUNC_ENTER();
    if ( !mReadThread ) {
        MMLOGV("not started\n");
        return;
    }

    EXIT_TMHANDLER(true);

    mReadThread->cork(1);
    flush();
    MMAutoLock lock2(mBufferLock);
    while (!mSeekSequence.empty()) {
        mSeekSequence.pop();
    }
    mSeekUs = 0;
    SeekSequence sequence = {0, 1};
    mSeekSequence.push(sequence);
    SET_STATE(STATE_PREPARED);
    FUNC_LEAVE();
}

mm_status_t AVDemuxer::pause()
{
/*    FUNC_ENTER();
    MMAutoLock lock(mLock);
    RET_IF_ALREADY_STATE(STATE_PAUSED, MM_ERROR_SUCCESS);
    if ( mState != STATE_STARTED ) {
        MMLOGE("invalid sate(%d)\n", mState);
        return MM_ERROR_INVALID_STATE;
    }

    EXIT_TMHANDLER(true);

    mReadThread->cork(1);
    SET_STATE(STATE_PAUSED);
    FUNC_LEAVE();*/
    MMLOGV("ignore\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::resume()
{
/*    FUNC_ENTER();
    return start();*/
    MMLOGV("ignore\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t AVDemuxer::seek(int msec, int seekSequence)
{
    FUNC_ENTER();
    MMLOGV("pos: %d\n", msec);
    if ( msec < 0 ) {
        MMLOGE("invalid pos: %d\n", msec);
        return MM_ERROR_INVALID_PARAM;
    }

    MMAutoLock lock(mLock);
    if ( mState < STATE_PREPARED ) {
        MMLOGE("invalid sate(%d)\n", mState);
        return MM_ERROR_INVALID_STATE;
    }

    if ( !isSeekableInternal() ) {
        MMLOGE("not seekable\n");
        return MM_ERROR_IVALID_OPERATION;
    }

    int64_t usec = msec * 1000LL;
    if ( usec > durationUs() ) {
        if ((usec - durationUs()) > 500) {
            //this case will avoid in cowplayer seek.
            MMLOGE("invalid pos: %d, starttime: %" PRId64 ", dur: %" PRId64 "\n", msec, startTimeUs(), durationUs());
        }
        //seek this position to show the last frame
        usec = durationUs() - 1000;
    }

    // if we just 'notify' the latest seek, std::queue isn't necessary for mSeekSequence
    MMAutoLock lock2(mBufferLock);
    mSeekUs = usec + startTimeUs();
    SeekSequence sequence = {(uint32_t)seekSequence, 0};
    mSeekSequence.push(sequence);
    MMLOGI("req pos: %" PRId64 " -> %" PRId64 "\n", usec, mSeekUs);

    // signal av_read_frame first.
    // av_read_frame will cost some time in bad network
    EXIT_TMHANDLER(true);

    mReadThread->read();

    FUNC_LEAVE();
    return MM_ERROR_ASYNC;
}

bool AVDemuxer::setTargetTimeUs(int64_t seekTimeUs)
{
    if (hasMediaInternal(kMediaTypeVideo)) {
        mStreamInfoArray[kMediaTypeVideo].mTargetTimeUs = seekTimeUs;
    }
    if (hasMediaInternal(kMediaTypeAudio)) {
        mStreamInfoArray[kMediaTypeAudio].mTargetTimeUs = seekTimeUs;
    }
    return true;
}

bool AVDemuxer::checkBufferSeek(int64_t seekUs)
{
    MMASSERT((int64_t)AV_NOPTS_VALUE < 0);
    int i;
    int videoIndex = -1;
    int audioIndex = -1;
    for ( i = 0; i < kMediaTypeCount; ++i ) {
        StreamInfo * si = &mStreamInfoArray[i];
        if ( si->mMediaType == kMediaTypeUnknown ||
#ifndef __MM_YUNOS_YUNHAL_BUILD__
            //FIXME: one invalid PTS/AV_NOPTS_VALUE make us not able to use av_seek_frame() to seek
            // see the bailongma.mp3
            si->mNotCheckBuffering ||
#endif
            !si->mPeerInstalled ||
            ( (mScaledPlayRate != SCALED_PLAY_RATE) && (i == kMediaTypeAudio))) {
            MMLOGV("ignore: mediatype: %d, notcheck: %d, perrinstalled: %d, mScaledPlayRate %d\n",
                si->mMediaType, si->mNotCheckBuffering, si->mPeerInstalled, mScaledPlayRate);
            continue;
        }

        int64_t firstTs = si->bufferedFirstTs();
        if ( firstTs < 0 ) {
            MMLOGV("buffer seek not enough, mediatype: %d, firstTs: %" PRId64 "\n", si->mMediaType, firstTs);
            return false;
        }

        int64_t lastTs = si->bufferedLastTs();
        if ( seekUs < firstTs || seekUs > lastTs + mBufferSeekExtra ) {
            MMLOGV("buffer seek not enough, mediatype: %d, firstTs: %" PRId64 ", lastTs: %" PRId64 ", reqto: %" PRId64 "\n", si->mMediaType, firstTs, lastTs, seekUs);
            return false;
        }

        if ( si->mMediaType == kMediaTypeVideo )
            videoIndex = i;
        else if ( si->mMediaType == kMediaTypeAudio )
            audioIndex = i;
    }

    int64_t realSeekTs = 0;
    bool checkVideo = true;
    if ( videoIndex >= 0 && (checkVideo = mStreamInfoArray[videoIndex].removeUntilTs(seekUs, &realSeekTs)) ) {
        if ( audioIndex >= 0 ) {
            mStreamInfoArray[audioIndex].removeUntilTs(realSeekTs, &realSeekTs);
        }
    } else {
        if ( audioIndex >= 0 ) {
            mStreamInfoArray[audioIndex].removeUntilTs(seekUs, &realSeekTs);
        }
    }

    if (!checkVideo) {
        MMLOGD("check video buffer fail");
        return false;
    }

    if ( videoIndex >= 0 ) {
        if ( mStreamInfoArray[videoIndex].bufferedFirstTs() >= 0 ) {
            mCheckVideoKeyFrame = false;
        } else {
            mCheckVideoKeyFrame = true;
        }
    } else {
        mCheckVideoKeyFrame = false;
    }

    int64_t min;
    if ( checkBuffer(min) && min < mBufferingTime ) {
        SET_BUFFERING_STATE(kBufferStateBuffering);
    } else {
        SET_BUFFERING_STATE(kBufferStateNormal);
    }

    return true;
}

void AVDemuxer::checkSeek()
{
    int64_t seekUs = SEEK_NONE ;
    std::queue<SeekSequence> seekSequence;
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock lock2(mBufferLock);
    {
        if ( mSeekUs == SEEK_NONE ) {
            ASSERT(mSeekSequence.empty());
            return;
        }

        seekUs = mSeekUs;
        ASSERT(mSeekSequence.size());
        std::swap(seekSequence, mSeekSequence);
        mSeekUs = SEEK_NONE;
    }

    do { // make it easier to break to the end of func
        if ( checkBufferSeek(seekUs) ) {
            DEBUG("checkBufferSeek is ok: %" PRId64, seekUs);
            break;
        }
        flushInternal();

        MMLOGI("do seeking to mSeekUs: %" PRId64 ", seekUs: %" PRId64, mSeekUs, seekUs);
        mInterruptHandler->start(mSeekTimeout);
        int ret = av_seek_frame(mAVFormatContext, -1, seekUs, AVSEEK_FLAG_BACKWARD);
        mInterruptHandler->end();
        if ( ret < 0 ) {
            MMLOGW("seek failed, seek result: %d \n", ret);
            status = mInterruptHandler->isTimeout() ? MM_ERROR_TIMED_OUT : MM_ERROR_UNKNOWN;
            //If seek failed, don't reset mCheckVideoKeyFrame, just continue playing.
            mCheckVideoKeyFrame = false;
        }
    }while (0);

    while (seekSequence.size()) {
        MMParamSP mmparam;
        SeekSequence sequence = seekSequence.front();
        mmparam.reset(new MMParam);
        mmparam->writeInt32(sequence.index);
        seekSequence.pop();
        if (!sequence.internal)
            NOTIFY(kEventSeekComplete, status, 0, mmparam);
    }

    setTargetTimeUs(seekUs);
    SET_BUFFERING_STATE(kBufferStateBuffering);
}

int64_t AVDemuxer::startTimeUs()
{
    if (!mAVFormatContext || mAVFormatContext->start_time == (int64_t)AV_NOPTS_VALUE)
        return 0;
    return mAVFormatContext->start_time;
}

int64_t AVDemuxer::durationUs()
{
    if (!mAVFormatContext || mAVFormatContext->duration == (int64_t)AV_NOPTS_VALUE)
        return 0;
    return mAVFormatContext->duration; //time base: AV_TIME_BASE
}

mm_status_t AVDemuxer::reset()
{
    FUNC_ENTER();
    WAIT_PREPARE_OVER();

    MMAutoLock lock(mLock);
    switch ( mState ) {
        case STATE_PREPARING:
            MMASSERT(0);
            break;
        case STATE_STARTED:
//        case STATE_PAUSED:
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

mm_status_t AVDemuxer::flush()
{
    FUNC_ENTER();
    MMAutoLock lock(mBufferLock);
    return flushInternal();
}

mm_status_t AVDemuxer::flushInternal()
{
    FUNC_ENTER();
    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        StreamInfo * si = &mStreamInfoArray[i];
        if ( si->mMediaType == kMediaTypeUnknown )
            continue;
        MMLOGD("clear %s buffer[0].size %d, buffer[1].size %d\n",
            si->mMediaType == kMediaTypeVideo ? "Video" : "Audio",
            si->mBufferList[0].size(), si->mBufferList[1].size());
        si->mBufferList[0].clear();
        si->mBufferList[1].clear();

        //reset last pts when seeking
        si->mLastDts = (int64_t)AV_NOPTS_VALUE;
        si->mLastPts = (int64_t)AV_NOPTS_VALUE;
    }

    mCheckVideoKeyFrame = hasMediaInternal(kMediaTypeVideo);
    if (mCheckVideoKeyFrame) {
        MMLOGI("flush buffer may clear the I video frame, so need to check video key frame\n");
    }

    SET_BUFFERING_STATE(kBufferStateBuffering);
    mReadThread->read();

    FUNC_LEAVE();
    return MM_ERROR_SUCCESS;
}

bool AVDemuxer::checkPacketWritable(StreamInfo * si, AVPacket * packet) {
    if ( !si->mPeerInstalled ) {
        MMLOGV("mediatype %d, peerInstalled %d\n",
            si->mMediaType, si->mPeerInstalled);
        return false;
    }

    if (mScaledPlayRateCur != mScaledPlayRate) {
        if (mScaledPlayRate > mScaledThresholdRate && mScaledPlayRateCur <= mScaledThresholdRate) {
            mCheckVideoKeyFrame = true;
            MMLOGI("playrate change %d->%d, need to check video key frame\n", mScaledPlayRate, mScaledPlayRateCur);
        }

        mScaledPlayRate = mScaledPlayRateCur;
    }

    if ( (mScaledPlayRate != SCALED_PLAY_RATE) && (si->mMediaType == kMediaTypeAudio)) {
        MMLOGV("mediatype %d, mScaledPlayRate %d\n",
            si->mMediaType, mScaledPlayRate);
        return false;
    }

    if (packet->stream_index != si->mSelectedStream && packet->stream_index != si->mSelectedStreamPending)
        return false;

    if ((si->mMediaType == kMediaTypeVideo) && (packet->flags & AV_PKT_FLAG_KEY))
        MMLOGV("key frame\n");

    if (mScaledPlayRate > mScaledThresholdRate) {
        if (!(packet->flags & AV_PKT_FLAG_KEY)) {
            MMLOGV("playRate: %d, skip B or P frame\n", mScaledPlayRate);
            return false;
        }
    }

    return true;
}


int AVDemuxer::avRead(uint8_t *buf, int buf_size)
{
    //MMAutoLock lock(mFileMutex);
    ssize_t size = ::read(mFd, buf, buf_size);
#ifdef DUMP_INPUT
    encodedDataDump(buf, buf_size);
#endif

    if (size == -1) {
        MMLOGE("read return error %s", strerror(errno));
    } else {
        MMLOGV("read return size %d, request size %d\n", size, buf_size);
    }
    return size;
}

/*static */int AVDemuxer::avRead(void *opaque, uint8_t *buf, int buf_size)
{
    AVDemuxer * me = static_cast<AVDemuxer*>(opaque);
    if ( !me ) {
        MMLOGE("invalid cb\n");
        return -1;
    }

    return me->avRead(buf, buf_size);
}

/*static */int64_t AVDemuxer::avSeek(void *opaque, int64_t offset, int whence)
{
    AVDemuxer * me = static_cast<AVDemuxer*>(opaque);
    if ( !me ) {
        MMLOGE("invalid cb\n");
        return -1;
    }

    if ( whence == SEEK_SET && offset < 0 ) {
        MMLOGE("seek from start, but offset < 0\n");
        return -1;
    }

    if ( whence == AVSEEK_SIZE ) {
        MMLOGI("AVSEEK_SIZE supported, file length %" PRId64 "\n", me->mLength);
        return me->mLength;
    }

    if ( whence == AVSEEK_FORCE ) {
        MMLOGE("%d not supported\n", whence);
        return -1;
    }


    return me->avSeek(offset, whence);
}

int64_t AVDemuxer::avSeek(int64_t offset, int whence)
{
    //MMAutoLock lock(mFileMutex);
    int64_t result = lseek(mFd, offset + mOffset, whence);
    if (result == -1) {
        MMLOGE("seek to %" PRId64 " failed", offset + mOffset);
        return -1;
    }
    MMLOGD("offset %" PRId64 " whence %d", offset + mOffset, whence);
    return 0;
}



mm_status_t AVDemuxer::readFrame()
{
    MMLOGV("+\n");

    MMAutoLock lock(mBufferLock);
    if ( mBufferState == kBufferStateFull ) {
        MMLOGV("full\n");
        return MM_ERROR_FULL;
    }

    if ( mBufferState == kBufferStateEOS ) {
        MMLOGV("no data to read\n");
        return MM_ERROR_NO_MORE;
    }

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
        lock.unlock();
        int64_t readStartTime = ElapsedTimer::getUs();
        int ret = av_read_frame(mAVFormatContext, packet); //0: ok, <0: error/end
        int64_t readCosts = ElapsedTimer::getUs() - readStartTime;
        lock.lock();
        mInterruptHandler->end();

        if ( ret < 0 ) {
            free(packet);
            packet = NULL;

            char errorBuf[256] = {0};
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            MMLOGW("read_frame failed: %d (%s)\n", ret, errorBuf);
            if ( ret == AVERROR_EOF ) {
                MMLOGI("eof\n");
                if (mBufferState == kBufferStateBuffering) {
                    MMLOGI("EOS, buffering, send 100 percent\n");
                    NOTIFY(kEventInfoBufferingUpdate, 100, 0, nilParam);
                }
                SET_BUFFERING_STATE(kBufferStateEOS);
                // eos event of DataSource is not needed
                //NOTIFY(kEventEOS, 0, 0, nilParam);
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
                    s->write(buf, s->mSelectedStream);
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
        MMASSERT(si != NULL);
        if (si->mMediaType == kMediaTypeSubtitle) {
            if (si->mSelectedStream == packet->stream_index) {
                INFO("subtitle stream index = %d", packet->stream_index);
            } else {
                //drop other subtitle streams
                FREE_AVPACKET(packet);
                break;
            }
        }

        //discard frames which no need to render
        if (!checkPacketWritable(si, packet)) {
            FREE_AVPACKET(packet);
            usleep(0);
            continue;
        }

        int64_t dtsDiff = 0;
        int64_t ptsDiff = 0;
        //Live stream is always not seekable
        if (isSeekableInternal() &&
            (packet->pts == (int64_t)AV_NOPTS_VALUE || packet->dts == (int64_t)AV_NOPTS_VALUE) &&
            (si->mLastPts != (int64_t)AV_NOPTS_VALUE && si->mLastDts != (int64_t)AV_NOPTS_VALUE)) {
                MMLOGW("invalid pts/dts, using the last pts/dts, pts: %" PRId64 ", dts:%" PRId64 ", last pts: %" PRId64 ", dts:%" PRId64 "\n",
                    packet->pts, packet->dts, si->mLastPts, si->mLastDts);

                //last pts is scaled time;
                packet->pts = av_rescale_q(si->mLastPts, gTimeBase, si->mTimeBase);
                packet->dts = av_rescale_q(si->mLastDts, gTimeBase, si->mTimeBase);
        }

        //If packet timestamp is -1, no need to checkHighWater
        if (packet->pts != (int64_t)AV_NOPTS_VALUE){
            av_packet_rescale_ts(packet, si->mTimeBase, gTimeBase);

            if (si->mLastDts != AV_NOPTS_VALUE && si->mLastPts != AV_NOPTS_VALUE) {
                dtsDiff = packet->dts - si->mLastDts;
                ptsDiff = packet->pts - si->mLastPts;
                if ( MM_UNLIKELY(dtsDiff  > 1000000 || ptsDiff > 1000000  || dtsDiff <-1000000 || ptsDiff <-1000000) ) {
                    MMLOGW("stream: %d, diff too large, pkt ( %" PRId64 ",  %" PRId64 "), last ( %" PRId64 ", %" PRId64 ")",
                        si->mMediaType, packet->dts, packet->pts, si->mLastDts, si->mLastPts);
                }
            }
            si->mLastDts = packet->dts;
            si->mLastPts = packet->pts;

            if ( packet->duration > 0 ) {
                checkHighWater(readCosts, packet->duration);
            }
        }

        si->mPacketCount++;

        int64_t startTime = startTimeUs();
        if (startTime != 0) {
            MMLOGV("change pts according to startTime: %" PRId64 "\n", startTime);
            packet->dts -= startTime;
            packet->pts -= startTime;
        }

        MediaBufferSP buf = AVBufferHelper::createMediaBuffer(packet, true);
        if ( !buf ) {
            MMLOGE("failed to createMediaBuffer\n");
            FREE_AVPACKET(packet);
            NOTIFY_ERROR(MM_ERROR_NO_MEM);
            return MM_ERROR_NO_MEM;
        }

        //check
        if ( MM_UNLIKELY(mCheckVideoKeyFrame)) {
            if (si->mMediaType == kMediaTypeVideo &&
                buf->isFlagSet(MediaBuffer::MBFT_KeyFrame)) {
                mCheckVideoKeyFrame = false;
                MMLOGD("get first video key frame after seek\n");
            } else {
                // FIXME, move it to checkPacketWritable()
                //Audio Frame, frame before first video key frame;
                //Video Frame, frame before first key frame;
                MMLOGD("skip %s frame before got first key video frame\n",
                    si->mMediaType == kMediaTypeVideo ? "Video" : "Audio");
                return MM_ERROR_SUCCESS; //continue buffering
            }
        }


        MMLOGD("packet: media: %s, seq: %d, pts: %0.3f , dts: %0.3f, data: %p, size: %d, stream_index: %d, flags: %d, duration: %" PRId64 ", pos: %" PRId64 ", convergence_duration: %" PRId64 ", num: %d, den: %d, dts diff: %" PRId64 ", last dts: %0.3f\n",
            si->mMediaType == kMediaTypeVideo ? "Video" : "Audio",
            si->mPacketCount,
            packet->pts/1000000.0f,
            packet->dts/1000000.0f,
            packet->data,
            packet->size,
            packet->stream_index,
            packet->flags,
            packet->duration,
            packet->pos,
            packet->convergence_duration,
            mAVFormatContext->streams[packet->stream_index]->time_base.num,
            mAVFormatContext->streams[packet->stream_index]->time_base.den,
            dtsDiff,
            si->mLastDts/1000000.0f
            );
        if (si->mCodecId != kCodecIDVC1 &&
            si->mMediaType == kMediaTypeVideo && packet->data && packet->data[0] != 0) {
            // usually video pkt data doesn't start with non-zero
            // - NAL unit start with start code 0x001
            // - avcC's nal size shouldn't be more than 16M
            MMLOGW("video buffer size error");
        }

#if 0
        //ignore frame before mTargetSeek
        if ((mTargetSeek != SEEK_NONE) && (packet->pts < mTargetSeek)) {
            MMLOGD("Frame will be discarded, pts %0.3f, mTargetSeek 0.3f\n",
                packet->pts/1000000.0f, mTargetSeek/1000000.0f);
            //buf->getMediaMeta()->setInt32(MEDIA_ATTR_FRAME_DISCARD, true);
            buf->getMediaMeta()->setInt64(MEDIA_ATTR_TARGET_TIME, mTargetSeek);
        }
#endif
        si->write(buf, packet->stream_index);

        // mNotCheckBuffering is true for live stream
        // If this stream is seekable, so buffer seek is available.
#if 0
        if ( packet->pts == int64_t(AV_NOPTS_VALUE)) {
            MMLOGE("mediatype %s AV_NOPTS_VALUE\n", si->mMediaType == kMediaTypeVideo ? "Video" : "Audio");
            // FIXME, maybe we can use pre packet pts instead; or use pkt count instead of all-pkt-duration
            si->mNotCheckBuffering = true;
        }
#endif

        if (si->mSelectedStreamPending != WANT_TRACK_NONE) {
            if (packet->stream_index == si->mSelectedStream) {
                //StreamInfo::read() depends on packet dts to switch to another track.
                // so, we discard current track after the packet is written
                if (si->bufferedFirstTs(true) > 0 && packet->dts >= si->bufferedFirstTs(true)) {
                    DEBUG("mSelectedStream: %d, mSelectedStreamPending: %d", si->mSelectedStream, si->mSelectedStreamPending);
                    DEBUG("discard_packet index: %d, dts: %" PRId64 " si->bufferedFirstTs(true): %" PRId64,
                        packet->stream_index, packet->dts, si->bufferedFirstTs(true));
                    mAVFormatContext->streams[packet->stream_index]->discard = AVDISCARD_ALL;
                }
            }
        }
    } while(0);


    int64_t min = INT64_MAX;
    if ( !checkBuffer(min) ) {
        MMLOGI("not check buffer\n");
        if ( mBufferState == kBufferStateBuffering ) {
            NOTIFY(kEventInfoBufferingUpdate, 100, 0, nilParam);
            SET_BUFFERING_STATE(kBufferStateNormal);
        }
        return MM_ERROR_SUCCESS;
    }

    if ( mBufferState == kBufferStateBuffering ) {
        int percent = min * 100 / mBufferingTime;
        if ( percent > 100 )
            percent = 100;
        if ( percent != mReportedBufferingPercent ) {
            MMLOGI("buffering percent: %d, notify\n", percent);
            NOTIFY(kEventInfoBufferingUpdate, percent, 0, nilParam);
            if ( percent == 100 ) {
                SET_BUFFERING_STATE(kBufferStateNormal);
                mReportedBufferingPercent = REPORTED_PERCENT_NONE;
            } else {
                mReportedBufferingPercent = percent;
            }
        }
        return MM_ERROR_SUCCESS;
    }

    if ( min > mBufferingTimeHigh ) {
        MMLOGV("full\n");
        SET_BUFFERING_STATE(kBufferStateFull);
        return MM_ERROR_FULL;
    }

    return MM_ERROR_SUCCESS;
}

bool AVDemuxer::checkBuffer(int64_t & min)
{
    min = INT64_MAX;
    bool check = false;
    for ( int i = 0; i < kMediaTypeCount; ++i ) {
        StreamInfo * si = &mStreamInfoArray[i];
        if ( si->mMediaType == kMediaTypeUnknown ||
            si->mNotCheckBuffering ||
            !si->mPeerInstalled ||
            ( (mScaledPlayRate != SCALED_PLAY_RATE) && (i == kMediaTypeAudio))) {
            MMLOGV("ignore: mediatype: %d, notcheck: %d, perrinstalled: %d, mScaledPlayRate : %d\n",
                si->mMediaType, si->mNotCheckBuffering, si->mPeerInstalled, mScaledPlayRate);
            continue;
        }

        int64_t j = si->bufferedTime();
        if ( min > j ) {
            MMLOGV("min change from %" PRId64 " to %" PRId64 "(mediatype: %d)\n", min, j, si->mMediaType);
            min = j;
        }
        check = true;
    }

    MMLOGV("need check: %d\n", check);
    return check;
}

/*static */snd_format_t AVDemuxer::convertAudioFormat(AVSampleFormat avFormat)
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

CowCodecID AVDemuxer::AVCodecId2CodecId(AVCodecID id)
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

MM_LOG_DEFINE_MODULE_NAME("AVDemuxerCreater");

Component * createComponent(const char* mimeType, bool isEncoder)
{
    FUNC_ENTER();
    AVDemuxer * demuxer = new AVDemuxer();
    if ( !demuxer ) {
        MMLOGE("no mem\n");
        return NULL;
    }

    MMLOGI("ret: %p\n", demuxer);
    return demuxer;
}

void releaseComponent(Component * component)
{
    MMLOGI("%p\n", component);
    if ( component ) {
        AVDemuxer * demuxer = DYNAMIC_CAST<AVDemuxer*>(component);
        MMASSERT(demuxer != NULL);
        MM_RELEASE(demuxer);
    }
}
}

}

