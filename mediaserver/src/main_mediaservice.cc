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

#if defined PLUGIN_HAL && PLUGIN_HAL_CVG && 0
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <MediaServiceBinder.h>
#endif

#include "MediaService.h"
#ifdef HAVE_SYSSOUND
#include <multimedia/SysSoundService.h>
#endif

#ifdef HAVE_PLAYBACK_CHANNEL_SERVICE
#include <multimedia/PlaybackChannelService.h>
#endif

#ifdef __USING_OMX_SERVICE__
#include "omx_service.h"
#endif

#include "multimedia/mm_debug.h"

using namespace YUNOS_MM;
#ifdef __USING_OMX_SERVICE__
using namespace YunOSMediaCodec;
#endif

MM_LOG_DEFINE_MODULE_NAME("MediaServiceMain");

int main(int argc, char** argv) {

#ifdef HAVE_SYSSOUND
    SysSoundServicePtr s = SysSoundService::publish();
    if (!s) {
        MMLOGE("failed to publish SysSoundService\n");
        // return 0;
    }
#endif

#ifdef HAVE_PLAYBACK_CHANNEL_SERVICE
    PlaybackChannelServiceSP service = PlaybackChannelService::publish();
    if (!service) {
        MMLOGE("failed to publish PlaybackChannelService\n");
        // return 0;
    }
#endif

#ifdef __USING_OMX_SERVICE__
    OMXServiceSP omx = OMXService::publish();
    if (!omx) {
        MMLOGE("failed to publish OMXService\n");
        // return 0;
    }
#endif

    if (MediaService::createService(true))
    //if (MediaService::createService())
        INFO("yunos media service is started");
    else
        WARNING("fail to start yunos media service");

#if defined PLUGIN_HAL && PLUGIN_HAL_CVG && 0
    sp<ProcessState> proc(ProcessState::self());
    sp<IServiceManager> sm = defaultServiceManager();
    INFO("ServiceManager: %p", sm.get());
    MediaServiceBinder::getInstance();
    //CameraService::instantiate();
    ProcessState::self()->startThreadPool();
    IPCThreadState::self()->joinThreadPool();
#endif

}
