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
#include "pipeline_videorecorder.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("PLP-VPRV")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;

PipelineVideoRecorder::PipelineVideoRecorder()
    : mHasVideo(false)
    , mHasAudio(false)
{
    FUNC_TRACK();
}

PipelineVideoRecorder::~PipelineVideoRecorder()
{
    FUNC_TRACK();
    /*
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    listener->removeWatcher();
    */
}

mm_status_t PipelineVideoRecorder::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_UNKNOWN;
    mMediaMetaVideo->dump();
    mMediaMetaAudio->dump();
    mMediaMetaFile->dump();


    ComponentSP audioSource;
    ComponentSP videoSource;

    //check recording stream
    mHasVideo = (mUsage & RU_VideoRecorder) ? true : false;
    mHasAudio = (mUsage & RU_AudioRecorder) ? true : false;

    ASSERT(mComponents.size() == 0);

    bool isRtp = false;
    if ( strstr(mOutputFormat.c_str(), "rtp") ||
          strstr(mOutputFormat.c_str(), "RTP"))
          isRtp = true;

    if (mHasAudio) {
        audioSource = createComponentHelper(NULL,
            Pipeline::getSourceUri(mAudioSourceUri.c_str(), true).c_str(),
            mListenerReceive, false);
        ASSERT(audioSource);

        SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(audioSource.get());
        ASSERT(recorderSource);
        recorderSource->setUri(mAudioSourceUri.c_str());

        audioSource->setParameter(mMediaMetaAudio);
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        audioSource->setAudioConnectionId(mAudioConnectionId.c_str());
#endif

    }

    std::string videoSourceMime = Pipeline::getSourceUri(mVideoSourceUri.c_str(), false);
    if (mHasVideo) {
        videoSource = createComponentHelper(NULL, videoSourceMime.c_str(), mListenerReceive, false);
        ASSERT(videoSource);

        SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(videoSource.get());
        ASSERT(recorderSource);
        recorderSource->setUri(mVideoSourceUri.c_str());

        videoSource->setParameter(mMediaMetaVideo);
    }

    ComponentSP muxer, fileSink;
    const char *muxerName = isRtp ? MEDIA_MIMETYPE_MEDIA_RTP_MUXER : MEDIA_MIMETYPE_MEDIA_MUXER;
    muxer = createComponentHelper(NULL, muxerName, mListenerReceive, false);
    ASSERT(muxer);
    if ( !isRtp ) {
        fileSink = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_FILE_SINK, mListenerReceive, false);
        ASSERT(fileSink);
    }

    /*
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
        if (!isRtp) {
            mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));
            mSinkIndex = mComponents.size() - 1;
            mSinkClockIndex = mSinkIndex;
        }
        mConnectedStreamCount++;

    }


    ComponentSP audioEncoder;
    ComponentSP mediaFission, videoEncoder, videoSink;


    if (mHasAudio) {
        // create components, setup pipeline
        audioEncoder = createComponentHelper(NULL, mAudioEncoderMime.c_str(), mListenerReceive, true);
        ASSERT(audioEncoder);
        audioEncoder->setParameter(mMediaMetaAudio);

        DEBUG("audio encoder: %p, muxer: %p, fileSink: %p\n", audioEncoder.get(), muxer.get(), fileSink.get());
        if (!audioEncoder || !muxer || ((!fileSink)&&(!isRtp))) {
            return MM_ERROR_NO_COMPONENT;
        }


        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioSource, ComponentInfo::kComponentTypeSource));
            mAudioSourceIndex = mComponents.size()-1;
            mComponents.push_back(ComponentInfo(audioEncoder, ComponentInfo::kComponentTypeFilter));
            mAudioCodecIndex = mComponents.size()-1;
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

    }

    bool addCameraRecordPreview = mSurface && !strcmp(videoSourceMime.c_str(), MEDIA_MIMETYPE_MEDIA_CAMERA_SOURCE);
    if (mHasVideo) {
        // create components, setup pipeline
        if (addCameraRecordPreview) { // attach recording buffer to surface/BQProducer for preview
            mediaFission = createComponentHelper("MediaFission", "media/all", mListenerReceive);
            videoSink = createComponentHelper("VideoSinkSurface", "video/render", mListenerReceive);
            ASSERT(mediaFission && videoSink);
        }
        videoEncoder = createComponentHelper(NULL, mVideoEncoderMime.c_str(), mListenerReceive, true);
        ASSERT(videoEncoder);

        DEBUG("video encoder: %p, muxer: %p, fileSink: %p\n", videoEncoder.get(), muxer.get(), fileSink.get());
        if (!videoEncoder || !muxer || (!fileSink  && !isRtp) || (addCameraRecordPreview && (!mediaFission || !videoSink))) {
            return MM_ERROR_NO_COMPONENT;
        }

        status = videoEncoder->setParameter(mMediaMetaVideo);
        if (videoSink)
            status = videoSink->setParameter(mMediaMetaVideo);

        {
            MMAutoLock locker(mLock);
            if (mediaFission && videoSink) {
                mComponents.push_back(ComponentInfo(mediaFission, ComponentInfo::kComponentTypeFilter));
                mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
            }
            // mVideoSinkIndex = mComponents.size()-1;
            mComponents.push_back(ComponentInfo(videoSource, ComponentInfo::kComponentTypeSource));
            mVideoSourceIndex = mComponents.size()-1;
            mComponents.push_back(ComponentInfo(videoEncoder, ComponentInfo::kComponentTypeFilter));
            mVideoCodecIndex = mComponents.size() - 1;
        }

        // need to prepare source first
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


        // construct pipeline graph
        if (addCameraRecordPreview) {
            // videoSource-->mediaFission-->videoSink; videoSource-->mediaFission-->videoEncoder-->
            ASSERT(mediaFission && videoSink);
            status = mediaFission->addSource(videoSource.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

            status = videoEncoder->addSource(mediaFission.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            status = videoEncoder->addSink(muxer.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

            status = mediaFission->addSink(videoSink.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            mConnectedStreamCount ++;
        } else {
            status = videoEncoder->addSource(videoSource.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

            status = videoEncoder->addSink(muxer.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }
    }

    if (mHasAudio | mHasVideo) {
        if (!isRtp) {
            status = muxer->addSink(fileSink.get(),
                Component::MediaType(mHasVideo ? Component::kMediaTypeVideo : Component::kMediaTypeAudio));
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

            // setup components parameters
            status = muxer->setParameter(mMediaMetaFile);
            if (status != MM_ERROR_SUCCESS)
                return status;

            status = fileSink->setParameter(mMediaMetaFile);
            if (status != MM_ERROR_SUCCESS)
                return status;
        } else {
            status = muxer->setParameter(mMediaMetaFile);
            if (status != MM_ERROR_SUCCESS)
                return status;
        }
    }

    return status;
}

mm_status_t PipelineVideoRecorder::stopInternal()
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

mm_status_t PipelineVideoRecorder::resetInternal()
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

mm_status_t PipelineVideoRecorder::setParameter(const MediaMetaSP & meta)
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

mm_status_t PipelineVideoRecorder::getParameter(MediaMetaSP & meta)
{
    return PipelineRecorderBase::getParameter(meta);
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    YUNOS_MM::PipelineVideoRecorder *pipelineVideoRecorder = new YUNOS_MM::PipelineVideoRecorder();
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
