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
#include "pipeline_ar_recorder.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("PipelineArRecorder")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;

PipelineArRecorder::PipelineArRecorder()
    : mHasVideo(false)
    , mHasAudio(false)
{
    FUNC_TRACK();
}

PipelineArRecorder::~PipelineArRecorder()
{
    FUNC_TRACK();
    /*
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    listener->removeWatcher();
    */
}

mm_status_t PipelineArRecorder::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;
    ComponentSP currSource, muxer, fileSink;

    mHasVideo = true;
    mMediaMetaVideo->dump();
    mMediaMetaAudio->dump();
    mMediaMetaFile->dump();
    ASSERT(mComponents.size() == 0);

    // #### 1
    // add example filter components
    ComponentFactory::appendPluginsXml("/etc/cow_plugins_example.xml");

    // #### 2 preview from recording stream: videoSource --> filter -->mediaFission-->videoSink
    if (mHasVideo) {
        ComponentSP videoSource, filter, mediaFission, videoSink;
        // create camera source
        std::string videoSourceMime = Pipeline::getSourceUri(mVideoSourceUri.c_str(), false);
        videoSource = createComponentHelper(NULL, videoSourceMime.c_str(), mListenerReceive, false);
        ASSERT(videoSource);
        if (!videoSource)
            return MM_ERROR_NO_COMPONENT;
        status = videoSource->setParameter(mMediaMetaVideo);
        SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(videoSource.get());
        ASSERT(recorderSource);
        status = recorderSource->setUri(mVideoSourceUri.c_str());

        // create video filter
        const char* filterName = "video/filter-example";
        std::string tmpFilterName = mm_get_env_str("mm.recorder.video.filter", "MM_RECORDER_VIDEO_FILTER");
        if (!tmpFilterName.empty())
            filterName = tmpFilterName.c_str();
        filter = createComponentHelper(NULL, filterName, mListenerReceive, false);
        ASSERT(filter);
        if (!filter)
            return MM_ERROR_NO_COMPONENT;

        // preview (display video stream)
        if ( mSurface && !strcmp(videoSourceMime.c_str(), MEDIA_MIMETYPE_MEDIA_CAMERA_SOURCE)) {
            // preview camera stream when surface is set, attach recording buffer to surface/BQProducer for preview
            mediaFission = createComponentHelper("MediaFission", "media/all", mListenerReceive);
            videoSink = createComponentHelper("VideoSinkSurface", "video/render", mListenerReceive);
            ASSERT(mediaFission && videoSink);
            if (!mediaFission || !videoSink)
                return MM_ERROR_NO_COMPONENT;
            status = videoSink->setParameter(mMediaMetaVideo);
        }

        // add components to pipeline
        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoSource, ComponentInfo::kComponentTypeSource));
            mVideoSourceIndex = mComponents.size()-1;
            if (filter)
                mComponents.push_back(ComponentInfo(filter, ComponentInfo::kComponentTypeFilter));
            if (mediaFission)
                mComponents.push_back(ComponentInfo(mediaFission, ComponentInfo::kComponentTypeFilter));
            if (videoSink)
                mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
            // mVideoSinkIndex = mComponents.size()-1;
        }

        // prepare video source first
        setState(mComponents[mVideoSourceIndex].state, kComponentStatePreparing);
        mm_status_t ret = videoSource->prepare();
        if ( ret == MM_ERROR_SUCCESS)
            setState(mComponents[mVideoSourceIndex].state, kComponentStatePrepared);
        else if ( ret == MM_ERROR_ASYNC)
            ret = waitUntilCondition(mComponents[mVideoSourceIndex].state,
                                 kComponentStatePrepared, false/*pipeline state*/);
        if (ret != MM_ERROR_SUCCESS) {
            ERROR("source component prepare failed\n");
            return MM_ERROR_OP_FAILED;
        }

        // construct preview pipeline graph: videoSource-->filter-->mediaFission-->videoSink
        currSource = videoSource;
        if (filter) {
            status = filter->addSource(currSource.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            currSource = filter;
        }

        if (mediaFission && videoSink) {
            status = mediaFission->addSource(currSource.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            currSource = mediaFission;
            status = mediaFission->addSink(videoSink.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            mConnectedStreamCount ++;
        }
    }

    // #### return success if preview only
    bool simplePreview = false;
    simplePreview = mm_check_env_str("mm.ar.preview.only", "MM_AR_PREVIEW_ONLY", "1", simplePreview);
    if (simplePreview)
        return status;

    // #### 3. create components to save compressed strea
    // create muxer and filesink
    if ( strstr(mOutputFormat.c_str(), "rtp") ||strstr(mOutputFormat.c_str(), "RTP")) {
        muxer = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_RTP_MUXER, mListenerReceive, false);
        ASSERT(muxer);
    } else {
        muxer = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_MUXER, mListenerReceive, false);
        fileSink = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_FILE_SINK, mListenerReceive, false);
        ASSERT(muxer && fileSink);
        if (!fileSink)
            return MM_ERROR_NO_COMPONENT;
        status = fileSink->setParameter(mMediaMetaFile);
    }

    if (!muxer) {
        return MM_ERROR_NO_COMPONENT;
    }
    status = muxer->setParameter(mMediaMetaFile);

    /* FIXME:
     * Sink and Source compoents should be ahead of codec component in vector.
     * If codec component changes to prepared/started state first,
     * codec will pull source data from source component right now,
     * but source component have not change to prepared/started state, which may cause to read failed/error.
     * it's the same to sink component.
     */
    {
        MMAutoLock locker(mLock);
        mComponents.push_back(ComponentInfo(muxer, ComponentInfo::kComponentTypeFilter));
        mMuxIndex = mComponents.size() - 1;
        if (fileSink) {
            mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));
            mSinkIndex = mComponents.size() - 1;
            mSinkClockIndex = mSinkIndex;
        }
    }

    // #### 4. construct video encode pipeline: currSource-->videoEncoder-->muxer-->fileSink
    if (mHasVideo) {
        ComponentSP videoEncoder;
        // create video encoder
        videoEncoder = createComponentHelper(NULL, mVideoEncoderMime.c_str(), mListenerReceive, true);
        ASSERT(videoEncoder);
        if (!videoEncoder) {
            return MM_ERROR_NO_COMPONENT;
        }
        status = videoEncoder->setParameter(mMediaMetaVideo);
        DEBUG("video encoder: %p, muxer: %p, fileSink: %p\n", videoEncoder.get(), muxer.get(), fileSink.get());

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoEncoder, ComponentInfo::kComponentTypeFilter));
            mVideoCodecIndex = mComponents.size() - 1;
            mConnectedStreamCount++;
        }

        // construct recording pipeline
        status = videoEncoder->addSource(currSource.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoEncoder->addSink(muxer.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (fileSink) {
            status = muxer->addSink(fileSink.get(), Component::MediaType(mHasVideo ? Component::kMediaTypeVideo : Component::kMediaTypeAudio));
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }
    }

    // #### 5. setup audio recorder pipeline if required
    if (mUsage & RU_AudioRecorder) {
        mHasAudio = true;
        ComponentSP audioSource, audioEncoder;
        // create audio source
        audioSource = createComponentHelper(NULL, Pipeline::getSourceUri(mAudioSourceUri.c_str(), true).c_str(), mListenerReceive, false);
        ASSERT(audioSource);
            SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(audioSource.get());
        ASSERT(recorderSource);
        recorderSource->setUri(mAudioSourceUri.c_str());
        audioSource->setParameter(mMediaMetaAudio);

        // create audio encoder
        audioEncoder = createComponentHelper(NULL, mAudioEncoderMime.c_str(), mListenerReceive, true);
        ASSERT(audioEncoder);
        audioEncoder->setParameter(mMediaMetaAudio);

        DEBUG("audio encoder: %p, muxer: %p, fileSink: %p\n", audioEncoder.get(), muxer.get(), fileSink.get());
        if (!audioSource || !audioEncoder) {
            return MM_ERROR_NO_COMPONENT;
        }


        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioSource, ComponentInfo::kComponentTypeSource));
            mAudioSourceIndex = mComponents.size()-1;
            mComponents.push_back(ComponentInfo(audioEncoder, ComponentInfo::kComponentTypeFilter));
            mAudioCodecIndex = mComponents.size()-1;
        }

        // prepare source first
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

        // construct audio pipeline graph
        status = audioEncoder->addSource(audioSource.get(), Component::kMediaTypeAudio);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = audioEncoder->addSink(muxer.get(), Component::kMediaTypeAudio);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
    }

    return status;
}

