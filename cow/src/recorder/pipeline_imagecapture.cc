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
#include "pipeline_imagecapture.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("PLP-IMGC")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;
#define DEFAULT_FILE_PATH "/tmp/"
#define DEFAULT_OUTPUT_FORMAT "jpg"

PipelineImageCapture::PipelineImageCapture() :
    mCameraId(0)
{
    FUNC_TRACK();
    //mComponents.reserve(3);
    mMediaMeta->setString(MEDIA_ATTR_FILE_PATH, DEFAULT_FILE_PATH);
    mMediaMeta->setString(MEDIA_ATTR_OUTPUT_FORMAT, DEFAULT_OUTPUT_FORMAT);
}

PipelineImageCapture::~PipelineImageCapture()
{
    FUNC_TRACK();
    /*
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    listener->removeWatcher();
    */
}

mm_status_t PipelineImageCapture::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    ComponentSP source;

#if defined (__MM_NATIVE_BUILD__)
    source = createComponentHelper(NULL,
        Pipeline::getSourceUri(mVideoSourceUri.c_str(), false).c_str(),
        mListenerReceive, false);
#else
    //take ImageSourceCamera as priority
    source = createComponentHelper(NULL, MEDIA_MIMETYPE_IMAGE_CAMERA_SOURCE, Component::ListenerSP((Component::Listener*)NULL), false);
    ASSERT(source);
    SourceComponent *recorderSource = DYNAMIC_CAST<SourceComponent*>(source.get());
    ASSERT(recorderSource);
    recorderSource->setUri(mVideoSourceUri.c_str());
#endif

    mMediaMeta->dump();

    mComponents.push_back(ComponentInfo(source, ComponentInfo::kComponentTypeSource));
    mVideoSourceIndex = mComponents.size()-1;
    source->setListener(mListenerReceive);
    source->setParameter(mMediaMeta);

    ComponentSP fileSink = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_FILE_SINK, mListenerReceive, true);
    if (!fileSink) {
        return MM_ERROR_NO_COMPONENT;
    }

#if defined(__MM_NATIVE_BUILD__)
    // create components, setup pipeline
    ComponentSP imageEncoder = createComponentHelper(NULL, MEDIA_MIMETYPE_IMAGE_JPEG, mListenerReceive, true);
    if (!imageEncoder) {
        return MM_ERROR_NO_COMPONENT;
    }
#endif

#if defined(__MM_NATIVE_BUILD__)
    status = imageEncoder->addSource(source.get(), Component::kMediaTypeVideo);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
    status = imageEncoder->addSink(fileSink.get(), Component::kMediaTypeVideo);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

    imageEncoder->setParameter(mMediaMeta);
#else
    status = source->addSink(fileSink.get(), Component::kMediaTypeImage);
    ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);

#endif

    fileSink->setParameter(mMediaMeta);

    // add video filters if necessary
    {
        MMAutoLock locker(mLock);
#if defined(__MM_NATIVE_BUILD__)
        mComponents.push_back(ComponentInfo(imageEncoder, ComponentInfo::kComponentTypeFilter));
        mVideoCodecIndex = mComponents.size() - 1;
#endif

        mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));
        mSinkIndex = mComponents.size() - 1;
        //mSinkClockIndex = mComponents.size() - 1;
        mConnectedStreamCount++;
    }

    return status;
}

mm_status_t PipelineImageCapture::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    mm_status_t status = PipelineRecorderBase::setParameter(meta);
    if (status != MM_ERROR_SUCCESS) {
        ERROR();
        return status;
    }

    for (uint32_t i = 0; i < mComponents.size(); i++) {
        status = mComponents[i].component->setParameter(mMediaMeta);
        if (status != MM_ERROR_SUCCESS) {
            ERROR();
            return status;
        }
    }

    return status;
}

mm_status_t PipelineImageCapture::getParameter(MediaMetaSP & meta)
{
    return PipelineRecorderBase::getParameter(meta);
}

mm_status_t PipelineImageCapture::getCamera(void *&camera, void *&recordingProxy)
{
    camera = NULL;
    recordingProxy = NULL;

    Component* component = getSourceComponent(false);
    if (!component) {
        ERROR("component is NULL\n");
        return MM_ERROR_INVALID_PARAM;
    }

    MediaMetaSP meta;
    mm_status_t status = MM_ERROR_SUCCESS;
    status = component->getParameter(meta);
    if (status != MM_ERROR_SUCCESS)
        return MM_ERROR_INVALID_PARAM;

    meta->getPointer(MEDIA_ATTR_CAMERA_OBJECT, camera);
    meta->getPointer(MEDIA_ATTR_RECORDING_PROXY_OBJECT, recordingProxy);
    DEBUG("camera %p, recordingProxy %p\n", camera, recordingProxy);
#if __MM_YUNOS_CNTRHAL_BUILD__
    if (!camera || !recordingProxy) {
        return MM_ERROR_INVALID_PARAM;
    }
#endif
    return MM_ERROR_SUCCESS;
}

mm_status_t PipelineImageCapture::notify(int msg, int param1, int param2, const MediaMetaSP obj)
{
    if (msg == Component::kEventInfo && (param1 >=Component::kEventInfoDiscontinuety && param1 <= Component::kEventInfoMediaRenderStarted))
        INFO("sending kEventInfo param1=%s, param2=%d to client app\n", Component::sEventInfoStr[param1], param2);
    else if (msg >= 0 && msg < Component::kEventMax)
        INFO("sending event %s with params (%d, %d) to app client\n", Component::sEventStr[msg], param1, param2);
    else
        WARNING("sending event=%d, param1=%d, param2=%d to client app\n", msg, param1, param2);

    if ( !mListenerSend ) {
        ERROR("no registered listener of Pipeline\n");
        return MM_ERROR_NOT_INITED;
    }

    if (msg == Component::kEventImageCapture) {
        //skip messages except kEventImageCapture
        mListenerSend->onMessage(msg, param1, param2, obj);
    }

    return MM_ERROR_SUCCESS;
}


} // YUNOS_MM
