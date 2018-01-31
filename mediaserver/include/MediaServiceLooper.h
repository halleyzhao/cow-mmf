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

#include <thread/LooperThread.h>
#include <string/String.h>

#include <multimedia/mm_cpp_utils.h>

#ifndef __media_service_looper_h
#define __media_service_looper_h

namespace yunos {
    class DService;
    class DAdaptor;
    class DProxy;
    class DServiceManager;
};

namespace YUNOS_MM {

using namespace yunos;

class MMSession;

// TODO abstract super class for MediaPlayerClient and MediaPlayerInstance
class MediaServiceLooper {

public:
    MediaServiceLooper(const String &service,
                        const String &path,
                        const String &iface);

    virtual ~MediaServiceLooper();

    void createAdaptor();
    void createProxy();
    void destroyDBus();

    bool init();

    void requestExitAndWait();
    String getServiceName() { return mServiceName; }
    MMSession *getSession() { return mSession; }

protected:
    static const char * MM_LOG_TAG;
    SharedPtr<LooperThread> mLooper;

    const String mServiceName;
    const String mObjectPath;
    const String mInterface;
    String mThreadName;

    bool mInit;
    bool mIsServer;

    SharedPtr<DServiceManager> mManager;
    SharedPtr<DService> mService;
    SharedPtr<DAdaptor> mDAdaptor;
    SharedPtr<DProxy> mDProxy;

    Lock mLock;
    YUNOS_MM::Condition mCondition;

    // media session running on the looper
    MMSession* mSession;

    bool initCheck();
private:
    virtual SharedPtr<DAdaptor> createMediaAdaptor() = 0;
    virtual SharedPtr<DProxy> createMediaProxy() = 0;

private:
    MediaServiceLooper(const MediaServiceLooper &);
    MediaServiceLooper & operator=(const MediaServiceLooper &);
};

} // end of YUNOS_MM
#endif
