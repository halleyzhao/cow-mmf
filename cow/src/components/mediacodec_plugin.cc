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

#include <multimedia/mm_debug.h>
#include "mediacodec_decoder.h"
#include "mediacodec_encoder.h"

MM_LOG_DEFINE_MODULE_NAME("MCP")

extern "C" YUNOS_MM::Component *createComponent(const char* mime, bool encode) {
    YUNOS_MM::Component *comp;

    if (encode)
        comp = new YUNOS_MM::MediaCodecEnc(mime);
    else
        comp = new YUNOS_MM::MediaCodecDec(mime);

    INFO("MediaCodec Component (encode is %d) is created", encode);

    return comp;
}

extern "C" void releaseComponent(YUNOS_MM::Component *component) {
    INFO("MediaCodec Component is deleted");
    delete component;
}
