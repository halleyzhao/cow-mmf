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
#include <unistd.h>

#include "multimedia/media_attr_str.h"
#include "multimedia/cowplayer.h"
#include "pipeline_player.h"
#include "pipeline_LPA.h"
#include "multimedia/pipeline_audioplayer.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-PLY")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()
static const char * MMMSGTHREAD_NAME = "CowPlayer::Private";


#define COWPLAYER_MSG_BRIDGE(msg, param1, param2, async, status)  do{                           \
    status = MM_ERROR_ASYNC;                                            \
    if (async) {                                                                     \
        mPriv->postMsgBridge(msg, param1, param2);                                         \
    } else {                                                                        \
        MMMsgThread::param1_type rsp_param1;                                        \
        MMMsgThread::param2_type rsp_param2;                                        \
        if(mPriv->sendMsgBridge(msg, param1, param2, &rsp_param1, &rsp_param2)) {    \
            return MM_ERROR_UNKNOWN;                                                \
        }                                                                           \
        status = mm_status_t(rsp_param1);                                           \
    }                                                                               \
    INFO("%s return status %d", __func__, status);                                 \
}while(0)

#define ENSURE_PIPELINE() do {                  \
    if (!mPriv->mPipeline) {                    \
        if (!mPriv->createDefaultPipeline())    \
            return MM_ERROR_NO_PIPELINE;        \
    }                                           \
    ASSERT(mPriv->mPipeline);                   \
} while(0)


class CowPlayer::ListenerPlayer : public Pipeline::Listener
{
  public:
    ListenerPlayer(CowPlayer* player)
        :mWatcher(player)
    { }
    virtual ~ListenerPlayer() { }
    void removeWatcher()
    {
        // FIXME, use weak_ptr for mWatcher is another option
        MMAutoLock locker(mWatchLock);
        mWatcher = NULL;
    }
    void onMessage(int msg, int param1, int param2, const MMParamSP obj)
    {
        //FUNC_TRACK();
        MMAutoLock locker(mWatchLock);
        // FIXME, suppose it runs in Pipeline's MMMsgThread, it is light weight thread for message only
        if (mWatcher) {
            mWatcher->notify(msg, param1, param2, obj);
        }
        else
            WARNING("new msg(%d) comes, but watcher left already\n", msg);
    }

  private:
    CowPlayer* mWatcher;
    Lock mWatchLock;

    MM_DISALLOW_COPY(ListenerPlayer)
};

class CowPlayer::Private : public MMMsgThread
{
  public:
    static PrivateSP create(Pipeline::ListenerSP listener, int playType)
    {
        FUNC_TRACK();
        PrivateSP priv(new Private(), MMMsgThread::releaseHelper);
        if (priv) {
            if (priv->init() == MM_ERROR_SUCCESS) {
                priv->mListenerReceive = listener;
                priv->mPlayType = playType;
            } else
                priv.reset();
        }

        return priv;
    }

    mm_status_t init() {
        FUNC_TRACK();
        int ret = MMMsgThread::run();
        if (ret != 0) {
            ERROR("init failed, ret %d", ret);
            return MM_ERROR_OP_FAILED;
        }

        return MM_ERROR_SUCCESS;
    }

    ~Private() {}

    int postMsgBridge(msg_type what, param1_type param1, param2_type param2, int64_t timeoutUs = 0)
    {
        FUNC_TRACK();
        return postMsg(what, param1, param2, timeoutUs);
    }

    int postMsgBridge2(msg_type what, param1_type param1, param2_type param2, param3_type param3, int64_t timeoutUs = 0)
    {
        FUNC_TRACK();
        return postMsg(what, param1, param2, timeoutUs, param3);
    }

    int sendMsgBridge(msg_type what, param1_type param1, param2_type param2, param1_type * resp_param1, param2_type * resp_param2)
    {
        FUNC_TRACK();
        return sendMsg(what, param1, param2, resp_param1, resp_param2);
    }

    PipelineSP mPipeline;
    Pipeline::ListenerSP mListenerReceive;

