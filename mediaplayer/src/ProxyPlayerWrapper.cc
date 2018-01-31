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

#include "ProxyPlayerWrapper.h"

#include <multimedia/mm_debug.h>

#include <MediaClientHelper.h>
#include <MediaPlayerClient.h>

#include <dbus/DAdaptor.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(ProxyPlayerWrapper)

#define fenter() MMLOGI("+\n")
#define fleave_nocode() do { MMLOGV("-\n"); return; } while(0)
#define fleave(_code) do { MMLOGV("-[%d]\n", _code); return (_code); }while(0)
#define fleave_invalid(_code) do { MMLOGE("-[%d]\n", _code); return (_code); }while(0)

#define call_Proxyplayer(_func) do {   \
    if (!mPlayer)                      \
        return MM_ERROR_NOT_INITED;    \
    return mPlayer->_func;             \
}while(0)


ProxyPlayerWrapper::ProxyPlayerWrapper(int playType, const void * userDefinedData/* = NULL*/)
    : mPlayType(playType)
{
    fenter();

    String serviceName, objectPath, iface;

    bool result = MediaClientHelper::connect(
                      playType,
                      "MediaPlayer",
                       serviceName,
                       objectPath,
                       iface);

    INFO("player service instance is connected with return value %d\n", result);

    if (!result) {
        mPlayer = NULL;
        MMLOGE("create player is rejected by media service\n");
        fleave_nocode();
    }

    mPlayer = MediaClientHelper::getMediaPlayerClient(serviceName, objectPath, iface);
    if ( !mPlayer ) {
        MMLOGE("failed to create player\n");
        fleave_nocode();
    }

    fleave_nocode();
}

ProxyPlayerWrapper::~ProxyPlayerWrapper()
{
    fenter();

    if ( mPlayer ) {
        mPlayer->reset();
        mPlayer->release();

        MediaClientHelper::disconnect("MediaPlayer", mPlayer);

        mPlayer->requestExitAndWait();
    }

    MM_RELEASE(mPlayer);
    fleave_nocode();
}

mm_status_t ProxyPlayerWrapper::setListener(Listener * listener)
{
    fenter();
    
    MediaPlayer::setListener(listener);
    call_Proxyplayer(setListener(listener));
}

mm_status_t ProxyPlayerWrapper::setDataSource(const char * uri,
                        const std::map<std::string, std::string> * headers)
{
    MMLOGI("uri: %s, headers: %p\n", uri, headers);
    call_Proxyplayer(setDataSource(uri, headers));
}

mm_status_t ProxyPlayerWrapper::setDataSource(int fd, int64_t offset, int64_t length)
{
    MMLOGI("fd: %d, offset: %" PRId64 ", length: %" PRId64 "\n", fd, offset, length);
    call_Proxyplayer(setDataSource(fd, offset, length));
}

mm_status_t ProxyPlayerWrapper::setDataSource(const unsigned char * mem, size_t size)
{
    MMLOGI("mem: %p, size: %d\n", mem, size);
    fleave_invalid(MM_ERROR_UNSUPPORTED);
}

mm_status_t ProxyPlayerWrapper::setSubtitleSource(const char* uri)
{
    if (mPlayType)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("subtitle uri: %s\n", PRINTABLE_STR(uri));
    call_Proxyplayer(setSubtitleSource(uri));
}

mm_status_t ProxyPlayerWrapper::setDisplayName(const char* name)
{
    if (mPlayType)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("display name: %s\n", name);
    call_Proxyplayer(setDisplayName(name));
}

mm_status_t ProxyPlayerWrapper::setVideoDisplay(void * handle)
{
    if (mPlayType)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("handle: %p\n", handle);
    call_Proxyplayer(setVideoDisplay(handle));
}

mm_status_t ProxyPlayerWrapper::setVideoSurfaceTexture(void * handle)
{
    if (mPlayType)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("handle: %p\n", handle);
    call_Proxyplayer(setVideoSurfaceTexture(handle));
}

mm_status_t ProxyPlayerWrapper::prepare()
{
    //fleave_invalid(MM_ERROR_UNSUPPORTED);
     call_Proxyplayer(prepare());
}

