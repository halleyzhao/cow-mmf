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

#include <multimedia/mediaplayer.h>

#ifdef __USEING_DEVICE_MANAGER__
#include "CowPlayerDMWrapper.h"
#else
#include "CowPlayerWrapper.h"
#endif

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
#include <multimedia/mm_cpp_utils.h>

#ifdef __USING_PLAYER_SERVICE__
#include "ProxyPlayerWrapper.h"
#endif

namespace YUNOS_MM {

DEFINE_LOGTAG(MediaPlayer)

static MediaPlayer* createCowPlayer(MediaPlayer::PlayerType type) {
    if (type == MediaPlayer::PlayerType_DEFAULT) {
#ifdef __USEING_DEVICE_MANAGER__
        return new CowPlayerDMWrapper();
#else
        return new CowPlayerWrapper();
#endif
    }

#ifdef __USEING_DEVICE_MANAGER__
        return new CowPlayerDMWrapper(type);
#else
        return new CowPlayerWrapper(type);
#endif
}

/*static*/ MediaPlayer * MediaPlayer::create(PlayerType type/* = PlayerType_DEFAULT*/, const void * userDefinedData/* = NULL*/)
{
    MMLOGI("+type: %d\n", type);

#ifdef __USING_PLAYER_SERVICE__
    const char* key = "host.media.player.type";
    std::string envStr = mm_get_env_str(key, NULL);
    const char* value = envStr.c_str();
#endif

    switch ( type ) {
        case PlayerType_DEFAULT:
#ifdef __USING_PLAYER_SERVICE__
            INFO("player type: %s", value);
            if (strncasecmp(value, "local", 5))
                return new ProxyPlayerWrapper();
#endif
        case PlayerType_COW:
            return createCowPlayer(PlayerType_DEFAULT);
        case PlayerType_COWAudio:
#ifdef __USING_PLAYER_SERVICE__
            INFO("player type: %s", value);
            if (strncasecmp(value, "local", 5))
                return new ProxyPlayerWrapper(PlayerType_COWAudio);
#endif
            return createCowPlayer(PlayerType_COWAudio);
#ifdef __USING_PLAYER_SERVICE__
        case PlayerType_PROXY:
            return new ProxyPlayerWrapper();
        case PlayerType_PROXY_Audio:
            return new ProxyPlayerWrapper(PlayerType_COWAudio);
#endif
        case PlayerType_LPA:
#ifdef __USING_PLAYER_SERVICE__
            INFO("player type: %s", value);
            if (strncasecmp(value, "local", 5))
                return new ProxyPlayerWrapper(PlayerType_LPA);
#endif
            return createCowPlayer(PlayerType_LPA);
        case PlayerType_LOCAL_Audio:
            return createCowPlayer(PlayerType_COWAudio);
        default:
            MMLOGE("invalid type: %d\n", type);
            return NULL;
    }

}

/*static*/ void MediaPlayer::destroy(MediaPlayer * player)
{
    MMLOGI("+\n");
    MM_RELEASE(player);
}

MediaPlayer::MediaPlayer() : mListener(NULL)
{
}

MediaPlayer::~MediaPlayer()
{
}

mm_status_t MediaPlayer::setListener(Listener * listener)
{
    MMLOGI("+\n");
    mListener = listener;
    return MM_ERROR_SUCCESS;
}

mm_status_t MediaPlayer::setAudioConnectionId(const char * connectionId)
{
    MMLOGW("unsupported\n");
    return MM_ERROR_UNSUPPORTED;
}

const char * MediaPlayer::getAudioConnectionId() const
{
    MMLOGW("unsupported\n");
    return "";
}

}