  // private:
    Lock mLock;
    // copied data of setDataSouce
    std::string mUri;
    std::string mSubtitleUri;
    std::string mDisplayName;
    std::map<std::string, std::string> mHeaders;
    int mFd;
    int64_t mOffset;
    int64_t mLength;
    //MediaMetaSP mParam;
    SeekEventParamSP mSeekParam;
    int mPlayType;

    Private()
        : MMMsgThread(MMMSGTHREAD_NAME)
        , mFd(-1)
        , mOffset(-1)
        , mLength(-1)
        , mPlayType(0)
    {
        FUNC_TRACK();
        mSeekParam = SeekEventParam::create();
    }

    mm_status_t setPipeline(PipelineSP pipeline) {
        FUNC_TRACK();
        if (pipeline) {
            mPipeline = pipeline;
            mPipeline->setListener(mListenerReceive);
        }
        return MM_ERROR_SUCCESS;
    }
    bool createDefaultPipeline() {
        FUNC_TRACK();

        if (mPlayType == PlayerType_COWAudio) {
            mPipeline = Pipeline::create(new PipelineAudioPlayer(), mListenerReceive);
            ASSERT(mPipeline);
        #if defined(__PHONE_BOARD_QCOM__)
        } else if (mPlayType == PlayerType_LPA) {
            mPipeline = Pipeline::create(new PipelineLPA(), mListenerReceive);
            ASSERT(mPipeline);
        #endif
        } else {
            mPipeline = Pipeline::create(new PipelinePlayer(), mListenerReceive);
            ASSERT(mPipeline);
        }
        return mPipeline;
    }

    void flushCommandList() // discard the pending actions in message List
    {
        MMAutoLock locker(mMsgThrdLock);
        // FIXME, we need lock MMMsgThread msg queue; add flushMsgQueue to MMMsgThread
        // make sure no mem leak to clear the message queue
        mMsgQ.clear();
    }

    MM_DISALLOW_COPY(Private);

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPipelineMessage)
    DECLARE_MSG_HANDLER(onSetDataSource_1)
    DECLARE_MSG_HANDLER(onSetDataSource_2)
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onPrepareAsync)
    DECLARE_MSG_HANDLER(onSetDisplayName)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onSeek)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER2(onSetParameter)
};

#define CPP_MSG_pipelineMessage (MMMsgThread::msg_type)1
#define CPP_MSG_setDataSource1Message (MMMsgThread::msg_type)2
#define CPP_MSG_setDataSource2Message (MMMsgThread::msg_type)3
#define CPP_MSG_prepareMessage (MMMsgThread::msg_type)4
#define CPP_MSG_prepareAsyncMessage (MMMsgThread::msg_type)5
#define CPP_MSG_startMessage (MMMsgThread::msg_type)6
#define CPP_MSG_stopMessage (MMMsgThread::msg_type)7
#define CPP_MSG_pauseMessage (MMMsgThread::msg_type)8
#define CPP_MSG_seekMessage (MMMsgThread::msg_type)9
#define CPP_MSG_resetMessage (MMMsgThread::msg_type)10
#define CPP_MSG_setParameterMessage (MMMsgThread::msg_type)11
#define CPP_MSG_setDisplayName (MMMsgThread::msg_type)12
#define CPP_MSG_setSubtitleSource (MMMsgThread::msg_type)13


BEGIN_MSG_LOOP(CowPlayer::Private)
    MSG_ITEM(CPP_MSG_pipelineMessage, onPipelineMessage)
    MSG_ITEM(CPP_MSG_setDataSource1Message, onSetDataSource_1)
    MSG_ITEM(CPP_MSG_setDataSource2Message, onSetDataSource_2)
    MSG_ITEM(CPP_MSG_prepareMessage, onPrepare)
    MSG_ITEM(CPP_MSG_prepareAsyncMessage, onPrepareAsync)
    MSG_ITEM(CPP_MSG_setDisplayName, onSetDisplayName)
    MSG_ITEM(CPP_MSG_startMessage, onStart)
    MSG_ITEM(CPP_MSG_stopMessage, onStop)
    MSG_ITEM(CPP_MSG_pauseMessage, onPause)
    MSG_ITEM(CPP_MSG_seekMessage, onSeek)
    MSG_ITEM(CPP_MSG_resetMessage, onReset)
    MSG_ITEM2(CPP_MSG_setParameterMessage, onSetParameter)
