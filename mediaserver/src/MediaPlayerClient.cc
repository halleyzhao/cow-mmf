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

#include <MediaPlayerClient.h>

#include <multimedia/mm_debug.h>
#include <multimedia/mm_cpp_utils.h>
#include <media_surface_texture.h>
#include <native_surface_help.h>

#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>

#ifdef __USING_VR_VIDEO__
#include <VrVideoView.h>
using namespace yunos::yvr;
#endif

namespace YUNOS_MM {

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)


// libbase name space
using namespace yunos;

class TextureProxy : public YunOSMediaCodec::MediaSurfaceTexture::ConsumerProxy {
public:
    TextureProxy(MediaPlayerClient* owner)
        : mOwner(owner) {
    }

    virtual ~TextureProxy() {}

    void returnAcquiredBuffers() {
        if (mOwner)
            mOwner->returnAcquiredBuffers();
    }

    MMNativeBuffer* acquireBuffer(int index) {
        if (mOwner)
            return mOwner->acquireBuffer(index);

        return NULL;
    }

    int returnBuffer(MMNativeBuffer* anb) {
        if (mOwner)
            return mOwner->returnBuffer(anb);

        return -1;
    }

    int setWindowSurface(void *ws) {
        if (mOwner)
            return mOwner->setWindowSurface(ws);

        return -1;
    }

    int setShowFlag(bool show) {
        if (mOwner)
            return mOwner->setShowFlag(show);

        return -1;
    }

    bool getShowFlag() {
        if (mOwner)
            return mOwner->getShowFlag();

        return false;
    }

    int setListener(YunOSMediaCodec::SurfaceTextureListener *listener) {
        if (mOwner)
            return mOwner->setMstListener(listener);

        return 0;
    }

private:
    MediaPlayerClient *mOwner;
};

DEFINE_LOGTAG(MediaPlayerClient);

MediaPlayerClient::MediaPlayerClient(const String &service,
                                     const String &path,
                                     const String &iface)
    : MediaServiceLooper(service, path, iface)
#ifdef SINGLE_THREAD_PROXY
      , mMethodCondition(mMethodLock)
#endif
{
    INFO("create media player client");
    mIsServer = false;
    mListener = NULL;
    mClientListener = new ClientListener(this);
    mTextureProxy = NULL;
    mMst = NULL;
    mServerDie = false;
    mCallSeq = 0;

#ifdef __USING_VR_VIDEO__
    if (mm_check_env_str("media.player.vr", NULL, "1")) {
        INFO("work as vr player");
        mVrView.reset(VrVideoView::createVrView());
    }
#endif
}

MediaPlayerClient::~MediaPlayerClient() {
    INFO("destroy media player client");
    if (mClientListener)
        delete mClientListener;

    if (mMst)
        mMst->setConsumerProxy(NULL);

    if (mTextureProxy)
        delete mTextureProxy;
}

/* run on mediaservicelooper */
SharedPtr<DProxy> MediaPlayerClient::createMediaProxy() {
    MediaPlayerProxy* p = new MediaPlayerProxy(mService, mObjectPath, mInterface);

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

#define CHECK_PLAYER()                                            \
    if (!mDProxy || mServerDie) {                                 \
        ERROR("player proxy is NULL, %p", mDProxy.pointer());     \
        ERROR("server die %d", mServerDie);                       \
        return MM_ERROR_NOT_INITED;                               \
    }

#define CHECK_PLAYER_VOID()                                       \
    if (!mDProxy || mServerDie) {                                 \
        ERROR("player proxy is NULL, %p", mDProxy.pointer());     \
        ERROR("server die %d", mServerDie);                       \
        return MM_ERROR_INVALID_PARAM;                                                   \
    }

mm_status_t MediaPlayerClient::setListener(MediaPlayer::Listener * listener) {
    ENTER();
    CHECK_PLAYER();

    mListener = listener;
    //return getProxy()->setListener(mClientListener);
    return MM_ERROR_SUCCESS;
}

mm_status_t MediaPlayerClient::setDataSource(const char * uri,
                const std::map<std::string, std::string> * headers) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_DATA_SOURCE_URI);
    param.writeCString(uri);
    param.writeRawPointer((uint8_t*)headers);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setDataSource(uri, headers);
