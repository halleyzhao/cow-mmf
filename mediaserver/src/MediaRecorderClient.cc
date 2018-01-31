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

#include <MediaRecorderClient.h>

#include <multimedia/mm_debug.h>

#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>

namespace YUNOS_MM {

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)


// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaRecorderClient)

MediaRecorderClient::MediaRecorderClient(const String &service,
                                     const String &path,
                                     const String &iface)
    : MediaServiceLooper(service, path, iface)
#ifdef SINGLE_THREAD_PROXY
      , mMethodCondition(mMethodLock)
#endif
{
    INFO("create media recorder client");
    mIsServer = false;
    mListener = NULL;
    mClientListener = new ClientListener(this);
    mServerDie = false;
    mCallSeq = 0;
}

MediaRecorderClient::~MediaRecorderClient() {
    INFO("destroy media recorder client");
    if (mClientListener)
        delete mClientListener;
}

/* run on mediaservicelooper */
SharedPtr<DProxy> MediaRecorderClient::createMediaProxy() {
    MediaRecorderProxy* p = new MediaRecorderProxy(mService, mObjectPath, mInterface);
#ifdef SINGLE_THREAD_PROXY
    mDProxy = p;
    //INIT_MEDIA_METHOD(MM_METHOD_SET_LISTENER);
    //param.writeRawPointer((uint8_t*)mClientListener);

    /* post task from mediaservicelooper thread */
    //sendMethodCommand(param, false);

    // on looper thread
    p->setListener(mClientListener);
#else
    p->setListener(mClientListener);
#endif
    return p;
}

#define CHECK_RECORDER()                                            \
    if (!mDProxy || mServerDie) {                                   \
        ERROR("recorder proxy is NULL, %p", mDProxy.pointer());     \
        ERROR("server die %d", mServerDie);                         \
        return MM_ERROR_NOT_INITED;                                 \
    }                                                               \
    MediaRecorderProxy *proxy = getProxy();                         \
    if (!proxy) {                                                   \
        ERROR("recorder proxy static_cast error");                  \
        return MM_ERROR_NOT_INITED;                                 \
    }

#define CHECK_RECORDER_INT64()                                      \
    if (!mDProxy || mServerDie) {                                   \
        ERROR("recorder proxy is NULL, %p", mDProxy.pointer());     \
        ERROR("server die %d", mServerDie);                         \
        return (int64_t)(-1);                                       \
    }                                                               \
    MediaRecorderProxy *proxy = getProxy();                         \
    if (!proxy) {                                                   \
        ERROR("recorder proxy static_cast error");                  \
        return (int64_t)(-1);                                       \
    }

mm_status_t MediaRecorderClient::setListener(MediaRecorder::Listener * listener) {
    ENTER();
    CHECK_RECORDER();

    mListener = listener;
    //return proxy->setListener(mClientListener);
    return MM_ERROR_SUCCESS;
}

mm_status_t MediaRecorderClient::setCamera(VideoCapture *camera, RecordingProxy *recordingProxy)
{
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_CAMERA);
    param.writeRawPointer((uint8_t*)camera);
    param.writeRawPointer((uint8_t*)recordingProxy);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setCamera(camera, recordingProxy);
#endif
}

mm_status_t MediaRecorderClient::setVideoSourceUri(const char * uri,
                const std::map<std::string, std::string> * headers) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VIDEO_SOURCE_URI);
    param.writeCString(uri);
    param.writeRawPointer((uint8_t*)headers);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setVideoSourceUri(uri, headers);
#endif
}

mm_status_t MediaRecorderClient::setAudioSourceUri(const char * uri,
                const std::map<std::string, std::string> * headers) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_AUDIO_SOURCE_URI);
    param.writeCString(uri);
    param.writeRawPointer((uint8_t*)headers);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setAudioSourceUri(uri, headers);
#endif
}

mm_status_t MediaRecorderClient::setVideoSourceFormat(
                int width, int height, uint32_t format) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VIDEO_SOURCE_FORMAT);
    param.writeInt32(width);
    param.writeInt32(height);
    param.writeInt32(format);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setVideoSourceFormat(width, height, format);
#endif
}

mm_status_t MediaRecorderClient::setVideoEncoder(const char *mime) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VIDEO_ENCODER);
    param.writeCString(mime);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setVideoEncoder(mime);
#endif
}

mm_status_t MediaRecorderClient::setAudioEncoder(const char *mime) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_AUDIO_ENCODER);
    param.writeCString(mime);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setAudioEncoder(mime);
#endif
}

mm_status_t MediaRecorderClient::setOutputFormat(const char *mime) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_OUTPUT_FORMAT);
    param.writeCString(mime);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setOutputFormat(mime);
#endif
}

mm_status_t MediaRecorderClient::setOutputFile(const char *filePath) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_OUTPUT_FILE_PATH);
    param.writeCString(filePath);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setOutputFile(filePath);
#endif
}

mm_status_t MediaRecorderClient::setOutputFile(int fd) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_OUTPUT_FILE_FD);
    param.writeInt32(fd);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setOutputFile(fd);
#endif
}

mm_status_t MediaRecorderClient::setRecorderUsage(RecorderUsage usage) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_RECORDER_USAGE);
    param.writeInt32((int)usage);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setRecorderUsage((int)usage);
