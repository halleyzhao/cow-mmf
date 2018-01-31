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
#ifndef cowplayer_h
#define cowplayer_h

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmparam.h"
#include "multimedia/media_meta.h"
#include "multimedia/pipeline.h" // give client opportunity to customize pipeline

namespace YUNOS_MM {

class CowPlayer {
/* most action (setDataSource, prepare, start/stop etc) are handled in async mode,
 * and nofity upper layer (Listener) after it has been executed.
 */
public:
    enum PlayerType {
        PlayerType_DEFAULT,
        PlayerType_ADO,
        PlayerType_COMPAT,
        PlayerType_COW,
        PlayerType_COWAudio,
        PlayerType_LPA,
        PlayerType_PROXY,
        PlayerType_PROXY_Audio,
    };

    CowPlayer(int playType = 0);
    virtual ~CowPlayer();
    class Listener{
      public:
        Listener(){}
        virtual ~Listener(){}

        virtual void onMessage(int msg, int param1, int param2, const MMParamSP param) = 0;

        MM_DISALLOW_COPY(Listener)
    };
    typedef MMSharedPtr<Listener> ListenerSP;

public:
    virtual mm_status_t setPipeline(PipelineSP pipeline);
    virtual mm_status_t setListener(Listener * listener);
    virtual void removeListener();
    virtual mm_status_t setDataSource(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setDataSource(int fd, int64_t offset, int64_t length);
    virtual mm_status_t setDataSourceAsync(int fd, int64_t offset, int64_t length);
    virtual mm_status_t setDataSource(const unsigned char * mem, size_t size);
    virtual mm_status_t setSubtitleSource(const char* uri);
    virtual mm_status_t setDisplayName(const char* name);
    virtual mm_status_t setNativeDisplay(void * display);
    virtual mm_status_t setVideoSurface(void * handle, bool isTexture = false);
    virtual mm_status_t prepare();
    virtual mm_status_t prepareAsync();
    virtual mm_status_t setVolume(const float left, const float right);
    virtual mm_status_t getVolume(float& left, float& right) const;
    virtual mm_status_t setMute(bool mute);
    virtual mm_status_t getMute(bool * mute) const;
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t seek(int64_t msec);
    virtual mm_status_t reset();
    virtual bool isPlaying() const;
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    virtual mm_status_t getCurrentPosition(int64_t& msec) const;
    virtual mm_status_t getDuration(int64_t& msec) const;
    virtual mm_status_t setAudioStreamType(int type);
    virtual mm_status_t getAudioStreamType(int *type);
    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;
    virtual mm_status_t setLoop(bool loop);
    virtual bool isLooping() const;
    virtual mm_status_t selectTrack(int mediaType, int index);
    virtual int getSelectedTrack(int mediaType);

    virtual MMParamSP getTrackInfo();
    // A language code in either way of ISO-639-1 or ISO-639-2. When the language is unknown or could not be determined, MM_ERROR_UNSUPPORTED will returned
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply);
    // virtual mm_status_t captureVideo();

    virtual mm_status_t pushData(MediaBufferSP & buffer);
    virtual mm_status_t enableExternalSubtitleSupport(bool enable);

  private:
     mm_status_t notify(int msg, int param1, int param2, const MMParamSP obj);
     mm_status_t seekInternal(int64_t msec);
    // avoid dependency on internal header files pipeline_player.h
    class ListenerPlayer;
    class Private;
    typedef MMSharedPtr<Private> PrivateSP;
    PrivateSP mPriv;
    Listener* mListenderSend;
    bool mLoop;
    int mPlayType;

    MM_DISALLOW_COPY(CowPlayer)

   };
} // YUNOS_MM

#endif // cowplayer_h
