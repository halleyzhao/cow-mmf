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

//#include <multimedia/mediaplayer.h>
#include <multimedia/cowplayer.h>
#include <multimedia/mm_cpp_utils.h>
#include <NativeSurfaceBase.h>

#include <MMSession.h>
#include "native_surface_help.h"
#include <media_surface_texture.h>

#ifndef __media_player_adaptor_h
#define __media_player_adaptor_h

namespace yunos {
    class DService;
    class DAdaptor;

namespace libgui {
    class BufferPipeProducer;
};

};

//class yunos::libgui::BaseNativeSurface;
//struct ANativeWindowBuffer;

namespace YUNOS_MM {

using namespace yunos;

class SideBandIPC;

class MediaPlayerAdaptor : public DAdaptor,
                           public MMSession {

public:
    MediaPlayerAdaptor(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface,
                       pid_t pid,
                       uid_t uid,
                       int playType);

    virtual ~MediaPlayerAdaptor();

    virtual bool handleMethodCall(const SharedPtr<DMessage>& msg);
    virtual void onDeath(const DLifecycleListener::DeathInfo& deathInfo);
    virtual void onBirth(const DLifecycleListener::BirthInfo& birthInfo);

    virtual const char* debugInfoMsg();

    void setCallBackLooper(SharedPtr<LooperThread> looper);

friend class Listener;

protected:
    // MMSession
    virtual void update();

private:

    class Listener : public CowPlayer::Listener {
    public:
        Listener(MediaPlayerAdaptor* owner) { mOwner = owner; }
        virtual ~Listener() {}
        virtual void onMessage(int msg, int param1, int param2, const MMParamSP meta);

    private:
        MediaPlayerAdaptor *mOwner;
    };

    class MstListener : public YunOSMediaCodec::SurfaceTextureListener {
    public:
        MstListener(MediaPlayerAdaptor* owner) { mOwner = owner; }
        virtual ~MstListener() {}
        virtual void onMessage(int msg, int param1, int param2);

    private:
        MediaPlayerAdaptor *mOwner;
    };

    void notify(int msg, int param1, int param2, const MMParamSP meta);
    void fillBackGround();

    static const char * MM_LOG_TAG;
    String mCallbackName;

    CowPlayer *mPlayer;
    MMSharedPtr<Listener> mListener;
    MMSharedPtr<MstListener> mMstListener;
    bool mUseMstListener;
    yunos::libgui::BaseNativeSurface *mWindowSurface;
    String mProducerName;
    YunOSMediaCodec::MediaSurfaceTexture *mMst;
    bool mMstShow;
    uid_t mUid;
    //Lock mLock;
    //TODO merge tow mSideBand
    MMSharedPtr<SideBandIPC> mSideBand;
    MMSharedPtr<SideBandIPC> mSideBand2;
    MMNativeBuffer* mCurBuffer;

    SharedPtr<LooperThread> mLooper;

    static void postNotify1(MediaPlayerAdaptor* p, int msg, int param1, int param2, MMParamSP param);
    void postNotify(int msg, int param1, int param2, MMParamSP param);

private:
    MediaPlayerAdaptor(const MediaPlayerAdaptor &);
    MediaPlayerAdaptor & operator=(const MediaPlayerAdaptor &);
};

} // end of YUNOS_MM
#endif
