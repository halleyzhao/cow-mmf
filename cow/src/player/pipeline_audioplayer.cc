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
#include "multimedia/pipeline_audioplayer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-PLAP")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;
PipelineAudioPlayer::PipelineAudioPlayer()
{
    mComponents.reserve(3);
    FUNC_TRACK();

}

mm_status_t PipelineAudioPlayer::prepareInternal()//audio source is followed by video source
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

    if (mStreamInfo[Component::kMediaTypeAudio].size()) {
        ASSERT(mSelectedTrack[Component::kMediaTypeAudio] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeAudio] < mStreamInfo[Component::kMediaTypeAudio].size());
        audioMime = mStreamInfo[Component::kMediaTypeAudio][mSelectedTrack[Component::kMediaTypeAudio]].mime.c_str();
    }
    INFO("media content info, audio: %s\n", audioMime);

    mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);

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


    return status;
}

} // YUNOS_MM