END_MSG_LOOP()

CowPlayer::CowPlayer(int playType)
    :mListenderSend(NULL),
     mLoop(false),
     mPlayType(playType)
{
    FUNC_TRACK();
    Pipeline::ListenerSP listener(new ListenerPlayer(this));
    mPriv = Private::create(listener, playType);
}

CowPlayer::~CowPlayer()
{
    FUNC_TRACK();
    ListenerPlayer* listener = DYNAMIC_CAST<ListenerPlayer*>(mPriv->mListenerReceive.get());
    if(listener){
        listener->removeWatcher();
    }

    mm_status_t status = reset();
    if (status != MM_ERROR_SUCCESS)
        WARNING("reset() failed during ~CowPlayer, continue\n");
}

mm_status_t CowPlayer::setPipeline(PipelineSP pipeline)
{
    FUNC_TRACK();
    return mPriv->setPipeline(pipeline);
}

mm_status_t CowPlayer::setListener(Listener * listener)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    mListenderSend = listener;
    return MM_ERROR_SUCCESS;
}

void CowPlayer::removeListener()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    mListenderSend = NULL;
}


#define CHECK_PIPELINE_RET(STATUS, CMD_NAME) do {                               \
                                                                                \
        if (STATUS == MM_ERROR_SUCCESS) {                                       \
            /* Pipeline::onComponentMessage has already notify success */ \
        } else if (STATUS == MM_ERROR_ASYNC) {                                  \
            /* not expected design */                                           \
            WARNING("not expected to reach here\n");                            \
        } else {                                                                \
            /* notify failure */                                                \
            ERROR("%s failed\n", CMD_NAME);                                     \
            mPipeline->postCowMsgBridge(Component::kEventError, STATUS, NULL);    \
        }                                                                       \
    } while(0)

mm_status_t CowPlayer::setDataSource(const char * uri,
                 const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    // FIXME, assume caller does reset. mediaplayer.h doesn't have reset(), does it by ourself
    mPriv->mUri = std::string(uri);
    INFO("got uri: %s\n", mPriv->mUri.c_str());

    if (headers)
        mPriv->mHeaders = *headers;
    else
        mPriv->mHeaders.clear();

    mPriv->postMsgBridge(CPP_MSG_setDataSource1Message, 0, NULL);

    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onSetDataSource_1(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);

    // FIXME, make sure uri and headers match
    mm_status_t status = mPipeline->load(mUri.c_str(), &mHeaders);
    CHECK_PIPELINE_RET(status, "setDataSource");
}

mm_status_t CowPlayer::setDataSourceAsync(int fd, int64_t offset, int64_t length)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mm_status_t status = MM_ERROR_UNKNOWN;

    // FIXME, assume caller does reset
    mPriv->mFd = fd;
    mPriv->mOffset = offset;
    mPriv->mLength = length;
    COWPLAYER_MSG_BRIDGE(CPP_MSG_setDataSource2Message, 0, 0, true, status);

    return status;
}

mm_status_t CowPlayer::setDataSource(int fd, int64_t offset, int64_t length)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mm_status_t status = MM_ERROR_UNKNOWN;
    // FIXME, assume caller does reset
    mPriv->mFd = fd;
    mPriv->mOffset = offset;
    mPriv->mLength = length;
    COWPLAYER_MSG_BRIDGE(CPP_MSG_setDataSource2Message, 0, 0, false, status);

    return status;
}

void CowPlayer::Private::onSetDataSource_2(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    mm_status_t status = mPipeline->load(mFd, mOffset, mLength);
    if (rspId) {
        postReponse(rspId, status, 0);
    }
}

mm_status_t CowPlayer::setDataSource(const unsigned char * mem, size_t size)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    // ENSURE_PIPELINE();

    ERROR("not implemented yet\n");
    return MM_ERROR_UNSUPPORTED;
    // return mPipeline->setDataSource(mem, size);
}

