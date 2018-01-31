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

#include <MediaProxy.h>

#include <string/String.h>
#include <dbus/DProxy.h>

#include <multimedia/mm_cpp_utils.h>

#include <map>
#include <string>

#ifndef __media_player_proxy_h
#define __media_player_proxy_h

namespace YUNOS_MM {

using namespace yunos;

class MediaPlayerProxy : public MediaProxy {

public:
    MediaPlayerProxy(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface);

    virtual ~MediaPlayerProxy();

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
    mm_status_t setVolume(float left, float right);
    mm_status_t getVolume(float &left, float &right);
    mm_status_t setMute(bool mute);
    mm_status_t getMute(bool * mute);
    mm_status_t start();
    mm_status_t stop();
    mm_status_t pause();
    mm_status_t seek(int msec);
    bool isPlaying();
    mm_status_t getVideoSize(int *width, int * height);
    mm_status_t getCurrentPosition(int * msec);
    mm_status_t getDuration(int * msec);
    mm_status_t setAudioStreamType(int type);
    mm_status_t getAudioStreamType(int *type);
    mm_status_t setLoop(bool loop);
    bool isLooping();
    mm_status_t setParameter(const MediaMetaSP & meta);
    mm_status_t getParameter(MediaMetaSP & meta);
    mm_status_t invoke(const MMParam * request, MMParam * reply);
    mm_status_t captureVideo();
    mm_status_t enableExternalSubtitleSupport(bool enable);

    /* MediaSurfaceTexture consumer proxy */
    void returnAcquiredBuffers();
    ProxyClientBuffer* acquireBuffer(int index);
    int returnBuffer(ProxyClientBuffer* anb);
    int setWindowSurface(void *ws);
    int setShowFlag(bool show);
    bool getShowFlag();
    int setMstListener(YunOSMediaCodec::SurfaceTextureListener *listener);

#ifdef SINGLE_THREAD_PROXY
    virtual int64_t handleMethod(MMParam &param);
#endif

private:
    static const char * MM_LOG_TAG;

private:
    MediaPlayerProxy(const MediaPlayerProxy &);
    MediaPlayerProxy & operator=(const MediaPlayerProxy &);
};

} // end of YUNOS_MM
#endif
