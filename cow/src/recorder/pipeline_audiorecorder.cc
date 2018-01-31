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
#include "pipeline_audiorecorder.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("PLP-VPRA")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;
PipelineAudioRecorder::PipelineAudioRecorder()
{
    FUNC_TRACK();
    mComponents.reserve(4);
}

PipelineAudioRecorder::~PipelineAudioRecorder()
{
    /*
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    listener->removeWatcher();
    */
}

mm_status_t PipelineAudioRecorder::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;
    mMediaMetaAudio->dump();

    ComponentSP audioSource;
    audioSource = createComponentHelper(NULL,
        Pipeline::getSourceUri(mAudioSourceUri.c_str(), true).c_str(),
        mListenerReceive, false);
    ASSERT(audioSource);
    audioSource->setParameter(mMediaMetaAudio);
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
    audioSource->setAudioConnectionId(mAudioConnectionId.c_str());
#endif
    ComponentSP muxer = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_MUXER, mListenerReceive, false);
    ASSERT(muxer);
    muxer->setParameter(mMediaMetaFile);
    ComponentSP fileSink = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_FILE_SINK, mListenerReceive, false);
    ASSERT(fileSink);
    fileSink->setParameter(mMediaMetaFile);

    {
        MMAutoLock locker(mLock);
        mComponents.push_back(ComponentInfo(muxer, ComponentInfo::kComponentTypeFilter));
        mMuxIndex = mComponents.size() - 1;
        mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));
        mSinkIndex = mComponents.size() - 1;
        mConnectedStreamCount++;
        mSinkClockIndex = mComponents.size() - 1;
    }

    ComponentSP audioEncoder;
    // create components, setup pipeline
    audioEncoder = createComponentHelper(NULL, mAudioEncoderMime.c_str(), mListenerReceive, true);
    ASSERT(audioEncoder);

    audioEncoder->setParameter(mMediaMetaAudio);

    DEBUG("audio encoder: %p, muxer: %p, fileSink: %p\n", audioEncoder.get(), muxer.get(), fileSink.get());
    if (!audioEncoder || !muxer || !fileSink) {
        return MM_ERROR_NO_COMPONENT;
    }


    {
        MMAutoLock locker(mLock);
        mComponents.push_back(ComponentInfo(audioSource, ComponentInfo::kComponentTypeSource));
        mAudioSourceIndex = mComponents.size()-1;
        mComponents.push_back(ComponentInfo(audioEncoder, ComponentInfo::kComponentTypeFilter));
    }

    // need to prepare source first
    setState(mComponents[mAudioSourceIndex].state, kComponentStatePreparing);
    mm_status_t ret = audioSource->prepare();
    if ( ret == MM_ERROR_SUCCESS)
        setState(mComponents[mAudioSourceIndex].state, kComponentStatePrepared);
    else if ( ret == MM_ERROR_ASYNC)
        ret = waitUntilCondition(mComponents[mAudioSourceIndex].state,
                             kComponentStatePrepared, false/*pipeline state*/);

    if (ret != MM_ERROR_SUCCESS) {
        ERROR("source component prepare failed\n");
        return MM_ERROR_OP_FAILED;
    }

    // construct pipeline graph
    status = audioEncoder->addSource(audioSource.get(), Component::kMediaTypeAudio);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

    status = audioEncoder->addSink(muxer.get(), Component::kMediaTypeAudio);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

    status = muxer->addSink(fileSink.get(),
        Component::MediaType(Component::kMediaTypeAudio));
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);


    return status;
}

mm_status_t PipelineAudioRecorder::stopInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    RecordSourceComponent *component = NULL;

    if (mAudioSourceIndex == -1 || mComponents.empty() || mAudioSourceIndex >= mComponents.size()) {
        INFO("not prepared, source index %d, components %d\n", mAudioSourceIndex, mComponents.size());
        return MM_ERROR_SUCCESS;
    }

    component = DYNAMIC_CAST<RecordSourceComponent*>(mComponents[mAudioSourceIndex].component.get());
    if(!component){
        ERROR("DYNAMIC_CAST\n");
        return MM_ERROR_OP_FAILED;
    }
    component->signalEOS();

    status = waitUntilCondition(mEosReceived, true, false/*pipeline state*/);

    if (status != MM_ERROR_SUCCESS) {
        ERROR("stop failed, status %d\n", status);
        return MM_ERROR_OP_FAILED;
    }

    return status;
}

mm_status_t PipelineAudioRecorder::resetInternal()
{
    FUNC_TRACK();
    mAudioSourceIndex = -1;
    mSinkIndex = -1;

    mConnectedStreamCount = 0;
    mEOSStreamCount = 0;
    mEosReceived = false;
    mSinkClockIndex = -1;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineAudioRecorder::setParameter(const MediaMetaSP & meta)
{
    mm_status_t status = PipelineRecorderBase::setParameter(meta);
    if (status != MM_ERROR_SUCCESS) {
        return status;
    }

    for (int32_t i = 0; i < (int32_t)mComponents.size(); i++) {
        if (i == mAudioSourceIndex || i == mAudioCodecIndex) {
            status = mComponents[i].component->setParameter(mMediaMetaAudio);
        } else if (i == mMuxIndex || i == mSinkIndex) {
            status = mComponents[i].component->setParameter(mMediaMetaFile);
        }

        if (status != MM_ERROR_SUCCESS) {
            ERROR("set params failed, index %d", i);
            return status;
        }
    }
    return status;
}

mm_status_t PipelineAudioRecorder::getParameter(MediaMetaSP & meta)
{
    return PipelineRecorderBase::getParameter(meta);
}


} // YUNOS_MM
