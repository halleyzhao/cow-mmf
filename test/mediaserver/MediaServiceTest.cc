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
#include "multimedia/mm_debug.h"

#include <dbus/dbus.h>
#include <dbus/DMessage.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <looper/Looper.h>
#include <thread/LooperThread.h>
#include <pointer/SharedPtr.h>
#include <string/String.h>

#include <unistd.h>

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("MediaServiceTest");

using namespace yunos;

class FactoryProxy : public DProxy {
public:

    FactoryProxy(const SharedPtr<DService>& service)
        : DProxy(service, String(gMServiceObjectPath),
                String(gMServiceInterface)) {
    }

    void createPlayer(String &service, String &object, String &iface) {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("createMediaPlayer"));
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        if (reply) {
            int result = reply->readInt32();
            service = reply->readString();
            object = reply->readString();
            iface = reply->readString();

            INFO("player service instance is created ,return %d\n", result);
            INFO("service:%s object:%s interface:%s\n",
                  service.c_str(), object.c_str(), iface.c_str());
        } else
            ERROR("reply is NULL");
    }

    static void handleCreatePlayer(void* data, const SharedPtr<DMessage>& msg) {
        //FactoryProxy* impl = static_cast<FactoryProxy*>(data);
        int result = msg->readInt32();
        String service, object, iface;
        service = msg->readString();
        object = msg->readString();
        iface = msg->readString();

        INFO("the result of createPlayer: %d\n", result);
        INFO("service:%s object:%s interface:%s\n",
              service.c_str(), object.c_str(), iface.c_str());
    }

    virtual bool handleSignal(const SharedPtr<DMessage>& msg) {
        INFO("handle signal = %s", msg->signalName().c_str());
        if (msg->signalName() == "signalA") {
            String p = msg->readString();
            INFO("SignalA result = %s", p.c_str());
            return true;
        } else if (msg->signalName() == "EOS") {
            String p = msg->readString();
            INFO("got EOS: %s", p.c_str());
            Looper::current()->quit();
            INFO("quit ...");
            return true;
        }
        return false;
    }

private:
};

#define MM_HANDLE_REPLY()                                              \
        if (reply) {                                                   \
            int result = reply->readInt32();                           \
            INFO("Player %s return: %d\n", __func__, result);          \
        } else                                                         \
            ERROR("reply is NULL");

class PlayerProxy : public DProxy {
public:

    PlayerProxy(const SharedPtr<DService>& service,
                const String &object,
                const String &iface)
        : DProxy(service, object, iface) {
    }

    void test(String &method) {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(method);
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        MM_HANDLE_REPLY();
    }

    void setDataSourceUri(const char *uri) {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setDataSourceUri"));
        msg->writeString(String(uri));
        msg->writeInt32(0);
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        MM_HANDLE_REPLY();
    }

    void setVideoDisplay(void *handle) {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setVideoDisplay"));
        msg->writeInt64((int64_t)handle);
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        MM_HANDLE_REPLY();
    }

    void prepare() {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("prepareAsync"));
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        MM_HANDLE_REPLY();
    }

    void start() {
        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("start"));
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);

        MM_HANDLE_REPLY();
    }

    virtual bool handleSignal(const SharedPtr<DMessage>& msg) {
        INFO("handle signal = %s", msg->signalName().c_str());
        if (msg->signalName() == "signalA") {
            String p = msg->readString();
            INFO("SignalA result = %s", p.c_str());
            return true;
        } else if (msg->signalName() == "EOS") {
            String p = msg->readString();
            INFO("got EOS: %s", p.c_str());
            Looper::current()->quit();
            INFO("quit ...");
            return true;
        }
        return false;
    }

private:
};

String gServiceName, gObject, gInterface;
PlayerProxy *gPlayer;

static void* createPlayerProxy(void *) {
    Looper looper;
    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    SharedPtr<DService> service1 = instance->getService(gServiceName);
    //PlayerProxy player(service1, gObject, gInterface);
    gPlayer = new PlayerProxy(service1, gObject, gInterface);

    INFO("create player in pthread");
    INFO("quite looper in 5s\n");
    looper.sendDelayedTask(Looper::getQuitTask(), 5000);
    // run the message loop.
    Looper::current()->run();
    return NULL;
}

static void createPlayerProxy1(void *p) {
    SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
    SharedPtr<DService> service1 = instance->getService(gServiceName);
    //PlayerProxy player(service1, gObject, gInterface);
    gPlayer = new PlayerProxy(service1, gObject, gInterface);
    INFO("create player in LooperThread");
}


void play() {
    INFO("play video");
    gPlayer->setDataSourceUri("/usr/bin/ut/res/video/trailer_short.mp4");
    usleep(10*1000);
    gPlayer->setVideoDisplay(NULL);
    gPlayer->prepare();
    usleep(1000*1000);
    gPlayer->start();
    usleep(10*1000*1000);
}

static void usage(int error_code)
{
    fprintf(stderr, "Usage: media-service-test\n");
    fprintf(stderr, "playback /usr/bin/ut/res/video/trailer_short.mp4");

    exit(error_code);
}

int gThreadOption = 0;
int gHasName = 0;
int gOnlyCreate = 0;

void parseCommandLine(int argc, char **argv)
{
    int res;
    while ((res = getopt(argc, argv, "cnp:")) >= 0) {
        switch (res) {
            case 'p':
                sscanf(optarg, "%08d", &gThreadOption);
                break;
            case 'n':
                gHasName = 1;
                break;
            case 'c':
                gOnlyCreate = 1;
                break;
            case '?':
            default:
                usage(-1);
                break;
        }
    }

}

int main(int argc, char** argv) {

    INFO("MediaServiceTest\n");

    parseCommandLine(argc, argv);

    if (!gHasName) {
        Looper looper;
        SharedPtr<DServiceManager> instance = DServiceManager::getInstance();
        SharedPtr<DService> service = instance->getService(String(gMServiceName));
        FactoryProxy factory(service);
        // enable signals.
        //factory.enableSignals();

        factory.createPlayer(gServiceName, gObject, gInterface);
    } else {
        gServiceName = "com.yunos.media.player0";
        gObject = "/com/yunos/media/player/0";
        gInterface = "com.yunos.media.player0.interface";
        printf("direct name\n");
    }

    if (gThreadOption == 1) {
        printf("create player proxy in pthread\n");
        pthread_t threadID;
        pthread_create(&threadID, NULL, (void *(*)(void *))createPlayerProxy, NULL);
    } else if (gThreadOption == 2) {
        printf("create player proxy in LooperThread\n");
        LooperThread *looper =  new LooperThread;
        looper->start();
        if (looper->isRunning() != true) {
            ERROR("LooperThread is not running");
            exit(-1);
        }
        looper->sendTask(Task(createPlayerProxy1, looper));
    } else {
        printf("create player proxy in main thread\n");
        createPlayerProxy(NULL);
    }

    while (!gPlayer) {
        usleep(1000);
    }

    if (gOnlyCreate) {
        printf("create player and just return\n");
        return 0;
    }
    play();
}
