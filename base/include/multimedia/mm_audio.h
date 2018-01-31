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

#ifndef __yunos_mm_audio_H
#define __yunos_mm_audio_H

#ifdef MM_USE_AUDIO_VERSION
  #if MM_USE_AUDIO_VERSION == 10
    #include <audio/hal/audio.h>
  #else
    #include <yunhal/Audio.h>
  #endif
#else
    #include <multimedia/audio.h>
#endif


static const uint32_t chmasks[8] ={
    ADEV_CHANNEL_OUT_MONO,
    ADEV_CHANNEL_OUT_STEREO,
    ADEV_CHANNEL_OUT_STEREO | ADEV_CHANNEL_OUT_FRONT_CENTER,
    ADEV_CHANNEL_OUT_QUAD,
    ADEV_CHANNEL_OUT_QUAD | ADEV_CHANNEL_OUT_FRONT_CENTER,
    ADEV_CHANNEL_OUT_5POINT1,
    ADEV_CHANNEL_OUT_5POINT1 | ADEV_CHANNEL_OUT_BACK_CENTER,
    ADEV_CHANNEL_OUT_7POINT1
};
static inline adev_channel_mask_t adev_get_out_mask_from_channel_count(uint32_t channel_count)
{
    if(channel_count <1 ||channel_count > 8)
        return 0;
    int id = channel_count-1;
    return chmasks[id];
}

static inline adev_channel_mask_t adev_get_in_mask_from_channel_count(uint32_t channel_count)
{
    if(channel_count == 1)
        return ADEV_CHANNEL_IN_MONO;
    else if(channel_count == 2)
        return ADEV_CHANNEL_IN_STEREO;
    else
        return 0;
}

static inline size_t adev_bytes_per_sample(snd_format_t format)
{
    size_t size = 0;

    switch (format) {
    case SND_FORMAT_PCM_32_BIT:
    case SND_FORMAT_PCM_8_24_BIT:
        size = sizeof(int32_t);
        break;
    case SND_FORMAT_PCM_16_BIT:
        size = sizeof(int16_t);
        break;
    case SND_FORMAT_PCM_8_BIT:
        size = sizeof(uint8_t);
        break;
    default:
        break;
    }
    return size;
}

#endif // __yunos_mm_audio_H