mm_status_t ProxyPlayerWrapper::prepareAsync()
{
    call_Proxyplayer(prepareAsync());
}

mm_status_t ProxyPlayerWrapper::reset()
{
    call_Proxyplayer(reset());
}

mm_status_t ProxyPlayerWrapper::setVolume(const VolumeInfo & volume)
{
    MMLOGI("left: %f, right: %f\n", volume.left, volume.right);
    call_Proxyplayer(setVolume(volume));
}

mm_status_t ProxyPlayerWrapper::getVolume(VolumeInfo * volume) const
{
    call_Proxyplayer(getVolume(volume));
}

mm_status_t ProxyPlayerWrapper::setMute(bool mute)
{
    //fleave_invalid(MM_ERROR_UNSUPPORTED);
    call_Proxyplayer(setMute(mute));
}

mm_status_t ProxyPlayerWrapper::getMute(bool * mute) const
{
    //fleave_invalid(MM_ERROR_UNSUPPORTED);
    call_Proxyplayer(getMute(mute));
}

mm_status_t ProxyPlayerWrapper::start()
{
    call_Proxyplayer(start());
}

mm_status_t ProxyPlayerWrapper::stop()
{
    call_Proxyplayer(stop());
}

mm_status_t ProxyPlayerWrapper::pause()
{
    call_Proxyplayer(pause());
}

bool ProxyPlayerWrapper::isPlaying() const
{
    fenter();
    if (mPlayer)
        return mPlayer->isPlaying();
    else
        return false;
}

mm_status_t ProxyPlayerWrapper::seek(int msec)
{
    MMLOGD("msec: %d\n", msec);
    call_Proxyplayer(seek(int64_t(msec)));
}

mm_status_t ProxyPlayerWrapper::getVideoSize(int *width, int * height) const
{
    fenter();

    if (mPlayType)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMASSERT(width!=NULL);
    MMASSERT(height!=NULL);
    call_Proxyplayer(getVideoSize(width, height));
}

mm_status_t ProxyPlayerWrapper::getCurrentPosition(int * msec) const
{
    fenter();
    MMASSERT(msec!=NULL);
    call_Proxyplayer(getCurrentPosition(msec));
}

mm_status_t ProxyPlayerWrapper::getDuration(int * msec) const
{
    fenter();
    MMASSERT(msec!=NULL);
    call_Proxyplayer(getDuration(msec));
}

mm_status_t ProxyPlayerWrapper::setAudioStreamType(as_type_t type)
{
    MMLOGD("type: %d\n", type);
    call_Proxyplayer(setAudioStreamType(type));
}

mm_status_t ProxyPlayerWrapper::getAudioStreamType(as_type_t *type)
{
    fenter();
    MMASSERT(type!=NULL);
    call_Proxyplayer(getAudioStreamType(type));
}

mm_status_t ProxyPlayerWrapper::setLoop(bool loop)
{
    MMLOGD("loop: %d\n", loop);
    call_Proxyplayer(setLoop(loop));
}

bool ProxyPlayerWrapper::isLooping() const
{
    fenter();
    if ( !mPlayer ) {
        MMLOGE("not inited\n");
        return false;
    }
    return mPlayer->isLooping();
}

mm_status_t ProxyPlayerWrapper::setParameter(const MediaMetaSP & meta)
{
    fenter();

    call_Proxyplayer(setParameter(meta));
}

mm_status_t ProxyPlayerWrapper::getParameter(MediaMetaSP & meta)
{
    fenter();

    call_Proxyplayer(getParameter(meta));
}

mm_status_t ProxyPlayerWrapper::invoke(const MMParam * request, MMParam * reply)
{
    fleave_invalid(MM_ERROR_UNSUPPORTED);
}

mm_status_t ProxyPlayerWrapper::enableExternalSubtitleSupport(bool enable)
{
    fenter();

    call_Proxyplayer(enableExternalSubtitleSupport(enable));
}

mm_status_t ProxyPlayerWrapper::captureVideo()
{
    fenter();

    call_Proxyplayer(captureVideo());
}

}
