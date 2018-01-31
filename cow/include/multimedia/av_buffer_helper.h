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
#ifndef av_buffer_helper_h
#define av_buffer_helper_h

#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#ifdef __cplusplus
}
#endif
#include "multimedia/media_buffer.h"

namespace YUNOS_MM {

class AVBufferHelper {
  public:
    // releasePkt flag indicates whether AVPacket will be automatically freed when MediaBufferSP reaches the end of life cycle
    static MediaBufferSP createMediaBuffer(AVPacket *pkt, bool releasePkt = false);
    static MediaBufferSP createMediaBuffer(AVFrame *frame, bool isAudio = false, bool releaseFrame = false);
    static bool convertToAVPacket(MediaBufferSP mediaBuffer, AVPacket **pkt);
    static bool convertToAVFrame(MediaBufferSP mediaBuffer, AVFrame **frame);


  private:
    static void AVBufferDefaultFree(void *opaque, uint8_t *data);
};

} // end of namespace YUNOS_MM

#endif // av_buffer_helper_h
