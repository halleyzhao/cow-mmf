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

#include <multimedia/mm_debug.h>
#include <multimedia/media_meta.h>
#include <multimedia/mmparam.h>

#include <MediaRecorderProxy.h>

#include <dbus/DMessage.h>
#include <dbus/DService.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaRecorderProxy)

MediaRecorderProxy::MediaRecorderProxy(const SharedPtr<DService>& service,
                                     const String &path,
                                     const String &iface)
    : MediaProxy(service, path, iface) {

}

MediaRecorderProxy::~MediaRecorderProxy() {
}

mm_status_t MediaRecorderProxy::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setCamera"));
    CHECK_MSG(msg)

    msg->writeInt64((int64_t)camera);
    msg->writeInt64((int64_t)recordingProxy);

    MM_SEND_MSG(msg)
    MM_HANDLE_REPLY();
}

mm_status_t MediaRecorderProxy::setVideoSourceUri(const char * uri,
                const std::map<std::string, std::string> * headers) {

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setVideoSourceUri"));
    CHECK_MSG(msg)

    msg->writeString(String(uri));

    if (headers) {
        size_t count = headers->size();
        if (count > 1000) {
            WARNING("headers is a big map");
            count = 1000;
        }
        msg->writeInt32(count);
        std::map<std::string, std::string>::const_iterator it = (*headers).begin();
        for (; it != (*headers).end(); it++) {
            msg->writeString(String(it->first.c_str()));
            msg->writeString(String(it->second.c_str()));
        }
    } else {
        msg->writeInt32(0);
    }

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
}

mm_status_t MediaRecorderProxy::setAudioSourceUri(const char * uri,
                const std::map<std::string, std::string> * headers) {

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setAudioSourceUri"));
    CHECK_MSG(msg)

    msg->writeString(String(uri));

    if (headers) {
        size_t count = headers->size();
        if (count > 1000) {
            WARNING("headers is a big map");
            count = 1000;
        }
        msg->writeInt32(count);
        std::map<std::string, std::string>::const_iterator it = (*headers).begin();
        for (; it != (*headers).end(); it++) {
            msg->writeString(String(it->first.c_str()));
            msg->writeString(String(it->second.c_str()));
        }
    } else {
        msg->writeInt32(0);
    }

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
}

mm_status_t MediaRecorderProxy::setVideoSourceFormat(int width,
                                                     int height,
                                                     uint32_t format) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setVideoSourceFormat"));
    CHECK_MSG(msg)

    msg->writeInt32(width);
    msg->writeInt32(height);
    msg->writeInt32(format);

    MM_SEND_MSG(msg)
    MM_HANDLE_REPLY();
}

mm_status_t MediaRecorderProxy::setVideoEncoder(const char* mime) {
    return makeCallSetCStr("setVideoEncoder", mime);
}

mm_status_t MediaRecorderProxy::setAudioEncoder(const char* mime) {
    return makeCallSetCStr("setAudioEncoder", mime);
}

mm_status_t MediaRecorderProxy::setOutputFormat(const char* mime) {
    return makeCallSetCStr("setOutputFormat", mime);
}

mm_status_t MediaRecorderProxy::setOutputFile(const char* path) {
    return makeCallSetCStr("setOutputFilePath", path);
}

mm_status_t MediaRecorderProxy::setOutputFile(int fd) {
    DEBUG("fd %d", fd);
    return makeCallSetFd("setOutputFileFd", fd);
}

mm_status_t MediaRecorderProxy::setRecorderUsage(int usage) {
    return makeCallSetInt("setRecorderUsage", usage);
}

mm_status_t MediaRecorderProxy::getRecorderUsage(int &usage) {
    return makeCallGetInt("getRecorderUsage", &usage);
}

mm_status_t MediaRecorderProxy::setPreviewSurface(void * handle) {
    if (handle && !strcmp("camera", (char*)handle))
        return makeCallSetInt64("setPreviewSurface", 1);
    else
        return makeCallSetPointer("setPreviewSurface", handle);
}

mm_status_t MediaRecorderProxy::prepare() {
    return makeCallVoid("prepare");
}

mm_status_t MediaRecorderProxy::reset() {

    return makeCallVoid("reset");
}

mm_status_t MediaRecorderProxy::start() {
    return makeCallVoid("start");
}

mm_status_t MediaRecorderProxy::stop() {
    return makeCallVoid("stop");
}

mm_status_t MediaRecorderProxy::stopSync() {
    return makeCallVoid("stopSync");
}

mm_status_t MediaRecorderProxy::pause() {
    return makeCallVoid("pause");
}