#endif
}

mm_status_t MediaPlayerClient::setDataSource(const unsigned char * mem, size_t size) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_DATA_SOURCE_MEM);
    param.writeRawPointer((uint8_t*)mem);
    param.writeInt64(size);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setDataSource(mem, size);
#endif
}

mm_status_t MediaPlayerClient::setDataSource(int fd, int64_t offset, int64_t length) {
    ENTER();
    CHECK_PLAYER();
    if (!fd)
        return MM_ERROR_INVALID_PARAM;

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_DATA_SOURCE_FD);
    param.writeInt32(fd);
    param.writeInt64(offset);
    param.writeInt64(length);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setDataSource(fd, offset, length);
#endif
}

mm_status_t MediaPlayerClient::setSubtitleSource(const char* uri) {
    ENTER();
    CHECK_PLAYER();
    if (!uri)
        return MM_ERROR_INVALID_PARAM;

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_SUBTITLE_URI);
    param.writeCString(uri);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setSubtitleSource(uri);
#endif
}

mm_status_t MediaPlayerClient::setDisplayName(const char* name) {
    ENTER();
    CHECK_PLAYER();
    if (!name)
        return MM_ERROR_INVALID_PARAM;

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_DISPLAY_NAME);
    param.writeCString(name);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setDisplayName(name);
#endif
}

mm_status_t MediaPlayerClient::setVideoDisplay(void * handle) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VIDEO_DISPLAY);
    param.writeRawPointer((uint8_t*)handle);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setVideoDisplay(handle);
#endif
}

mm_status_t MediaPlayerClient::setVideoSurfaceTexture(void * handle) {
    ENTER();
    CHECK_PLAYER();

    //TODO move surface/mediasurfacetexture out of mediacodec
    if (!mTextureProxy) {
        INFO("player client create texture proxy");
        mTextureProxy = new TextureProxy(this);
    }

    YunOSMediaCodec::MediaSurfaceTexture *mst = static_cast<YunOSMediaCodec::MediaSurfaceTexture*>(handle);

    bool show = mst->getShowFlag();
    void *surface = mst->getSurfaceName();

    mst->setConsumerProxy(mTextureProxy);
    mMst = mst;

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VIDEO_SURFACE_TEXTURE);
    param.writeRawPointer((uint8_t*)handle);
    mm_status_t status = (mm_status_t)sendMethodCommand(param);
#else
    mm_status_t status = getProxy()->setVideoSurfaceTexture(handle);
#endif

    if (status != MM_ERROR_SUCCESS)
        EXIT_AND_RETURN1(status);

#ifdef __USING_VR_VIDEO__
    if (mVrView) {
        bool ret;
        ret = mVrView->setSurfaceTexture(handle);
        // FIXME looks like 1080p is ok as we don't play 2k 4k video
        ret &= mVrView->setDisplayTarget((const char*)surface, 0, 0, 1920, 1080);
        //ret &= mVrView->setDisplayTarget((void*)surface, 0, 0, 1280, 720);
        ret &= mVrView->init();
        if (!ret) {
            ERROR("vr video view return error");
            status = MM_ERROR_NOT_INITED;
        }

        EXIT_AND_RETURN1(status);
    }
#endif
    setShowFlag(show);

    if (surface)
        setWindowSurface(surface);

    EXIT_AND_RETURN1(status);
}

mm_status_t MediaPlayerClient::prepare() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_PREPARE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->prepare();
#endif
}

mm_status_t MediaPlayerClient::prepareAsync() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_PREPARE_ASYNC);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->prepareAsync();
#endif
}

mm_status_t MediaPlayerClient::reset() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RESET);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->reset();
#endif
}

mm_status_t MediaPlayerClient::setVolume(const MediaPlayer::VolumeInfo & volume) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_VOLUME);
    param.writeFloat(volume.left);
    param.writeFloat(volume.right);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setVolume(volume.left, volume.right);
