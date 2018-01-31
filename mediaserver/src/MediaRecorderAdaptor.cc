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

#include "native_surface_help.h"
//libbase includes
#include <dbus/dbus.h>
#include <dbus/DMessage.h>
#include <dbus/DAdaptor.h>
//#include <dbus/DProxy.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <proc/ProcStat.h>
#include <pointer/SharedPtr.h>

#include <MediaRecorderAdaptor.h>

#include <multimedia/mm_debug.h>
#include <multimedia/mmparam.h>
#include <multimedia/media_meta.h>
#include <multimedia/mediarecorder.h>
#include <multimedia/component.h>

// support local window
#include <string/String.h>
#include <Permission.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;
#define MEDIA_SERVICE_PERMISSION_NAME  "MEDIA_SERVICE.permission.yunos.com"
#define REOCRD_AUDIO_PERMISSION_NAME  "RECORD_AUDIO.permission.yunos.com"

DEFINE_LOGTAG(MediaRecorderAdaptor)

MediaRecorderAdaptor::MediaRecorderAdaptor(const SharedPtr<DService>& service,
                   const String& path,
                   const String& iface,
                   pid_t pid,
                   uid_t uid)
    : DAdaptor(service, path, iface),
      MMSession(pid, uid, service->serviceName()),
      mWindowSurface(NULL),
      mFd(-1),
      mUid(uid) {

    INFO("create Media Recorder Session: %s", service->serviceName().c_str());

    mType = MU_Recorder;

    //mRecorder = MediaRecorder::create(MediaRecorder::RecorderType_COW);
    mListener.reset(new Listener(this));

    mRecorder = new CowRecorder(RecorderType_COWAudio);
    if ( !mRecorder ) {
        ERROR("failed to create recorder\n");
        return;
    }

/*
    if (audioOnly) {
        PipelineSP pipeline = Pipeline::createSP(new PipelineAudioRecorder());
        if (!pipeline) {
            MMLOGE("failed to create audio pipeline\n");
            fleave_nocode();
        }
        mRecorder->setPipeline(pipeline);
    }
*/
    mRecorder->setListener(mListener.get());
    setSessionState(INIT);
}

MediaRecorderAdaptor::~MediaRecorderAdaptor() {
    INFO("destruct MediaRecorderAdaptor");
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
    if (mRecorder != NULL)
        //MediaRecorder::destroy(mRecorder);
        delete mRecorder;
}

void MediaRecorderAdaptor::notify(int msg, int param1, int param2, const MMParamSP param) {
    DEBUG("notify msg(cow) %d, param1 %d param2 %d MMParam %p",
         msg, param1, param2, param.get());

    switch (msg) {
        case Component::kEventPrepareResult:
            msg = MediaRecorder::Listener::MSG_PREPARED;
            NOTIFY_STATUS(PREPARED);
            break;
        case Component::kEventEOS:
            msg = MediaRecorder::Listener::MSG_RECORDER_COMPLETE;
            break;
        case Component::kEventStartResult:
            msg = MediaRecorder::Listener::MSG_STARTED;
            NOTIFY_STATUS(PLAYING);
            break;
        case Component::kEventPaused:
            msg = MediaRecorder::Listener::MSG_PAUSED;
            NOTIFY_STATUS(PAUSED);
            break;
        case Component::kEventStopped:
            msg = MediaRecorder::Listener::MSG_STOPPED;
            NOTIFY_STATUS(STOPPED);
            break;
        case Component::kEventError:
            msg = MediaRecorder::Listener::MSG_ERROR;
            break;
        case Component::kEventMediaInfo:
            msg = MediaRecorder::Listener::MSG_INFO;
            break;
        case Component::kEventInfo:
            msg = MediaRecorder::Listener::MSG_INFO_EXT;
            break;
        case Component::kEventMusicSpectrum:
            msg = MediaRecorder::Listener::MSG_MUSIC_SPECTRUM;
            break;
        default:
            ERROR("unrecognized message: %d\n", msg);
            return;
    }

#if 0
    SharedPtr<DMessage> signal = obtainSignalMessage(mCallbackName);
    if (!signal) {
        ERROR("fail to create signal for callback: msg %d param1 %d param2 %d",
               msg, param1, param2);
        return;
    }

    signal->writeInt32(msg);
    signal->writeInt32(param1);
    signal->writeInt32(param2);
    sendMessage(signal);
#else
    if (mLooper)
        mLooper->sendTask(Task(MediaRecorderAdaptor::postNotify1, this, msg, param1, param2));
    else
        WARNING("looper is null, cannot send call back");
#endif
}

