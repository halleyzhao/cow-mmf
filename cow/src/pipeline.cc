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
#include "multimedia/pipeline.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/elapsedtimer.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("COW-PLB")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()


static MediaMetaSP nilMeta;
static const char * MMMSGTHREAD_NAME = "Pipeline";

const char* Pipeline::sInternalStateStr[] = {
    "Null",
    "Loading",
    "Loaded",
    "Preparing",
    "Prepared",
    "Pausing",
    "Paused",
    "Play",
    "Playing",
    "Stop",
    "Stopped",
    "Invalid",
    "seekComplete",
    "ResetComplete",
    "FlushComplete",
    "EOSComplete"
};


class ListenerPipeline : public Component::Listener
{
  public:
    ListenerPipeline(Pipeline* pipeline)
        :mWatcher(pipeline)
    {
    }
    ~ListenerPipeline()
    {
    }
    void removeWatcher()
    {
        // FIXME, use weak_ptr for mWatcher is another option
        MMAutoLock locker(mLock);
        DEBUG("remove watch");
        mWatcher = NULL;
    }
    void onMessage(int msg, int param1, int param2, const MMParamSP obj, const Component * sender)
    {
        MMRefBaseSP refBase (new PipelineParamRefBase(msg, sender, obj));

        // bridge the information to Pipeline
        {
            MMAutoLock locker(mLock);
            INFO("new msg(%d:%d) from %s arrived\n", msg, param1, sender->name());
            if (mWatcher) {
                // FIXME, it's a bug for 64bit support, supress warning for the time being
                intptr_t p2 = param2;
                VERBOSE("new msg(%d) from %s arrived\n", msg, sender->name());
                mWatcher->postMsgBridge(PL_MSG_componentMessage, param1, (void*)p2, refBase);
            }
            else {
                WARNING("new msg(%d) from %s arrived, but watcher left already\n", msg, sender->name());
                Pipeline::printMsgInfo(msg, param1, sender->name());
            }
        }
    }
  private:
    Pipeline* mWatcher;
    Lock mLock;

    MM_DISALLOW_COPY(ListenerPipeline)
};


void Pipeline::printMsgInfo(int event, int param1, const char* _sender)
{
    const char* sender = _sender ? _sender : "--";

    if (event>=0 && event <Component::kEventInfo)
        INFO("got event %s from %s\n", Component::sEventStr[event], sender);
    else if (event == Component::kEventInfo && (param1 >=Component::kEventInfoDiscontinuety && param1 <= Component::kEventInfoMediaRenderStarted))
        INFO("got kEventInfo param1=%s from %s\n", Component::sEventInfoStr[param1], sender);
    else
        WARNING("got event=%d, param1=%d from %s\n", event, param1, sender);

}



PipelineSP Pipeline::create(Pipeline *pipeline, Pipeline::ListenerSP listenerReceive)
{
    FUNC_TRACK();
    PipelineSP pipelineSP;
    if (!pipeline)
        return pipelineSP;

    pipelineSP.reset(pipeline, MMMsgThread::releaseHelper);

    if (pipelineSP->init() != MM_ERROR_SUCCESS) {
        pipelineSP.reset();
        return pipelineSP;
    }

    if (listenerReceive) {
        pipelineSP->setListener(listenerReceive);
    }
    return pipelineSP;
}

Pipeline::Pipeline() :  MMMsgThread(MMMSGTHREAD_NAME)
                        , mCondition(mLock)
                        , mEscapeWait(false)
                        , mErrorCode(0)
{
    FUNC_TRACK();
    mListenerReceive.reset(new ListenerPipeline(this));
}

Pipeline::~Pipeline()
{
    FUNC_TRACK();
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    if(listener){
        listener->removeWatcher();
    }
}

ComponentSP Pipeline::createComponentHelper(const char* componentName, const char* mimeType, bool isEncoder)
{
    FUNC_TRACK();
    ComponentSP comp;

    comp = ComponentFactory::create(componentName, mimeType, isEncoder);
    if (!comp)
        return comp;

    if (mListenerReceive) {
        comp->setListener(mListenerReceive);
    }

    return comp;
}

ComponentSP Pipeline::createComponentHelper(const char* componentName, const char* mimeType, Component::ListenerSP listener, bool isEncoder )
{
    FUNC_TRACK();
    ComponentSP comp;

    comp = ComponentFactory::create(componentName, mimeType, isEncoder);
    if (!comp)
        return comp;

    if (listener)
        comp->setListener(listener);

    if (!comp)
        ERROR("fail to create component <%s:%s> %s\n", componentName, mimeType, isEncoder ? "encoder" : "");

    return comp;

}

mm_status_t Pipeline::notify(int msg, int param1, int param2, const MMParamSP obj)
{
    if (msg == Component::kEventInfo && (param1 >=Component::kEventInfoDiscontinuety && param1 <= Component::kEventInfoMediaRenderStarted))
        INFO("sending kEventInfo param1=%s, param2=%d to client app\n", Component::sEventInfoStr[param1], param2);
    else if (msg >= 0 && msg < Component::kEventMax)
        INFO("sending event %s with params (%d, %d) to app client\n", Component::sEventStr[msg], param1, param2);
    else
        WARNING("sending event=%d, param1=%d, param2=%d to client app\n", msg, param1, param2);

    if ( !mListenerSend ) {
        ERROR("no registered listener of Pipeline\n");
        return MM_ERROR_NOT_INITED;
    }

    mListenerSend->onMessage(msg, param1, param2, obj);
    return MM_ERROR_SUCCESS;
}

