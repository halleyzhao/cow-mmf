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

#ifndef camera_common_h
#define camera_common_h

#include <multimedia/mm_types.h>
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/mm_camera_compat.h"

namespace YUNOS_MM {

enum RecorderType {
    RecorderType_DEFAULT,
    RecorderType_COW,
    RecorderType_BINDER_PROXY,
    RecorderType_COWAudio,
    RecorderType_PROXY,
    RecorderType_RTSP
};


enum RecorderUsage {
    RU_None             = 0,
    RU_VideoRecorder    = 1 << 0,
    RU_AudioRecorder    = 1 << 1,
    RU_RecorderMask     = RU_VideoRecorder | RU_AudioRecorder,
};

}

namespace YunOSCameraNS {
    class VideoCapture;
    class RecordingProxy;
}
using YunOSCameraNS::VideoCapture;
using YunOSCameraNS::RecordingProxy;


#endif // camera_common_h