void MediaRecorderAdaptor::Listener::onMessage(
        int msg, int param1, int param2, const MMParamSP param) {
    if (mOwner)
        mOwner->notify(msg, param1, param2, param);
}

#define FINISH_METHOD_CALL(val)             \
        reply->writeInt32(status);          \
        sendMessage(reply);                 \
        return val;

#define RECORD_AUDIO_PERMISSION_CHECK() do {                                                                               \
    if (!audioPerm.checkPermission(msg, this)) {                                                                           \
        status = MM_ERROR_PERMISSION_DENIED;                                                                               \
        ERROR("operation not allowed, client (pid %d) need permission[%s]", msg->getPid(), REOCRD_AUDIO_PERMISSION_NAME);  \
        FINISH_METHOD_CALL(false);                                                                                         \
    }                                                                                                                      \
}while(0)

#define MEDIA_SERVICE_PERMISSION_CHECK() do {                                                                              \
    if (!servicePerm.checkPermission(msg, this)) {                                                                         \
        status = MM_ERROR_PERMISSION_DENIED;                                                                               \
        ERROR("operation not allowed, client (pid %d) need permission[%s]", msg->getPid(), MEDIA_SERVICE_PERMISSION_NAME); \
        FINISH_METHOD_CALL(false);                                                                                         \
    }                                                                                                                      \
}while(0)

