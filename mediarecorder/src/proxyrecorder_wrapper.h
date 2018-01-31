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

#ifndef proxyrecorder_wrapper_h
#define proxyrecorder_wrapper_h

#include <multimedia/mediarecorder.h>

namespace YUNOS_MM {

class MediaRecorderClient;

class ProxyRecorderWrapper : public MediaRecorder {
public:
    ProxyRecorderWrapper(const void * userDefinedData = NULL);
    virtual ~ProxyRecorderWrapper();

public:
    virtual mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy);
    virtual mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setVideoSourceFormat(int width, int height, uint32_t format);
    // virtual mm_status_t setProfile(CamcorderProfile profile) ;

    virtual mm_status_t setVideoEncoder(const char* mime) ;
    virtual mm_status_t setAudioEncoder(const char* mime) ;
    virtual mm_status_t setOutputFormat(const char* mime) ;
    virtual mm_status_t setOutputFile(const char* filePath) ;
    virtual mm_status_t setOutputFile(int fd) ;

    virtual mm_status_t setListener(Listener * listener);
    virtual mm_status_t setRecorderUsage(RecorderUsage usage);
    virtual mm_status_t getRecorderUsage(RecorderUsage &usage);
    virtual mm_status_t setPreviewSurface(void * handle) ;
    virtual mm_status_t prepare() ;
    virtual mm_status_t reset() ;
    virtual mm_status_t start() ;
    virtual mm_status_t stop() ;
    virtual mm_status_t stopSync() ;
    virtual mm_status_t pause() ;
    virtual bool isRecording() const ;
    virtual mm_status_t getVideoSize(int *width, int * height) const ;
    virtual mm_status_t getCurrentPosition(int64_t * msec) const ;
    // parameters like bitrate/framerate/key-frame-internal goes here
    virtual mm_status_t setParameter(const MediaMetaSP & meta) ;
    virtual mm_status_t getParameter(MediaMetaSP & meta) ;
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply) ;

    virtual mm_status_t setMaxDuration(int64_t msec) ;
    virtual mm_status_t setMaxFileSize(int64_t bytes) ;

private:
    MediaRecorderClient *mRecorder;
    RecorderUsage mUsage = RU_None;

    MM_DISALLOW_COPY(ProxyRecorderWrapper)
    DECLARE_LOGTAG()
};

}

#endif // proxyrecorder_wrapper_h
