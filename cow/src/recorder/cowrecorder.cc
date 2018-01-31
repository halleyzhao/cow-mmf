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
#include "multimedia/cowrecorder.h"
#include "pipeline_videorecorder.h"
#include "pipeline_audiorecorder.h"
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
#include "pipeline_rtsprecorder.h"
#endif
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"


namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-REC")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()
static const char * MMMSGTHREAD_NAME = "CowRecorder::Private";

#define EXECUTE_PIPELINE_FUNC(STATUS, FUNC_NAME, PARAM...) do {                                         \
    MMAutoLock locker(mLock);                                                                             \
    mm_status_t s;                                                                                      \
    Pipeline *base = mPipeline.get();                                                                   \
    s = ((PipelineRecorderBase*)base)->FUNC_NAME(PARAM);                                                \
                                                                                                        \
    if (s != MM_ERROR_SUCCESS) {                                                                        \
        ERROR("%s error %d\n", #FUNC_NAME, s);                                                          \
        STATUS=s;                                                                                       \
    }                                                                                                   \
}while(0)


#define CHECK_PIPELINE_RET(STATUS, CMD_NAME) do {                                           \
    if (STATUS == MM_ERROR_ASYNC) {                                                         \
        /* not expected design */                                                           \
        WARNING("not expected to reach here\n");                                            \
    } else if (STATUS != MM_ERROR_SUCCESS) {                                                \
        /* notify failure */                                                                \
        ERROR("%s failed %d\n", #CMD_NAME, STATUS);                                         \
        /* FIXME, more than one pipeline */                                                 \
        mPipeline->postCowMsgBridge(Component::kEventError, STATUS, NULL);                  \
    }                                                                                       \
    /* Pipeline::onComponentMessage has already notify success */                           \
} while(0)


#define EXECUTE_PIPELINE_CMD(STATUS, CMD_NAME) do {                             \
    mm_status_t s;                                                              \
    s = mPipeline->CMD_NAME();                                                  \
    CHECK_PIPELINE_RET(s, CMD_NAME);                                            \
    if (s != MM_ERROR_SUCCESS) {                                                \
        STATUS = s;                                                             \
    }                                                                           \
} while(0)


#define CHECK_AUDIO_ONLY_VALID() do {   \
    if (mAudioOnly) {                   \
        return MM_ERROR_INVALID_PARAM;  \
    }                                   \
}while(0)

#define ENSURE_PIPELINE() do {                  \
        if (!mPriv->mPipeline) {                    \
            if (!mPriv->createDefaultPipeline())    \
                return MM_ERROR_NO_PIPELINE;        \
        }                                           \
        ASSERT(mPriv->mPipeline);                   \
    } while(0)

class CowRecorder::ListenerRecorder : public Pipeline::Listener
{
  public:
    ListenerRecorder(CowRecorder* player)
        :mWatcher(player)
    { }
    virtual ~ListenerRecorder() { }
    void removeWatcher()
    {
        // FIXME, use weak_ptr for mWatcher is another option
        MMAutoLock locker(mLock);
        mWatcher = NULL;
    }
    void onMessage(int msg, int param1, int param2, const MMParamSP obj)
    {
        // FUNC_TRACK();
        MMAutoLock locker(mLock);
        // FIXME, suppose it runs in Pipeline's MMMsgThread, it is light weight thread for message only
        if (mWatcher) {
            mWatcher->notify(msg, param1, param2, obj);
        }
        else
            WARNING("new msg(%d) comes, but watcher left already\n", msg);
    }

  private:
    CowRecorder* mWatcher;
    Lock mLock;

    MM_DISALLOW_COPY(ListenerRecorder)
};

class CowRecorder::Private : public MMMsgThread
{
  public:
    static PrivateSP create(Pipeline::ListenerSP listener, RecorderType type)
    {
        FUNC_TRACK();
        PrivateSP priv(new Private(), MMMsgThread::releaseHelper);
        if (priv) {
            if (priv->init() == MM_ERROR_SUCCESS) {
                priv->mListenerReceive = listener;

                priv->mAudioOnly = (type == RecorderType_COWAudio ? true : false);
                priv->mType = type;
            } else {
                priv.reset();
            }
        }
        return priv;
    }
    mm_status_t setPipeline(PipelineSP pipeline) {
        FUNC_TRACK();
        mPipeline = pipeline;
        mPipeline->setListener(mListenerReceive);
        return MM_ERROR_SUCCESS;
    }

    bool createDefaultPipeline()
    {
        mPipeline.reset();
        #ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        if (mType == RecorderType_RTSP) {
            mPipeline = Pipeline::create(new PipelineRTSPRecorder(), mListenerReceive);
        } else
        #endif
        {
            if (mAudioOnly) {
                mPipeline = Pipeline::create(new PipelineAudioRecorder(), mListenerReceive);
                ASSERT(mPipeline);
            } else {
                mPipeline = Pipeline::create(new PipelineVideoRecorder(), mListenerReceive);
                ASSERT(mPipeline);
            }
        }