mm_status_t CowPlayer::setSubtitleSource(const char* uri)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();
    if (!uri)
        return MM_ERROR_INVALID_PARAM;
    mPriv->mSubtitleUri = std::string(uri);
    INFO("got subtitle uri: %s\n", mPriv->mSubtitleUri.c_str());

    return mPriv->mPipeline->loadSubtitleUri(uri);
}

mm_status_t CowPlayer::setDisplayName(const char* name)
{

    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();
    if (!name)
        return MM_ERROR_INVALID_PARAM;
    mPriv->mDisplayName = std::string(name);
    INFO("got display name: %s\n", mPriv->mDisplayName.c_str());

    mPriv->postMsgBridge(CPP_MSG_setDisplayName, 0, NULL);

    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onSetDisplayName(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);

    mPipeline->setDisplayName(mDisplayName.c_str());
}


mm_status_t CowPlayer::setNativeDisplay(void * display)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    return mPriv->mPipeline->setNativeDisplay(display);
}
mm_status_t CowPlayer::setVideoSurface(void * handle, bool isTexture)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    return mPriv->mPipeline->setVideoSurface(handle, isTexture);
}

mm_status_t CowPlayer::prepare()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;

    ENSURE_PIPELINE();

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    if (mPriv->sendMsgBridge(CPP_MSG_prepareMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);

    return status;

}

void CowPlayer::Private::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(rspId);
    mm_status_t status = mPipeline->prepare();

    if (status != MM_ERROR_SUCCESS) {
        ERROR("prepare faild\n");
    }

    postReponse(rspId, status, 0);
}

mm_status_t CowPlayer::prepareAsync()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_prepareAsyncMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onPrepareAsync(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = mPipeline->prepare();
    CHECK_PIPELINE_RET(status, "prepareAsync");
}

mm_status_t CowPlayer::setVolume(const float left, const float right)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    return mPriv->mPipeline->setVolume(left, right);
}

mm_status_t CowPlayer::getVolume(float& left, float& right) const
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    return mPriv->mPipeline->getVolume(left, right);
}

mm_status_t CowPlayer::setMute(bool mute)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();
    PipelinePlayerBase* playBase = DYNAMIC_CAST<PipelinePlayerBase*>(mPriv->mPipeline.get());
    if (playBase)
        return playBase->setMute(mute);
    else
        return MM_ERROR_OP_FAILED;
}

mm_status_t CowPlayer::getMute(bool * mute) const
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();
    PipelinePlayerBase* playBase = DYNAMIC_CAST<PipelinePlayerBase*>(mPriv->mPipeline.get());
    if (playBase)
        return playBase->getMute(mute);
    else
        return MM_ERROR_OP_FAILED;
}

mm_status_t CowPlayer::start()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_startMessage, 0, 0);
    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = MM_ERROR_SUCCESS;

    Pipeline::ComponentStateType state;
    mPipeline->getState(state);
    if (state == Pipeline::kComponentStatePaused) {
        mm_status_t status = mPipeline->resume();
        CHECK_PIPELINE_RET(status, "resume");
    } else {
        mm_status_t status = mPipeline->start();
        CHECK_PIPELINE_RET(status, "start");
    }
}

mm_status_t CowPlayer::stop()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_stopMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    mm_status_t status = mPipeline->stop();
    CHECK_PIPELINE_RET(status, "stop");

    if (rspId)
        postReponse(rspId, status, 0);
}

mm_status_t CowPlayer::pause()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_pauseMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowPlayer::Private::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = mPipeline->pause();

    CHECK_PIPELINE_RET(status, "pause");
}

