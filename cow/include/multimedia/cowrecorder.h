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
#ifndef cowrecorder_h
#define cowrecorder_h

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmparam.h"
#include "multimedia/media_meta.h"
#include "multimedia/recorder_common.h"
#include "multimedia/pipeline.h" // give client opportunity to customize pipeline

namespace YUNOS_MM {

class CowRecorder {
/* most action (setDataSource, prepare, start/stop etc) are handled in async mode,
 * and nofity upper layer (Listener) after it has been executed.
 */
public:
    CowRecorder(RecorderType type = RecorderType_COW);
    virtual ~CowRecorder();

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
    virtual mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy);
    virtual mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setVideoSourceFormat(int width, int height, uint32_t format);
    // virtual mm_status_t setProfile(CamcorderProfile profile);

    virtual mm_status_t setVideoEncoder(const char* mime);
    virtual mm_status_t setAudioEncoder(const char* mime);
    virtual mm_status_t setOutputFormat(const char* mime);
    virtual mm_status_t setOutputFile(const char* filePath);
    virtual mm_status_t setOutputFile(int fd);

    virtual mm_status_t setRecorderUsage(RecorderUsage usage);
    virtual mm_status_t getRecorderUsage(RecorderUsage &usage);
    virtual mm_status_t setListener(Listener * listener);
    virtual void removeListener();
    virtual mm_status_t setPreviewSurface(void * handle);
    virtual mm_status_t prepare();
    virtual mm_status_t prepareAsync();
    virtual mm_status_t start();
    virtual mm_status_t stopSync() ;
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t reset();
    virtual bool isRecording() const;
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    virtual mm_status_t getCurrentPosition(int64_t& msec) const;
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply);

    virtual mm_status_t setMaxDuration(int64_t msec);
    virtual mm_status_t setMaxFileSize(int64_t bytes);

    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;

  private:
     mm_status_t notify(int msg, int param1, int param2, const MMParamSP obj);
    // avoid dependency on internal header files pipeline_xxxx.h
    class ListenerRecorder;
    class Private;
    typedef MMSharedPtr<Private> PrivateSP;
    PrivateSP mPriv;
    Lock mLock;
    Listener* mListenderSend;
    bool mAudioOnly;

    MM_DISALLOW_COPY(CowRecorder)
   };

} // YUNOS_MM

#endif // cowrecorder_h
