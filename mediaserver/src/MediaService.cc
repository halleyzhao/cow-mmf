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

#include "MediaService.h"
#include "MediaServiceName.h"
#include "MediaPlayerInstance.h"
#include "MediaRecorderInstance.h"
#include "MMSession.h"

#include <multimedia/mm_debug.h>
#include <multimedia/mm_cpp_utils.h>

//libbase includes
#include <dbus/dbus.h>
#include <dbus/DMessage.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <looper/Looper.h>
#include <pointer/SharedPtr.h>
#include <string/String.h>
#include <Permission.h>

#include <map>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;
#define MEDIA_SERVICE_PERMISSION_NAME  "MEDIA_SERVICE.permission.yunos.com"

DEFINE_LOGTAG(MediaService)

MediaService::MediaService()
    : MMThread("MediaServ", false) {
    INFO("created");

#if (defined __PLATFORM_TABLET__) && (defined __TABLET_BOARD_INTEL__)
    if (!mm_set_env_str("host.media.player.type", NULL, "local"))
        WARNING("fail to set property: host.media.player.type");
#endif

}

MediaService::~MediaService() {
    INFO("destory");
}

/*static*/ bool MediaService::createService(bool blocked) {
    MediaService * service = new MediaService;
    int ret = 0;

    if (!blocked)
        ret = service->create();
    else
        service->main();

    return (ret == 0);
}

class MediaServiceAdaptor : public DAdaptor {
public:
    MediaServiceAdaptor(const SharedPtr<DService>& service);
    virtual ~MediaServiceAdaptor() {}

    virtual bool handleMethodCall(const SharedPtr<DMessage>& msg);

    void destroyMediaInstance(String &serviceName,
                              MMSession::MediaUsage type,
                              uid_t uid);

    void MediaUsageUpdate(pid_t pid, MMSession::MediaUsage type, bool turnOff);
private:

    bool createMediaInstance(String &serviceName,
                             String &objectPath,
                             String &iface,
                             MMSession::MediaUsage type,
                             pid_t pid,
                             uid_t uid,
                             int playType = 0);


    void dumpService(String &sysInfo);

private:
    // handleMethodCall in sequence, no need lock?
    Lock mLock;

    typedef std::map<String, MediaServiceLooper*> InstanceMap;
    typedef std::map<String, MediaServiceLooper*>::iterator InstanceMapIterator;

    typedef std::map<String, MMSession*> SessionMap;
    typedef std::map<String, MMSession*>::iterator SessionMapIterator;

    void UsageToServiceName(
              MMSession::MediaUsage type,
              String &serviceName,
              String &objectPath,
              String &iface,
              uint32_t &seq);

    InstanceMap& MediaUsageToMap(MMSession::MediaUsage type);

    uint32_t mPlayerSeq;
    uint32_t mMediaCodecSeq;
    uint32_t mRecorderSeq;
    uint32_t mWfdSourceSeq;
    uint32_t mWfdSinkSeq;

    InstanceMap mPlayerMap;
    InstanceMap mMediaCodecMap;
    InstanceMap mRecorderMap;
    InstanceMap mWfdSourceMap;
    InstanceMap mWfdSinkMap;

    SessionMap mSessions;

private:
    static const char * MM_LOG_TAG;
};

MediaServiceAdaptor *gMediaService = NULL;
Looper *gLooper;

void MediaService::main() {
    Looper looper;
    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    SharedPtr<DService> service = instance->registerService(String(gMServiceName));
    SharedPtr<MediaServiceAdaptor> adaptor = new MediaServiceAdaptor(service);
    gMediaService = adaptor.pointer();

    adaptor->publish();
    INFO("run looper");


    gLooper = &looper;
    looper.run();
    INFO("end looper");

    gMediaService = NULL;
    gLooper = NULL;
    instance->unregisterService(service);
}

DEFINE_LOGTAG(MediaServiceAdaptor)

MediaServiceAdaptor::MediaServiceAdaptor(const SharedPtr<DService>& service)
    : DAdaptor(service, String(gMServiceObjectPath), String(gMServiceInterface)),
      mPlayerSeq(0),
      mMediaCodecSeq(0),
      mRecorderSeq(0),
      mWfdSourceSeq(0),
      mWfdSinkSeq(0) {
    INFO("create media service adaptor");

}

void MediaServiceAdaptor::UsageToServiceName(
                              MMSession::MediaUsage type,
                              String &serviceName,
                              String &objectPath,
                              String &iface,
                              uint32_t &seq) {

    switch(type) {
        case MMSession::MU_Player:
            serviceName = gMediaPlayerName;
            objectPath = gMPObjectPath;
            iface = gMPInterface;
            // TODO wrap back if reach max, check next free seq each time
            seq = mPlayerSeq++;
            break;
        case MMSession::MU_MediaCodec:
            serviceName = gMediaCodecName;
            objectPath = gMCObjectPath;
            iface = gMCInterface;
            seq = mMediaCodecSeq++;
            break;
        case MMSession::MU_Recorder:
            serviceName = gMediaRecorderName;
            objectPath = gMRObjectPath;
            iface = gMRInterface;
            seq = mRecorderSeq++;
            break;
        case MMSession::MU_WFDSource:
            serviceName = gWfdSourceName;
            objectPath = gWfdSrcObjectPath;
            iface = gWfdSrcInterface;
            seq = mWfdSourceSeq++;
            break;
        case MMSession::MU_WFDSink:
            serviceName = gWfdSinkName;
            objectPath = gWfdSinkObjectPath;
            iface = gWfdSinkInterface;
            seq = mWfdSinkSeq++;
            break;
        default:
            ERROR("undefined media usage %d", type);
    }
}

