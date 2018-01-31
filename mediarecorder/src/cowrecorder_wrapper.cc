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

#include "cowrecorder_wrapper.h"
#include "multimedia/component.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {
#define FUNC_TRACK() FuncTracker tracker("CowRecorderWrapper", __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

DEFINE_LOGTAG(CowRecorderWrapper)
DEFINE_LOGTAG(CowRecorderWrapper::CowListener)

CowRecorderWrapper::CowRecorderWrapper(RecorderType type, const void * userDefinedData/* = NULL*/) : mCowListener(NULL)
{
    FUNC_TRACK();
    mRecorder = new CowRecorder(type);
    if ( !mRecorder ) {
        ERROR("failed to create recorder\n");
    }
}

CowRecorderWrapper::~CowRecorderWrapper()
{
    FUNC_TRACK();
    if ( mRecorder ) {
        mRecorder->reset();
    }
    MM_RELEASE(mRecorder);
    MM_RELEASE(mCowListener);
}

mm_status_t CowRecorderWrapper::setPipeline(PipelineSP pipeline)
{
    FUNC_TRACK();
    return mRecorder->setPipeline(pipeline);
}

mm_status_t CowRecorderWrapper::setListener(Listener * listener)
{
    FUNC_TRACK();
    if ( !mCowListener ) {
        mCowListener = new CowListener(this);
        if ( !mCowListener ) {
            ERROR("no mem\n");
            return MM_ERROR_NO_MEM;
        }
        mRecorder->setListener(mCowListener);
    } else {
        MMLOGW("already set\n");
    }
    return MediaRecorder::setListener(listener);
}

mm_status_t CowRecorderWrapper::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    FUNC_TRACK();
    return mRecorder->setCamera(camera, recordingProxy);
}

mm_status_t CowRecorderWrapper::setVideoSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    return mRecorder->setVideoSourceUri(uri, headers);
}

mm_status_t CowRecorderWrapper::setAudioSourceUri(const char * uri, const std::map<std::string, std::string> * headers)
{
    FUNC_TRACK();
    return mRecorder->setAudioSourceUri(uri, headers);
}

mm_status_t CowRecorderWrapper::setVideoSourceFormat(int width, int height, uint32_t format)
{
    FUNC_TRACK();
    return mRecorder->setVideoSourceFormat(width, height, format);
}

mm_status_t CowRecorderWrapper::setVideoEncoder(const char* mime)
{
    FUNC_TRACK();
    return mRecorder->setVideoEncoder(mime);
}
mm_status_t CowRecorderWrapper::setAudioEncoder(const char* mime)
{
    FUNC_TRACK();
    return mRecorder->setAudioEncoder(mime);
}

mm_status_t CowRecorderWrapper::setOutputFormat(const char* mime)
{
    FUNC_TRACK();
    return mRecorder->setOutputFormat(mime);
}
mm_status_t CowRecorderWrapper::setOutputFile(const char* filePath)
{
    FUNC_TRACK();
    return mRecorder->setOutputFile(filePath);
}
mm_status_t CowRecorderWrapper::setOutputFile(int fd)
{
    FUNC_TRACK();
    return mRecorder->setOutputFile(fd);
}

mm_status_t CowRecorderWrapper::prepare()
{
    FUNC_TRACK();
    return mRecorder->prepare();
}

mm_status_t CowRecorderWrapper::prepareAsync()
{
    FUNC_TRACK();
    return mRecorder->prepareAsync();
}

mm_status_t CowRecorderWrapper::setRecorderUsage(RecorderUsage usage)
{
    FUNC_TRACK();
    return mRecorder->setRecorderUsage(usage);
}

mm_status_t CowRecorderWrapper::getRecorderUsage(RecorderUsage &usage)
{
    FUNC_TRACK();
    return mRecorder->getRecorderUsage(usage);
}

mm_status_t CowRecorderWrapper::setPreviewSurface(void * handle)
{
    FUNC_TRACK();
    return mRecorder->setPreviewSurface(handle);
}

mm_status_t CowRecorderWrapper::reset()
{
    FUNC_TRACK();
    return mRecorder->reset();
}

mm_status_t CowRecorderWrapper::start()
{
    FUNC_TRACK();
    return mRecorder->start();
}

mm_status_t CowRecorderWrapper::stop()
{
    FUNC_TRACK();
    return mRecorder->stop();
}

