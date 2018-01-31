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

#include <MediaClientHelper.h>
#include <MediaServiceLooper.h>
//#include <MediaService.h>
#include <MediaServiceName.h>
#include <MediaPlayerClient.h>
#include <MediaRecorderClient.h>

#include <multimedia/mm_debug.h>

#include <dbus/DServiceManager.h>
#include <dbus/DMessage.h>
#include <dbus/DService.h>
#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>
#include <looper/Looper.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(MediaClientHelper)
using namespace yunos;

#define MM_MSG_TIMEOUT 6000

#ifdef MM_SEND_MSG
#undef MM_SEND_MSG
#endif

#define MM_SEND_MSG(msg)                                                                          \
    DError err;                                                                                   \
    SharedPtr<DMessage> reply = factory.sendMessageWithReplyBlocking(msg, MM_MSG_TIMEOUT, &err);  \


#ifdef MM_HANDLE_REPLY
#undef MM_HANDLE_REPLY
#endif

#define MM_HANDLE_REPLY()                                              \
        if (!reply) {                                                  \
            ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());        \
            return false;                                   \
        }

/* static */
bool MediaClientHelper::connect_l(int playType,
                                const char *mediaName,
                                String &serviceName,
                                String &objectPath,
                                String &iface) {
    if (!mediaName) {
        ERROR("media type is not provided");
        return false;
    }

    INFO("try to connect %s, type (%d)", mediaName, playType);

    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    if (!instance) {
        ERROR("fail to get service manager");
        return false;
    }
    SharedPtr<DService> service = instance->getService(String(gMServiceName));
    if (!service) {
        ERROR("fail to get service %s", gMServiceName);
        return false;
    }

    DProxy factory(service,
               String(gMServiceObjectPath),
               String(gMServiceInterface));

    String method("create");
    method.append(mediaName);
    INFO("connect with method name %s", method.c_str());

    SharedPtr<DMessage> msg = factory.obtainMethodCallMessage(method);
    msg->writeInt32(playType);

    MM_SEND_MSG(msg);
    MM_HANDLE_REPLY();

    bool result = reply->readBool();
    serviceName = reply->readString();
    objectPath = reply->readString();
    iface = reply->readString();

    INFO("connected -  service:%s object:%s interface:%s\n",
          serviceName.c_str(), objectPath.c_str(), iface.c_str());

    return result;
}

/*static*/
bool MediaClientHelper::connect(int playType,
                                const char *mediaName,
                                String &serviceName,
                                String &objectPath,
                                String &iface) {
    if (Looper::current() == NULL) {
        Looper looper;
        INFO("connect to media service with local Looper::current %p", Looper::current());
        return connect_l(playType, mediaName, serviceName, objectPath, iface);
    } else {
        INFO("connect to media service with Looper::current %p", Looper::current());
        return connect_l(playType, mediaName, serviceName, objectPath, iface);
    }
}

/*static*/
void MediaClientHelper::disconnect_l(const char* mediaName,
                                   MediaServiceLooper *client) {
    if (!client || !mediaName) {
        WARNING("NULL pointer(name %p and client %p) just reutrn",
                mediaName, client);
        return;
    }

    String serviceName = client->getServiceName();

    INFO("disconnect to %s", serviceName.c_str());

    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    if (!instance) {
        ERROR("fail to get service manager");
        return;
    }
    SharedPtr<DService> service = instance->getService(String(gMServiceName));
    if (!service) {
        ERROR("fail to get service %s", gMServiceName);
        return;
    }

    DProxy factory(service,
               String(gMServiceObjectPath),
               String(gMServiceInterface));

    String method("destroy");
    method.append(mediaName);
    INFO("disconnect with method name %s", method.c_str());

    SharedPtr<DMessage> msg = factory.obtainMethodCallMessage(method);
    msg->writeString(serviceName);

    MM_SEND_MSG(msg);

    if (!reply) {
        ERROR("get NULL reply from service when destroy %s",
              serviceName.c_str());
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());
    }
}

/*static*/
void MediaClientHelper::disconnect(const char* mediaName,
                                   MediaServiceLooper *client) {
    if (Looper::current() == NULL) {
        Looper looper;
        INFO("disconnect to media service with local Looper::current %p", Looper::current());
        disconnect_l(mediaName, client);
    } else {
        INFO("disconnect to media service with Looper::current %p", Looper::current());
        disconnect_l(mediaName, client);
    }
}

/*static*/
MediaPlayerClient* MediaClientHelper::getMediaPlayerClient(
                                  const String &serviceName,
                                  const String &objectPath,
                                  const String &iface) {
    MediaPlayerClient* player;

    player = new MediaPlayerClient(serviceName, objectPath, iface);

    // init MediaPlayerInstance
    player->init();

    // avoid calling proxy from multiple threads
    //String method("FirstBreathe");
    //player->getProxy()->test(method);

    return player;
}

/*static*/
MediaRecorderClient* MediaClientHelper::getMediaRecorderClient(
                                  const String &serviceName,
                                  const String &objectPath,
                                  const String &iface) {
    MediaRecorderClient* recorder;

    recorder = new MediaRecorderClient(serviceName, objectPath, iface);

    // init MediaRecorderInstance
    recorder->init();

    String method("FirstBreathe");
    recorder->getProxy()->test(method);

    return recorder;
}

/*static*/
String MediaClientHelper::dumpsys_l() {

    String sysInfo;

    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    if (!instance) {
        ERROR("fail to get service manager");
        return sysInfo;
    }

    SharedPtr<DService> service = instance->getService(String(gMServiceName));
    if (!service) {
        ERROR("fail to get service %s", gMServiceName);
        return sysInfo;
    }

    DProxy factory(service,
               String(gMServiceObjectPath),
               String(gMServiceInterface));

    String method("dumpsys");
    INFO("connect with method name %s", method.c_str());

    SharedPtr<DMessage> msg = factory.obtainMethodCallMessage(method);

    MM_SEND_MSG(msg);
    if (!reply) {
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());
        return sysInfo;
    }

    sysInfo = reply->readString();

    return sysInfo;
}

String MediaClientHelper::dumpsys() {
    if (Looper::current() == NULL) {
        Looper looper;
        INFO("connect to media service with local Looper::current %p", Looper::current());
        return dumpsys_l();
    } else {
        INFO("connect to media service with Looper::current %p", Looper::current());
        return dumpsys_l();
    }
}

} // end of YUNOS_MM
