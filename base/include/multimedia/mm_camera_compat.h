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

#ifndef __yunos_mm_camera_H
#define __yunos_mm_camera_H

#include <videocapture/VideoCapture.h>
#ifdef VC_FRAMEWORK_API_VERSION_BASIC
#undef MM_USE_CAMERA_VERSION
#define MM_USE_CAMERA_VERSION 30
#endif

#if MM_USE_CAMERA_VERSION>=20
    #include <yunhal/VCMSHMem.h>
    #include <yunhal/VideoCaptureDevice.h>
    #include <videocapture/VideoCaptureUtils.h>
    #if MM_USE_CAMERA_VERSION>=30
        #include <yunhal/VideoCaptureParam.h>
    #else
        #include <yunhal/VideoCaptureProperties.h>
    #endif
#else
    #include <videocapture/VCMSHMem.h>
    #include <videocapture/hal/VideoCaptureDevice.h>
    #include <videocapture/hal/VideoCaptureProperties.h>
#endif
#include <videocapture/VideoCaptureCallback.h>

// for camera module namespace change
#if MM_USE_CAMERA_VERSION>=30
#define YunOSCameraNS yunos
#else
#define YunOSCameraNS YunOS
#endif

#endif // __yunos_mm_camera_H
