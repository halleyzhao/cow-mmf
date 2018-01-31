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
#include "pipeline_rtsprecorder.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
DEFINE_LOGTAG(PipelineRTSPRecorder)
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;

PipelineRTSPRecorder::PipelineRTSPRecorder()
    : mHasVideo(false)
    , mHasAudio(false)
{
    MMLOGV("+\n");
    MMLOGV("-\n");
}

PipelineRTSPRecorder::~PipelineRTSPRecorder()
{
    MMLOGV("+\n");
    MMLOGV("-\n");
}

mm_status_t PipelineRTSPRecorder::prepareInternal()
{
    MMLOGV("+\n");
    ASSERT(mComponents.size() == 0);
    mm_status_t status = MM_ERROR_UNKNOWN;

    MMLOGV("Creating muxer\n");
    ComponentSP muxer = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_MUXER, mListenerReceive, false);
    if (muxer == NULL) {
        MMLOGE("failed to create muxer\n");
        return MM_ERROR_OP_FAILED;
    }
    MMLOGV("set file meta to muxer\n");
    muxer->setParameter(mMediaMetaFile);

    MMLOGV("Creating filesink\n");
    ComponentSP fileSink = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_FILE_SINK, mListenerReceive, false);
    if (fileSink == NULL) {
        MMLOGE("failed to create fileSink\n");
        return MM_ERROR_OP_FAILED;
    }
    MMLOGV("set file meta to fileSink\n");
    fileSink->setParameter(mMediaMetaFile);
    {
        MMAutoLock locker(mLock);
        mComponents.push_back(ComponentInfo(muxer, ComponentInfo::kComponentTypeFilter));
        mMuxIndex = mComponents.size() - 1;

        mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));
        mSinkIndex = mComponents.size() - 1;
        mSinkClockIndex = mSinkIndex;
        mConnectedStreamCount++;
    }

    ComponentSP audioSource;
    ComponentSP videoSource;

    MMLOGD("usage: 0x%x, %s, %s\n", mUsage, mAudioSourceUri.c_str(), mVideoSourceUri.c_str());
    mHasVideo = (mUsage & RU_VideoRecorder) ? true : false;
    mHasAudio = (mUsage & RU_AudioRecorder) ? true : false;

    MMLOGV("Add file sink to muxer sink\n");
    status = muxer->addSink(fileSink.get(), mHasVideo ? Component::kMediaTypeVideo : Component::kMediaTypeAudio);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

    bool audioRTSPSource = mAudioSourceUri != "" && !strncmp(mAudioSourceUri.c_str(), "rtsp://", 7);
    bool videoRTSPSource = mVideoSourceUri != "" && !strncmp(mVideoSourceUri.c_str(), "rtsp://", 7);

    if (mHasAudio) {
        MMLOGV("creating audio source\n");
        audioSource = createComponentHelper(NULL,
            Pipeline::getSourceUri(mAudioSourceUri.c_str(), true).c_str(),
            mListenerReceive, false);
        if (!audioSource) {
            MMLOGE("failed to create audio source\n");
            return MM_ERROR_OP_FAILED;
        }

        SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(audioSource.get());
        ASSERT(recorderSource);
        recorderSource->setUri(mAudioSourceUri.c_str());

        audioSource->setParameter(mMediaMetaAudio);
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
        if (!audioRTSPSource) {
            MMLOGI("not rtsp source, set connection id: %s\n", mAudioConnectionId.c_str());
            audioSource->setAudioConnectionId(mAudioConnectionId.c_str());
        }
#endif

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioSource, ComponentInfo::kComponentTypeSource));
            mAudioSourceIndex = mComponents.size()-1;
        }

        MMLOGV("Preparing audio source\n");
        mm_status_t ret = audioSource->prepare();
        if (ret == MM_ERROR_ASYNC) {
            ret = waitUntilCondition(mComponents[mAudioSourceIndex].state,
                                 kComponentStatePrepared, false/*pipeline state*/);
        }
        if (ret != MM_ERROR_SUCCESS) {
            ERROR("source component prepare failed\n");
            return MM_ERROR_OP_FAILED;
        }
        setState(mComponents[mAudioSourceIndex].state, kComponentStatePrepared);

        if (audioRTSPSource) {
            MMLOGD("rtsp audio, prepare and add sink\n");
            status = audioSource->addSink(muxer.get(), Component::kMediaTypeAudio);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        } else {
            MMLOGD("creating audio encoder\n");
            ComponentSP audioEncoder;
            audioEncoder = createComponentHelper(NULL, mAudioEncoderMime.c_str(), mListenerReceive, true);
            ASSERT(audioEncoder);
            audioEncoder->setParameter(mMediaMetaAudio);
            {
                MMAutoLock locker(mLock);
                mComponents.push_back(ComponentInfo(audioEncoder, ComponentInfo::kComponentTypeFilter));
                mAudioCodecIndex = mComponents.size()-1;
            }
            MMLOGD("connecting audio encoder\n");
            status = audioEncoder->addSource(audioSource.get(), Component::kMediaTypeAudio);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            status = audioEncoder->addSink(muxer.get(), Component::kMediaTypeAudio);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }

    }

    if (mHasVideo) {
        if (mAudioSourceUri == mVideoSourceUri && audioSource != NULL) {
            MMLOGI("video source is the same as audio, and already created\n");
            videoSource = audioSource;
        } else {
            MMLOGV("creating video source\n");
            videoSource = createComponentHelper(NULL, Pipeline::getSourceUri(mVideoSourceUri.c_str(), false).c_str(), mListenerReceive, false);
            if (!videoSource) {
                MMLOGE("failed to create video source\n");
                return MM_ERROR_OP_FAILED;
            }

            SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(videoSource.get());
            ASSERT(recorderSource);
            recorderSource->setUri(mVideoSourceUri.c_str());

            videoSource->setParameter(mMediaMetaVideo);

        }
        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoSource, ComponentInfo::kComponentTypeSource));
            mVideoSourceIndex = mComponents.size()-1;
        }

        if (videoRTSPSource) {
            setState(mComponents[mVideoSourceIndex].state, kComponentStatePreparing);
            mm_status_t ret = videoSource->prepare();
            if ( ret == MM_ERROR_ASYNC)
                ret = waitUntilCondition(mComponents[mVideoSourceIndex].state,
                                     kComponentStatePrepared, false/*pipeline state*/);

            if (ret != MM_ERROR_SUCCESS) {
                ERROR("source component prepare failed\n");
                return MM_ERROR_OP_FAILED;
            }

            setState(mComponents[mVideoSourceIndex].state, kComponentStatePrepared);

            bool preview = true;
            if (preview) {
                MMLOGV("has preview\n");
                ComponentSP mediaFission = createComponentHelper("MediaFission", "media/all", mListenerReceive);
                if (!mediaFission) {
                    MMLOGV("Failed to create mediafission\n");
                    return MM_ERROR_OP_FAILED;
                }
                ComponentSP videoSink = createComponentHelper(NULL, MEDIA_MIMETYPE_VIDEO_RENDER);
                if (!videoSink) {
                    MMLOGV("Failed to create videoSink\n");
                    return MM_ERROR_OP_FAILED;
                }
                MMLOGV("Add fission to video source sink\n");
                status = videoSource->addSink(mediaFission.get(), Component::kMediaTypeVideo);
                ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

                SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(videoSource.get());
                ASSERT(recorderSource);
                std::string videoMime;
                if (getMediaMime(recorderSource, Component::kMediaTypeVideo, videoMime)
                    != MM_ERROR_SUCCESS) {
                    MMLOGV("Failed to get video mime\n");
                    return MM_ERROR_OP_FAILED;
                }
                ComponentSP videoDecoder = createComponentHelper(NULL, videoMime.c_str());
                if (!videoDecoder) {
                    MMLOGV("Failed to create videoDecoder \n");
                    return MM_ERROR_OP_FAILED;
                }

                MMLOGV("Add fission to video decoder source\n");
                status = videoDecoder->addSource(mediaFission.get(), Component::kMediaTypeVideo);
                ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

                MMLOGV("Add video sink to video decoder sink\n");
                status = videoDecoder->addSink(videoSink.get(), Component::kMediaTypeVideo);
                ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
                MMLOGV("Add muxer to fission sink\n");
                status = mediaFission->addSink(muxer.get(), Component::kMediaTypeVideo);
                ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
                {
                    MMAutoLock locker(mLock);
                    mComponents.push_back(ComponentInfo(videoDecoder, ComponentInfo::kComponentTypeFilter));
                    mComponents.push_back(ComponentInfo(mediaFission, ComponentInfo::kComponentTypeFilter));
                    mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
                }
            } else {
                MMLOGV("Add muxer to video source sink\n");
                status = videoSource->addSink(muxer.get(), Component::kMediaTypeVideo);
                ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            }
        } else {
            // not implemented
            MMASSERT(0);
        }

    }

    MMLOGV("-\n");
    return status;
}


