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

#ifndef mediarecorder_h
#define mediarecorder_h

#include <map>
#include <string>
#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmlistener.h"
#include "multimedia/recorder_common.h"
#include "multimedia/media_meta.h"

namespace YUNOS_MM {
class Pipeline;
typedef MMSharedPtr<Pipeline> PipelineSP;

class MediaRecorder {
public:
    static MediaRecorder * create(RecorderType type = RecorderType_DEFAULT, const void * userDefinedData = NULL);
    static void destroy(MediaRecorder * recorder);

public:
    class Listener : public MMListener {
    public:
        Listener(){}
        virtual ~Listener(){}

        enum message_type {
            // only for test
            MSG_NOP               = 0,
            // recorder has prepared over.
            // params:
            //     param1: prepare result
            MSG_PREPARED          = 1,
            // record has completed
            // params: none
            MSG_RECORDER_COMPLETE = 2,
            // recorder started
            // params:
            //     param1: start result
            MSG_STARTED           = 6,
            // recorder paused
            // params:
            //     param1: pause result
            MSG_PAUSED            = 7,
            // recorder stopped
            // params:
            //     param1: stop result
            MSG_STOPPED           = 8,
            MSG_SKIPPED = 20,
            // an error occured
            // params:
            //     param1: erro code, see mm_errors_t
            MSG_ERROR             = 100,
            // information
            // params:
            //     param1: see info_type
            MSG_INFO              = 200,
            // extended information, by manufacture
            // params:
            //     param1: see manufacture's document
            MSG_INFO_EXT              = 401,
            // music spectrum
            // params:
            //     param1: spectrum
            //     param2: not defined
            //     obj: not defined
            MSG_MUSIC_SPECTRUM = 402,
        };

        enum info_type {
            // Info indicates unknown or not precessed
            MEDIA_RECORDER_INFO_UNKNOWN                   = 1,

            // Info indicates record file reached the max duration
            // Client should stop recording when received this info msg
            MEDIA_RECORDER_INFO_MAX_DURATION_REACHED      = 800,
            // Info indicates record file reached the max limit size
            // Client should stop recording when received this info msg
            MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED      = 801,

            // All track related informtional events start here
            // List start
            // -->>
            MEDIA_RECORDER_TRACK_INFO_LIST_START           = 1000,

            // Info indicates track recording is completed
            MEDIA_RECORDER_TRACK_INFO_COMPLETION_STATUS    = 1000,
            // Info indicates track recording progress, with time in ms
            MEDIA_RECORDER_TRACK_INFO_PROGRESS_IN_TIME     = 1001,
            // Info indicates whether is audio/video track .etc
            MEDIA_RECORDER_TRACK_INFO_TYPE                 = 1002,
            // Info indicates track durations
            // Time in ms
            MEDIA_RECORDER_TRACK_INFO_DURATION_MS          = 1003,

            // Info to measure the max chunk duration, time in ms
            MEDIA_RECORDER_TRACK_INFO_MAX_CHUNK_DUR_MS     = 1004,

            // Info indicates total encoded frames
            MEDIA_RECORDER_TRACK_INFO_ENCODED_FRAMES       = 1005,

            // Info to measure how well the audio and video
            // track data is interleaved.
            MEDIA_RECORDER_TRACK_INTER_CHUNK_TIME_MS       = 1006,

            // Info to measure system response, time in ms
            // Note that the delay does not include the intentional delay
            // we use to eliminate the recording sound.
            MEDIA_RECORDER_TRACK_INFO_INITIAL_DELAY_MS     = 1007,

            // The time  for track started offset,
            // which is used to compensate for initial A/V sync.
            MEDIA_RECORDER_TRACK_INFO_START_OFFSET_MS      = 1008,

            // Total number of bytes of the recording file.
            // Note: recording file should be small than limit file size if set
            MEDIA_RECORDER_TRACK_INFO_DATA_KBYTES          = 1009,

            // Track info end
            // <<--
            MEDIA_RECORDER_TRACK_INFO_LIST_END             = 2000,

        };

    };

    // FIXME, remove EInvokeKey
    enum EInvokeKey {
        INVOKE_ID_GET_TRACK_INFO = 1,
        INVOKE_ID_ADD_EXTERNAL_SOURCE = 2,
        INVOKE_ID_ADD_EXTERNAL_SOURCE_FD = 3,
        INVOKE_ID_SELECT_TRACK = 4,
        INVOKE_ID_UNSELECT_TRACK = 5,
        INVOKE_ID_SET_VIDEO_SCALING_MODE = 6,
        INVOKE_ID_PRINT_DUMP_INFO =7,
    };

    enum ETrackType {
        TRACK_TYPE_UNKNOWN = 0,
        TRACK_TYPE_VIDEO = 1,
        TRACK_TYPE_AUDIO = 2,
        TRACK_TYPE_TIMEDTEXT = 3,
        TRACK_TYPE_SUBTITLE = 4,
    };

public:
    struct VolumeInfo {
        float left;
        float right;
    };
    virtual mm_status_t setPipeline(PipelineSP pipeline) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy) = 0;
    virtual mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL) = 0;
    virtual mm_status_t setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL) = 0;
    virtual mm_status_t setVideoSourceFormat(int width, int height, uint32_t format) = 0;
    virtual mm_status_t setVideoEncoder(const char* mime) = 0;
    virtual mm_status_t setAudioEncoder(const char* mime) = 0;
    virtual mm_status_t setOutputFormat(const char* mime) = 0;
    virtual mm_status_t setOutputFile(const char* filePath) = 0;
    virtual mm_status_t setOutputFile(int fd) = 0;
    virtual mm_status_t setListener(Listener * listener);
    virtual mm_status_t setRecorderUsage(RecorderUsage usage) = 0;
    virtual mm_status_t getRecorderUsage(RecorderUsage &usage) = 0;
    virtual mm_status_t setPreviewSurface(void * handle) = 0;
    virtual mm_status_t prepare() = 0;
    virtual mm_status_t reset() = 0;
    virtual mm_status_t start() = 0;
    // Async method
    virtual mm_status_t stop() = 0;
    // Sync method
    virtual mm_status_t stopSync() = 0;
    virtual mm_status_t pause() = 0;
    virtual bool isRecording() const = 0;
    virtual mm_status_t getVideoSize(int *width, int * height) const = 0;
    virtual mm_status_t getCurrentPosition(int64_t * msec) const = 0;

    // parameters like bitrate/framerate/key-frame-internal goes here
    virtual mm_status_t setParameter(const MediaMetaSP & meta) = 0;
    virtual mm_status_t getParameter(MediaMetaSP & meta) = 0;
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply) = 0;

    virtual mm_status_t setMaxDuration(int64_t msec) = 0;
    virtual mm_status_t setMaxFileSize(int64_t bytes) = 0;

    virtual mm_status_t setAudioConnectionId(const char * connectionId) { return MM_ERROR_UNSUPPORTED; }
    virtual const char * getAudioConnectionId() const { return ""; }

protected:
    MediaRecorder();
    virtual ~MediaRecorder();

protected:
    Listener * mListener;
    void* mLibHandle;
private:
    static MediaRecorder *createBinderProxy();

    MM_DISALLOW_COPY(MediaRecorder)
    DECLARE_LOGTAG()
};

}

#endif /* mediarecorder_h */