bool MediaRecorderAdaptor::handleMethodCall(const SharedPtr<DMessage>& msg) {

    SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
    mm_status_t status = MM_ERROR_IVALID_OPERATION;

    if (!msg) {
        ERROR("get NULL msg");
        FINISH_METHOD_CALL(false);
    }

    if (!mRecorder) {
        ERROR("cow recorder is not created");
        FINISH_METHOD_CALL(false);
    }

    static PermissionCheck servicePerm(String(MEDIA_SERVICE_PERMISSION_NAME));
    MEDIA_SERVICE_PERMISSION_CHECK();

    static PermissionCheck audioPerm(String(REOCRD_AUDIO_PERMISSION_NAME));

    INFO("recorder(%s) get call %s",
        interface().c_str(), msg->methodName().c_str());
    uid_t uid = msg->getUid();

    if (msg->methodName() == "setListener") {
        mCallbackName = msg->readString();
        if (mCallbackName) {
            INFO("get signal name for callback: %s", mCallbackName.c_str());
            status = MM_ERROR_SUCCESS;
        }
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setCamera") {
        //int64_t param1 = msg->readInt64();
        //int64_t param2 = msg->readInt64();

        //VideoCapture *camera = reinterpret_cast<VideoCapture*>(param1);
        //RecordingProxy *proxy = reinterpret_cast<RecordingProxy*>(param2);

        //status= mRecorder->setCamera(camera, proxy);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setVideoSourceUri" ||
               msg->methodName() == "setAudioSourceUri") {
        String uri, str1, str2;
        int count, maxCnt = 10000, i;
        std::map<std::string, std::string>  headers;

        uri = msg->readString();
        count = msg->readInt32();

        /*
        if (count <= 0 || count > maxCnt) {
            status = mRecorder->setDataSource(uri.c_str(), &headers);
            FINISH_METHOD_CALL(true);
        }
        */

        for (i = 0; (i < count) && (count < maxCnt); i++) {
            str1 = msg->readString();
            str2 = msg->readString();
            headers[std::string(str1.c_str())] = std::string(str2.c_str());
        }

        if (msg->methodName() == "setVideoSourceUri")
            status = mRecorder->setVideoSourceUri(uri.c_str(), &headers);
        else
            status = mRecorder->setAudioSourceUri(uri.c_str(), &headers);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setVideoSourceFormat") {
        int width, height;
        uint32_t format;
        width = msg->readInt32();
        height = msg->readInt32();
        format = msg->readInt32();

        status= mRecorder->setVideoSourceFormat(width, height, format);
        ERROR("create CowAudioRecorder, video is not supported");
        status = MM_ERROR_IVALID_OPERATION;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setVideoEncoder") {
        String mime = msg->readString();

        status = mRecorder->setVideoEncoder(mime.c_str());
        ERROR("create CowAudioRecorder, video is not supported");
        status = MM_ERROR_IVALID_OPERATION;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setAudioEncoder") {
        String mime = msg->readString();

        status = mRecorder->setAudioEncoder(mime.c_str());
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setOutputFormat") {
        String mime = msg->readString();

        status = mRecorder->setOutputFormat(mime.c_str());
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setOutputFilePath") {
        String mime = msg->readString();

        status = mRecorder->setOutputFile(mime.c_str());
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setOutputFileFd") {
        if (mFd >= 0) {
            DEBUG("close file handle mFd: %d first", mFd);
            close(mFd);
            mFd = 0;
        }
        mFd = dup(msg->readFd());
        DEBUG("setOutputFileFd fd: %d", mFd);
        status = mRecorder->setOutputFile(mFd);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setRecorderUsage") {
        int usage = msg->readInt32();

        status = mRecorder->setRecorderUsage((RecorderUsage)usage);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "getRecorderUsage") {
        RecorderUsage usage;
        status = mRecorder->getRecorderUsage(usage);
        reply->writeInt32(status);
        reply->writeInt32((int)usage);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "setPreviewSurface") {
        // TODO create surface here
        int64_t handle = msg->readInt64();
#if 1
        if (handle == 0) {
            INFO("create preview target by plugin at service side");
            mWindowSurface = createSimpleSurface(640, 480);
            if (mWindowSurface) {
                //mWindowSurface->setOffset(0, 0);
                WINDOW_API(set_scaling_mode)(mWindowSurface,
                          SCALING_MODE_PREF(SCALE_TO_WINDOW));
            }
            status = mRecorder->setPreviewSurface(mWindowSurface);
            //status = mRecorder->setPreviewSurface(NULL);
        } else {
            INFO("assume nest surface preview target");
            status = mRecorder->setPreviewSurface((void *)"camera");
            FINISH_METHOD_CALL(true);
        }
#else
        status = mRecorder->setPreviewSurface(NULL);
#endif
        FINISH_METHOD_CALL(false);
    } else if (msg->methodName() == "prepare") {
        RECORD_AUDIO_PERMISSION_CHECK();
        setSessionState(PREPARING);
        status = mRecorder->prepare();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "reset") {
        setSessionState(RESETTING);
        onSessionStateUpdate(true);

        status = mRecorder->reset();
        setSessionState(RESET);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "start") {
        RECORD_AUDIO_PERMISSION_CHECK();
        setSessionState(STARTING);
        onSessionStateUpdate(false);

        status = mRecorder->start();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "stop") {
        RECORD_AUDIO_PERMISSION_CHECK();
        setSessionState(STOPPING);
        onSessionStateUpdate(true);

        status = mRecorder->stop();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "stopSync") {
        RECORD_AUDIO_PERMISSION_CHECK();
        setSessionState(STOPPING);
        onSessionStateUpdate(true);

        status = mRecorder->stopSync();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "pause") {
        RECORD_AUDIO_PERMISSION_CHECK();
        setSessionState(PAUSING);
        onSessionStateUpdate(true);

        status = mRecorder->pause();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "isRecording") {
        bool ret;

        ret = mRecorder->isRecording();
        reply->writeBool(ret);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getVideoSize") {
        int width, height;

        status = mRecorder->getVideoSize(width, height);
        reply->writeInt32(status);
        reply->writeInt32(width);
        reply->writeInt32(height);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getCurrentPosition") {
        int64_t msec;

        status = mRecorder->getCurrentPosition(msec);
        reply->writeInt32(status);
        reply->writeInt64(msec);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getParameter") {
        MediaMetaSP meta;
        status = mRecorder->getParameter(meta);
        meta->dump();
        meta->writeToMsg(reply);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setParameter") {
        MediaMetaSP meta = MediaMeta::create();
        meta->readFromMsg(msg);
        status = mRecorder->setParameter(meta);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "invoke") {

        ERROR("recorder service doesn't support it");
        status = MM_ERROR_SUCCESS;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setMaxDuration") {
        int64_t msec = msg->readInt64();

        status = mRecorder->setMaxDuration(msec);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setMaxFileSize") {
        int64_t bytes = msg->readInt64();

        status = mRecorder->setMaxFileSize(bytes);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "tearDown") {
        if (mUid == uid) {
            if (mRecorder) {
                delete mRecorder;
                mRecorder = NULL;
            }

            INFO("cow recorder is destructed");

            if (mWindowSurface)
                destroySimpleSurface(mWindowSurface);

            mWindowSurface = NULL;
        }

        status = MM_ERROR_SUCCESS;

        FINISH_METHOD_CALL(true);
    }

    ERROR("unknown method call");
    FINISH_METHOD_CALL(false);
}

