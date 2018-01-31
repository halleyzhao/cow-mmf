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

#include "CowPlayerDMWrapper.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_meta.h"
#include "media_attr_str.h"
#include "multimedia/component.h"
#include "json/json.h"


#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(CowPlayerDMWrapper)
DEFINE_LOGTAG(CowPlayerDMWrapper::CowListener)

#define fenter() MMLOGI("+\n")
#define fleave_nocode() do { MMLOGV("-\n"); return; } while(0)
#define fleave(_code) do { MMLOGV("-[%d]\n", _code); return (_code); }while(0)
#define fleave_invalid(_code) do { MMLOGE("-[%d]\n", _code); return (_code); }while(0)
#ifdef MM_LOG_OUTPUT_V
#define call_cowplayer(_func) do { \
    MMLOGV("calling %s\n", #_func);\
    mm_status_t cow_ret = mPlayer->_func; \
    MMLOGV("call over\n");\
    fleave(cow_ret); \
}while(0)
#else
#define call_cowplayer(_func) do {\
    return mPlayer->_func; \
}while(0)
#endif

#define SET_PLAYER_STATE(state) do { \
    pthread_mutex_lock(&mStateMutex); \
    mState = state;    \
    pthread_mutex_unlock(&mStateMutex); \
} while(0)

enum {
    STATE_IDLE,
    STATE_INIT,
    STATE_RES_PREPARING,
    STATE_PLAYER_PREPARING,
    STATE_PREPARED,
    STATE_STARTED,
    STATE_PAUSED,
    STATE_STOPED,
    STATE_PLAYBACK_COMPLETED,
    STATE_ERROR,
    STATE_RELEASE,
};



CowPlayerDMWrapper::CowPlayerDMWrapper(bool audioOnly, const void * userDefinedData/* = NULL*/)
    : mCowListener(NULL)
    , mAudioOnly(audioOnly)
{
    fenter();
    mPlayer = new CowPlayer(audioOnly);
    if ( !mPlayer ) {
        MMLOGE("failed to create player\n");
        fleave_nocode();
    }

    pthread_mutex_init(&mStateMutex, /*&attr*/NULL);
    pthread_cond_init(&mPrepareCond, NULL);
    mState = STATE_IDLE;

    mDMListener = new DeviceListener(this);
    yunos::DeviceManager* Mng = yunos::DeviceManager::getInstance();
    mClient = Mng->createClient("CowPlayerDMWrapper",mDMListener);
    mClient->setUageScene(yunos::DefaultType);
    mIsDataSet = false;

    fleave_nocode();
}

CowPlayerDMWrapper::~CowPlayerDMWrapper()
{
    fenter();

    if ( mPlayer ) {
        mPlayer->reset();
    }

    if (mClient != NULL) {
        delete mClient;
    }
    pthread_mutex_destroy(&mStateMutex);
    pthread_cond_destroy(&mPrepareCond);

    MM_RELEASE(mPlayer);
    MM_RELEASE(mCowListener);
    fleave_nocode();
}

mm_status_t CowPlayerDMWrapper::setPipeline(PipelineSP pipeline)
{
    call_cowplayer(setPipeline(pipeline));
}

mm_status_t CowPlayerDMWrapper::setListener(Listener * listener)
{
    fenter();
    if ( !mCowListener ) {
        mCowListener = new CowListener(this);
        if ( !mCowListener ) {
            MMLOGE("no mem\n");
            return MM_ERROR_NO_MEM;
        }
        mPlayer->setListener(mCowListener);
    } else {
        MMLOGW("already set\n");
    }
    return MediaPlayer::setListener(listener);
}

