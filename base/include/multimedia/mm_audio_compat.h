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

#ifndef __yunos_mm_audio_compat_H
#define __yunos_mm_audio_compat_H
#ifdef __AUDIO_CRAS__
#include <audio/AudioManager.h>
#include <audio/AudioRender.h>
#include <audio/AudioCapture.h>
#include "multimedia/mm_audio.h"
#elif defined(__AUDIO_PULSE__)
#include <pulse/error.h>
#include <pulse/sample.h>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
//#include <pulse/ext-audiotrack.h>
#endif
typedef uint32_t audio_channel_mask_t;
#ifdef YUNOS_AUDIO_INTERFACE_VERSION
#undef MM_USE_AUDIO_VERSION
#define MM_USE_AUDIO_VERSION 30
#endif

// for audio module namespace change
#if MM_USE_AUDIO_VERSION>=30
#define YunOSAudioNS yunos
#else
#define YunOSAudioNS YunOS
#endif

#endif // __yunos_mm_audio_compat_H
