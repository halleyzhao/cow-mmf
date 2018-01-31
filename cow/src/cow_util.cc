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
#include <arpa/inet.h>

#include <multimedia/mm_types.h>
#include "multimedia/mm_debug.h"
#include "cow_util.h"


namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("media-util");

bool isAnnexBByteStream(const uint8_t *source, size_t len)
{
    uint32_t offset = 0;
    if (source == NULL || len < 3)
        return false;

    // Skip any number of leading 0x00.
    while (offset < len && !source[offset])
        offset++;

    if (offset == len)
        return false;

    // A valid startcode consists of at least two 0x00 bytes followed by 0x01.
    if (offset < 2 || 0x01 != source[offset])
        return false;

    return true;
}

mm_status_t getNextNALUnit(const uint8_t **sourceData, size_t *sourceSize,
                           const uint8_t **startNal, size_t *sizeNal,
                           bool followsStartCode)
{
    // Check param valid
    if (sourceData == NULL || sourceSize == NULL || *sourceSize == 0 ||
        startNal == NULL || sizeNal == NULL) {
        VERBOSE("invalid param\n");
        return MM_ERROR_INVALID_PARAM;
    }
    const uint8_t *source = *sourceData;
    uint32_t len = *sourceSize;
    *startNal = NULL;
    *sizeNal = 0;

    // Skip any number of leading 0x00.
    uint32_t offset = 0;
    while (offset < len && 0x00 == source[offset]) {
        ++offset;
    }

    if (offset == len) {
        ERROR("len is too small, offset %d, len %d\n", offset, len);
        return MM_ERROR_INVALID_PARAM;
    }

    // A valid startcode consists of at least two 0x00 bytes followed by 0x01.
    if (offset < 2 || 0x01 != source[offset]) {
        ERROR("wrong stream, offset %d, source[offset] 0x%0x\n", offset, source[offset]);
        return MM_ERROR_MALFORMED;
    }

    uint32_t start = ++offset;
    do {
        while (offset < len && 0x01 != source[offset]) { // need further check for NAL ending: come to data end, or meet another NAL unit
            ++offset;
        }

        if (offset == len) { // data ending
            if (followsStartCode) {
                offset = len + 2; // pseudo ending with another NALU, minus 2 will be done below
                break;
            }
            return MM_ERROR_INVALID_PARAM;
        }

        if (0x00 == source[offset - 1] && 0x00 == source[offset - 2]) { // find next NALU
            break;
        }
    } while(++offset);

    uint32_t end = offset - 2;
    // strip trailer zero (or said, the leading zero before next NALU)
    int32_t tmp = 0;
    while (source[end - 1] == 0x00 &&
        start + 1 < end &&
        ++tmp < 2) {
        --end;
    }

    // update information of current NALU
    *startNal = &source[start];
    *sizeNal = end - start;

    // update remaining data information
    if (len > offset + 2) {
        *sourceData = source + offset - 2;
        *sourceSize = len - offset + 2;
    } else {
        *sourceData = NULL;
        *sourceSize = 0;
    }

    return MM_ERROR_SUCCESS;
}

static int makeExtraData(size_t spsSize,
                   const uint8_t *sps,
                   size_t ppsSize,
                   const uint8_t *pps,
                   uint8_t *extraData)
{

    uint8_t *data = extraData;
    int extraDataSize;

    if (!sps || !pps || !extraData)
        return 0;

    /*get  sps startpos need skip 6 bytes:
    8 bit for configurationVersion
    8 bit for AVCProfileIndication
    8 bit for profile_compatibility
    8 bit for AVCLevelIndication
    6 bit for reserved
    2 bit for lengthSizeMinusOne
    3 bit for reserved
    5 bit for numOfSequenceParameterSets
    */

    memset(data, 0xff, 6);

    // TODO support other profile and level if needed
    *data = 1;            // MP4 avcC, configurationVersion
    *(data + 1) = 0x42;   // profile baseline
    *(data + 2) = 0;      //
    *(data + 3) = 0x28;   // level4
    *(data + 4) = (0x3f << 2) | 3;   // lengthSizeMinusOne == 4 bytes
    *(data + 5) = 0xe0 | 1;   // Number of sps

    data += 6;

    *((int16_t *)data) = htons(spsSize);  //sequenceParameterSetLength
    data += 2;
    memcpy(data, sps, spsSize);
    data += spsSize;

    //pps
    *data++ = 1;                          // Number of pps
    *((int16_t *)data) = htons(ppsSize);  // pictureParameterSetLength
    data += 2;
    memcpy(data, pps, ppsSize);
    data += ppsSize;

    extraDataSize = data - extraData;

    return extraDataSize;
}

static bool releaseBufferHelper(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}


MediaBufferSP MakeAVCCodecExtradata(const uint8_t *data, size_t size)
{
    const uint8_t *sps = NULL, *pps = NULL;
    size_t spsSize, ppsSize;

    hexDump(data, size, 16);

    MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!mediaBuf) {
        ERROR("no mem\n");
        return mediaBuf;
    }

    // TODO parse all NALs and check nal_type
    getNextNALUnit(&data, &size, &sps, &spsSize, true);
    getNextNALUnit(&data, &size, &pps, &ppsSize, true);
    if (!sps || !pps) {
        ERROR("find no sps or pps\n");
        return mediaBuf;
    }

    uint8_t *extraData = new uint8_t[spsSize + ppsSize + 64];
    if (!extraData) {
        ERROR("no mem %d\n", spsSize + ppsSize + 64);
        return mediaBuf;
    }


    int extraDataSize = makeExtraData(spsSize, sps, ppsSize, pps, extraData);

    DEBUG("make extradata(%d bytes) for sps(%d bytes) and pps(%d bytes)",
         extraDataSize, spsSize, ppsSize);

    mediaBuf->setBufferInfo((uintptr_t *)&extraData, NULL, &extraDataSize, 1);
    mediaBuf->addReleaseBufferFunc(releaseBufferHelper);
    hexDump(extraData, extraDataSize, 16);

    return mediaBuf;

}


void h264_avcC2ByteStream(uint8_t *dstBuf, uint8_t *sourceBuf, const int32_t length)
{
// assume length filed has 4 bytes, actually it can be configtured to 1/2/4
// 14496-15 5.4.1.2 lengthSizeMinusOne
#define NAL_LENGTH_OFFSET 4
#define START_CODE 0x01000000

    int32_t offset = 0;
    if (!dstBuf)
        dstBuf = sourceBuf;
    else
        memcpy (dstBuf, sourceBuf, length);

    while (offset+4 < length) {
        uint32_t *ptr = (uint32_t*)(dstBuf+offset);
        uint32_t nalLength = ntohl(*ptr);
        *ptr = START_CODE;

        offset +=  NAL_LENGTH_OFFSET + nalLength;
    }
}

}
