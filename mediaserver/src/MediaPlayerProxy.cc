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

#include <MediaPlayerProxy.h>
#include <native_surface_help.h>
#include <media_surface_texture.h>

#include <dbus/DMessage.h>
#include <dbus/DService.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaPlayerProxy)

MediaPlayerProxy::MediaPlayerProxy(const SharedPtr<DService>& service,
                                     const String &path,
                                     const String &iface)
    : MediaProxy(service, path, iface) {
}

MediaPlayerProxy::~MediaPlayerProxy() {
}

mm_status_t MediaPlayerProxy::setDataSource(const char * uri,
                const std::map<std::string, std::string> * headers) {

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setDataSourceUri"));
    CHECK_MSG(msg)

    msg->writeString(String(uri));

    if (headers && !headers->empty()) {
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

mm_status_t MediaPlayerProxy::setDataSource(const unsigned char * mem, size_t size) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setDataSourceMem"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
}

mm_status_t MediaPlayerProxy::setDataSource(int fd, int64_t offset, int64_t length) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setDataSourceFd"));
    CHECK_MSG(msg)

    msg->writeFd(fd);
    msg->writeInt64(offset);
    msg->writeInt64(length);

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
}

mm_status_t MediaPlayerProxy::setSubtitleSource(const char *uri) {
    if (uri)
        INFO("set subtitle uri %s", uri);
    else {
        INFO("set subtitle uri is NULL");
        uri = "bad-name";
    }

    return makeCallSetCStr("setSubtitleSource", uri);
}

mm_status_t MediaPlayerProxy::setDisplayName(const char *name) {
    if (name)
        INFO("set video display, socket name %s", name);
    else {
        INFO("set video display, socket name is NULL");
        name = "bad-name";
    }

    return makeCallSetCStr("setVideoDisplay", name);
}

mm_status_t MediaPlayerProxy::setVideoDisplay(void * handle) {
    const char* str = (char*)handle;
    if (str)
        INFO("set video display, socket name %s", str);
    else {
        INFO("set video display, socket name is NULL");
        str = "bad-name";
    }

    return makeCallSetCStr("setVideoDisplay", str);
}

mm_status_t MediaPlayerProxy::setVideoSurfaceTexture(void * handle) {
    return makeCallSetPointer("setVideoSurfaceTexture", handle);
}

mm_status_t MediaPlayerProxy::prepare() {
    return makeCallVoid("prepare");
}

mm_status_t MediaPlayerProxy::prepareAsync() {
    return makeCallVoid("prepareAsync");
}

mm_status_t MediaPlayerProxy::reset() {

    return makeCallVoid("reset");
}

mm_status_t MediaPlayerProxy::setVolume(float left, float right) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setVolume"));
    CHECK_MSG(msg)

    msg->writeDouble((double)left);
    msg->writeDouble((double)right);

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
}

mm_status_t MediaPlayerProxy::getVolume(float &left, float &right) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("getVolume"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    MM_CHECK_RESULT();
    left = (float)reply->readDouble();
    right = (float)reply->readDouble();

    return result;
}

mm_status_t MediaPlayerProxy::setMute(bool mute) {
    return makeCallSetBool("setMute", mute);
}

mm_status_t MediaPlayerProxy::getMute(bool * mute) {
    return makeCallGetBool("getMute", mute);
}

mm_status_t MediaPlayerProxy::start() {
    return makeCallVoid("start");
}

mm_status_t MediaPlayerProxy::stop() {
    return makeCallVoid("stop");
}

mm_status_t MediaPlayerProxy::pause() {
    return makeCallVoid("pause");
}

mm_status_t MediaPlayerProxy::seek(int msec) {
    return makeCallSetInt("seek", msec);
}

bool MediaPlayerProxy::isPlaying() {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("isPlaying"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    if (reply)
        return reply->readBool();
    else
        return false;
}

mm_status_t MediaPlayerProxy::getVideoSize(int *width, int * height) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("getVideoSize"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    MM_CHECK_RESULT();

    *width = reply->readInt32();
    *height = reply->readInt32();

    return result;
}

mm_status_t MediaPlayerProxy::getCurrentPosition(int * msec) {
    return makeCallGetInt("getCurrentPosition", msec);
}

mm_status_t MediaPlayerProxy::getDuration(int * msec) {
    return makeCallGetInt("getDuration", msec);
}

mm_status_t MediaPlayerProxy::setAudioStreamType(int type) {
    return makeCallSetInt("setAudioStreamType", type);
}

mm_status_t MediaPlayerProxy::getAudioStreamType(int *type) {
    return makeCallGetInt("getAudioStreamType", type);
}

mm_status_t MediaPlayerProxy::setLoop(bool loop) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setLoop"));
    CHECK_MSG(msg)

    msg->writeBool(loop);

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
    return makeCallSetBool("setLoop", loop);
}

bool MediaPlayerProxy::isLooping() {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("isLooping"));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg)

    if (reply)
        return reply->readBool();
    else
        return false;
}

