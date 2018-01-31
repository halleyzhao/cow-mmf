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
 #include <unistd.h>
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/pipeline_player_base.h"

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
#define USE_CUSTOM_VIDEO_SINK       1
#endif
#if USE_CUSTOM_VIDEO_SINK
#include "custom_video_sink.h"
#endif

using namespace YUNOS_MM;
MM_LOG_DEFINE_MODULE_NAME("ExamplePipeline")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)

class CustomPipeline : public PipelinePlayerBase {
  public:

    CustomPipeline();
    ~CustomPipeline();

  protected:
    virtual mm_status_t prepareInternal();

  private:
    MM_DISALLOW_COPY(CustomPipeline)
}; // CustomPipeline

CustomPipeline::CustomPipeline()
{
    mComponents.reserve(5);
    FUNC_TRACK();

}

CustomPipeline::~CustomPipeline()
{
    FUNC_TRACK();
}

mm_status_t CustomPipeline::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    const char* videoMime = NULL;
    const char* audioMime = NULL;
    mm_status_t status = MM_ERROR_SUCCESS;
    mm_status_t ret = MM_ERROR_SUCCESS;
    ComponentSP videoSink;
    ComponentSP videoDecoder;
    // MMAutoLock locker(mLock); NO big lock
    setState(mState, kComponentStatePreparing);
    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);

    status = source->prepare();
    ASSERT_RET(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC, status);
    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mComponents[mDemuxIndex].state, kComponentStatePrepared, false/*pipeline state*/);
    }
    if (status != MM_ERROR_SUCCESS)
        return status;

    status = updateTrackInfo();
    ASSERT_RET(status == MM_ERROR_SUCCESS, status);

    if (mStreamInfo[Component::kMediaTypeVideo].size()) {
        ASSERT(mSelectedTrack[Component::kMediaTypeVideo] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeVideo] < mStreamInfo[Component::kMediaTypeVideo].size());
        videoMime = mStreamInfo[Component::kMediaTypeVideo][mSelectedTrack[Component::kMediaTypeVideo]].mime.c_str();
    }
    if (mStreamInfo[Component::kMediaTypeAudio].size()) {
        ASSERT(mSelectedTrack[Component::kMediaTypeAudio] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeAudio] < mStreamInfo[Component::kMediaTypeAudio].size());
        audioMime = mStreamInfo[Component::kMediaTypeAudio][mSelectedTrack[Component::kMediaTypeAudio]].mime.c_str();
    }
    INFO("media content info, video: %s, audio: %s\n", videoMime, audioMime);

    if ((audioMime == NULL) &&
        (videoMime == NULL)) {
        return MM_ERROR_OP_FAILED;
    }
    if (audioMime != NULL && !strcmp(audioMime, "unknown")) {
        notify(Component::kEventError, MM_ERROR_NO_AUDIODECODER, 0, nilParam);
        audioMime = NULL;
        ret = MM_ERROR_NO_AUDIODECODER;
    }
    if (videoMime != NULL && !strcmp(videoMime, "unknown")) {
        notify(Component::kEventError, MM_ERROR_NO_VIDEODECODER, 0, nilParam);
        videoMime = NULL;
        ret = MM_ERROR_NO_VIDEODECODER;
    }

    mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);


    while (videoMime) {
        // setup video components
        videoDecoder = createComponentHelper(NULL, videoMime);
#if USE_CUSTOM_VIDEO_SINK
        DEBUG("use custom video sink");
        VideoSinkCustom* customVideoSink =  new VideoSinkCustom();
        if (customVideoSink) {
            mm_status_t status = customVideoSink->init();
            if (status != MM_ERROR_SUCCESS) {
                ERROR("com %s init failed %d", customVideoSink->name(), status);
                delete customVideoSink;
                break;
            }
            if (mListenerReceive) {
                customVideoSink->setListener(mListenerReceive);
            }
            videoSink.reset(customVideoSink, releaseCustomVideoSink);
        }
#else
        videoSink = createComponentHelper(NULL, MEDIA_MIMETYPE_VIDEO_RENDER);
#endif
        //ASSERT_RET(videoDecoder && videoSink, MM_ERROR_NO_COMPONENT);
        if (!videoDecoder) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video decoder\n");
            notify(Component::kEventError, MM_ERROR_NO_VIDEODECODER, 0, nilParam);
            break;
        }

        if (!videoSink) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video sink\n");
            break;
        }

        status = videoDecoder->addSource(source, Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoDecoder->addSink(videoSink.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        // add video filters if necessary
        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoDecoder, ComponentInfo::kComponentTypeFilter));
            mVideoCodecIndex = mComponents.size() - 1;
            mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
            mVideoSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }


        //Media which only contains audio stream doesn't support variable rate
        mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, true);
        mHasVideo = true;

        break;
    }

    {
        MMAutoLock locker(mLock);
        //protect mSurface
        if (mSurface && videoDecoder) {
            if (mIsSurfaceTexture)
                mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, mSurface);
            else
                mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mSurface);
            videoDecoder->setParameter(mMediaMeta);
        }
    }

    while (audioMime) {
        // setup audio components
        ComponentSP audioDecoder = createComponentHelper(NULL, audioMime);
        ComponentSP audioSink = createComponentHelper(NULL, MEDIA_MIMETYPE_AUDIO_RENDER);
        // ASSERT_RET(audioDecoder && audioSink, MM_ERROR_NO_COMPONENT);
        if (!audioDecoder) {
            ERROR("fail to create audio decoder\n");
            notify(Component::kEventError, MM_ERROR_NO_AUDIODECODER, 0, nilParam);
            break;
        }

        if (!audioSink) {
            ERROR("fail to create audio sink\n");
            break;
        }

        status = audioDecoder->addSource(source, Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio decoder\n");
            break;
        }

        status = audioDecoder->addSink(audioSink.get(), Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio decoder\n");
            break;
        }

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioDecoder, ComponentInfo::kComponentTypeFilter));
            mAudioCodecIndex = mComponents.size() - 1;
            mComponents.push_back(ComponentInfo(audioSink, ComponentInfo::kComponentTypeSink));
            mAudioSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }
        if (videoSink) {
            ClockSP clock = audioSink->provideClock();
            videoSink->setClock(clock);
        }
        PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(audioSink.get());
        if(sink){
            sink->setAudioStreamType(mAudioStreamType);
        }
        else{
            ERROR("DYNAMIC_CAST fail\n");
        }

        mHasAudio = true;
        break; //always break here
    }

    if (MM_ERROR_SUCCESS != ret)
        status = ret;
    return status;
}

/////////////////////////////////////////////////////////////////////////////////////
Pipeline* createCustomPipeline()
{
    CustomPipeline *pipelinePlayer = new CustomPipeline();
    if (pipelinePlayer == NULL) {
        return NULL;
    }
    if (pipelinePlayer->init() != MM_ERROR_SUCCESS) {
        delete pipelinePlayer;
        pipelinePlayer = NULL;
        return NULL;
    }

    return pipelinePlayer;
}