        return mPipeline;
    }

    mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
    {
        FUNC_TRACK();
        CHECK_AUDIO_ONLY_VALID();
        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setCamera, camera, recordingProxy);
        return status;
    }

    mm_status_t setVideoSourceFormat(int width, int height, uint32_t format)
    {
        FUNC_TRACK();
        CHECK_AUDIO_ONLY_VALID();
        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setVideoSourceFormat, width, height, format);
        return status;
    }

    mm_status_t setVideoSourceUri(const char * uri)
    {
        FUNC_TRACK();
        CHECK_AUDIO_ONLY_VALID();
        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setVideoSource, uri);
        return MM_ERROR_SUCCESS;
    }

    mm_status_t setAudioSourceUri(const char* uri)
    {
        FUNC_TRACK();
#ifndef __DISABLE_AUDIO_STREAM__
        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setAudioSource, uri);
#endif
        return MM_ERROR_SUCCESS;

    }
    mm_status_t setVideoEncoder(const char* mime)
    {
        FUNC_TRACK();
        CHECK_AUDIO_ONLY_VALID();
        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setVideoEncoder, mime);
        return status;
    }
    mm_status_t setAudioEncoder(const char* mime)
    {
        FUNC_TRACK();

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setAudioEncoder, mime);
        return status;
    }

    mm_status_t setOutputFormat(const char* mime)
    {
        FUNC_TRACK();
        INFO("output format %s\n", mime);

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setOutputFormat, mime);
        return status;
    }

    mm_status_t setOutputFile(int fd)
    {
        FUNC_TRACK();
        INFO("output fd %d\n", fd);

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setOutputFile, fd);
        return status;
    }

    mm_status_t setOutputFile(const char* filePath)
    {
        FUNC_TRACK();
        INFO("filePath %s\n", filePath);

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setOutputFile, filePath);
        return status;
    }

    mm_status_t setMaxDuration(int64_t msec)
    {
        FUNC_TRACK();
        INFO("max duration msec %" PRId64 "\n", msec);

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setMaxDuration, msec);
        return status;
    }

    mm_status_t setMaxFileSize(int64_t bytes)
    {
        FUNC_TRACK();
        INFO("max file size %" PRId64 "\n", bytes);

        mm_status_t status = MM_ERROR_SUCCESS;
        EXECUTE_PIPELINE_FUNC(status, setMaxFileSize, bytes);
        return status;
    }

    mm_status_t setRecorderUsage(RecorderUsage usage) {
        FUNC_TRACK();
        if ((usage & RU_RecorderMask) == 0) {
            ERROR("unsupported recorder usage: 0x%x\n", usage);
            return MM_ERROR_OP_FAILED;
        }

        mUsage = usage;
        ((PipelineRecorderBase*)mPipeline.get())->setRecorderUsage(mUsage);
        return MM_ERROR_SUCCESS;
    }

    mm_status_t getRecorderUsage(RecorderUsage &usage) {
        usage = mUsage;
        return MM_ERROR_SUCCESS;
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

    int sendMsgBridge(msg_type what, param1_type param1, param2_type param2, param1_type * resp_param1, param2_type * resp_param2)
    {
        FUNC_TRACK();
        return sendMsg(what, param1, param2, resp_param1, resp_param2);
    }

    PipelineSP mPipeline;

    Pipeline::ListenerSP mListenerReceive;

  // private:
    Lock mLock;
    RecorderUsage mUsage;

    int32_t mVideoWidth;
    int32_t mVideoHeight;
    uint32_t mVideoFourcc;
    float mVideoFramerate;

    bool mAudioOnly;
    RecorderType mType = RecorderType_COW;

    Private() : MMMsgThread(MMMSGTHREAD_NAME)
        , mUsage(RU_None)
        , mVideoWidth(640)
        , mVideoHeight(480)
        , mVideoFourcc('NV12')
        , mVideoFramerate(30.0f)
        , mAudioOnly(false)
    {
        FUNC_TRACK();
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
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onPrepareAsync)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onReset)
};

#define CPP_MSG_prepareMessage (MMMsgThread::msg_type)2
#define CPP_MSG_prepareAsyncMessage (MMMsgThread::msg_type)3
#define CPP_MSG_startMessage (MMMsgThread::msg_type)4
#define CPP_MSG_stopMessage (MMMsgThread::msg_type)5
#define CPP_MSG_pauseMessage (MMMsgThread::msg_type)6
#define CPP_MSG_resetMessage (MMMsgThread::msg_type)7
#define CPP_MSG_setParameterMessage (MMMsgThread::msg_type)8


