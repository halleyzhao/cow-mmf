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
#include <multimedia/webrtc/video_decode_plugin.h>
#include "video_decode_plugin_imp.h"

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>


namespace YUNOS_MM {

DEFINE_LOGTAG(VideoDecodePlugin);

/*static */woogeen::base::VideoDecoderInterface*  VideoDecodePlugin::create(void * surface, int surfaceType)
{
    try {
        VideoDecodePluginImp * p = new VideoDecodePluginImp(surface, surfaceType/* platform dependent surface type */);
        return p;
    } catch (...) {
        MMLOGE("no mem\n");
        return NULL;
    }
}

/*static */void VideoDecodePlugin::destroy(woogeen::base::VideoDecoderInterface* plugin)
{
    if (plugin) {
        delete plugin;
    }
}

}

