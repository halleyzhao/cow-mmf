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

#ifndef __webrtc_video_encode_plugin_H
#define __webrtc_video_encode_plugin_H

#include <stdio.h>
#include <vector>

#include <stdint.h>
#include <woogeen/base/videoencoderinterface.h>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

namespace YUNOS_MM {

class VideoEncodePlugin : public woogeen::base::VideoEncoderInterface {
public:
    static woogeen::base::VideoEncoderInterface * create(
#ifdef WEBRTC_MEDIA_API_2
        void* camera, void* surface
#endif
    );
    // there is already VideoEncoderInterface->Release(), seems this destroy() is unnecessary.
    static void destroy(woogeen::base::VideoEncoderInterface * plugin);

protected:
    VideoEncodePlugin(){}
    virtual ~VideoEncodePlugin(){}
private:
    static woogeen::base::VideoEncoderInterface* mMasterEncoder;
    DECLARE_LOGTAG()
};

}
#endif