bool MediaServiceAdaptor::createMediaInstance(
                              String &serviceName,
                              String &objectPath,
                              String &iface,
                              MMSession::MediaUsage type,
                              pid_t pid,
                              uid_t uid,
                              int playType) {
    MMAutoLock lock(mLock);

    char seq[16];
    uint32_t mediaSeq = 0;
    //serviceName = gMediaPlayerName;
    //objectPath = gMPObjectPath;
    //iface = gMPInterface;
    UsageToServiceName(type, serviceName, objectPath, iface, mediaSeq);

    //sprintf(seq, "%d", mPlayerSeq);
    sprintf(seq, "%d", mediaSeq);

    serviceName.append(seq);;
    objectPath.append(seq);
    iface.append(seq);
    iface.append(".interface");

    INFO("create %s: service(%s) obj(%s) iface(%s)",
         MMSession::UsageToString(type),
         serviceName.c_str(),
         objectPath.c_str(),
         iface.c_str());

    MediaServiceLooper *instance = NULL;
    switch(type) {
        case MMSession::MU_Player:
            instance = new MediaPlayerInstance(
                                       serviceName,
                                       objectPath,
                                       iface,
                                       pid,
                                       uid,
                                       playType);
            mPlayerMap[serviceName] = instance;
            break;
        case MMSession::MU_MediaCodec:
            break;
        case MMSession::MU_Recorder:
            instance = new MediaRecorderInstance(
                                       serviceName,
                                       objectPath,
                                       iface,
                                       pid,
                                       uid);
            mRecorderMap[serviceName] = instance;
            break;
        case MMSession::MU_WFDSource:
            break;
        case MMSession::MU_WFDSink:
            break;
        default:
            ERROR("undefined media usage %d", type);
    }

    if (!instance)
        return false;

    bool ret = instance->init();

    {
        MMAutoLock lock(MMSession::sStatusLock);
        mSessions[serviceName] = instance->getSession();
    }

    return ret;
}

MediaServiceAdaptor::InstanceMap& MediaServiceAdaptor::MediaUsageToMap(MMSession::MediaUsage type) {

    switch(type) {
        case MMSession::MU_Player:
            return mPlayerMap;
        case MMSession::MU_MediaCodec:
            return mMediaCodecMap;
        case MMSession::MU_Recorder:
            return mRecorderMap;
        case MMSession::MU_WFDSource:
            return mWfdSourceMap;
        case MMSession::MU_WFDSink:
            return mWfdSinkMap;
        default:
            ERROR("undefined media usage %d", type);
    }

    return mPlayerMap;
}

void MediaServiceAdaptor::destroyMediaInstance(String &serviceName, MMSession::MediaUsage type, uid_t uid) {

    MMAutoLock lock(mLock);

    InstanceMap &map = MediaUsageToMap(type);;
    InstanceMapIterator it = map.find(serviceName);
    SessionMapIterator session = mSessions.find(serviceName);

    if (session == mSessions.end()) {
        ERROR("unknown media service session(%s)", serviceName.c_str());
        return;
    }

    if (session->second->getUid() != uid) {
        WARNING("need uid permission!");
        return;
    }

    if (it == map.end()) {
        ERROR("unknown media service instance (%s)", serviceName.c_str());
        return;
    }

    MediaServiceLooper *looper = it->second;

    map.erase(it);

    if (!looper) {
        ERROR("media service instance (%s) is NULL", serviceName.c_str());
        return;
    }

    looper->requestExitAndWait();

    delete looper;

    {
        MMAutoLock lock(MMSession::sStatusLock);
        mSessions.erase(session);
    }
}

void MediaServiceAdaptor::MediaUsageUpdate(pid_t pid, MMSession::MediaUsage type, bool turnOff) {
    MMAutoLock lock(MMSession::sStatusLock);

    SessionMapIterator session = mSessions.begin();
    int numOfActiveSession = 0;
    bool found = false;

    for (; session != mSessions.end(); session++) {
        if (!session->second) {
            ERROR("%s has no session info", session->first.c_str());
            continue;
        }

        if (session->second->getPid() != pid || session->second->getType() != type)
            continue;

        found = true;

        if (session->second->isActive())
            numOfActiveSession++;
    }

    if (!found) {
        ERROR("session (pid %d, type %s) is not found", pid, MMSession::UsageToString(type));
        return;
    }

    INFO("turnOff is %d, numOfActiveSession is %d, pid %d", turnOff, numOfActiveSession, pid);
    if ((turnOff && !numOfActiveSession) ||
        (!turnOff && (numOfActiveSession == 1)))
        MMSession::sysUpdateMediaUsage(pid, type, turnOff);
}