mm_status_t CowPlayer::seekInternal(int64_t msec)
{
    INFO("seek %ld msec\n", msec);
#if 0 // FIXME skip seek temp to run H5 video
    if (msec == 0) {
        WARNING("seek skipped\n", msec);
        return MM_ERROR_SUCCESS;
    }
#endif
    ENSURE_PIPELINE();

    if (msec<0)
        return MM_ERROR_INVALID_PARAM;

    int64_t durationMsec = 0;
    mm_status_t ret = getDuration(durationMsec);
    if (ret != MM_ERROR_SUCCESS)
        return ret;
    if (msec > durationMsec) {
        ERROR("invalid seek position: %" PRId64 " ms, duration: %" PRId64 " ms\n", msec,durationMsec);
        return MM_ERROR_INVALID_PARAM;
    }

    ASSERT_RET(mPriv->mSeekParam, MM_ERROR_UNKNOWN);
    mPriv->mSeekParam->updateSeekTime(msec);
    mPriv->postMsgBridge(CPP_MSG_seekMessage, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t CowPlayer::seek(int64_t msec)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    mm_status_t status = MM_ERROR_SUCCESS;
    status = seekInternal(msec);

    // notify kEventSeekComplete. we shouldn't wait until onSeek which runs in a heavy thread
    mPriv->mPipeline->postCowMsgBridge(Component::kEventSeekComplete, MM_ERROR_SUCCESS, NULL, 5);

    return status;
}

void CowPlayer::Private::onSeek(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    PipelinePlayer *pipelinePlayer = (PipelinePlayer *)mPipeline.get();
    mm_status_t status = pipelinePlayer->seek(mSeekParam);
    CHECK_PIPELINE_RET(status, "seek");
}

mm_status_t CowPlayer::reset()
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

#if 0 // async mode, client is required to wait KEventResetResult before delete CowPlayer
    // discard pending commands
    mPriv->flushCommandList();
    // unblock current running execution (may be blocked)
    mPriv->mPipeline->unblock();
    // FIXME: expect no stop() is required before reset(), in case comp is in bad state and stuck in stop()
    // mPriv->postMsgBridge(CPP_MSG_stopMessage, 0, 0);
    // does reset
    mPriv->postMsgBridge(CPP_MSG_resetMessage, 0, 0);
    return MM_ERROR_ASYNC;
#else
    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    mm_status_t status = MM_ERROR_SUCCESS;
    if (mPriv && mPriv->mPipeline) {
        Pipeline::ComponentStateType state = Pipeline::kComponentStateInvalid;
        status = mPriv->mPipeline->getState(state);
        if (status == MM_ERROR_SUCCESS && state != Pipeline::kComponentStateStopped && state != Pipeline::kComponentStateStop) {
            if (mPriv->sendMsgBridge(CPP_MSG_stopMessage, 0, 0, &rsp_param1, &rsp_param2)) {
                return MM_ERROR_UNKNOWN;
            }
            status = mm_status_t(rsp_param1);
        }
    }

    /*
    if (status != MM_ERROR_SUCCESS)
        return status;
    */

    if (mPriv->sendMsgBridge(CPP_MSG_resetMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);

    return status;
#endif
}

void CowPlayer::Private::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    mm_status_t status = mPipeline->reset();

    CHECK_PIPELINE_RET(status, "reset");

    if (rspId)
        postReponse(rspId, status, 0);
}

bool CowPlayer::isPlaying() const
{
    FUNC_TRACK();
    Pipeline::ComponentStateType state;
    ENSURE_PIPELINE();

    if (mPriv->mPipeline->getState(state) != MM_ERROR_SUCCESS)
        return false;

    DEBUG("state : %d", state);
    if (state != Pipeline::kComponentStatePlaying)
        return false;

    return true;
}

mm_status_t CowPlayer::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->getVideoSize(width, height);
}

mm_status_t CowPlayer::getCurrentPosition(int64_t& msec) const
{
    //FUNC_TRACK();
    ENSURE_PIPELINE();

    mm_status_t status = mPriv->mPipeline->getCurrentPosition(msec);
    DEBUG("current playback position: %" PRId64 " ms", msec);
    return status;
}

mm_status_t CowPlayer::getDuration(int64_t& msec) const
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->getDuration(msec);
}

mm_status_t CowPlayer::setAudioStreamType(int type)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->setAudioStreamType(type);
}
mm_status_t CowPlayer::getAudioStreamType(int *type)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->getAudioStreamType(type);
}