#endif
}

mm_status_t MediaPlayerClient::getVolume(MediaPlayer::VolumeInfo * volume) const {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_VOLUME);
    param.writeRawPointer((uint8_t*)&volume->left);
    param.writeRawPointer((uint8_t*)&volume->right);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getVolume(volume->left, volume->right);
#endif
}

mm_status_t MediaPlayerClient::setMute(bool mute) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_MUTE);
    param.writeInt32((int)mute);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setMute(mute);
#endif
}

mm_status_t MediaPlayerClient::getMute(bool * mute) const {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_MUTE);
    param.writeRawPointer((uint8_t*)mute);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getMute(mute);
#endif
}

mm_status_t MediaPlayerClient::start() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_START);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->start();
#endif
}

mm_status_t MediaPlayerClient::stop() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_STOP);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->stop();
#endif
}

mm_status_t MediaPlayerClient::pause() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_PAUSE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->pause();
#endif
}

mm_status_t MediaPlayerClient::seek(int msec) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SEEK);
    param.writeInt32(msec);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->seek(msec);
#endif
}

bool MediaPlayerClient::isPlaying() const {
    ENTER1();
    if (!mDProxy) {
        ERROR("player proxy is NULL");
        return false;
    }

    INFO("check playing");
#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_IS_PLAYING);
    return (bool)sendMethodCommand(param);
#else
    return getProxy()->isPlaying();
#endif
}

mm_status_t MediaPlayerClient::getVideoSize(int *width, int * height) const {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_VIDEO_SIZE);
    param.writeRawPointer((uint8_t*)width);
    param.writeRawPointer((uint8_t*)height);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getVideoSize(width, height);
#endif
}

mm_status_t MediaPlayerClient::getCurrentPosition(int * msec) const {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_CURRENT_POSITION);
    param.writeRawPointer((uint8_t*)msec);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getCurrentPosition(msec);
#endif
}

mm_status_t MediaPlayerClient::getDuration(int * msec) const {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_DURATION);
    param.writeRawPointer((uint8_t*)msec);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getDuration(msec);
#endif
}

mm_status_t MediaPlayerClient::setAudioStreamType(MediaPlayer::as_type_t type) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_AUDIO_STREAM_TYPE);
    param.writeInt32((int)type);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setAudioStreamType((int)type);
#endif
}

mm_status_t MediaPlayerClient::getAudioStreamType(MediaPlayer::as_type_t *type) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_AUDIO_STREAM_TYPE);
    param.writeRawPointer((uint8_t*)type);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getAudioStreamType((int *)type);
#endif
}

mm_status_t MediaPlayerClient::setLoop(bool loop) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_LOOP);
    param.writeInt32(loop);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setLoop(loop);
#endif
}

bool MediaPlayerClient::isLooping() const {
    ENTER();
    if (!mDProxy) {
        ERROR("player proxy is NULL");
        return false;
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_IS_LOOPING);
    return (bool)sendMethodCommand(param);
#else
    return getProxy()->isLooping();
#endif
}

mm_status_t MediaPlayerClient::setParameter(const MediaMetaSP & meta) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_PARAMETER);
    getProxy()->setMeta1(meta);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->setParameter(meta);
#endif
}

mm_status_t MediaPlayerClient::getParameter(MediaMetaSP & meta) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_PARAMETER);
    getProxy()->setMeta2(meta);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->getParameter(meta);
#endif
}

mm_status_t MediaPlayerClient::invoke(const MMParam * request, MMParam * reply) {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_INVOKE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->invoke(request, reply);
#endif
}

mm_status_t MediaPlayerClient::captureVideo() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_CAPTURE_VIDEO);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->captureVideo();
#endif
}

mm_status_t MediaPlayerClient::enableExternalSubtitleSupport(bool enable)
{
        ENTER();
        CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
        INIT_MEDIA_METHOD(MM_METHOD_ENABLE_EXTERNAL_SUBTITLE);
        return (mm_status_t)sendMethodCommand(param);
#else
        return getProxy()->enableExternalSubtitleSupport(enable);
#endif
}