mm_status_t PipelineRTSPRecorder::stopInternal()
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

mm_status_t PipelineRTSPRecorder::resetInternal()
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

mm_status_t PipelineRTSPRecorder::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    mm_status_t status = PipelineRecorderBase::setParameter(meta);
    if (status != MM_ERROR_SUCCESS) {
        MMLOGE("Failed to setparam by base\n");
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

mm_status_t PipelineRTSPRecorder::getParameter(MediaMetaSP & meta)
{
    return PipelineRecorderBase::getParameter(meta);
}

mm_status_t PipelineRTSPRecorder::getMediaMime(SourceComponent * component,
        Component::MediaType mediaType,
        std::string & mime)
{
    MMASSERT(component != NULL);
    int32_t selectedTrack = component->getSelectedTrack(mediaType);
    if (selectedTrack < 0) {
        MMLOGE("no tracks slected\n");
        return MM_ERROR_INVALID_PARAM;
    }
    MMLOGV("selectedTrack: %d\n", selectedTrack);

    MMParamSP ti = component->getTrackInfo();
    if (!ti) {
        MMLOGE("no tracks\n");
        return MM_ERROR_INVALID_PARAM;
    }

    int32_t streamCount = ti->readInt32();
    MMLOGI("streamCount: %d\n", streamCount);
    for ( int32_t i = 0; i < streamCount; ++i ) {
        int32_t trackType = ti->readInt32();
        int32_t trackCount = ti->readInt32();
        MMLOGI("\ttrackType: %d, count: %d\n", trackType, trackCount);
        for ( int32_t j = 0; j < trackCount; ++j ) {
            int32_t id = ti->readInt32();
            int32_t codecId = ti->readInt32();
            const char * codecName = ti->readCString();
            const char * _mime = ti->readCString();
            const char * title = ti->readCString();
            const char * lang = ti->readCString();
            MMLOGI("\t\tid: %d, codecId: %d, codecName: %s, mime: %s, title: %s, lang: %s\n",
                id, codecId, codecName, _mime, title, lang);
            if ((int)mediaType == trackType && j == selectedTrack) {
                mime = _mime;
                goto success;
            }
        }
    }
    MMLOGE("media: %d, mime: not found\n", mediaType);
    return MM_ERROR_INVALID_PARAM;

success:
    MMLOGI("media: %d, mime: %s\n", mediaType, mime.c_str());
    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM

/*
/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    try {
        YUNOS_MM::PipelineRTSPRecorder * r = new YUNOS_MM::PipelineRTSPRecorder();
        if (r->init() != MM_ERROR_SUCCESS) {
            delete r;
            return NULL;
        }
        return r;
    } catch (...) {
        return NULL;
    }
}

void releasePipeline(YUNOS_MM::Pipeline *pipeline)
{
    if (pipeline) {
        pipeline->uninit();
        delete pipeline;
    }
}
}
*/