mm_status_t CowPlayerDMWrapper::setDataSource(const char * uri,
                        const std::map<std::string, std::string> * headers)
{
    MMLOGI("uri11: %s, headers: %p\n", uri, headers);
    Json::Reader reader;
    Json::Value value;

    if (headers != NULL) {
        std::map<std::string, std::string> *h = (std::map<std::string, std::string>*)headers;
        std::map<std::string, std::string>::iterator iter;
        for(iter = h->begin(); iter != h->end(); ++iter) {
            mHeader.insert(std::pair<std::string, std::string>(iter->first, iter->second));
        }
    }


    std::string dataUri;
    if (strncasecmp(uri, "dataset://", 10) == 0) {

        mIsDataSet = true;
        dataUri = uri + 10;
        MMLOGI("uri12: %s, headers: %p\n", dataUri.c_str(), headers);

        if (reader.parse(dataUri.c_str(), value)) {
            Json::Value dataJson =  value["data"];

            if (dataJson.isArray()) {
                for (unsigned int i = 0;i < dataJson.size(); i++) {
                    Json::Value item = dataJson[i];
                    if (item["type"].isInt() && item["type"].asInt() == 1) {
                        mUriHigh = item["url"].asString();
                    } else if (item["type"].isInt() && item["type"].asInt() == 2) {
                        mUriLow = item["url"].asString();
                    } 
                }

                if (dataJson.size() == 1) {
                    mUriLow = mUriLow;
                }
            }

        } else {
            mUriHigh = uri;
            mUriLow = uri;
        }

    } else {

        call_cowplayer(setDataSource(uri, headers));
    }



    //call_cowplayer(setDataSource(uri, headers));
    return 0;
}

mm_status_t CowPlayerDMWrapper::setDataSource(int fd, int64_t offset, int64_t length)
{
    MMLOGI("fd: %d, offset: %" PRId64 ", length: %" PRId64 "\n", fd, offset, length);
    call_cowplayer(setDataSource(fd, offset, length));
}

mm_status_t CowPlayerDMWrapper::setDataSource(const unsigned char * mem, size_t size)
{
    MMLOGI("mem: %p, size: %d\n", mem, size);
    fleave_invalid(MM_ERROR_UNSUPPORTED);
}

mm_status_t CowPlayerDMWrapper::setSubtitleSource(const char* uri)
{
    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("subtitle uri: %s\n", PRINTABLE_STR(uri));
    call_cowplayer(setSubtitleSource(uri));
}

mm_status_t CowPlayerDMWrapper::setDisplayName(const char* name)
{
    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("display name: %s\n", PRINTABLE_STR(name));
    call_cowplayer(setDisplayName(name));
}


mm_status_t CowPlayerDMWrapper::setNativeDisplay(void * display)
{
    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("display: %p\n", display);
    call_cowplayer(setNativeDisplay(display));
}

mm_status_t CowPlayerDMWrapper::setVideoDisplay(void * handle)
{
    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("handle: %p\n", handle);
    call_cowplayer(setVideoSurface(handle));
}

mm_status_t CowPlayerDMWrapper::setVideoSurfaceTexture(void * handle)
{
    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMLOGI("handle: %p\n", handle);
    call_cowplayer(setVideoSurface(handle, true));
}

mm_status_t CowPlayerDMWrapper::prepare()
{

    pthread_mutex_lock(&mStateMutex); 
    mState = STATE_RES_PREPARING;    
    mm_status_t ret = getResource();

    while (mState == STATE_RES_PREPARING) {
        pthread_cond_wait(&mPrepareCond, &mStateMutex);
    }
    pthread_mutex_unlock(&mStateMutex);

    return ret;
}

mm_status_t CowPlayerDMWrapper::prepareAsync()
{
    SET_PLAYER_STATE(STATE_RES_PREPARING);
    return getResource();
}

mm_status_t CowPlayerDMWrapper::reset()
{
    call_cowplayer(reset());
}

mm_status_t CowPlayerDMWrapper::setVolume(const VolumeInfo & volume)
{
    MMLOGI("left: %f, right: %f\n", volume.left, volume.right);
    call_cowplayer(setVolume(volume.left, volume.right));
}

mm_status_t CowPlayerDMWrapper::getVolume(VolumeInfo * volume) const
{
    call_cowplayer(getVolume(volume->left, volume->right));
}

mm_status_t CowPlayerDMWrapper::setMute(bool mute)
{
    //fleave_invalid(MM_ERROR_UNSUPPORTED);
    call_cowplayer(setMute(mute));
}

mm_status_t CowPlayerDMWrapper::getMute(bool * mute) const
{
    //fleave_invalid(MM_ERROR_UNSUPPORTED);
    call_cowplayer(getMute(mute));
}

mm_status_t CowPlayerDMWrapper::start()
{
    call_cowplayer(start());
}

mm_status_t CowPlayerDMWrapper::stop()
{
    return releaseResource();
    
}

mm_status_t CowPlayerDMWrapper::pause()
{
    call_cowplayer(pause());
}

bool CowPlayerDMWrapper::isPlaying() const
{
    fenter();
    return mPlayer->isPlaying();
}