mm_status_t CowRecorderWrapper::stopSync()
{
    FUNC_TRACK();
    return mRecorder->stopSync();
}


mm_status_t CowRecorderWrapper::pause()
{
    FUNC_TRACK();
    return mRecorder->pause();
}

bool CowRecorderWrapper::isRecording() const
{
    FUNC_TRACK();
    return mRecorder->isRecording();
}

mm_status_t CowRecorderWrapper::getVideoSize(int *width, int * height) const
{
    FUNC_TRACK();
    if (!width || !height)
        return MM_ERROR_INVALID_PARAM;

    return mRecorder->getVideoSize(*width, *height);
}

mm_status_t CowRecorderWrapper::getCurrentPosition(int64_t * msec) const
{
    //FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    if (!msec)
        return MM_ERROR_INVALID_PARAM;

    status = mRecorder->getCurrentPosition(*msec);
    return status;
}

mm_status_t CowRecorderWrapper::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();
    return mRecorder->setParameter(meta);
}

mm_status_t CowRecorderWrapper::getParameter(MediaMetaSP & meta)
{
    FUNC_TRACK();
    return mRecorder->getParameter(meta);
}

mm_status_t CowRecorderWrapper::invoke(const MMParam * request, MMParam * reply)
{
    FUNC_TRACK();
    WARNING("%s %s isn't supported yet\n", __FILE__, __FUNCTION__);
    return MM_ERROR_UNSUPPORTED;
}

mm_status_t CowRecorderWrapper::setMaxDuration(int64_t msec)
{
    FUNC_TRACK();
    return mRecorder->setMaxDuration(msec);
}
mm_status_t CowRecorderWrapper::setMaxFileSize(int64_t bytes)
{
    FUNC_TRACK();
    return mRecorder->setMaxFileSize(bytes);
}

CowRecorderWrapper::CowListener::CowListener(CowRecorderWrapper * watcher)
                        : mWatcher(watcher)
{
    MMASSERT(watcher != NULL);
}

CowRecorderWrapper::CowListener::~CowListener()
{
}

void CowRecorderWrapper::CowListener::onMessage(int eventType, int param1, int param2, const MMParamSP meta)
{
    MMParam param;

    MMLOGD("eventType: %d\n", eventType);
    switch ( eventType ) {
        case Component::kEventPrepareResult:
            mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_PREPARED, param1, 0, NULL);
            break;
        case Component::kEventEOS:
            mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_RECORDER_COMPLETE, 0, 0, NULL);
            break;
        case Component::kEventStartResult:
            mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_STARTED, param1, 0, NULL);
            break;
        case Component::kEventPaused:
            mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_PAUSED, param1, 0, NULL);
            break;
        case Component::kEventStopped:
            mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_STOPPED, param1, 0, NULL);
            break;
        case Component::kEventError:
            {
                mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_ERROR, param1, 0, NULL);
            }
            break;
        case Component::kEventMediaInfo:
            {
                // FIXME
                // int i = convertCowInfoCode(getInfoType());
                int i = -1;
                mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_INFO, i, 0, NULL);
            }
            break;
        case Component::kEventInfo:
            switch (param1)
            {
                case Component::kEventMaxFileSizeReached:
                    {
                        INFO("try to send MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED msg\n");
                        mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_INFO, 801/* MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED */, 0, NULL);
                    }
                break;
                case Component::kEventMaxDurationReached:
                    {
                        INFO("try to send MEDIA_RECORDER_INFO_MAX_DURATION_REACHED msg\n");
                        mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_INFO, 800/* MEDIA_RECORDER_INFO_MAX_DURATION_REACHED */, 0, NULL);
                    }
                break;
                default:
                    mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_INFO, param1, 0, NULL);
                break;
            }
            break;
        case Component::kEventMusicSpectrum:
            {
                mWatcher->mListener->onMessage(MediaRecorder::Listener::MSG_MUSIC_SPECTRUM, param1, 0, NULL);
            }
            break;
        default:
            ERROR("unrecognized message: %d\n", eventType);
    }
}

mm_status_t CowRecorderWrapper::setAudioConnectionId(const char * connectionId)
{
    FUNC_TRACK();
    return mRecorder->setAudioConnectionId(connectionId);
}

const char * CowRecorderWrapper::getAudioConnectionId() const
{
    FUNC_TRACK();
    return mRecorder->getAudioConnectionId();
}


}


