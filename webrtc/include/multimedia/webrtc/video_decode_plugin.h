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

#ifndef __webrtc_video_decode_plugin_H
#define __webrtc_video_decode_plugin_H

#include <stdint.h>
#include <woogeen/base/videodecoderinterface.h>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mmlistener.h>
#include <multimedia/mm_buffer.h>

namespace YUNOS_MM {

class VideoDecodePlugin : public woogeen::base::VideoDecoderInterface {
public:
    static woogeen::base::VideoDecoderInterface* create(
#ifdef WEBRTC_MEDIA_API_2
        void * surface, int surfaceType = 0/* platform dependent surface type */
#endif
    );
    static void destroy(woogeen::base::VideoDecoderInterface* plugin);

public:
    virtual ~VideoDecodePlugin(){}

protected:
    VideoDecodePlugin(){}

    MM_DISALLOW_COPY(VideoDecodePlugin)
    DECLARE_LOGTAG()
};

}
#endif
