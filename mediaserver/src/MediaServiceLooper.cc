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

#include <MediaServiceLooper.h>
//#include <MediaPlayerAdaptor.h>
//#include <MediaPlayerProxy.h>

//libbase includes
#include <dbus/dbus.h>
#include <dbus/DMessage.h>
//#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <pointer/SharedPtr.h>

#include <multimedia/mm_debug.h>

#include <string>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaServiceLooper)

static void createAdaptor(MediaServiceLooper* instance) {
    if (instance)
        instance->createAdaptor();
}

static void createProxy(MediaServiceLooper* instance) {
    if (instance)
        instance->createProxy();
}

static void destroyDBus(MediaServiceLooper* instance) {
    if (instance)
        instance->destroyDBus();
}

bool MediaServiceLooper::init() {

    MMAutoLock lock(mLock);

    mLooper = new LooperThread();

    std::string name = mServiceName.c_str();
    size_t offset = 0, preOffset;
    do {
        preOffset = offset;
        offset = name.find('.', offset + 1);
        INFO("offset is %d", offset);
    } while(offset != std::string::npos);

    mThreadName = name.c_str() + preOffset + 1;

    INFO("start looper thread %s", mThreadName.c_str());

    mLooper->start(mThreadName.c_str());
    if (mLooper->isRunning() != true) {
        ERROR("LooperThread is not running");
        return false;
    }

    if (mIsServer)
        mLooper->sendTask(Task(YUNOS_MM::createAdaptor, this));
    else
        mLooper->sendTask(Task(YUNOS_MM::createProxy, this));

    mCondition.wait();

    if (!(initCheck())) {
        ERROR("fail to init DAdaptor");
        mLooper->requestExitAndWait();
        ERROR("quit LooperThread");
        return false;
    }

    INFO("Media %s is initialzied", mIsServer ? "DAdaptor": "DProxy");

    return true;
}

void MediaServiceLooper::requestExitAndWait() {
    if (!mLooper) {
        INFO("looper is not NULL, just return immediately");
        return;
    }

    mLooper->sendTask(Task(YUNOS_MM::destroyDBus, this));

    INFO("request exit loop: %s", mServiceName.c_str());
    mLooper->requestExitAndWait();
    INFO("loop exited: %s", mServiceName.c_str());
}

MediaServiceLooper::MediaServiceLooper(const String &service,
                                         const String &path,
                                         const String &iface)
    : mServiceName(service),
      mObjectPath(path),
      mInterface(iface),
      mInit(false),
      mIsServer(true),
      mCondition(mLock),
      mSession(NULL) {

}

MediaServiceLooper::~MediaServiceLooper() {

    if (mManager)
        mManager->unregisterService(mService);
}

bool MediaServiceLooper::initCheck() {
    return mInit;
}

// run on LooperThread
void MediaServiceLooper::createAdaptor() {
    MMAutoLock lock(mLock);

    INFO("create adaptor: service(%s) path(%s) interface(%s)",
         mServiceName.c_str(), mObjectPath.c_str(), mInterface.c_str());

    mManager = DServiceManager::getInstance();
    mService = mManager->registerService(mServiceName);
    //mDAdaptor = new MediaPlayerAdaptor(mService, mObjectPath, mInterface);
    mDAdaptor = createMediaAdaptor();
    mDAdaptor->publish();
    INFO("adaptor is published");

    mInit = true;
    mCondition.signal();
}

// run on LooperThread
void MediaServiceLooper::createProxy() {
    MMAutoLock lock(mLock);

    INFO("create proxy: service(%s) path(%s) interface(%s)",
         mServiceName.c_str(), mObjectPath.c_str(), mInterface.c_str());

    mManager = DServiceManager::getInstance();
    mService = mManager->getService(mServiceName);
    //mDProxy = new MediaPlayerProxy(mService, mObjectPath, mInterface);

    mInit = true;
    mDProxy = createMediaProxy();

    mCondition.signal();
}

void MediaServiceLooper::destroyDBus() {
    MMAutoLock lock(mLock);
    INFO("%s", mServiceName.c_str());
    mDProxy.reset();
    mDAdaptor.reset();

    if (mManager)
        mManager->unregisterService(mService);

    mService.reset();
    mManager.reset();
}

#if 0
// override it for other media type
SharedPtr<DAdaptor> MediaServiceLooper::createMediaAdaptor() {
    new MediaPlayerAdaptor(mService, mObjectPath, mInterface);
}


#endif

} // end of namespace YUNOS_MM