mm_status_t MediaPlayerProxy::setParameter(const MediaMetaSP & meta) {
    //MM_VOID_METHOD_CALL("setParameter");
    return makeCallSetMeta("setParameter", meta);
}

mm_status_t MediaPlayerProxy::getParameter(MediaMetaSP & meta) {
    //MM_VOID_METHOD_CALL("getParameter");
    return makeCallGetMeta("getParameter", meta);
}

mm_status_t MediaPlayerProxy::invoke(const MMParam * request, MMParam * reply1) {
    //MM_VOID_METHOD_CALL("invoke");
    return makeCallVoid("invoke");
}

mm_status_t MediaPlayerProxy::captureVideo() {
    //MM_VOID_METHOD_CALL("captureVideo");
    return makeCallVoid("captureVideo");
}

mm_status_t MediaPlayerProxy::enableExternalSubtitleSupport(bool enable) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("enableExternalSubtitleSupport"));
    CHECK_MSG(msg)

    msg->writeBool(enable);

    MM_SEND_MSG(msg)

    MM_HANDLE_REPLY();
    return makeCallSetBool("enableExternalSubtitleSupport", enable);
}

void MediaPlayerProxy::returnAcquiredBuffers() {
    makeCallVoid("returnAcquiredBuffers");
}

MMNativeBuffer* MediaPlayerProxy::acquireBuffer(int index) {

    int64_t handle = makeCallGetAnbHandle("acquireBuffer", index);
    std::map<int64_t, MMNativeBuffer*>::iterator it = mHandleMap.find(handle);

    if (handle == 0) {
        WARNING("acquire buffer[%d] from server is NULL", index);
        return NULL;
    } else if (it != mHandleMap.end()) {
        DEBUG("buffer[%d] %p", index, it->second);
        return it->second;
    }

    MMNativeBuffer* r = makeCallGetAnb("acquireBufferIPC", handle);
    DEBUG("buffer[%d] %p", index, r);
    return r;
}

int MediaPlayerProxy::returnBuffer(MMNativeBuffer* anb) {
    DEBUG("%p", anb);
    return makeCallSetAnb("returnBuffer", anb);
}

int MediaPlayerProxy::setWindowSurface(void *ws) {
    const char* str = (char*)ws;
    if (str)
        INFO("set video display, socket name %s", str);
    else {
        INFO("set video display, socket name is NULL");
        str = "bad-name";
    }

    mm_status_t status = makeCallSetCStr("setWindowSurface", str);

    if (status == MM_ERROR_SUCCESS)
        return 0;
    else
        return 1;
}

int MediaPlayerProxy::setShowFlag(bool show) {
    return (makeCallSetBool("setShowFlag", show) == MM_ERROR_SUCCESS);
}

bool MediaPlayerProxy::getShowFlag() {
    bool show = false;
    makeCallGetBool("setShowFlag", &show);
    return show;
}

int MediaPlayerProxy::setMstListener(YunOSMediaCodec::SurfaceTextureListener *listener) {
    mMstListener = listener;
    bool clear = (listener == NULL);
    return makeCallSetBool("setMstListener", clear);
}

