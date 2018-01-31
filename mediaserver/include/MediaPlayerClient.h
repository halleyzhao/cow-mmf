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

#include <multimedia/mediaplayer.h>
#include <multimedia/mm_cpp_utils.h>
#include <MediaServiceLooper.h>

#include <MediaPlayerProxy.h>
#include <MediaMethodID.h>

#ifndef __media_player_client_h
#define __media_player_client_h

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
struct NativeSurfaceBuffer;
#define PlayerClientBuffer NativeSurfaceBuffer
#elif __MM_YUNOS_CNTRHAL_BUILD__
struct ANativeWindowBuffer; //ctnr
#define PlayerClientBuffer ANativeWindowBuffer //ctnr
#elif defined __MM_YUNOS_LINUX_BSP_BUILD__
#define PlayerClientBuffer void
#endif

#ifdef __USING_VR_VIDEO__
namespace yunos {
namespace yvr {
class VrVideoView;
}
}
#endif
namespace YUNOS_MM {

using namespace yunos;

class TextureProxy;

class MediaPlayerClient : public MediaServiceLooper {

public:

    MediaPlayerClient(const String &service,
                      const String &path,
                      const String &iface);
    virtual ~MediaPlayerClient();

    // MediaPlayer interface
    mm_status_t setListener(MediaPlayer::Listener * listener);
    mm_status_t setDataSource(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    mm_status_t setDataSource(const unsigned char * mem, size_t size);
    mm_status_t setDataSource(int fd, int64_t offset, int64_t length);
    mm_status_t setSubtitleSource(const char* uri);
    mm_status_t setDisplayName(const char* name);
    mm_status_t setVideoDisplay(void * handle);
    mm_status_t setVideoSurfaceTexture(void * handle);
    mm_status_t prepare();
    mm_status_t prepareAsync();
    mm_status_t reset();
    mm_status_t setVolume(const MediaPlayer::VolumeInfo & volume);
    mm_status_t getVolume(MediaPlayer::VolumeInfo * volume) const;
    mm_status_t setMute(bool mute);
    mm_status_t getMute(bool * mute) const;
    mm_status_t start();
    mm_status_t stop();
    mm_status_t pause();
    mm_status_t seek(int msec);
    bool isPlaying() const;
    mm_status_t getVideoSize(int *width, int * height) const;
    mm_status_t getCurrentPosition(int * msec) const;
    mm_status_t getDuration(int * msec) const;
    mm_status_t setAudioStreamType(MediaPlayer::as_type_t type);
    mm_status_t getAudioStreamType(MediaPlayer::as_type_t *type);
    mm_status_t setLoop(bool loop);
    bool isLooping() const;
    mm_status_t setParameter(const MediaMetaSP & meta);
    mm_status_t getParameter(MediaMetaSP & meta);
    mm_status_t invoke(const MMParam * request, MMParam * reply);
    mm_status_t captureVideo();
    mm_status_t enableExternalSubtitleSupport(bool enable);

    mm_status_t release();

friend class MediaClientHelper;
friend class TextureProxy;

private:
    /* MediaSurfaceTexture consumer proxy */
    void returnAcquiredBuffers();
    PlayerClientBuffer* acquireBuffer(int index);
    int returnBuffer(PlayerClientBuffer* anb);
    int setWindowSurface(void *ws);
    int setShowFlag(bool show);
    bool getShowFlag();
    int setMstListener(YunOSMediaCodec::SurfaceTextureListener *listener);

#ifdef SINGLE_THREAD_PROXY
    int64_t sendMethodCommand(MMParam &param);
    static void sendMethodCommand1(MediaPlayerClient* p, MMParam &param, uint32_t seq);
#endif

    class ClientListener : public MediaProxy::ProxyListener {
    public:
        ClientListener(MediaPlayerClient* owner)
            : mOwner(owner) {
        }
        virtual ~ClientListener() {};

        virtual void onMessage(int msg, int param1, int param2, const MMParam *obj);
        virtual void onServerUpdate(bool die);

    private:
        MediaPlayerClient *mOwner;
    };

    inline MediaPlayerProxy* getProxy() const {
        return static_cast<MediaPlayerProxy*>(mDProxy.pointer());
    }

    virtual SharedPtr<DAdaptor> createMediaAdaptor() { return NULL; }
    virtual SharedPtr<DProxy> createMediaProxy();

friend class ClientListener;

private:
    MediaPlayer::Listener *mListener;
    ClientListener *mClientListener;
    TextureProxy *mTextureProxy;
    YunOSMediaCodec::MediaSurfaceTexture *mMst;
    bool mServerDie;
    uint32_t mCallSeq;
    std::map<uint32_t, int64_t> mCallSeqMap;

#ifdef __USING_VR_VIDEO__
    MMSharedPtr<yunos::yvr::VrVideoView> mVrView;
#endif
#ifdef SINGLE_THREAD_PROXY
    Lock mMethodLock;
    Condition mMethodCondition;
#endif

    static const char * MM_LOG_TAG;

private:
    MediaPlayerClient(const MediaPlayerClient &);
    MediaPlayerClient & operator=(const MediaPlayerClient &);
};

} // end of YUNOS_MM
#endif