#endif
}

mm_status_t MediaRecorderClient::getRecorderUsage(RecorderUsage &usage) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_RECORDER_USAGE);
    param.writeRawPointer((uint8_t*)(&usage));
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->getRecorderUsage((int &)usage);
#endif
}

mm_status_t MediaRecorderClient::setPreviewSurface(void * handle) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_PREVIEW_SURFACE);
    param.writeRawPointer((uint8_t*)handle);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setPreviewSurface(handle);
#endif
}

mm_status_t MediaRecorderClient::prepare() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_PREPARE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->prepare();
#endif
}

mm_status_t MediaRecorderClient::reset() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RESET);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->reset();
#endif
}

mm_status_t MediaRecorderClient::start() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_START);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->start();
#endif
}

mm_status_t MediaRecorderClient::stop() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_STOP);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->stop();
#endif
}

mm_status_t MediaRecorderClient::stopSync() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_stopSync);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->stopSync();
#endif
}

mm_status_t MediaRecorderClient::pause() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_PAUSE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->pause();
#endif
}

bool MediaRecorderClient::isRecording() const {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_IS_RECORDING);
    return (bool)sendMethodCommand(param);
#else
    return proxy->isRecording();
#endif
}

mm_status_t MediaRecorderClient::getVideoSize(int *width, int * height) const {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_VIDEO_SIZE);
    param.writeRawPointer((uint8_t*)width);
    param.writeRawPointer((uint8_t*)height);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->getVideoSize(width, height);
#endif
}

mm_status_t MediaRecorderClient::getCurrentPosition(int64_t * msec) const {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_CURRENT_POSITION);
    param.writeRawPointer((uint8_t*)msec);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->getCurrentPosition(msec);
#endif
}

mm_status_t MediaRecorderClient::setParameter(const MediaMetaSP & meta) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_PARAMETER);
    proxy->setMeta1(meta);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setParameter(meta);
#endif
}

mm_status_t MediaRecorderClient::getParameter(MediaMetaSP & meta) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_PARAMETER);
    proxy->setMeta2(meta);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->getParameter(meta);
#endif
}

mm_status_t MediaRecorderClient::invoke(const MMParam * request, MMParam * reply) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_INVOKE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->invoke(request, reply);
#endif
}

mm_status_t MediaRecorderClient::setMaxDuration(int64_t msec) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_MAX_DURATION);
    param.writeInt64(msec);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setMaxDuration(msec);
#endif
}

mm_status_t MediaRecorderClient::setMaxFileSize(int64_t bytes) {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_MAX_FILE_SIZE);
    param.writeInt64(bytes);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->setMaxFileSize(bytes);
#endif
}

mm_status_t MediaRecorderClient::release() {
    ENTER();
    CHECK_RECORDER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RELEASE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return proxy->release();
#endif
}

void MediaRecorderClient::ClientListener::onMessage(int msg, int param1, int param2, const MMParam *obj) {
    if (mOwner && mOwner->mListener)
        return mOwner->mListener->onMessage(msg, param1, param2, obj);
}

void MediaRecorderClient::ClientListener::onServerUpdate(bool die) {
    if (mOwner) {
        mOwner->mServerDie = true;

        if (mOwner->mListener)
            mOwner->mListener->onMessage(MediaRecorder::Listener::MSG_ERROR, MM_ERROR_SERVER_DIED, 0, NULL);
    }
}

#ifdef SINGLE_THREAD_PROXY
int64_t MediaRecorderClient::sendMethodCommand(MMParam &param) {
    ENTER();
    CHECK_RECORDER_INT64();

    if (!mInit) {
        ERROR("looper is not init, cannot send method %d", param.readInt32());
        return (int64_t)MM_ERROR_NOT_INITED;
    }

    bool done = false;
    uint32_t seq;
    int64_t ret;

    {
        MMAutoLock lock(mMethodLock);
        seq = mCallSeq++;
        mLooper->sendTask(Task(MediaRecorderClient::sendMethodCommand1, this, param, seq));
    }

    while(!done) {
        MMAutoLock lock(mMethodLock);

        std::map<uint32_t, int64_t>::iterator it = mCallSeqMap.find(seq);
        if (it == mCallSeqMap.end()) {
            mMethodCondition.wait();
            continue;
        }

        ret = it->second;
        done = true;
        mCallSeqMap.erase(it);
    }

    DEBUG("method task done");
    return ret;
}

/*static*/
void MediaRecorderClient::sendMethodCommand1(MediaRecorderClient* p, MMParam &param, uint32_t seq) {

    if (!p || !(p->mInit)) {
        ERROR("looper is not init or ret ptr is not provided, cannot send method");
        if (p) {
            MMAutoLock lock(p->mMethodLock);
            p->mCallSeqMap[seq] = (int64_t)MM_ERROR_NOT_INITED;
            p->mMethodCondition.broadcast();
        }
        return;
    }

    {
        MMAutoLock lock(p->mMethodLock);
        p->mCallSeqMap[seq] = p->getProxy()->handleMethod(param);
        p->mMethodCondition.broadcast();
    }
}
#endif

} // end of namespace YUNOS_MM
