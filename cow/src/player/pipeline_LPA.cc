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
#include "pipeline_LPA.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-PLLPA")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define AUDIO_SINK_LPA "AudioSinkLPA"

PipelineLPA::PipelineLPA(): mIsLPASupport(false)
{
    FUNC_TRACK();
    mComponents.reserve(3);
    // LPA consume data quickly during start time, produce too many kEventInfoBufferingUpdate
    mBufferUpdateEventFilterCount = 20;
}

PipelineLPA::~PipelineLPA()
{
    FUNC_TRACK();
}

mm_status_t PipelineLPA::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    const char* audioMime = NULL;
    mm_status_t status = MM_ERROR_SUCCESS;
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
    mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);

    while (mm_check_env_str("mm.audio.lpa", "MM_AUDIO_LPA", "1", true)) {
        // try to setup audio sink LPA component
        ComponentSP audioSinkLPA = createComponentHelper(AUDIO_SINK_LPA, MEDIA_MIMETYPE_AUDIO_RENDER);

        if (!audioSinkLPA) {
            ERROR("fail to create audio sink\n");
            break;
        }

        PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(audioSinkLPA.get());
        if(sink){
            sink->setAudioStreamType(mAudioStreamType);
        }
        else{
            ERROR("DYNAMIC_CAST fail\n");
        }

        status = audioSinkLPA->addSource(source, Component::kMediaTypeAudio);
        if (status == MM_ERROR_LPA_NOT_SUPPORT) {
            ERROR("connect failed from demuxer to audio sink LPA\n");
            break;
        }

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioSinkLPA, ComponentInfo::kComponentTypeSink));
            mAudioSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }

        mHasAudio = true;
        mIsLPASupport = true;
        break; //always break here
    }

    if (!mIsLPASupport) {
        if (mStreamInfo[Component::kMediaTypeAudio].size()) {
            ASSERT(mSelectedTrack[Component::kMediaTypeAudio] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeAudio] < mStreamInfo[Component::kMediaTypeAudio].size());
            audioMime = mStreamInfo[Component::kMediaTypeAudio][mSelectedTrack[Component::kMediaTypeAudio]].mime.c_str();
        }
        INFO("media content info, audio: %s\n", audioMime);

        while (audioMime) {
            // setup audio components
            ComponentSP audioDecoder = createComponentHelper(NULL, audioMime);
            ComponentSP audioSink = createComponentHelper(NULL, MEDIA_MIMETYPE_AUDIO_RENDER);
            if (!audioDecoder) {
                ERROR("fail to create audio decoder\n");
                break;
            }

            if (!audioSink) {
                ERROR("fail to create audio sink\n");
                break;
            }

            status = audioDecoder->addSource(source, Component::kMediaTypeAudio);
            if (status != MM_ERROR_SUCCESS) {
                ERROR("connect failed from demuxer to audio decoder\n");
                break;
            }

            status = audioDecoder->addSink(audioSink.get(), Component::kMediaTypeAudio);
            if (status != MM_ERROR_SUCCESS) {
                ERROR("connect failed from demuxer to audio decoder\n");
                break;
            }

            {
                MMAutoLock locker(mLock);
                mComponents.push_back(ComponentInfo(audioDecoder, ComponentInfo::kComponentTypeFilter));
                mAudioCodecIndex = mComponents.size() - 1;
                mComponents.push_back(ComponentInfo(audioSink, ComponentInfo::kComponentTypeSink));
                mSinkClockIndex = mComponents.size() - 1;
            }
            PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(audioSink.get());
            if(sink){
                sink->setAudioStreamType(mAudioStreamType);
            }
            else{
                ERROR("DYNAMIC_CAST fail\n");
            }

            break; //always break here
        }
    }

    return status;
}

mm_status_t PipelineLPA::seek(SeekEventParamSP param)
{
    mm_status_t status = MM_ERROR_SUCCESS;
    int64_t msec = 0;
    param->getTargetSeekTime(msec);
    status = PipelinePlayerBase::seek(param);
    if (status == MM_ERROR_SUCCESS && mIsLPASupport) {
        PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(mComponents[mSinkClockIndex].component.get());
        if(sink){
            sink->seek(msec, mSeekSequence);
        }
    }
    return status;
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    YUNOS_MM::PipelineLPA *pipelineLPA = new YUNOS_MM::PipelineLPA();
    if (pipelineLPA == NULL) {
        return NULL;
    }
    if (pipelineLPA->init() != MM_ERROR_SUCCESS) {
        delete pipelineLPA;
        pipelineLPA = NULL;
        return NULL;
    }
    return static_cast<YUNOS_MM::Pipeline*>(pipelineLPA);
}

void releasePipeline(YUNOS_MM::Pipeline *pipeline)
{
    if (pipeline) {
        pipeline->uninit();
        delete pipeline;
        pipeline = NULL;
    }
}
}

