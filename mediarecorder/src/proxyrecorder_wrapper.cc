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

#include <string.h>

#include <dbus/DAdaptor.h>

#include "multimedia/component.h"
#include <multimedia/mm_debug.h>
#include <MediaClientHelper.h>
#include <MediaRecorderClient.h>
#include "proxyrecorder_wrapper.h"


namespace YUNOS_MM {
#define FUNC_TRACK() FuncTracker tracker("ProxyRecorderWrapper", __FUNCTION__, __LINE__)
// #define FUNC_TRACK()
#define CHECK_INIT()                 \
    if (!mRecorder)                  \
        return MM_ERROR_NOT_INITED;

DEFINE_LOGTAG(ProxyRecorderWrapper)

ProxyRecorderWrapper::ProxyRecorderWrapper(const void * userDefinedData/* = NULL*/)
{
    FUNC_TRACK();
    String serviceName, objectPath, iface;

    bool result = MediaClientHelper::connect(
                      false,
                      "MediaRecorder",
                       serviceName,
                       objectPath,
                       iface);

    INFO("recorder service instance is connected with return value %d\n", result);

    if (!result) {
        mRecorder = NULL;
        MMLOGE("create recorder is rejected by media service\n");
        return;
    }

    mRecorder = MediaClientHelper::getMediaRecorderClient(serviceName, objectPath, iface);
    if ( !mRecorder ) {
        ERROR("failed to create recorder\n");
    }
}

ProxyRecorderWrapper::~ProxyRecorderWrapper()
{
    FUNC_TRACK();
    if ( mRecorder ) {
        mRecorder->reset();
        mRecorder->release();

        MediaClientHelper::disconnect("MediaRecorder", mRecorder);

        mRecorder->requestExitAndWait();
    }
    MM_RELEASE(mRecorder);
}

mm_status_t ProxyRecorderWrapper::setListener(Listener * listener)
{
    FUNC_TRACK();
    CHECK_INIT();

    mRecorder->setListener(listener);
    return MediaRecorder::setListener(listener);
}

mm_status_t ProxyRecorderWrapper::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setCamera(camera, recordingProxy);
}

mm_status_t ProxyRecorderWrapper::setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setVideoSourceUri(uri, headers);
}

mm_status_t ProxyRecorderWrapper::setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setAudioSourceUri(uri, headers);
}

mm_status_t ProxyRecorderWrapper::setVideoSourceFormat(int width, int height, uint32_t format)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setVideoSourceFormat(width, height, format);
}

mm_status_t ProxyRecorderWrapper::setVideoEncoder(const char* mime)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setVideoEncoder(mime);
}
mm_status_t ProxyRecorderWrapper::setAudioEncoder(const char* mime)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setAudioEncoder(mime);
}

mm_status_t ProxyRecorderWrapper::setOutputFormat(const char* mime)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setOutputFormat(mime);
}
mm_status_t ProxyRecorderWrapper::setOutputFile(const char* filePath)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setOutputFile(filePath);
}
mm_status_t ProxyRecorderWrapper::setOutputFile(int fd)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setOutputFile(fd);
}

mm_status_t ProxyRecorderWrapper::prepare()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->prepare();
}

mm_status_t ProxyRecorderWrapper::setRecorderUsage(RecorderUsage usage)
{
    FUNC_TRACK();
    CHECK_INIT();

    if (usage & RU_RecorderMask == RU_None) {
        ERROR("invalid usage 0x%0x", usage);
        return MM_ERROR_INVALID_PARAM;
    }
    mUsage = usage;
    DEBUG("usage %0x", mUsage);
    return mRecorder->setRecorderUsage(usage);
}

mm_status_t ProxyRecorderWrapper::getRecorderUsage(RecorderUsage &usage)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mUsage;
}

mm_status_t ProxyRecorderWrapper::setPreviewSurface(void * handle)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setPreviewSurface(handle);
}

mm_status_t ProxyRecorderWrapper::reset()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->reset();
}

mm_status_t ProxyRecorderWrapper::start()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->start();
}

mm_status_t ProxyRecorderWrapper::stop()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->stop();
}

mm_status_t ProxyRecorderWrapper::stopSync()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->stopSync();
}

mm_status_t ProxyRecorderWrapper::pause()
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->pause();
}

bool ProxyRecorderWrapper::isRecording() const
{
    FUNC_TRACK();
    if (!mRecorder)
        return false;
    return mRecorder->isRecording();
}

mm_status_t ProxyRecorderWrapper::getVideoSize(int *width, int * height) const
{
    FUNC_TRACK();
    CHECK_INIT();
    if (!width || !height)
        return MM_ERROR_INVALID_PARAM;

    if ((mUsage & RU_VideoRecorder) == 0) {
        ERROR("unsupported, usage 0x%0x", mUsage);
        return MM_ERROR_UNSUPPORTED;
    }
    return mRecorder->getVideoSize(width, height);
}

mm_status_t ProxyRecorderWrapper::getCurrentPosition(int64_t * msec) const
{
    FUNC_TRACK();
    CHECK_INIT();
    mm_status_t status = MM_ERROR_SUCCESS;
    if (!msec)
        return MM_ERROR_INVALID_PARAM;

    status = mRecorder->getCurrentPosition(msec);
    return status;
}

mm_status_t ProxyRecorderWrapper::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setParameter(meta);
}

mm_status_t ProxyRecorderWrapper::getParameter(MediaMetaSP & meta)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->getParameter(meta);
}

mm_status_t ProxyRecorderWrapper::invoke(const MMParam * request, MMParam * reply)
{
    FUNC_TRACK();
    CHECK_INIT();
    WARNING("%s %s isn't supported yet\n", __FILE__, __FUNCTION__);
    return MM_ERROR_UNSUPPORTED;
}

mm_status_t ProxyRecorderWrapper::setMaxDuration(int64_t msec)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setMaxDuration(msec);
}
mm_status_t ProxyRecorderWrapper::setMaxFileSize(int64_t bytes)
{
    FUNC_TRACK();
    CHECK_INIT();
    return mRecorder->setMaxFileSize(bytes);
}

}


