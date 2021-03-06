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

#ifndef __CowPlayerDMWrapper_H
#define __CowPlayerDMWrapper_H

#include <multimedia/mediaplayer.h>
#include "multimedia/cowplayer.h"
#include <pthread.h>
#include "DeviceManager.h"

namespace YUNOS_MM {

using yunos::DeviceClientListener;
using yunos::DeviceClient;
using yunos::DeviceUsbComponent;
using yunos::DeviceUsbComponentType;
using yunos::DeviceComponent;


class CowPlayerDMWrapper : public MediaPlayer {
public:
    CowPlayerDMWrapper(bool audioOnly = false, const void * userDefinedData = NULL);
    ~CowPlayerDMWrapper();

public:
    virtual mm_status_t setPipeline(PipelineSP pipeline);
    virtual mm_status_t setListener(Listener * listener);
    virtual mm_status_t setDataSource(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setDataSource(int fd, int64_t offset, int64_t length);
    virtual mm_status_t setDataSource(const unsigned char * mem, size_t size);
    virtual mm_status_t setSubtitleSource(const char* uri);
    virtual mm_status_t setDisplayName(const char* name);
    virtual mm_status_t setNativeDisplay(void * display);
    virtual mm_status_t setVideoDisplay(void * handle);
    virtual mm_status_t setVideoSurfaceTexture(void * handle);
    virtual mm_status_t prepare();
    virtual mm_status_t prepareAsync();
    virtual mm_status_t setVolume(const VolumeInfo & volume);
    virtual mm_status_t getVolume(VolumeInfo * volume) const;
    virtual mm_status_t setMute(bool mute);
    virtual mm_status_t getMute(bool * mute) const;
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t seek(int msec);
    virtual mm_status_t reset();
    virtual bool isPlaying() const;
    virtual mm_status_t getVideoSize(int *width, int * height) const;
    virtual mm_status_t getCurrentPosition(int * msec) const;
    virtual mm_status_t getDuration(int * msec) const;
    virtual mm_status_t setAudioStreamType(as_type_t type);
    virtual mm_status_t getAudioStreamType(as_type_t *type);
    virtual mm_status_t setLoop(bool loop);
    virtual bool isLooping() const;
    // A language code in either way of ISO-639-1 or ISO-639-2. When the language is unknown or could not be determined, MM_ERROR_UNSUPPORTED will returned
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply);

    virtual mm_status_t captureVideo();

    virtual mm_status_t pushData(MediaBufferSP & buffer);
    virtual mm_status_t enableExternalSubtitleSupport(bool enable);

#if 0
protected:
    static int convertCowErrorCode(int code);
    static int convertCowInfoCode(int code);
#endif

private:
    mm_status_t getResource();
    mm_status_t releaseResource();
    mm_status_t playVideoByHardware();
    mm_status_t playVideoBySoftware();
    void onPrepare();

private:
    class CowListener : public CowPlayer::Listener {
    public:
        CowListener(CowPlayerDMWrapper * watcher);
        virtual ~CowListener();

    public:
        virtual void onMessage(int eventType, int param1, int param2, const MMParamSP meta);

    private:
        CowListener();

    private:
        CowPlayerDMWrapper * mWatcher;

        MM_DISALLOW_COPY(CowListener)
        DECLARE_LOGTAG()
    };

    friend class CowListener;


    class DeviceListener: public DeviceClientListener {
        public:
            DeviceListener(CowPlayerDMWrapper* player):mPlayer(player) { }
            ~DeviceListener() { }

            virtual void onDevicesAvailable(DeviceClient *client) { }
            virtual void onLostUsbDevice(DeviceClient *client, const DeviceUsbComponent &dev) { }
            virtual void onLostComponentDevice(DeviceClient *client, DeviceComponent comp) { }
            virtual void onRespond(DeviceClient *client, int32_t id, int32_t result,
                    const DeviceUsbComponent *getDevices, int32_t size,
                    const DeviceComponent *lostComponents, int32_t len,
                    const DeviceUsbComponentType* lostDevices, int32_t devsize);

            virtual void onReleaseDevices(DeviceClient *client);
        private:
            CowPlayerDMWrapper* mPlayer;
    };
    friend class DeviceListener;

private:
    CowPlayer * mPlayer;
    CowListener * mCowListener;
    bool mAudioOnly;

    DeviceClient* mClient;
    DeviceListener* mDMListener;
    int32_t mState;
    pthread_mutex_t mStateMutex;
    pthread_cond_t mPrepareCond;

    std::string mUriHigh;
    std::string mUriLow;
    std::map<std::string, std::string> mHeader;
    bool mIsDataSet;
    MM_DISALLOW_COPY(CowPlayerDMWrapper)
    DECLARE_LOGTAG()
};

}

#endif // __CowPlayerDMWrapper_H