void Pipeline::dump()
{
    uint32_t i=0;
    for (i=0; i<mComponents.size(); i++) {
        DEBUG("%s state %s, state2 %s\n",
            mComponents[i].component->name(),
            sInternalStateStr[mComponents[i].state],
            sInternalStateStr[mComponents[i].state2]);
    }
}

// bridge Component event to Pipeline MMMsgThread
int Pipeline::postMsgBridge(msg_type what, param1_type param1, param2_type param2, param3_type param3, int64_t timeoutUs)
{
    FUNC_TRACK();
    return postMsg(what, param1, param2, timeoutUs, param3);
}

/* bridge CowPlayer event to Pipeline MMMsgThread, since CowPlayer and Pipeline post event to app in single thread.
 1. mm_status_t CowPlayer::seek(int64_t msec)
     mPriv->mPipeline->postCowMsgBridge(Component::kEventSeekComplete, MM_ERROR_SUCCESS, NULL, 5);
 2. int Pipeline::postCowMsgBridge(Component::Event event, param1_type param1, param2_type param2, int64_t timeoutUs)
     return postMsg(PL_MSG_cowMessage, param1, param2, timeoutUs, refBase);
 3. void PipelinePlayerBase::onCowMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId)
     notify(event, param1, 0, nilParam);
 4. mm_status_t Pipeline::notify(int msg, int param1, int param2, const MMParamSP obj)
     mListenerSend->onMessage(msg, param1, param2, obj);
 5. class CowPlayer::ListenerPlayer : public Pipeline::Listener
     {
         void onMessage(int msg, int param1, int param2, const MMParamSP obj)
                 mWatcher->notify(msg, param1, param2, obj);
         CowPlayer* mWatcher;
     };
 6. mm_status_t CowPlayer::notify(int msg, int param1, int param2, const MMParamSP param)
         mListenderSend->onMessage(msg, param1, param2, param);
 */
int Pipeline::postCowMsgBridge(Component::Event event, param1_type param1, param2_type param2, int64_t timeoutUs)
{
    FUNC_TRACK();

    MMRefBaseSP refBase (new PipelineParamRefBase(event, NULL, nilParam));
    return postMsg(PL_MSG_cowMessage, param1, param2, timeoutUs, refBase);
}

mm_status_t Pipeline::init() {
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
        return MM_ERROR_OP_FAILED;
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t Pipeline::uninit() {
    FUNC_TRACK();
    MMMsgThread::exit();
    return MM_ERROR_SUCCESS;
}

/*static*/std::string Pipeline::getSourceUri(const char * uri, bool isAudio) {
    DEBUG("uri: %s\n", uri);
    std::string avUri;

    if (!strncmp(uri, "rtsp://", 7)) {
        return MEDIA_MIMETYPE_MEDIA_EXTERNAL_SOURCE;
    }

    if (!isAudio) {
        if (!strncmp(uri,"file", 4)) {
            avUri = MEDIA_MIMETYPE_VIDEO_FILE_SOURCE;
        } else if (!strncmp(uri,"auto", 4)) {
            avUri = MEDIA_MIMETYPE_VIDEO_TEST_SOURCE;
        } else if (!strncmp(uri,"camera", 6)) {
        #if (defined __MM_NATIVE_BUILD__)
            avUri = MEDIA_MIMETYPE_VIDEO_UVC_SOURCE;
        #else
            avUri =  MEDIA_MIMETYPE_MEDIA_CAMERA_SOURCE;
        #endif
        } else if (!strncmp(uri,"wfd", 3)) {
            avUri = MEDIA_MIMETYPE_VIDEO_WFD_SOURCE;
        } else if (!strncmp(uri,"screen", 6)) {
            avUri = MEDIA_MIMETYPE_VIDEO_SCREEN_SOURCE;
        } else if (!strncmp(uri,"surface", 7)) {
            avUri = MEDIA_MIMETYPE_VIDEO_SURFACE_SOURCE;
        } else {
            avUri = MEDIA_MIMETYPE_VIDEO_TEST_SOURCE;
        }
    } else {
#ifndef __DISABLE_AUDIO_STREAM__
     if (!strncmp(uri, "file", 4)) {
        avUri = MEDIA_MIMETYPE_AUDIO_SOURCE_FILE;
     } else if (!strncmp(uri,"pulse", 5)) {
        avUri = MEDIA_MIMETYPE_AUDIO_SOURCE;
     } else if (!strncmp(uri,"cras", 4)) {
        avUri = MEDIA_MIMETYPE_AUDIO_SOURCE;
     } else {
        avUri = MEDIA_MIMETYPE_AUDIO_SOURCE;
     }
#endif
    }

    return avUri;
}

} // YUNOS_MM
