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

#include <string/String.h>
#include <dbus/DAdaptor.h>
#include <thread/LooperThread.h>

//#include <multimedia/mediarecorder.h>
#include <multimedia/cowrecorder.h>
#include <multimedia/mm_cpp_utils.h>
#include <NativeSurfaceBase.h>

#include <MMSession.h>

#ifndef __media_recorder_adaptor_h
#define __media_recorder_adaptor_h

namespace yunos {
    class DService;
    class DAdaptor;
};

class Surface;

namespace YUNOS_MM {

using namespace yunos;

class MediaRecorderAdaptor : public DAdaptor,
                             public MMSession {

public:
    MediaRecorderAdaptor(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface,
                       pid_t pid,
                       uid_t uid);

    virtual ~MediaRecorderAdaptor();

    virtual bool handleMethodCall(const SharedPtr<DMessage>& msg);
    virtual void onDeath(const DLifecycleListener::DeathInfo& deathInfo);
    virtual void onBirth(const DLifecycleListener::BirthInfo& birthInfo);

    virtual const char* debugInfoMsg();

    void setCallBackLooper(SharedPtr<LooperThread> looper) { mLooper = looper; }

friend class Listener;

private:

    class Listener : public CowRecorder::Listener {
    public:
        Listener(MediaRecorderAdaptor* owner) { mOwner = owner; }
        virtual ~Listener() {}
        virtual void onMessage(int msg, int param1, int param2, const MMParamSP meta);

    private:
        MediaRecorderAdaptor *mOwner;
    };

    void notify(int msg, int param1, int param2, const MMParamSP meta);

    static const char * MM_LOG_TAG;
    String mInterface;
    String mCallbackName;

    CowRecorder *mRecorder;
    MMSharedPtr<Listener> mListener;
    yunos::libgui::BaseNativeSurface *mWindowSurface;
    //Lock mLock;
    int mFd;
    uid_t mUid;

    SharedPtr<LooperThread> mLooper;

    static void postNotify1(MediaRecorderAdaptor* p, int msg, int param1, int param2);
    void postNotify(int msg, int param1, int param2);

private:
    MediaRecorderAdaptor(const MediaRecorderAdaptor &);
    MediaRecorderAdaptor & operator=(const MediaRecorderAdaptor &);
};

} // end of YUNOS_MM
#endif
