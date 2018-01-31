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

//libbase includes
#include <dbus/dbus.h>
#include <dbus/DMessage.h>
#include <dbus/DAdaptor.h>
//#include <dbus/DProxy.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <proc/ProcStat.h>
#include <pointer/SharedPtr.h>

#include <MediaPlayerAdaptor.h>
#include <SideBandIPC.h>

#include <multimedia/mm_debug.h>
#include <multimedia/mmparam.h>
#include <multimedia/mediaplayer.h>
#include <media_surface_texture.h>

// support local window
#include <string/String.h>
#include <Permission.h>

namespace YUNOS_MM {

MMParamSP nullParam;

// libbase name space
using namespace yunos;
#define MEDIA_SERVICE_PERMISSION_NAME  "MEDIA_SERVICE.permission.yunos.com"

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

DEFINE_LOGTAG(MediaPlayerAdaptor)

MediaPlayerAdaptor::MediaPlayerAdaptor(const SharedPtr<DService>& service,
                   const String& path,
                   const String& iface,
                   pid_t pid,
                   uid_t uid,
                   int playType)
    : DAdaptor(service, path, iface),
      MMSession(pid, uid, service->serviceName()),
      mWindowSurface(NULL),
      mMst(NULL),
      mMstShow(false),
      mUid(uid),
      mCurBuffer(NULL) {
    ENTER1();

    mType = MU_Player;
    INFO("create Media Player Session: %s", service->serviceName().c_str());

    mListener.reset(new Listener(this));
    mMstListener.reset(new MstListener(this));
    mUseMstListener = false;

    mPlayer = new CowPlayer(playType);
    if ( !mPlayer ) {
        ERROR("failed to create player\n");
        EXIT1();
    }

/*
    if (audioOnly) {
        PipelineSP pipeline = Pipeline::create(new PipelineAudioPlayer());
        if (!pipeline) {
            MMLOGE("failed to create audio pipeline\n");
            fleave_nocode();
        }
        mPlayer->setPipeline(pipeline);
    }
*/
    mPlayer->setListener(mListener.get());
    setSessionState(INIT);

    EXIT1();
}

MediaPlayerAdaptor::~MediaPlayerAdaptor() {
    ENTER1();

    if (mPlayer != NULL)
        //MediaPlayer::destroy(mPlayer);
        delete mPlayer;

    if (mMst)
        delete mMst;

    if (mWindowSurface) {
        if (mProducerName.c_str()) {
            mWindowSurface->unrefMe();
        }
        else
            destroySimpleSurface(((WindowSurface*)mWindowSurface));
    }

    INFO("surface is destroy");

    EXIT1();
}

void MediaPlayerAdaptor::notify(int msg, int param1, int param2, const MMParamSP param) {
    DEBUG("notify msg(cow) %d, param1 %d param2 %d MMParam %p",
         msg, param1, param2, param.get());

    switch (msg) {
        case Component::kEventPrepareResult:
            msg = MediaPlayer::Listener::MSG_PREPARED;
            NOTIFY_STATUS(PREPARED);
            break;
        case Component::kEventEOS:
            msg = MediaPlayer::Listener::MSG_PLAYBACK_COMPLETE;
            break;
        case Component::kEventInfoBufferingUpdate:
            msg = MediaPlayer::Listener::MSG_BUFFERING_UPDATE;
            mBuffering_100 = param1;
            break;
        case Component::kEventSeekComplete:
            msg = MediaPlayer::Listener::MSG_SEEK_COMPLETE;
            break;
        case Component::kEventGotVideoFormat:
            msg = MediaPlayer::Listener::MSG_SET_VIDEO_SIZE;
            break;
        case Component::kEventStartResult:
            msg = MediaPlayer::Listener::MSG_STARTED;
            NOTIFY_STATUS(PLAYING);
            break;
        case Component::kEventPaused:
            msg = MediaPlayer::Listener::MSG_PAUSED;
            NOTIFY_STATUS(PAUSED);
            break;
        case Component::kEventStopped:
            msg = MediaPlayer::Listener::MSG_STOPPED;
            NOTIFY_STATUS(STOPPED);
            break;
        case Component::kEventError:
            msg = MediaPlayer::Listener::MSG_ERROR;
            break;
        case Component::kEventMediaInfo:
            msg = MediaPlayer::Listener::MSG_INFO;
            break;
        case Component::kEventInfo:
            msg = MediaPlayer::Listener::MSG_INFO_EXT;
            switch (param1)
            {
                case Component::kEventInfoVideoRenderStart:
                    {
                        INFO("try to send TYPE_MSG_FIRST_FRAME_TIME msg\n");
                        // mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO_EXT, 306/* TYPE_MSG_FIRST_FRAME_TIME */, 0, &param);
                        param1 = 306;
                    }
                break;
                default:
                    // mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO_EXT, param1, 0, &param);
                     ;
                break;
            }
            break;
        case Component::kEventUpdateTextureImage:
            msg = MediaPlayer::Listener::MSG_UPDATE_TEXTURE_IMAGE;
            break;
        case Component::kEventInfoSubtitleData:
            msg = MediaPlayer::Listener::MSG_SUBTITLE_UPDATED;
            break;
        case Component::kEventInfoDuration:
            msg = MediaPlayer::Listener::MSG_DURATION_UPDATE;
            break;
        case Component::kEventVideoRotationDegree:
            msg = MediaPlayer::Listener::MSG_VIDEO_ROTATION_DEGREE;
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
        mLooper->sendTask(Task(MediaPlayerAdaptor::postNotify1, this, msg, param1, param2, param));
    else
        WARNING("looper is null, cannot send call back");
#endif
}

void MediaPlayerAdaptor::Listener::onMessage(
        int msg, int param1, int param2, const MMParamSP param) {
    if (mOwner)
        mOwner->notify(msg, param1, param2, param);
}

void MediaPlayerAdaptor::MstListener::onMessage(int msg, int param1, int param2) {
        if (mOwner) {
            mOwner->notify(Component::kEventUpdateTextureImage, param1, param2, nullParam);
         }
}

#define FINISH_METHOD_CALL(val)             \
        reply->writeInt32(status);          \
        sendMessage(reply);                 \
        return val;

bool MediaPlayerAdaptor::handleMethodCall(const SharedPtr<DMessage>& msg) {

    SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
    mm_status_t status = MM_ERROR_IVALID_OPERATION;

    if (!msg) {
        ERROR("get NULL msg");
        FINISH_METHOD_CALL(false);
    }

    if (!mPlayer) {
        ERROR("media player is not created");
        FINISH_METHOD_CALL(false);
    }

    static PermissionCheck perm(String(MEDIA_SERVICE_PERMISSION_NAME));
    if (!perm.checkPermission(msg, this)) {
        ERROR("operation not allowed, client (pid %d) need permission[%s]", msg->getPid(), MEDIA_SERVICE_PERMISSION_NAME);
        FINISH_METHOD_CALL(false);
    }

    INFO("player(%s) get call %s",
        interface().c_str(), msg->methodName().c_str());
    uid_t uid = msg->getUid();

    if (msg->methodName() == "setListener") {
        mCallbackName = msg->readString();
        if (mCallbackName) {
            INFO("get signal name for callback: %s", mCallbackName.c_str());
            status = MM_ERROR_SUCCESS;
        }
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setDataSourceUri") {
        String uri, str1, str2;
        int count, maxCnt = 10000, i;
        std::map<std::string, std::string>  headers;

        uri = msg->readString();
        count = msg->readInt32();
        mUri = uri;

        if (count <= 0 || count > maxCnt) {
            status = mPlayer->setDataSource(uri.c_str(), &headers);
            FINISH_METHOD_CALL(true);
        }

        for (i = 0; i < count; i++) {
            str1 = msg->readString();
            str2 = msg->readString();
            headers[std::string(str1.c_str())] = std::string(str2.c_str());
        }

        status = mPlayer->setDataSource(uri.c_str(), &headers);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setDataSourceMem") {
        FINISH_METHOD_CALL(false);
    } else if (msg->methodName() == "setDataSourceFd") {
        int fd;
        int64_t offset, length;
        fd = msg->readFd();
        offset = msg->readInt64();
        length = msg->readInt64();

        status= mPlayer->setDataSource(fd, offset, length);

        mUri = "";
        mUri.appendFormat("fd %d offset %" PRId64 " length %" PRId64 "", fd, offset, length);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setSubtitleSource") {
        String uri = msg->readString();
        if (!strcmp(uri.c_str(), "bad-name")) {
            ERROR("invalid parameter for subtitle uri");
            FINISH_METHOD_CALL(true);
        } else {
            status = mPlayer->setSubtitleSource(uri.c_str());
        }

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "sideBandChannel") {
        reply->writeInt32(MM_ERROR_SUCCESS);
        String str = interface();
        str.append(".sideband");
        INFO("sideband channel: %s", str.c_str());
        reply->writeString(str);
        sendMessage(reply);
        mSideBand.reset(new SideBandIPC(str.c_str(), false));
        return true;
    } else if (msg->methodName() == "sideBandChannel2") {
        reply->writeInt32(MM_ERROR_SUCCESS);
        String str("/mnt/data/share/media/.");
        str.append(interface());
        str.append(".sideband2");
        INFO("sideband channel: %s", str.c_str());
        reply->writeString(str);
        mSideBand2.reset(new SideBandIPC(str.c_str(), true));
        if (!mSideBand2->initAsync()) {
            ERROR("fail to init side band ipc");
        }

        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "setVideoDisplay") {
        String name = msg->readString();
        if (!strcmp(name.c_str(), "bad-name")) {
            mWindowSurface = createSimpleSurface(640, 480);
            if (mWindowSurface) {
                //mWindowSurface->setOffset(0, 0);
                WINDOW_API(set_scaling_mode)(mWindowSurface,
                          SCALING_MODE_PREF(SCALE_TO_WINDOW));
            }
        } else {
            INFO("setVideoDisplay get producer %s", name.c_str());

            mProducerName = name;

            mWindowSurface = new libgui::Surface(name.c_str());
        }

        status = mPlayer->setVideoSurface(mWindowSurface);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setVideoSurfaceTexture") {
        if (!mMst)
            mMst = new YunOSMediaCodec::MediaSurfaceTexture;

        if (mWindowSurface) {
            mMst->setWindowSurface(mWindowSurface);
            mMst->setShowFlag(mMstShow);
        }

        if (mUseMstListener) {
            INFO("set MST listener");
            mMst->setListener(mMstListener.get());
        } else {
            INFO("clear MST listener");
            mMst->setListener(NULL);
        }

        status = mPlayer->setVideoSurface(mMst, true);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "prepare") {
        setSessionState(PREPARING);
        status = mPlayer->prepare();
        setSessionState(PREPARED);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "prepareAsync") {
        setSessionState(PREPARING);
        status = mPlayer->prepareAsync();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "reset") {
        // reply to unblock client
        reply->writeInt32(MM_ERROR_SUCCESS);
        sendMessage(reply);

        setSessionState(RESETTING);
        onSessionStateUpdate(true);

        status = mPlayer->reset();
        setSessionState(RESET);
        INFO("player is reset");

        if (mMst) {
            delete mMst;
            mMst = NULL;
        }

        return true;
    } else if (msg->methodName() == "setVolume") {
        MediaPlayer::VolumeInfo volume;
        volume.left = (float)msg->readDouble();
        volume.right = (float)msg->readDouble();

        status = mPlayer->setVolume(volume.left, volume.right);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "getVolume") {
        MediaPlayer::VolumeInfo volume;

        status = mPlayer->getVolume(volume.left, volume.right);
        reply->writeInt32(status);
        reply->writeDouble((double)volume.left);
        reply->writeDouble((double)volume.right);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "setMute") {
        bool mute = msg->readBool();

        status = mPlayer->setMute(mute);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "getMute") {
        bool mute;

        status = mPlayer->getMute(&mute);
        reply->writeInt32(status);
        reply->writeBool(mute);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "start") {
        setSessionState(STARTING);
        onSessionStateUpdate(false);

        status = mPlayer->start();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "stop") {
        setSessionState(STOPPING);
        onSessionStateUpdate(true);

        status = mPlayer->stop();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "pause") {
        setSessionState(PAUSING);
        onSessionStateUpdate(true);

        status = mPlayer->pause();

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "seek") {
        int msec = msg->readInt32();

        status = mPlayer->seek(msec);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "isPlaying") {
        bool ret;

        ret = mPlayer->isPlaying();
        reply->writeBool(ret);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getVideoSize") {
        int width, height;

        status = mPlayer->getVideoSize(width, height);
        reply->writeInt32(status);
        reply->writeInt32(width);
        reply->writeInt32(height);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getCurrentPosition") {
        int64_t msec;

        status = mPlayer->getCurrentPosition(msec);
        reply->writeInt32(status);
        reply->writeInt32((int)msec);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getDuration") {
        int64_t msec;

        status = mPlayer->getDuration(msec);
        reply->writeInt32(status);
        reply->writeInt32((int)msec);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "setAudioStreamType") {
        MediaPlayer::as_type_t type;
        type = (MediaPlayer::as_type_t)msg->readInt32();

        status = mPlayer->setAudioStreamType((int)type);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "getAudioStreamType") {
        //MediaPlayer::as_type_t type;
        int type;

        status = mPlayer->getAudioStreamType(&type);
        reply->writeInt32(status);
        reply->writeInt32(type);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "setLoop") {
        bool loop;
        loop = msg->readBool();

        status = mPlayer->setLoop(loop);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "enableExternalSubtitleSupport") {
        bool enable;
        enable = msg->readBool();

        status = mPlayer->enableExternalSubtitleSupport(enable);
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "isLooping") {
        bool loop;

        loop = mPlayer->isLooping();
        reply->writeBool(loop);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "getParameter") {

        ERROR("player service doesn't support it");
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setParameter") {
        MediaMetaSP meta = MediaMeta::create();
        meta->readFromMsg(msg);
        status = mPlayer->setParameter(meta);

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "invoke") {

        ERROR("player service doesn't support it");
        status = MM_ERROR_SUCCESS;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "captureVideo") {

        //status = mPlayer->captureVideo();
        status = MM_ERROR_UNSUPPORTED;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "returnAcquiredBuffers") {
        if (mMst)
            mMst->returnAcquiredBuffers();
        status = MM_ERROR_SUCCESS;
        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "acquireBuffer") {
        if (!mMst) {
            ERROR("surface texture is not exist");
            mCurBuffer = NULL;
            reply->writeInt64(0);
        } else {
            int index = msg->readInt32();
            MMNativeBuffer* anb = mMst->acquireBuffer(index);
            reply->writeInt64((int64_t)anb);
            if (anb) {
                reply->writeInt32(anb->width);
                reply->writeInt32(anb->height);
                reply->writeInt32(anb->stride);
                reply->writeInt32(anb->format);

#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
                // ANativeWindowBuffer
                reply->writeInt32(anb->usage);

                reply->writeInt32(anb->handle->version);
                reply->writeInt32(anb->handle->numFds);
                reply->writeInt32(anb->handle->numInts);
                for (int i = 0; i < anb->handle->numInts; i++)
                    reply->writeInt32(anb->handle->data[i + anb->handle->numFds]);
#else
                // NativeSurfaceBuffer
                reply->writeInt32(anb->flags);

                reply->writeInt32(anb->target->pounds);
                reply->writeInt32(anb->target->fds.num);
                reply->writeInt32(anb->target->attributes.num);

                for (int i = 0; i < anb->target->attributes.num; i++)
                    reply->writeInt32(anb->target->attributes.data[i]);
#endif
            }
            mCurBuffer = anb;
        }
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "acquireBufferIPC") {
//#ifdef USE_SIDEBAND_2
        if (!mMst || !mCurBuffer || !mSideBand2) {
            ERROR("fail IPC acquiring buffer, mst %p, cur buffer %p, sideband %p",
                  mMst, mCurBuffer, mSideBand2.get());
            reply->writeInt32(MM_ERROR_NO_MEM);
            sendMessage(reply);
            return true;
        }

        status = MM_ERROR_SUCCESS;
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
        // ANativeWindowBuffer
        INFO("send %d fds", mCurBuffer->handle->numFds);
        for (int i = 0; i < mCurBuffer->handle->numFds; i++) {
            int fd = mCurBuffer->handle->data[i];
            INFO("send fd %d", fd);
            if (MM_ERROR_ASYNC != mSideBand2->sendFdAsync(fd)) {
                ERROR("fail to send fd from socket");
                status = MM_ERROR_OP_FAILED;
                break;
            }
        }
#else
        // NativeSurfaceBuffer
        INFO("send %d fds", mCurBuffer->target->fds.num);
        for (int i = 0; i < mCurBuffer->target->fds.num; i++) {
            int fd = mCurBuffer->target->fds.data[i];
            INFO("send fd %d", fd);
            if (MM_ERROR_ASYNC != mSideBand2->sendFdAsync(fd)) {
                ERROR("fail to send fd from socket");
                status = MM_ERROR_OP_FAILED;
                break;
            }
        }
#endif
        reply->writeInt32(status);
        sendMessage(reply);
        return true;

    } else if (msg->methodName() == "returnBuffer") {
        if (!mMst) {
            ERROR("Surface Texture is NULL");
            FINISH_METHOD_CALL(true);
        }

        MMNativeBuffer *anb = reinterpret_cast<MMNativeBuffer*>(msg->readInt64());

        if (!mMst->returnBuffer(anb))
            status = MM_ERROR_SUCCESS;

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "setWindowSurface") {
        // mst consumer proxy
        String name = msg->readString();
        if (!strcmp(name.c_str(), "bad-name")) {
            ERROR("invalid parameter for socket name");
            FINISH_METHOD_CALL(true);
        } else {
            INFO("setWindowSurfce: get producer %s", name.c_str());

            mProducerName = name;

            //auto producer = std::make_shared<libgui::BufferPipeProducer>(name.c_str());
            //mWindowSurface = new libgui::Surface(producer);
            mWindowSurface = new libgui::Surface(name.c_str());

            fillBackGround();
        }

        if (mMst)
            mMst->setWindowSurface(mWindowSurface);

        status = MM_ERROR_SUCCESS;

        FINISH_METHOD_CALL(true);

    } else if (msg->methodName() == "setShowFlag") {

        mMstShow = msg->readBool();

        if (!mMst)
            status = MM_ERROR_SUCCESS;
        else if (!mMst->setShowFlag(mMstShow))
            status = MM_ERROR_SUCCESS;

        FINISH_METHOD_CALL(true);
    } else if (msg->methodName() == "getShowFlag") {

        bool show;

        if (mMst)
            show = mMst->getShowFlag();
        else
            show = mMstShow;

        status = MM_ERROR_SUCCESS;
        reply->writeInt32(status);
        reply->writeBool(show);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "tearDown") {

        status = MM_ERROR_SUCCESS;

        // reply to unblock client
        reply->writeInt32(status);
        sendMessage(reply);

        if (mPlayer && mUid == uid) {
            delete mPlayer;
            mPlayer = NULL;
        }

        INFO("cow player is destructed");

        return true;
    } else if (msg->methodName() == "setMstListener") {
        bool clear = msg->readBool();

        mUseMstListener = !clear;
        if (mMst) {
            if (mUseMstListener) {
                INFO("set MST listener");
                mMst->setListener(mMstListener.get());
            } else {
                INFO("clear MST listener");
                mMst->setListener(NULL);
            }
        }
        FINISH_METHOD_CALL(true);

    }

    ERROR("known method call: %s", msg->methodName().c_str());
    FINISH_METHOD_CALL(false);
}

void MediaPlayerAdaptor::onDeath(const DLifecycleListener::DeathInfo& deathInfo) {
    INFO("client die: %s", interface().c_str());
    DAdaptor::onDeath(deathInfo);
    if (mPlayer) {
        mPlayer->stop();
        mPlayer->reset();
        INFO("player is reset");
        if (mMst) {
            delete mMst;
            mMst = NULL;
        }
    }

    setSessionState(INVALID);
    onSessionStateUpdate(true);

    onSessionDeath(MU_Player);
}

void MediaPlayerAdaptor::onBirth(const DLifecycleListener::BirthInfo& brithInfo) {
    INFO("client birth: %s", interface().c_str());
    DAdaptor::onBirth(brithInfo);

    setSessionState(CONNECTED);
}

const char* MediaPlayerAdaptor::debugInfoMsg() {

    update();

    mDebugInfoMsg = "+ ";
    mDebugInfoMsg.append(mSessionName);
    mDebugInfoMsg.append("\n");
    mDebugInfoMsg.append("\n");

    mDebugInfoMsg.appendFormat("    client pid: %d, state: %s\n\n", mPid, stateToString(getSessionState()));
    mDebugInfoMsg.appendFormat("    cmdline: ");
    mDebugInfoMsg.append(ProcStat::getCmdLine(mPid));
    mDebugInfoMsg.appendFormat("\n\n");
    mDebugInfoMsg.appendFormat("    uri: %s\n\n", mUri.c_str());

    if (getSessionState() < PREPARED || getSessionState() > STOPPED)
        return mDebugInfoMsg.c_str();

    // other meta data
    mDebugInfoMsg.appendFormat("    Duration %.3fs, Position %.3fs, Buffering %d\n\n", mDurationMs/1000.0f, mPositionMs/1000.0f, mBuffering_100);

    mDebugInfoMsg.append("\n");
    return mDebugInfoMsg.c_str();
}


void MediaPlayerAdaptor::setCallBackLooper(SharedPtr<LooperThread> looper) {
    mLooper = looper;
}

void MediaPlayerAdaptor::update() {
    if (getSessionState() < PREPARED || getSessionState() > STOPPED)
        return;

    if (!mPlayer) {
        ERROR("player is not exist");
        return;
    }

    mm_status_t status;

    status = mPlayer->getDuration(mDurationMs);
    if (status != MM_ERROR_SUCCESS)
        mDurationMs = -1;

    status = mPlayer->getCurrentPosition(mPositionMs);
    if (status != MM_ERROR_SUCCESS)
        mPositionMs = -1;
}

/* static */
void MediaPlayerAdaptor::postNotify1(MediaPlayerAdaptor* p, int msg, int param1, int param2, MMParamSP param) {
    if (!p) {
        ERROR("adaptor is null");
        return;
    }

    p->postNotify(msg, param1, param2, param);
}

// run on LooperThread
void MediaPlayerAdaptor::postNotify(int msg, int param1, int param2, MMParamSP param) {

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
    if (msg == MediaPlayer::Listener::MSG_SUBTITLE_UPDATED && param) {
        signal->writeString(String(param->readCString()));
        signal->writeString(String(param->readCString()));
        signal->writeString(String(param->readCString()));
        signal->writeInt32(param->readInt32());
        signal->writeInt32(param->readInt32());
        signal->writeInt32(param->readInt32());
    }
    sendMessage(signal);
}

void MediaPlayerAdaptor::fillBackGround() {

    if (!mm_check_env_str("mm.ms.enable.fill", NULL, "1", true))
        return;

    uint32_t usage = ALLOC_USAGE_PREF(HW_RENDER) | ALLOC_USAGE_PREF(HW_TEXTURE);
    int w = 640, h = 480;
    uint8_t *ptr = NULL;
    Rect crop;
    crop.left = 0;
    crop.right = w;
    crop.top = 0;
    crop.bottom = h;

    if (!mWindowSurface) {
        WARNING("no target to fill, skip fill background");
        return;
    }

    int ret = WINDOW_API(set_buffers_dimensions)(GET_ANATIVEWINDOW(mWindowSurface), w, h);
    if (ret) {
        WARNING("set_buffers_dimensions fail, skip fill background");
        return;
    }

    ret = WINDOW_API(set_buffers_format)(GET_ANATIVEWINDOW(mWindowSurface), FMT_PREF(RGB_565));
    if (ret) {
        WARNING("set_buffers_format fail, skip fill background");
        return;
    }

    ret = mm_setSurfaceUsage(mWindowSurface, usage);
    if (ret) {
        WARNING("mm_setSurfaceUsage fail, skip fill background");
        return;
    }

    ret = WINDOW_API(set_buffer_count)(GET_ANATIVEWINDOW(mWindowSurface), 3);
    if (ret) {
        WARNING("set_buffer_count fail, skip fill background");
        return;
    }

    MMNativeBuffer *buffer = NULL;
    ret = mm_dequeueBufferWait(mWindowSurface, &buffer);
    NativeWindowBuffer* nwb = static_cast<NativeWindowBuffer*>(buffer);
    if (ret || !nwb) {
        WARNING("obtain buffer fail, skip fill background");
        return;
    }

    nwb->lock(usage, crop, &ptr);
    if (ptr)
        memset(ptr, 0, w*h*2);
    nwb->unlock();

    ret = mm_queueBuffer(mWindowSurface, buffer, -1);

    if (ret != 0)
      ERROR("fail to fill background");

    INFO("fill blank done");
}

} // end of namespace YUNOS_MM