BEGIN_MSG_LOOP(CowRecorder::Private)
    MSG_ITEM(CPP_MSG_prepareMessage, onPrepare)
    MSG_ITEM(CPP_MSG_prepareAsyncMessage, onPrepareAsync)
    MSG_ITEM(CPP_MSG_startMessage, onStart)
    MSG_ITEM(CPP_MSG_stopMessage, onStop)
    MSG_ITEM(CPP_MSG_pauseMessage, onPause)
    MSG_ITEM(CPP_MSG_resetMessage, onReset)
END_MSG_LOOP()

CowRecorder::CowRecorder(RecorderType type) : mAudioOnly(false)
{
    FUNC_TRACK();
    Pipeline::ListenerSP listener(new ListenerRecorder(this));
    mPriv = Private::create(listener, type);
    mListenderSend = NULL;
    DEBUG("type %d\n", type);
}

CowRecorder::~CowRecorder()
{
    FUNC_TRACK();
    ListenerRecorder* listener = DYNAMIC_CAST<ListenerRecorder*>(mPriv->mListenerReceive.get());
    if(listener){
        listener->removeWatcher();
    }

    mm_status_t status = reset();
    if (status != MM_ERROR_SUCCESS)
        WARNING("reset() failed during ~CowRecorder, continue\n");
}

mm_status_t CowRecorder::setPipeline(PipelineSP pipeline)
{
    FUNC_TRACK();
    return mPriv->setPipeline(pipeline);
}

mm_status_t CowRecorder::setVideoSourceFormat(int width, int height, uint32_t fourcc)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setVideoSourceFormat(width, height, fourcc);
}

mm_status_t CowRecorder::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setCamera(camera, recordingProxy);
}

mm_status_t CowRecorder::setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setVideoSourceUri(uri);
}

mm_status_t CowRecorder::setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setAudioSourceUri(uri);
}

mm_status_t CowRecorder::setVideoEncoder(const char* mime)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setVideoEncoder(mime);
}

mm_status_t CowRecorder::setAudioEncoder(const char* mime)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setAudioEncoder(mime);
}

mm_status_t CowRecorder::setOutputFormat(const char* mime)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setOutputFormat(mime);
}

mm_status_t CowRecorder::setOutputFile(const char* filePath)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setOutputFile(filePath);
}

mm_status_t CowRecorder::setOutputFile(int fd)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setOutputFile(fd);
}

mm_status_t CowRecorder::setRecorderUsage(RecorderUsage usage)
{
    ENSURE_PIPELINE();
    return mPriv->setRecorderUsage(usage);
}

mm_status_t CowRecorder::getRecorderUsage(RecorderUsage &usage)
{
    return mPriv->getRecorderUsage(usage);
}

mm_status_t CowRecorder::setListener(Listener * listener)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mListenderSend = listener;
    return MM_ERROR_SUCCESS;
}

void CowRecorder::removeListener()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mListenderSend = NULL;
}

mm_status_t CowRecorder::setPreviewSurface(void * handle)
{
    FUNC_TRACK();
    CHECK_AUDIO_ONLY_VALID();
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    status = mPriv->mPipeline->setPreviewSurface(handle);
    return status;
}

mm_status_t CowRecorder::prepare()
{
    FUNC_TRACK();

    //NOTE: sync method SHOULD NOT hold mLock!
    //If we hold mLock here, it will lead to dead lock when components notify messages to pipeline
    //-->Component::notify
    //-->CowRecorder::ListenerRecorder::onMessage
    //-->CowRecorder::notify

    //MMAutoLock locker(mPriv->mLock);
    mm_status_t status = MM_ERROR_SUCCESS;
    ENSURE_PIPELINE();

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    if (mPriv->sendMsgBridge(CPP_MSG_prepareMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);

    return status;
}

void CowRecorder::Private::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(rspId);
    // FIXME, multiple pipelines run prepare in parallel
    mm_status_t status = MM_ERROR_SUCCESS;
    DEBUG("mUsage: 0x%x\n", mUsage);
    EXECUTE_PIPELINE_CMD(status, prepare);

    if (rspId)
        postReponse(rspId, status, NULL);
}

mm_status_t CowRecorder::prepareAsync()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_prepareAsyncMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowRecorder::Private::onPrepareAsync(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NOTNEED_RSP(rspId);
    mm_status_t status = MM_ERROR_SUCCESS;

    EXECUTE_PIPELINE_CMD(status, prepare);
}

mm_status_t CowRecorder::start()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_startMessage, 0, 0);
    return MM_ERROR_ASYNC;
}