mm_status_t PipelineArRecorder::stopInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    //if audio exists
    RecordSourceComponent *component = NULL;
    if (mHasAudio) {
        component = DYNAMIC_CAST<RecordSourceComponent*>(mComponents[mAudioSourceIndex].component.get());
        if(!component){
            ERROR("audio component DYNAMIC_CAST fail\n");
            status = MM_ERROR_NO_COMPONENT;
        }
        else{
            component->signalEOS();
        }
    }

    //if video exists
    if (mHasVideo) {
        component = DYNAMIC_CAST<RecordSourceComponent*>(mComponents[mVideoSourceIndex].component.get());
        if(!component){
            ERROR("video component DYNAMIC_CAST fail\n");
            status = MM_ERROR_NO_COMPONENT;
        }
        else{
            component->signalEOS();
        }
    }

    if (!mErrorCode) {
        status = waitUntilCondition(mEosReceived, true, false/*pipeline state*/);
    } else {
        ERROR("mErrorCode %d", mErrorCode);
        return MM_ERROR_UNKNOWN;
    }

    if (status != MM_ERROR_SUCCESS) {
        ERROR("stop failed, status %d\n", status);
        return MM_ERROR_OP_FAILED;
    }

    return status;
}

mm_status_t PipelineArRecorder::resetInternal()
{
    FUNC_TRACK();
    mAudioSourceIndex = -1;
    mVideoSourceIndex = -1;
    mSinkIndex = -1;
    mHasVideo = false;
    mHasAudio = false;
    mConnectedStreamCount = 0;
    mEOSStreamCount = 0;
    mEosReceived = false;
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineArRecorder::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    void* ptr = NULL;
    if (meta->getPointer(MEDIA_ATTR_CAMERA_OBJECT, ptr)) {
        mMediaMetaVideo->setPointer(MEDIA_ATTR_CAMERA_OBJECT, ptr);
        DEBUG("camera %p\n", ptr);
    }
    if (meta->getPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, ptr)) {
        mMediaMetaVideo->setPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, ptr);

        DEBUG("recordingProxy %p\n", ptr);
    }

    mm_status_t status = PipelineRecorderBase::setParameter(meta);
    if (status != MM_ERROR_SUCCESS) {
        ERROR();
        return status;
    }

    for (int32_t i = 0; i < (int32_t)mComponents.size(); i++) {
        if (i == mAudioSourceIndex || i == mAudioCodecIndex) {
            status = mComponents[i].component->setParameter(mMediaMetaAudio);
        } else if (i == mVideoSourceIndex || i == mVideoCodecIndex) {
            status = mComponents[i].component->setParameter(mMediaMetaVideo);
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

mm_status_t PipelineArRecorder::getParameter(MediaMetaSP & meta)
{
    return PipelineRecorderBase::getParameter(meta);
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    YUNOS_MM::PipelineArRecorder *pipelineVideoRecorder = new YUNOS_MM::PipelineArRecorder();
    if (pipelineVideoRecorder == NULL) {
        return NULL;
    }
    if (pipelineVideoRecorder->init() != MM_ERROR_SUCCESS) {
        delete pipelineVideoRecorder;
        pipelineVideoRecorder = NULL;
        return NULL;
    }
    return static_cast<YUNOS_MM::Pipeline*>(pipelineVideoRecorder);
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