bool MediaRecorderProxy::isRecording() {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("isRecording"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    if (reply)
        return reply->readBool();
    else
        return false;
}

mm_status_t MediaRecorderProxy::getVideoSize(int *width, int * height) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("getVideoSize"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    MM_CHECK_RESULT();

    *width = reply->readInt32();
    *height = reply->readInt32();

    return result;
}

mm_status_t MediaRecorderProxy::getCurrentPosition(int64_t * msec) {
    return makeCallGetInt64("getCurrentPosition", msec);
}

mm_status_t MediaRecorderProxy::setParameter(const MediaMetaSP & meta) {
    //MM_VOID_METHOD_CALL("setParameter");
    return makeCallSetMeta("setParameter", meta);
}

mm_status_t MediaRecorderProxy::getParameter(MediaMetaSP & meta) {
    //MM_VOID_METHOD_CALL("getParameter");
    return makeCallGetMeta("getParameter", meta);
}

mm_status_t MediaRecorderProxy::invoke(const MMParam * request, MMParam * reply1) {
    //MM_VOID_METHOD_CALL("invoke");
    return makeCallVoid("invoke");
}

mm_status_t MediaRecorderProxy::setMaxDuration(int64_t msec) {
    return makeCallSetInt64("setMaxDuration", msec);
}

mm_status_t MediaRecorderProxy::setMaxFileSize(int64_t bytes) {
    return makeCallSetInt64("setMaxFileSize", bytes);
}

#ifdef SINGLE_THREAD_PROXY
// run on LooperThread
int64_t MediaRecorderProxy::handleMethod(MMParam &param) {

    int method = param.readInt32();

    DEBUG("method id %d", method);

    if (method == MM_METHOD_SET_LISTENER) {
        ProxyListener* p =(ProxyListener*)param.readRawPointer();
        setListener(p);
        return 0;
    } else if (method == MM_METHOD_SET_CAMERA) {
        VideoCapture *camera = (VideoCapture *)param.readRawPointer();
        RecordingProxy *recordingProxy = (RecordingProxy *)param.readRawPointer();
        return (int64_t)setCamera(camera, recordingProxy);
    } else if (method == MM_METHOD_SET_VIDEO_SOURCE_URI) {
        const char * uri = param.readCString();
        const std::map<std::string, std::string> * headers =
            (const std::map<std::string, std::string> *)param.readRawPointer();
        return (int64_t)setVideoSourceUri(uri, headers);
    } else if (method == MM_METHOD_SET_AUDIO_SOURCE_URI) {
        const char * uri = param.readCString();
        const std::map<std::string, std::string> * headers =
            (const std::map<std::string, std::string> *)param.readRawPointer();
        return (int64_t)setAudioSourceUri(uri, headers);
    } else if (method == MM_METHOD_SET_VIDEO_SOURCE_FORMAT) {
        int width = param.readInt32();
        int height = param.readInt32();
        uint32_t format = param.readInt32();
        return (int64_t)setVideoSourceFormat(width, height, format);
    } else if (method == MM_METHOD_SET_VIDEO_ENCODER) {
        const char *mime = param.readCString();
        return (int64_t)setVideoEncoder(mime);
    } else if (method == MM_METHOD_SET_AUDIO_ENCODER) {
        const char *mime = param.readCString();
        return (int64_t)setAudioEncoder(mime);
    } else if (method == MM_METHOD_SET_OUTPUT_FORMAT) {
        const char *mime = param.readCString();
        return (int64_t)setOutputFormat(mime);
    } else if (method == MM_METHOD_SET_OUTPUT_FILE_PATH) {
        const char *path = param.readCString();
        return (int64_t)setOutputFile(path);
    } else if (method == MM_METHOD_SET_OUTPUT_FILE_FD) {
        int fd = param.readInt32();
        return (int64_t)setOutputFile(fd);
    } else if (method == MM_METHOD_SET_RECORDER_USAGE) {
        int usage = param.readInt32();
        return (int64_t)setRecorderUsage(usage);
    } else if (method == MM_METHOD_GET_RECORDER_USAGE) {
        int *usage = (int*)param.readRawPointer();
        return (int64_t)getRecorderUsage((int &)(*usage));
    } else if (method == MM_METHOD_SET_PREVIEW_SURFACE) {
        void *handle = (void*)param.readRawPointer();
        return (int64_t)setPreviewSurface(handle);
    } else if (method == MM_METHOD_PREPARE) {
        return (int64_t)prepare();
    } else if (method == MM_METHOD_RESET) {
        return (int64_t)reset();
    } else if (method == MM_METHOD_START) {
        return (int64_t)start();
    } else if (method == MM_METHOD_STOP) {
        return (int64_t)stop();
    } else if (method == MM_METHOD_stopSync) {
        return (int64_t)stopSync();
    } else if (method == MM_METHOD_PAUSE) {
        return (int64_t)pause();
    } else if (method == MM_METHOD_IS_RECORDING) {
        return (int64_t)isRecording();
    } else if (method == MM_METHOD_GET_VIDEO_SIZE) {
        int *width = (int*)param.readRawPointer();
        int *height= (int*)param.readRawPointer();
        return (int64_t)getVideoSize(width, height);
    } else if (method == MM_METHOD_GET_CURRENT_POSITION) {
        int64_t *msec = (int64_t*)param.readRawPointer();
        return (int64_t)getCurrentPosition(msec);
    } else if (method == MM_METHOD_SET_PARAMETER) {
        return (int64_t)setParameter(mMeta1);
    } else if (method == MM_METHOD_GET_PARAMETER) {
        return (int64_t)getParameter(mMeta2);
    } else if (method == MM_METHOD_INVOKE) {
        return (int64_t)invoke(NULL, NULL);
    } else if (method == MM_METHOD_SET_MAX_DURATION) {
        int64_t msec = param.readInt64();
        return (int64_t)setMaxDuration(msec);
    } else if (method == MM_METHOD_SET_MAX_FILE_SIZE) {
        int64_t bytes = param.readInt64();
        return (int64_t)setMaxFileSize(bytes);
    } else if (method == MM_METHOD_RELEASE) {
        return (int64_t)release();
    }

    ERROR("known method id %d", method);
    return MM_ERROR_UNKNOWN;
}
#endif

} // end of namespace YUNOS_MM
