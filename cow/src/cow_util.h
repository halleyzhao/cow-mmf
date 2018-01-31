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

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <multimedia/mm_types.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mm_errors.h>
#include <multimedia/media_buffer.h>



#ifndef cow_util_h
#define cow_util_h

namespace YUNOS_MM {

bool isAnnexBByteStream(const uint8_t *source, size_t len);
mm_status_t getNextNALUnit(
        const uint8_t **_data, size_t *_size,
        const uint8_t **nalStart, size_t *nalSize,
        bool startCodeFollows);


MediaBufferSP MakeAVCCodecExtradata(const uint8_t *data, size_t size);
void h264_avcC2ByteStream(uint8_t *dstBuf, uint8_t *sourceBuf, const int32_t length);

}

#endif //cow_util_h