mm_status_t MediaPlayerClient::release() {
    ENTER();
    CHECK_PLAYER();

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RELEASE);
    return (mm_status_t)sendMethodCommand(param);
#else
    return getProxy()->release();
#endif
}

void MediaPlayerClient::ClientListener::onMessage(int msg, int param1, int param2, const MMParam *obj) {
    if (mOwner && mOwner->mListener)
        return mOwner->mListener->onMessage(msg, param1, param2, obj);
}

void MediaPlayerClient::ClientListener::onServerUpdate(bool die) {
    if (mOwner) {
        mOwner->mServerDie = true;

        if (mOwner->mListener)
            mOwner->mListener->onMessage(MediaPlayer::Listener::MSG_ERROR, MM_ERROR_SERVER_DIED, 0, NULL);
    }
}

void MediaPlayerClient::returnAcquiredBuffers() {
    ENTER();
    if (!mDProxy) {
        ERROR("player proxy is NULL");
        EXIT1();
    }

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT1();
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RETURN_ACQUIRED_BUFFERS);
    sendMethodCommand(param);
#else
    return getProxy()->returnAcquiredBuffers();
#endif
}

MMNativeBuffer* MediaPlayerClient::acquireBuffer(int index) {
    ENTER();
    if (!mDProxy) {
        ERROR("player proxy is NULL");
        EXIT_AND_RETURN1(NULL);
    }

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(NULL);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_ACQUIRE_BUFFER);
    param.writeInt32(index);
    MMNativeBuffer* temp = (MMNativeBuffer*)sendMethodCommand(param);
    if (temp == NULL)
        INFO("anb is NULL");
    return temp;
#else
    return getProxy()->acquireBuffer(index);
#endif
}

int MediaPlayerClient::returnBuffer(MMNativeBuffer* anb) {
    ENTER();
    CHECK_PLAYER();

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(-1);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_RETURN_BUFFER);
    param.writeRawPointer((uint8_t*)anb);
    return (int)sendMethodCommand(param);
#else
    return getProxy()->returnBuffer(anb);
#endif
}

int MediaPlayerClient::setWindowSurface(void *ws) {
    ENTER();
    CHECK_PLAYER();

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(-1);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_WINDOW_SURFACE);
    param.writeRawPointer((uint8_t*)ws);
    return (int)sendMethodCommand(param);
#else
    return getProxy()->setWindowSurface(ws);
#endif
}

int MediaPlayerClient::setShowFlag(bool show) {
    ENTER();
    CHECK_PLAYER();

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(-1);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_SHOW_FLAG);
    param.writeInt32(show);
    return (int)sendMethodCommand(param);
#else
    return getProxy()->setShowFlag(show);
#endif
}

bool MediaPlayerClient::getShowFlag() {
    ENTER();
    CHECK_PLAYER();

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(false);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_GET_SHOW_FLAG);
    return (bool)sendMethodCommand(param);
#else
    return getProxy()->getShowFlag();
#endif
}

int MediaPlayerClient::setMstListener(YunOSMediaCodec::SurfaceTextureListener *listener) {
    ENTER();
    CHECK_PLAYER();

    if (!mTextureProxy) {
        ERROR("no texture proxy, should not be here");
        EXIT_AND_RETURN1(-1);
    }

#ifdef SINGLE_THREAD_PROXY
    INIT_MEDIA_METHOD(MM_METHOD_SET_MST_LISTENER);
    param.writeRawPointer((uint8_t*)listener);
    return (bool)sendMethodCommand(param);
#else
    return getProxy()->setMstListener(listener);
#endif
}

#ifdef SINGLE_THREAD_PROXY
int64_t MediaPlayerClient::sendMethodCommand(MMParam &param) {
    ENTER();
    CHECK_PLAYER_VOID();

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
        mLooper->sendTask(Task(MediaPlayerClient::sendMethodCommand1, this, param, seq));
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
void MediaPlayerClient::sendMethodCommand1(MediaPlayerClient* p, MMParam &param, uint32_t seq) {

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