void MediaRecorderAdaptor::onDeath(const DLifecycleListener::DeathInfo& deathInfo) {
    INFO("client die: %s", interface().c_str());
    DAdaptor::onDeath(deathInfo);
    if (mRecorder) {
        mRecorder->stop();
        mRecorder->reset();
    }

    setSessionState(INVALID);
    onSessionStateUpdate(true);

    onSessionDeath(MU_Recorder);
}

void MediaRecorderAdaptor::onBirth(const DLifecycleListener::BirthInfo& brithInfo) {
    INFO("client birth: %s", interface().c_str());
    DAdaptor::onBirth(brithInfo);

    setSessionState(CONNECTED);
}

const char* MediaRecorderAdaptor::debugInfoMsg() {
    mDebugInfoMsg = "+ ";
    mDebugInfoMsg.append(mSessionName);
    mDebugInfoMsg.append("\n");

    char temp[64];

    mDebugInfoMsg.append("client pid: ");
    sprintf(temp, "%d, ", mPid);
    mDebugInfoMsg.append((const char*)temp);

    mDebugInfoMsg.append("cmdline: ");
    String str = ProcStat::getCmdLine(mPid);
    mDebugInfoMsg.append(ProcStat::getCmdLine(mPid));
    mDebugInfoMsg.append(", ");

    mDebugInfoMsg.append("state: ");
    mDebugInfoMsg.append(stateToString(getSessionState()));


    mDebugInfoMsg.append("\n");
    mDebugInfoMsg.append("\n");

    return mDebugInfoMsg.c_str();
}

/* static */
void MediaRecorderAdaptor::postNotify1(MediaRecorderAdaptor* p, int msg, int param1, int param2) {
    if (!p) {
        ERROR("adaptor is null");
        return;
    }

    p->postNotify(msg, param1, param2);
}

// run on LooperThread
void MediaRecorderAdaptor::postNotify(int msg, int param1, int param2) {

    INFO("notify msg %d, param1 %d param2 %d",
         msg, param1, param2);

    SharedPtr<DMessage> signal = obtainSignalMessage(mCallbackName);
    if (!signal) {
        ERROR("fail to create signal for callback: msg %d param1 %d param2 %d",
               msg, param1, param2);
        return;
    }

    signal->writeInt32(msg);
    signal->writeInt32(param1);
    signal->writeInt32(param2);
    sendMessage(signal);
}

} // end of namespace YUNOS_MM