mm_status_t CowPlayerDMWrapper::seek(int msec)
{
    MMLOGD("msec: %d\n", msec);
    call_cowplayer(seek(int64_t(msec)));
}

mm_status_t CowPlayerDMWrapper::getVideoSize(int *width, int * height) const
{
    fenter();

    if (mAudioOnly)
        fleave_invalid(MM_ERROR_UNSUPPORTED);

    MMASSERT(width!=NULL);
    MMASSERT(height!=NULL);
    int w;
    int h;
    mPlayer->getVideoSize(w, h);
    *width = w;
    *height = h;
    return MM_ERROR_SUCCESS;
}

mm_status_t CowPlayerDMWrapper::getCurrentPosition(int * msec) const
{
    mm_status_t status = MM_ERROR_SUCCESS;
    fenter();
    MMASSERT(msec!=NULL);
    int64_t position = -1;
    status = mPlayer->getCurrentPosition(position);
    ASSERT(status == MM_ERROR_SUCCESS);
    *msec = (int)position;
    fleave(MM_ERROR_SUCCESS);
}

mm_status_t CowPlayerDMWrapper::getDuration(int * msec) const
{
    mm_status_t status = MM_ERROR_SUCCESS;
    fenter();
    MMASSERT(msec!=NULL);
    int64_t duration = -1;
    status = mPlayer->getDuration(duration);
    *msec = (int) duration;
    fleave(status);
}

mm_status_t CowPlayerDMWrapper::setAudioStreamType(as_type_t type)
{
    MMLOGD("type: %d\n", type);
    call_cowplayer(setAudioStreamType((int)type));
}

mm_status_t CowPlayerDMWrapper::getAudioStreamType(as_type_t *type)
{
    mm_status_t status = MM_ERROR_SUCCESS;
    fenter();
    MMASSERT(type!=NULL);
    int audioStreamType = -1;
    status = mPlayer->getAudioStreamType(&audioStreamType);
    ASSERT(status == MM_ERROR_SUCCESS);
    *type = (as_type_t)audioStreamType;
    fleave(MM_ERROR_SUCCESS);
}

mm_status_t CowPlayerDMWrapper::setLoop(bool loop)
{
    MMLOGD("loop: %d\n", loop);
    call_cowplayer(setLoop(loop));
}

bool CowPlayerDMWrapper::isLooping() const
{
    fenter();
    if ( !mPlayer ) {
        MMLOGE("not inited\n");
        return false;
    }
    return mPlayer->isLooping();
}

mm_status_t CowPlayerDMWrapper::setParameter(const MediaMetaSP & meta)
{
    fenter();

    call_cowplayer(setParameter(meta));
}

mm_status_t CowPlayerDMWrapper::getParameter(MediaMetaSP & meta)
{
    fenter();

    call_cowplayer(getParameter(meta));
}

mm_status_t CowPlayerDMWrapper::invoke(const MMParam * request, MMParam * reply)
{
    mm_status_t cow_ret = MM_ERROR_UNKNOWN;

    if (!request || !reply)
        return MM_ERROR_INVALID_PARAM;

    int32_t tempInt = -1;
    cow_ret = request->readInt32(&tempInt);
    if (cow_ret != MM_ERROR_SUCCESS)
        return MM_ERROR_INVALID_PARAM;

    EInvokeKey invoKey = (EInvokeKey)tempInt;
    switch(invoKey) {
        case INVOKE_ID_GET_TRACK_INFO: {
            MMParamSP param = mPlayer->getTrackInfo();
            if (!param)
                return MM_ERROR_NO_MEDIA_TRACK;

            *reply = *(param.get());
            return MM_ERROR_SUCCESS;
        }
        case INVOKE_ID_SELECT_TRACK: {
            int32_t type = -1;
            int32_t index = -1;

            cow_ret = request->readInt32(&type);
            if (cow_ret != MM_ERROR_SUCCESS)
                return MM_ERROR_INVALID_PARAM;

            cow_ret = request->readInt32(&index);
            if (cow_ret != MM_ERROR_SUCCESS)
                return MM_ERROR_INVALID_PARAM;

            cow_ret = mPlayer->selectTrack(type, index);
            return cow_ret;
        }
        case INVOKE_ID_RESOURCE_PRIORITY: {
            int32_t type = 0;
	    cow_ret = (yunos::SceneType)request->readInt32(&type);
	    mClient->setUageScene((yunos::SceneType)type);
	    return cow_ret;
        }
        default:
            fleave_invalid(MM_ERROR_UNSUPPORTED);
    }
}