mm_status_t CowPlayer::setAudioConnectionId(const char * connectionId)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->setAudioConnectionId(connectionId);
}

const char * CowPlayer::getAudioConnectionId() const
{
    FUNC_TRACK();
    if (!mPriv->mPipeline) {
        if (!mPriv->createDefaultPipeline())
            return "";
    }

    return mPriv->mPipeline->getAudioConnectionId();
}

mm_status_t CowPlayer::setLoop(bool loop)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    mLoop = loop;
    return MM_ERROR_SUCCESS;
}

bool CowPlayer::isLooping() const
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
     return mLoop;
}

MMParamSP CowPlayer::getTrackInfo()
{
    FUNC_TRACK();
    if (!mPriv->mPipeline) {
        if (!mPriv->createDefaultPipeline())
            return nilParam;
    }

    return mPriv->mPipeline->getTrackInfo();
}

mm_status_t CowPlayer::selectTrack(int mediaType, int index)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    Component::MediaType type = (Component::MediaType)mediaType;

    return mPriv->mPipeline->selectTrack(type, index);
}

int CowPlayer::getSelectedTrack(int mediaType)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    Component::MediaType type = (Component::MediaType)mediaType;
    return mPriv->mPipeline->getSelectedTrack(type);
}

// A language code in either way of ISO-639-1 or ISO-639-2. When the language is unknown or could not be determined, MM_ERROR_UNSUPPORTED will returned
mm_status_t CowPlayer::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    //MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    //mPriv->mParam = param;
    mPriv->postMsgBridge2(CPP_MSG_setParameterMessage, 0, 0, meta, 0);
    //mm_status_t status = mPriv->mPipeline->setParameter(meta);
    return MM_ERROR_ASYNC;
}

//#if 0
void CowPlayer::Private::onSetParameter(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
{
    FUNC_TRACK();
    MediaMetaSP meta = std::tr1::dynamic_pointer_cast<MediaMeta>(param3);
    mm_status_t status = mPipeline->setParameter(meta);
    CHECK_PIPELINE_RET(status, "setParameter");
}
//#endif
mm_status_t CowPlayer::getParameter(MediaMetaSP & meta)
{
    FUNC_TRACK();
     mm_status_t status = MM_ERROR_UNKNOWN;
     ENSURE_PIPELINE();

    // not necessary to run on another thread
    status = mPriv->mPipeline->getParameter(meta);
    return status;
}

mm_status_t CowPlayer::invoke(const MMParam * request, MMParam * reply)
{
    ERROR("unsupported invode yet\n");
    return MM_ERROR_UNSUPPORTED;
}

mm_status_t CowPlayer::pushData(MediaBufferSP & buffer)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;
    ENSURE_PIPELINE();

    status = mPriv->mPipeline->pushData(buffer);
    return status;
}

mm_status_t CowPlayer::enableExternalSubtitleSupport(bool enable)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;
    ENSURE_PIPELINE();

    status = mPriv->mPipeline->enableExternalSubtitleSupport(enable);
    return status;
}

void CowPlayer::Private::onPipelineMessage(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    // it is unnecessary. the message from pipeline are posted to app/listener directly.
}

mm_status_t CowPlayer::notify(int msg, int param1, int param2, const MMParamSP param)
{
    //FUNC_TRACK();
    {
        MMAutoLock locker(mPriv->mLock);
        if ( !mListenderSend ) {
            ERROR("listerner is removed or not register, skip msg: %d\n", msg);
            return MM_ERROR_NOT_INITED;
        }
        mListenderSend->onMessage(msg, param1, param2, param);
    }

    if (mLoop && msg == Component::kEventEOS) {
        INFO("replay from the begining\n");
        seek(0);
        start();
    } else if (msg == Component::kEventEOS) {
        INFO("EOS, no loop, pause\n");
        pause();
    } else if (msg == Component::kEventSeekRequire) {
        INFO("internal seek require position : %d\n", param1);
        MMAutoLock locker(mPriv->mLock);
        seekInternal(int(param1)/1000);
    }
    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM
