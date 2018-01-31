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
#include "multimedia/pipeline_basic.h"
#include "multimedia/cowapp_basic.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("AppBasic")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()
static const char * MMMSGTHREAD_NAME = "CowAppBasic";

class CowAppBasic::PipelineListener : public Pipeline::Listener
{
  public:
    PipelineListener(CowAppBasic* player)
        :mWatcher(player)     { }
    virtual ~PipelineListener() { }
    void removeWatcher()
    {
        MMAutoLock locker(mWatchLock);
        mWatcher = NULL;
    }
    void onMessage(int msg, int param1, int param2, const MMParamSP obj)
    {
        //FUNC_TRACK();
        MMAutoLock locker(mWatchLock);
        if (mWatcher) {
            mWatcher->notify(msg, param1, param2, obj);
        }
        else
            WARNING("new msg(%d) comes, but watcher left already\n", msg);
    }

  private:
    CowAppBasic* mWatcher;
    Lock mWatchLock;

    MM_DISALLOW_COPY(PipelineListener)
};

#define CPP_MSG_prepareMessage (MMMsgThread::msg_type)1
#define CPP_MSG_prepareAsyncMessage (MMMsgThread::msg_type)2
#define CPP_MSG_startMessage (MMMsgThread::msg_type)3
#define CPP_MSG_stopMessage (MMMsgThread::msg_type)4
#define CPP_MSG_pauseMessage (MMMsgThread::msg_type)5
#define CPP_MSG_resetMessage (MMMsgThread::msg_type)6


BEGIN_MSG_LOOP(CowAppBasic)
    MSG_ITEM(CPP_MSG_prepareMessage, onPrepare)
    MSG_ITEM(CPP_MSG_prepareAsyncMessage, onPrepareAsync)
    MSG_ITEM(CPP_MSG_startMessage, onStart)
    MSG_ITEM(CPP_MSG_stopMessage, onStop)
    MSG_ITEM(CPP_MSG_pauseMessage, onPause)
    MSG_ITEM(CPP_MSG_resetMessage, onReset)
END_MSG_LOOP()

CowAppBasic::CowAppBasic()
    : MMMsgThread(MMMSGTHREAD_NAME)
    , mListenderSend(NULL)
{
    FUNC_TRACK();
    mListenerReceive.reset(new PipelineListener(this));
}

CowAppBasic::~CowAppBasic()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    PipelineListener* listener = DYNAMIC_CAST<PipelineListener*>(mListenerReceive.get());
    if(listener){
        listener->removeWatcher();
    }

    if (mPipeline) {
        Pipeline::ComponentStateType state = Pipeline::kComponentStateInvalid;
        status = mPipeline->getState(state);
        if (status != MM_ERROR_SUCCESS || (state != Pipeline::kComponentStateNull && state != Pipeline::kComponentResetComplete)) {
            ERROR("!!!FATAL ERROR, reset() should be call before destruction");
            // we can't call reset here. the MMMsgThread::exit() can't be called in destruction
            return;
        }
        DEBUG();
        mPipeline.reset();
        DEBUG();
    }

}

static void releaseCowAppBasic(CowAppBasic *player)
{
    if (player) {
        player->reset();
        delete player;
        player = NULL;
    }
}
CowAppBasicSP CowAppBasic::create(CowAppBasic* app)
{
    CowAppBasicSP sp;
    if (app)
        sp.reset(app, releaseCowAppBasic);
    return sp;
}

mm_status_t CowAppBasic::setListener(Listener * listener)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    mListenderSend = listener;
    return MM_ERROR_SUCCESS;
}

void CowAppBasic::removeListener()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
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

mm_status_t CowAppBasic::setVideoSurface(void * handle, bool isTexture)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);
    return mPipeline->setVideoSurface(handle, isTexture);
}

mm_status_t CowAppBasic::setPipeline(PipelineSP pipeline) {
    FUNC_TRACK();
    if (pipeline) {
        mPipeline = pipeline;
        mPipeline->setListener(mListenerReceive);
    }
    return MM_ERROR_SUCCESS;
}

mm_status_t CowAppBasic::prepare()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;

    // runs the msg thread
    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        return MM_ERROR_OP_FAILED;
    }

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    if (sendMsg(CPP_MSG_prepareMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);

    DEBUG("status: %d", status);
    return status;

}

void CowAppBasic::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT_NEED_RSP(rspId);
    mm_status_t status = mPipeline->prepare();

    if (status != MM_ERROR_SUCCESS) {
        ERROR("prepare faild\n");
    }

    DEBUG("status: %d", status);
    postReponse(rspId, status, 0);
}