mm_status_t CowPlayerDMWrapper::captureVideo()
{
    fleave_invalid(MM_ERROR_UNSUPPORTED);
}

mm_status_t CowPlayerDMWrapper::pushData(MediaBufferSP & buffer)
{
    call_cowplayer(pushData(buffer));
}

#if 0
/*static */int CowPlayerDMWrapper::convertCowErrorCode(int code)
{
#define ITEM(_compat_code, _player_code)\
    case  cowplayer::_compat_code:\
        MMLOGD("%s -> %s", #_compat_code, #_player_code);\
        return _player_code

    switch ( code ) {
        ITEM(MEDIA_ERROR_UNKNOWN, MM_ERROR_UNKNOWN);
        ITEM(MEDIA_ERROR_SERVER_DIED, MM_ERROR_SERVER_DIED);
        ITEM(MEDIA_ERROR_NOT_VALID_FOR_PROGRESSIVE_PLAYBACK, MM_ERROR_NOT_VALID_FOR_PROGRESSIVE_PLAYBACK);
        default:
            MMLOGE("unrecognized message code: %d\n", code);
            return MM_ERROR_UNKNOWN;
    }
}

/*static */int CowPlayerDMWrapper::convertCowInfoCode(int code)
{
    MMLOGD("%d -> %d", code, code);
    return code;
}
#endif


mm_status_t CowPlayerDMWrapper::playVideoByHardware() 
{
    MMLOGV("calling %s\n", "prepare()");
    mm_log(MM_LOG_ERROR, "CowPlayerDMWrapper","hardware uri: %s", mUriHigh.c_str());

    if (mIsDataSet) {
        mPlayer->setDataSource(mUriHigh.c_str(), &mHeader);
    }
    ComponentFactory::appendPluginsXml("/etc/cow_plugins_hw_video.xml");
    mm_status_t cow_ret = mPlayer->prepare();
    SET_PLAYER_STATE(STATE_PLAYER_PREPARING);

    pthread_cond_signal(&mPrepareCond);
    MMLOGV("called %s\n", "prepare()");
    return cow_ret;
}

mm_status_t CowPlayerDMWrapper::playVideoBySoftware()
{
    MMLOGV("calling %s\n", "prepare()");
    mm_log(MM_LOG_ERROR, "CowPlayerDMWrapper","software uri: %s", mUriLow.c_str());

    
    if (mIsDataSet) {
        mPlayer->setDataSource(mUriLow.c_str(), &mHeader);
    }
    
    ComponentFactory::appendPluginsXml("/etc/cow_plugins_sw_video.xml");
    mm_status_t cow_ret = mPlayer->prepare();
    SET_PLAYER_STATE(STATE_PLAYER_PREPARING);

    pthread_cond_signal(&mPrepareCond);
    MMLOGV("MSG_INFO -1010 1");
    mListener->onMessage(MediaPlayer::Listener::MSG_INFO, -1010, 1, NULL);
    MMLOGV("called %s\n", "prepare()");
    return cow_ret;
}

void CowPlayerDMWrapper::onPrepare() {
    SET_PLAYER_STATE(STATE_PREPARED);
    pthread_cond_signal(&mPrepareCond);
}


mm_status_t CowPlayerDMWrapper::getResource() 
{
    yunos::DeviceRequire require;
    require.addComponent(yunos::ComponentVideoDecoder);
    require.addComponent(yunos::ComponentVideoRender);
    mClient->requestControls(0, require, 4000);

    return 0;
}


mm_status_t CowPlayerDMWrapper::releaseResource()
{

    mm_status_t cow_ret = mPlayer->stop();
    mClient->releaseControls();
    return cow_ret;
}


mm_status_t CowPlayerDMWrapper::enableExternalSubtitleSupport(bool enable)
{
    fenter();

    call_cowplayer(enableExternalSubtitleSupport(enable));
}

CowPlayerDMWrapper::CowListener::CowListener(CowPlayerDMWrapper * watcher)
                        : mWatcher(watcher)
{
    MMASSERT(watcher != NULL);
}

CowPlayerDMWrapper::CowListener::~CowListener()
{
    
}

void CowPlayerDMWrapper::CowListener::onMessage(int eventType, int param1, int param2, const MMParamSP param)
{
    MMLOGD("eventType: %d\n", eventType);
    switch ( eventType ) {
        // FIXME: emit the message from cowplayer
        #if 0
        case Component::MEDIA_NOP:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_NOP, 0, 0, NULL);
            break;
        #endif
        // FIXME: not necessary to translate case by case
        case Component::kEventPrepareResult:
            mWatcher->onPrepare();
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_PREPARED, param1, 0, NULL);
            break;
        case Component::kEventEOS:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_PLAYBACK_COMPLETE, param1, 0, NULL);
            break;
        case Component::kEventInfoBufferingUpdate:
            {
                int percent = param1;
                MMLOGV("percent: %d\n", percent);
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_BUFFERING_UPDATE, percent, 0, NULL);
            }
            break;
        case Component::kEventSeekComplete:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_SEEK_COMPLETE, param1, 0, NULL);
            break;
        case Component::kEventGotVideoFormat:
            {
                int w = param1;
                int h = param2;
                MMLOGV("w: %d, h: %d\n", w,h);
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_SET_VIDEO_SIZE, w, h, NULL);
            }
            break;
        case Component::kEventStartResult:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_STARTED, param1, 0, NULL);
            break;
        case Component::kEventPaused:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_PAUSED, param1, 0, NULL);
            break;
        case Component::kEventStopped:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_STOPPED, param1, 0, NULL);
            break;
        #if 0 // FIXME
        case Component::MEDIA_SKIPPED:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_SKIPPED, 0, 0, NULL);
            break;
        case Component::MEDIA_TIMED_TEXT:
            mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_TIMED_TEXT, 0, 0, NULL);
            break;
        #endif
        case Component::kEventError:
            {
                // FIXME
                // int c = convertCowErrorCode(getErrorType());
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_ERROR, param1, 0, NULL);
            }
            break;
        case Component::kEventMediaInfo:
            {
                // FIXME
                // int i = convertCowInfoCode(getInfoType());
                int i = -1;
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO, i, 0, NULL);
            }
            break;
        #if 0 // FIXME
        case Component::MEDIA_SUBTITLE_DATA:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_SUBTITLE_UPDATED, 0, 0, NULL);
            }
            break;
        #endif
        case Component::kEventInfo:
            switch (param1)
            {
                case Component::kEventInfoVideoRenderStart:
                    {
                        INFO("try to send TYPE_MSG_FIRST_FRAME_TIME msg\n");
                        // mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO, 306/* TYPE_MSG_FIRST_FRAME_TIME */, 0, NULL);
                        mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO_EXT, 306/* TYPE_MSG_FIRST_FRAME_TIME */, 0, param.get());
                        // info type 306 will remove later
                        mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO_EXT, MediaPlayer::Listener::INFO_RENDERING_START, 0, param.get());
                    }
                break;
                default:
                    mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_INFO_EXT, param1, 0, param.get());
                break;
            }
            break;
        case Component::kEventUpdateTextureImage:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_UPDATE_TEXTURE_IMAGE, param1, 0, NULL);
            }
            break;
        case Component::kEventInfoDuration:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_DURATION_UPDATE, param1, 0, NULL);
            }
            break;
        case Component::kEventRequestIDR:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_VIDEO_REQUEST_IDR, param1, 0, NULL);
            }
            break;
        case Component::kEventVideoRotationDegree:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_VIDEO_ROTATION_DEGREE, param1, 0, NULL);
            }
            break;
        case Component::kEventInfoSubtitleData:
            {
                mWatcher->mListener->onMessage(MediaPlayer::Listener::MSG_SUBTITLE_UPDATED, param1, 0, param.get());
            }
            break;
        default:
            MMLOGE("unrecognized message: %d\n", eventType);
    }
}


void CowPlayerDMWrapper::DeviceListener::onRespond(DeviceClient *client, int32_t id, int32_t result,
                    const DeviceUsbComponent *getDevices, int32_t size,
                    const DeviceComponent *lostComponents, int32_t len,
                    const DeviceUsbComponentType* lostDevices, int32_t devsize) 
{
     if (result == yunos::RespondSuccess) {
//         mPlayer->playVideoBySoftware();
         mPlayer->playVideoByHardware();
     } else {
         mPlayer->playVideoBySoftware();
     }


}


void CowPlayerDMWrapper::DeviceListener::onReleaseDevices(DeviceClient *client)
{
    mPlayer->releaseResource();
    mPlayer->mListener->onMessage(MediaPlayer::Listener::MSG_INFO, -1010, 2, NULL);
}




}


