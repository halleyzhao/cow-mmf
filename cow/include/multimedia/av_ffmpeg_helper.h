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
#ifndef av_ffmpeg_helper_h
#define av_ffmpeg_helper_h


#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C" {
#endif
#include <libavcodec/version.h>
#ifdef __cplusplus
}
#endif


#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 28, 1)
    #define av_frame_alloc avcodec_alloc_frame
    #if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(54, 28, 0)
        #define av_frame_free avcodec_free_frame
    #else
        #define av_frame_free av_freep
    #endif
#endif


#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 8, 0)
    #define av_free_packet av_packet_unref
#endif


#endif // av_ffmpeg_helper_h