void CowRecorder::Private::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = MM_ERROR_SUCCESS;

    Pipeline::ComponentStateType state;
    mPipeline->getState(state);
    if (state == Pipeline::kComponentStatePaused) {
        EXECUTE_PIPELINE_CMD(status, resume);

    } else {
        EXECUTE_PIPELINE_CMD(status, start);
    }
}

mm_status_t CowRecorder::stop()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_stopMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

mm_status_t CowRecorder::stopSync()
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    mm_status_t status;

    if (mPriv->sendMsgBridge(CPP_MSG_stopMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);
    return status;
}

void CowRecorder::Private::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    EXECUTE_PIPELINE_CMD(status, stop);

    if (rspId)
        postReponse(rspId, status, 0);
}

mm_status_t CowRecorder::pause()
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mPriv->postMsgBridge(CPP_MSG_pauseMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowRecorder::Private::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = MM_ERROR_SUCCESS;

    EXECUTE_PIPELINE_CMD(status, pause);

    if (rspId)
        postReponse(rspId, status, 0);
}

mm_status_t CowRecorder::reset()
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    mm_status_t status;

    if (mPriv->sendMsgBridge(CPP_MSG_resetMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);
    return status;

}

void CowRecorder::Private::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    PipelineRecorderBase *videoPipe = static_cast<PipelineRecorderBase*>(mPipeline.get());
    Pipeline::ComponentStateType state = Pipeline::kComponentStateNull;
    videoPipe->getState(state);
    //Don't stop again when pipeline is stopped or in stopping progress
    if (state != Pipeline::kComponentStateNull &&
        state != Pipeline::kComponentStateStop &&
        state != Pipeline::kComponentStateStopped) {
        EXECUTE_PIPELINE_CMD(status, stop);
    }

    EXECUTE_PIPELINE_CMD(status, reset);

    if (rspId)
        postReponse(rspId, status, 0);
}

bool CowRecorder::isRecording() const
{
    FUNC_TRACK();
    Pipeline::ComponentStateType state;
    ENSURE_PIPELINE();

    if (mPriv->mPipeline->getState(state) != MM_ERROR_SUCCESS)
        return false;

    if (state != Pipeline::kComponentStatePlaying)
        return false;

    return true;
}

mm_status_t CowRecorder::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();
    CHECK_AUDIO_ONLY_VALID();
    ENSURE_PIPELINE();

    return mPriv->mPipeline->getVideoSize(width, height);
}

mm_status_t CowRecorder::getCurrentPosition(int64_t& msec) const
{
    FUNC_TRACK();
    ENSURE_PIPELINE();

    PipelineRecorderBase *videoPipe = static_cast<PipelineRecorderBase*>(mPriv->mPipeline.get());
    return videoPipe->getCurrentPosition(msec);
}

mm_status_t CowRecorder::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    MMAutoLock locker(mPriv->mLock);
    ENSURE_PIPELINE();

    mm_status_t status = MM_ERROR_SUCCESS;
    status = mPriv->mPipeline->setParameter(meta);
    if (status != MM_ERROR_SUCCESS) {
        ERROR("setParameter failed, status %d\n", status);
    }
    return status;
}

mm_status_t CowRecorder::getParameter(MediaMetaSP & meta)
{
    FUNC_TRACK();
     mm_status_t status = MM_ERROR_UNKNOWN;
    ENSURE_PIPELINE();

    status = mPriv->mPipeline->getParameter(meta);
    return status;
}

mm_status_t CowRecorder::invoke(const MMParam * request, MMParam * reply)
{
    ERROR("unsupported invode yet\n");
    return MM_ERROR_UNSUPPORTED;
}

mm_status_t CowRecorder::notify(int msg, int param1, int param2, const MMParamSP param)
{
    // FUNC_TRACK();
    MMAutoLock locker(mLock);
    if ( !mListenderSend ) {
        ERROR("listerner is removed or not register, skip msg: %d\n", msg);
        return MM_ERROR_NOT_INITED;
    }

    mListenderSend->onMessage(msg, param1, param2, param);

    return MM_ERROR_SUCCESS;
}
mm_status_t CowRecorder::setMaxDuration(int64_t msec)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setMaxDuration(msec);
}

mm_status_t CowRecorder::setMaxFileSize(int64_t bytes)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->setMaxFileSize(bytes);
}

mm_status_t CowRecorder::setAudioConnectionId(const char * connectionId)
{
    FUNC_TRACK();
    ENSURE_PIPELINE();
    return mPriv->mPipeline->setAudioConnectionId(connectionId);
}

const char * CowRecorder::getAudioConnectionId() const
{
    FUNC_TRACK();
    if (!mPriv->mPipeline) {
        if (!mPriv->createDefaultPipeline())
            return "";
    }

    return mPriv->mPipeline->getAudioConnectionId();
}

} // YUNOS_MM