bool MediaServiceAdaptor::handleMethodCall(const SharedPtr<DMessage>& msg) {
    INFO("receive a method call = %s", msg->methodName().c_str());

    String serviceName, objectPath, iface;
    bool ret;
    static PermissionCheck perm(String(MEDIA_SERVICE_PERMISSION_NAME));
    if (!perm.checkPermission(msg, this)) {
        ERROR("operation not allowed, client (pid %d) need permission[%s]", msg->getPid(), MEDIA_SERVICE_PERMISSION_NAME);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(false);
        sendMessage(reply);
        return false;
    }

    pid_t pid = msg->getPid();
    uid_t uid = msg->getUid();

    if (msg->methodName() == "createMediaPlayer") {
        int playType = msg->readInt32();
        ret = createMediaInstance(serviceName, objectPath, iface, MMSession::MU_Player, pid, uid, playType);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(ret);
        reply->writeString(serviceName);
        reply->writeString(objectPath);
        reply->writeString(iface);
        sendMessage(reply);
        String sysInfo;
        dumpService(sysInfo);
        INFO("%s", sysInfo.c_str());
        return true;
    } else if (msg->methodName() == "createMediaCodec") {
        ret = createMediaInstance(serviceName, objectPath, iface, MMSession::MU_MediaCodec, pid, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(ret);
        reply->writeString(serviceName);
        reply->writeString(objectPath);
        reply->writeString(iface);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "createMediaRecorder") {
        ret = createMediaInstance(serviceName, objectPath, iface, MMSession::MU_Recorder, pid, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(ret);
        reply->writeString(serviceName);
        reply->writeString(objectPath);
        reply->writeString(iface);
        sendMessage(reply);
        String sysInfo;
        dumpService(sysInfo);
        INFO("%s", sysInfo.c_str());
        return true;
    } else if (msg->methodName() == "createWFDSourceSession") {
        ret = createMediaInstance(serviceName, objectPath, iface, MMSession::MU_WFDSource, pid, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(ret);
        reply->writeString(serviceName);
        reply->writeString(objectPath);
        reply->writeString(iface);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "createWFDSinkSession") {
        ret = createMediaInstance(serviceName, objectPath, iface, MMSession::MU_WFDSink, pid, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        reply->writeBool(ret);
        reply->writeString(serviceName);
        reply->writeString(objectPath);
        reply->writeString(iface);
        sendMessage(reply);
        return true;
    } else if (msg->methodName() == "destroyMediaPlayer") {
        serviceName = msg->readString();
        destroyMediaInstance(serviceName, MMSession::MU_Player, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        sendMessage(reply);
        String sysInfo;
        dumpService(sysInfo);
        INFO("%s", sysInfo.c_str());
        return true;
    } else if (msg->methodName() == "destroyMediaRecorder") {
        serviceName = msg->readString();
        destroyMediaInstance(serviceName, MMSession::MU_Recorder, uid);
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        sendMessage(reply);
        String sysInfo;
        dumpService(sysInfo);
        INFO("%s", sysInfo.c_str());
        return true;
    } else if (msg->methodName() == "dumpsys") {
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        String sysInfo;
        dumpService(sysInfo);
        reply->writeString(sysInfo);
        sendMessage(reply);
        return true;
    }

    WARNING("unknown method call: %s", msg->methodName().c_str());
    return false;
}

void MediaServiceAdaptor::dumpService(String &sysInfo) {
    MMAutoLock lock(mLock);
#ifdef __MM_PROD_MODE_ON__
    WARNING("Do not dump service in production mode");
    return;
#endif
    INFO("call dump service, there are %d sessions", mSessions.size());
    SessionMapIterator session = mSessions.begin();
    sysInfo = "\n************MediaService sessions************\n";

    const char* sessionInfo;
    for (; session != mSessions.end(); session++) {
        if (!session->second) {
            ERROR("%s has no session info", session->first.c_str());
            continue;
        }
        sessionInfo = session->second->debugInfoMsg();
        sysInfo.append(sessionInfo);
    }
}

static void rmMediaInstance(String mediaName, MMSession::MediaUsage type, uid_t uid) {
    if (!gMediaService) {
        return;
    }

    gMediaService->destroyMediaInstance(mediaName, type, uid);
}

void onMediaInstanceDeath(String &mediaName, MMSession::MediaUsage type, uid_t uid) {
    if (gLooper)
        gLooper->sendTask(Task(rmMediaInstance, mediaName, type, uid));
}

void onMediaUsageUpdate(pid_t pid, MMSession::MediaUsage type, bool turnOff) {
    if (!gMediaService) {
        return;
    }

    gMediaService->MediaUsageUpdate(pid, type, turnOff);
}

} // end of namespace YUNOS_MM
