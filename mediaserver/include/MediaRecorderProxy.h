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
#include <map>
#include <string>

#include <string/String.h>
#include <dbus/DProxy.h>

#include <multimedia/mm_cpp_utils.h>
#include "multimedia/recorder_common.h"

#include <MediaProxy.h>

#ifndef __media_recorder_proxy_h
#define __media_recorder_proxy_h

namespace YUNOS_MM {

using namespace yunos;

class MediaRecorderProxy : public MediaProxy {

public:
    MediaRecorderProxy(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface);

    virtual ~MediaRecorderProxy();
    mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy);
    mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    mm_status_t setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    mm_status_t setVideoSourceFormat(int width, int height, uint32_t format);
    mm_status_t setVideoEncoder(const char* mime);
    mm_status_t setAudioEncoder(const char* mime);
    mm_status_t setOutputFormat(const char* mime);
    mm_status_t setOutputFile(const char* filePath);
    mm_status_t setOutputFile(int fd);

    //mm_status_t setListener(ProxyListener * listener);
    mm_status_t setRecorderUsage(int usage);
    mm_status_t getRecorderUsage(int &usage);
    mm_status_t setPreviewSurface(void * handle);

    mm_status_t prepare();
    mm_status_t reset();
    mm_status_t start();
    mm_status_t stop();
    mm_status_t stopSync();
    mm_status_t pause();

    bool isRecording();
    mm_status_t getVideoSize(int *width, int * height);
    mm_status_t getCurrentPosition(int64_t * msec);

    mm_status_t setParameter(const MediaMetaSP & meta);
    mm_status_t getParameter(MediaMetaSP & meta);
    mm_status_t invoke(const MMParam * request, MMParam * reply);
    mm_status_t setMaxDuration(int64_t msec);
    mm_status_t setMaxFileSize(int64_t bytes);

#ifdef SINGLE_THREAD_PROXY
    virtual int64_t handleMethod(MMParam &param);
#endif

private:
    static const char * MM_LOG_TAG;

private:
    MediaRecorderProxy(const MediaRecorderProxy &);
    MediaRecorderProxy & operator=(const MediaRecorderProxy &);
};

} // end of YUNOS_MM
#endif
