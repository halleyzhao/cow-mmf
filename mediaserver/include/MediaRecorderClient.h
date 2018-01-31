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

#include <multimedia/mediarecorder.h>
#include <multimedia/mm_cpp_utils.h>
#include <MediaServiceLooper.h>

#include <MediaRecorderProxy.h>
#include <MediaMethodID.h>

#ifndef __media_recorder_client_h
#define __media_recorder_client_h

namespace YUNOS_MM {

using namespace yunos;

class MediaRecorderClient : public MediaServiceLooper {

public:

    MediaRecorderClient(const String &service,
                      const String &path,
                      const String &iface);
    virtual ~MediaRecorderClient();

    // MediaRecorder interface
    mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy);
    mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    mm_status_t setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    mm_status_t setVideoSourceFormat(int width, int height, uint32_t format);
    mm_status_t setVideoEncoder(const char* mime);
    mm_status_t setAudioEncoder(const char* mime);
    mm_status_t setOutputFormat(const char* mime);
    mm_status_t setOutputFile(const char* filePath);
    mm_status_t setOutputFile(int fd);

    mm_status_t setListener(MediaRecorder::Listener * listener);
    mm_status_t setRecorderUsage(RecorderUsage usage);
    mm_status_t getRecorderUsage(RecorderUsage &usage);
    mm_status_t setPreviewSurface(void * handle);

    mm_status_t prepare();
    mm_status_t reset();
    mm_status_t start();
    mm_status_t stop();
    mm_status_t stopSync() ;
    mm_status_t pause();
    bool isRecording() const;
    mm_status_t getVideoSize(int *width, int * height) const;
    mm_status_t getCurrentPosition(int64_t * msec) const;

    mm_status_t setParameter(const MediaMetaSP & meta);
    mm_status_t getParameter(MediaMetaSP & meta);
    mm_status_t invoke(const MMParam * request, MMParam * reply);
    mm_status_t setMaxDuration(int64_t msec);
    mm_status_t setMaxFileSize(int64_t bytes);

    mm_status_t release();

friend class MediaClientHelper;

private:

#ifdef SINGLE_THREAD_PROXY
    int64_t sendMethodCommand(MMParam &param);
    static void sendMethodCommand1(MediaRecorderClient* p, MMParam &param, uint32_t seq);
#endif

    class ClientListener : public MediaRecorderProxy::ProxyListener {
    public:
        ClientListener(MediaRecorderClient* owner)
            : mOwner(owner) {
        }
        virtual ~ClientListener() {};

        virtual void onMessage(int msg, int param1, int param2, const MMParam *obj);
        virtual void onServerUpdate(bool die);

    private:
        MediaRecorderClient *mOwner;
    };

    inline MediaRecorderProxy* getProxy() const {
        return static_cast<MediaRecorderProxy*>(mDProxy.pointer());
    }

    virtual SharedPtr<DAdaptor> createMediaAdaptor() { return NULL; }
    virtual SharedPtr<DProxy> createMediaProxy();

friend class ClientListener;

private:
    MediaRecorder::Listener *mListener;
    ClientListener *mClientListener;
    bool mServerDie;
    uint32_t mCallSeq;
    std::map<uint32_t, int64_t> mCallSeqMap;

#ifdef SINGLE_THREAD_PROXY
    Lock mMethodLock;
    Condition mMethodCondition;
#endif

    static const char * MM_LOG_TAG;

private:
    MediaRecorderClient(const MediaRecorderClient &);
    MediaRecorderClient & operator=(const MediaRecorderClient &);
};

} // end of YUNOS_MM
#endif