#ifdef SINGLE_THREAD_PROXY
// run on LooperThread
int64_t MediaPlayerProxy::handleMethod(MMParam &param) {

    int method = param.readInt32();

    DEBUG("method id %d", method);

    if (method == MM_METHOD_SET_LISTENER) {
        ProxyListener* p =(ProxyListener*)param.readRawPointer();
        setListener(p);
        return 0;
    } else if (method == MM_METHOD_SET_DATA_SOURCE_FD) {
        int fd = param.readInt32();
        int64_t offset = param.readInt64();
        int64_t length = param.readInt64();
        return (int64_t)setDataSource(fd, offset, length);
    } else if (method == MM_METHOD_SET_DATA_SOURCE_URI) {
        const char* uri = param.readCString();
        const std::map<std::string, std::string> * headers =
        (const std::map<std::string, std::string> *)param.readRawPointer();
        return (int64_t)setDataSource(uri, headers);
    } else if (method == MM_METHOD_SET_DATA_SOURCE_MEM) {
        const unsigned char * mem = param.readRawPointer();
        size_t size = param.readInt64();
        return (int64_t)setDataSource(mem, size);
    } else if (method == MM_METHOD_SET_SUBTITLE_URI) {
        const char* uri = param.readCString();
        return (int64_t)setSubtitleSource(uri);
    } else if (method == MM_METHOD_SET_DISPLAY_NAME) {
        const char* name = param.readCString();
        return (int64_t)setDisplayName(name);
    } else if (method == MM_METHOD_SET_VIDEO_DISPLAY) {
        void *handle = (void*)param.readRawPointer();
        return (int64_t)setVideoDisplay(handle);
    } else if (method == MM_METHOD_SET_VIDEO_SURFACE_TEXTURE) {
        void *handle = (void*)param.readRawPointer();
        return (int64_t)setVideoSurfaceTexture(handle);
    } else if (method == MM_METHOD_PREPARE) {
        return (int64_t)prepare();
    } else if (method == MM_METHOD_PREPARE_ASYNC) {
        return (int64_t)prepareAsync();
    } else if (method == MM_METHOD_RESET) {
        return (int64_t)reset();
    } else if (method == MM_METHOD_SET_VOLUME) {
        float left = param.readFloat();
        float right = param.readFloat();
        return (int64_t)setVolume(left, right);
    } else if (method == MM_METHOD_GET_VOLUME) {
        //float left, right
        float *p1 = (float*)param.readRawPointer();
        float *p2 = (float*)param.readRawPointer();
        mm_status_t status = getVolume(*p1, *p2);
        return (int64_t)status;
    } else if (method == MM_METHOD_SET_MUTE) {
        bool mute = (bool)param.readInt32();
        return (int64_t)setMute(mute);
    } else if (method == MM_METHOD_GET_MUTE) {
        bool *mute = (bool*)param.readRawPointer();
        return (int64_t)getMute(mute);
    } else if (method == MM_METHOD_START) {
        return (int64_t)start();
    } else if (method == MM_METHOD_STOP) {
        return (int64_t)stop();
    } else if (method == MM_METHOD_PAUSE) {
        return (int64_t)pause();
    } else if (method == MM_METHOD_SEEK) {
        int msec = param.readInt32();
        return (int64_t)seek(msec);
    } else if (method == MM_METHOD_IS_PLAYING) {
        return (int64_t)isPlaying();
    } else if (method == MM_METHOD_GET_VIDEO_SIZE) {
        int *width = (int*)param.readRawPointer();
        int *height = (int*)param.readRawPointer();
        return (int64_t)getVideoSize(width, height);
    } else if (method == MM_METHOD_GET_CURRENT_POSITION) {
        int *msec = (int*)param.readRawPointer();
        return (int64_t)getCurrentPosition(msec);
    } else if (method == MM_METHOD_GET_DURATION) {
        int *msec = (int*)param.readRawPointer();
        return (int64_t)getDuration(msec);
    } else if (method == MM_METHOD_SET_AUDIO_STREAM_TYPE) {
        int type = param.readInt32();
        return (int64_t)setAudioStreamType(type);
    } else if (method == MM_METHOD_GET_AUDIO_STREAM_TYPE) {
        int *type = (int*)param.readRawPointer();
        return (int64_t)getAudioStreamType(type);
    } else if (method == MM_METHOD_SET_LOOP) {
        bool loop = (bool)param.readInt32();
        return (int64_t)setLoop(loop);
    } else if (method == MM_METHOD_IS_LOOPING) {
        return (int64_t)isLooping();
    } else if (method == MM_METHOD_GET_PARAMETER) {
        return (int64_t)getParameter(mMeta2);
    } else if (method == MM_METHOD_SET_PARAMETER) {
        return (int64_t)setParameter(mMeta1);
    } else if (method == MM_METHOD_INVOKE) {
        return (int64_t)invoke(NULL, NULL);
    } else if (method == MM_METHOD_CAPTURE_VIDEO) {
        return (int64_t)captureVideo();
    } else if (method == MM_METHOD_ENABLE_EXTERNAL_SUBTITLE) {
        bool enable = (bool)param.readInt32();
        return (int64_t)enableExternalSubtitleSupport(enable);
    } else if (method == MM_METHOD_RELEASE) {
        return (int64_t)release();
    } else if (method == MM_METHOD_RETURN_ACQUIRED_BUFFERS) {
        returnAcquiredBuffers();
        return (int64_t)0;
    } else if (method == MM_METHOD_ACQUIRE_BUFFER) {
        int index = param.readInt32();
        return (int64_t)acquireBuffer(index);
    } else if (method == MM_METHOD_RETURN_BUFFER) {
        MMNativeBuffer* anb = (MMNativeBuffer*)param.readRawPointer();
        return (int64_t)returnBuffer(anb);
    } else if (method == MM_METHOD_SET_WINDOW_SURFACE) {
        void *ws = (void*)param.readRawPointer();
        return (int64_t)setWindowSurface(ws);
    } else if (method == MM_METHOD_SET_SHOW_FLAG) {
        bool show = (bool)param.readInt32();
        return (int64_t)setShowFlag(show);
    } else if (method == MM_METHOD_GET_SHOW_FLAG) {
        return (int64_t)getShowFlag();
    } else if (method == MM_METHOD_SET_MST_LISTENER) {
        YunOSMediaCodec::SurfaceTextureListener* listener =
            (YunOSMediaCodec::SurfaceTextureListener*)param.readRawPointer();
        return (int64_t)setMstListener(listener);
    }

    ERROR("known method id %d", method);
    return MM_ERROR_UNKNOWN;
}
#endif
} // end of namespace YUNOS_MM