mm_status_t CowAppBasic::prepareAsync()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    postMsg(CPP_MSG_prepareAsyncMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowAppBasic::onPrepareAsync(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = mPipeline->prepare();
    CHECK_PIPELINE_RET(status, "prepareAsync");
}

mm_status_t CowAppBasic::start()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    postMsg(CPP_MSG_startMessage, 0, 0);
    return MM_ERROR_ASYNC;
}

void CowAppBasic::onStart(param1_type param1, param2_type param2, uint32_t rspId)
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

mm_status_t CowAppBasic::stop()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    postMsg(CPP_MSG_stopMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowAppBasic::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    mm_status_t status = mPipeline->stop();
    CHECK_PIPELINE_RET(status, "stop");

    if (rspId)
        postReponse(rspId, status, 0);
}

mm_status_t CowAppBasic::pause()
{
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    postMsg(CPP_MSG_pauseMessage, 0, 0);

    return MM_ERROR_ASYNC;
}

void CowAppBasic::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    ASSERT(rspId == 0);
    mm_status_t status = mPipeline->pause();

    CHECK_PIPELINE_RET(status, "pause");
}

mm_status_t CowAppBasic::reset()
{
    FUNC_TRACK();

    MMMsgThread::param1_type rsp_param1;
    MMMsgThread::param2_type rsp_param2;
    mm_status_t status = MM_ERROR_SUCCESS;
    if (mPipeline) {
        Pipeline::ComponentStateType state = Pipeline::kComponentStateInvalid;
        status = mPipeline->getState(state);
        if (status == MM_ERROR_SUCCESS && state != Pipeline::kComponentStateStopped && state != Pipeline::kComponentStateStop) {
            if (sendMsg(CPP_MSG_stopMessage, 0, 0, &rsp_param1, &rsp_param2)) {
                return MM_ERROR_UNKNOWN;
            }
            status = mm_status_t(rsp_param1);
        }
    }

    if (sendMsg(CPP_MSG_resetMessage, 0, 0, &rsp_param1, &rsp_param2)) {
        return MM_ERROR_UNKNOWN;
    }
    status = mm_status_t(rsp_param1);

    // quit the msg thread
    MMMsgThread::exit();

    return status;
}

void CowAppBasic::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    mm_status_t status = mPipeline->reset();

    CHECK_PIPELINE_RET(status, "reset");

    if (rspId)
        postReponse(rspId, status, 0);
}

bool CowAppBasic::isPlaying() const
{
    FUNC_TRACK();
    Pipeline::ComponentStateType state;

    if (mPipeline->getState(state) != MM_ERROR_SUCCESS)
        return false;

    DEBUG("state : %d", state);
    if (state != Pipeline::kComponentStatePlaying)
        return false;

    return true;
}

mm_status_t CowAppBasic::getVideoSize(int& width, int& height) const
{
    FUNC_TRACK();

    return mPipeline->getVideoSize(width, height);
}

mm_status_t CowAppBasic::getCurrentPosition(int64_t& msec) const
{
    //FUNC_TRACK();

    mm_status_t status = mPipeline->getCurrentPosition(msec);
    DEBUG("current playback position: %" PRId64 " ms", msec);
    return status;
}

mm_status_t CowAppBasic::getDuration(int64_t& msec) const
{
    FUNC_TRACK();
    return mPipeline->getDuration(msec);
}

// A language code in either way of ISO-639-1 or ISO-639-2. When the language is unknown or could not be determined, MM_ERROR_UNSUPPORTED will returned
mm_status_t CowAppBasic::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    //MMAutoLock locker(mLock);

    mm_status_t status = mPipeline->setParameter(meta);
    return status;
}

mm_status_t CowAppBasic::getParameter(MediaMetaSP & meta)
{
    FUNC_TRACK();
     mm_status_t status = MM_ERROR_UNKNOWN;
    // not necessary to run on another thread
    status = mPipeline->getParameter(meta);
    return status;
}

mm_status_t CowAppBasic::notify(int msg, int param1, int param2, const MMParamSP param)
{
    //FUNC_TRACK();
    {
        MMAutoLock locker(mLock);
        if ( !mListenderSend ) {
            ERROR("listerner is removed or not register, skip msg: %d\n", msg);
            return MM_ERROR_SUCCESS;
        }
        mListenderSend->onMessage(msg, param1, param2, param);
    }
    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM
