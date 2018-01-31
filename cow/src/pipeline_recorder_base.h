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
#ifndef pipeline_recorder_base_h
#define pipeline_recorder_base_h
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_meta.h"
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/elapsedtimer.h"
#include "multimedia/recorder_common.h"
#include "multimedia/mm_debug.h"
#ifndef __DISABLE_AUDIO_STREAM__
#include "multimedia/mm_audio.h"
#endif

#include "multimedia/pipeline.h"

namespace YUNOS_MM {
class PipelineRecorderBase;
typedef MMSharedPtr<PipelineRecorderBase> PipelineRecorderBaseSP;

class PipelineRecorderBase : public Pipeline {
  public:
    // source: source component, camera or file with raw video data
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    virtual mm_status_t prepare();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventStartResult.
    //              others: error.
    virtual mm_status_t start();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventStopped.
    //              others: error.
    virtual mm_status_t stop();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventPaused.
    //              others: error.
    virtual mm_status_t pause();

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResumed.
    //              others: error.
    virtual mm_status_t resume();

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResetComplete.
    //              others: error.
    virtual mm_status_t reset();
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventFlushComplete.
    //              others: error.
    virtual mm_status_t flush();
//    virtual mm_status_t setListener(ListenerSP listener) { mListenerSend = listener; return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error.
    virtual mm_status_t getState(ComponentStateType& state);
    // get current playback position
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t getCurrentPosition(int64_t& position) const;

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    // unblock the wait of current command execution
    virtual mm_status_t  unblock();
#ifndef __DISABLE_AUDIO_STREAM__
    static adev_source_t getAudioSourceType(const char* uri = "cras://");
#endif

    virtual ~PipelineRecorderBase();

  protected:

     //stopInternal can be implemented by each derived class if needed
     virtual mm_status_t stopInternal(){return MM_ERROR_SUCCESS;}

     //resetInternal can be implemented by each derived class if needed
     virtual mm_status_t resetInternal(){return MM_ERROR_SUCCESS;}
     virtual Component* getSourceComponent(bool isAudio = false);

    //protected?
  public:
    virtual mm_status_t setCamera(VideoCapture *camera, RecordingProxy *recordingProxy);
    // uri for file source, device node for camera source
    virtual mm_status_t setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers = NULL) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setVideoSourceFormat(int width, int height, uint32_t format) ;
    // virtual mm_status_t setProfile(CamcorderProfile profile);
    virtual mm_status_t setAudioSource(const char* uri);
    virtual mm_status_t setVideoSource(const char* uri);

    virtual mm_status_t setVideoEncoder(const char* mime) ;
    virtual mm_status_t setAudioEncoder(const char* mime) ;
    virtual mm_status_t setOutputFormat(const char* mime);
    virtual mm_status_t setOutputFile(const char* filePath);
    virtual mm_status_t setOutputFile(int fd);

    virtual mm_status_t setPreviewSurface(void * handle);

    virtual mm_status_t setVolume(const float left, const float right) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t getVolume(float& left, float& right) const {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setMaxDuration(int64_t msec);
    virtual mm_status_t setMaxFileSize(int64_t bytes);
    virtual mm_status_t setRecorderUsage(RecorderUsage usage);
    void dump();

    mm_status_t PipelineRecorderBase::setAudioConnectionId(const char * connectionId);
    const char * PipelineRecorderBase::getAudioConnectionId() const;

  // FIXME: private
  protected:

    void* mSurface;

    ComponentStateType mState;
    ComponentStateType mState2; // help for seek/flush/reset status check
    int32_t mMuxIndex;
    int32_t mSinkIndex;
    int32_t mSinkClockIndex;

    int32_t mAudioSourceIndex;
    int32_t mVideoSourceIndex;
    int32_t mAudioCodecIndex;
    int32_t mVideoCodecIndex;

    int32_t mAudioBitRate;
    int32_t mChannelCount;
    int32_t mSampleRate;

#ifndef __DISABLE_AUDIO_STREAM__
    adev_source_t mAudioSource;
    snd_format_t mAudioFormat;//TODO
#endif

    std::string mAudioEncoderMime;
    std::string mVideoEncoderMime;
    std::string mVideoSourceUri;
    std::string mAudioSourceUri;


    bool mEscapeWait; // escape conditionly wait upon new command (stop/flush/reset etc)
    int32_t mMusicSpectrum;

    // meta information
    int64_t mDuration;
    int32_t mVideoWidth, mVideoHeight;
    float   mFrameRate;
    int32_t mRotation;
    int32_t mConnectedStreamCount; // how many streams are flowing in pipeline
    int32_t mEOSStreamCount;     // how many EOS received
    bool    mEosReceived;

    uint32_t mVideoColorFormat;
    MediaMetaSP mMediaMetaAudio;
    MediaMetaSP mMediaMetaVideo;
    MediaMetaSP mMediaMetaFile;
    MediaMetaSP mMediaMetaOutput;

    std::string mOutputFormat;

    RecorderUsage mUsage;
    int64_t mDelayTimeUs; // start delay time, in us
    std::string mAudioConnectionId;

    PipelineRecorderBase();

    MM_DISALLOW_COPY(PipelineRecorderBase)

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER2(onComponentMessage)
    DECLARE_MSG_HANDLER2(onCowMessage)

}; // PipelineRecorderBase

} // YUNOS_MM

#endif // pipeline_recorder_base_h

