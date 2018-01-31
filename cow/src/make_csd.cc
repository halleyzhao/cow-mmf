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
#include <multimedia/media_attr_str.h>
#include <multimedia/media_meta.h>
#include <cow_util.h>
#include <make_csd.h>

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(StreamCSDMaker)
DEFINE_LOGTAG(AVCCSDMaker)
DEFINE_LOGTAG(AACCSDMaker)
DEFINE_LOGTAG(HEVCCSDMaker)


#define FENTER() INFO(">>>\n")
#define FLEAVE() do {INFO(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

static MediaBufferSP nilMediaBuffer;

StreamCSDMaker::StreamCSDMaker(MediaMetaSP &metaData) : mIsInited(false)
{
    mMediaMeta = metaData->copy();

    FENTER();
    FLEAVE();
}


StreamCSDMaker::StreamCSDMaker() : mIsInited(false)
{
    FENTER();
    FLEAVE();
}



StreamCSDMaker::~StreamCSDMaker()
{
    FENTER();
    FLEAVE();
}

//for encoder
/*virtual*/MediaBufferSP StreamCSDMaker::getExtraDataFromRawData(uint8_t *data, size_t size)
{
    MediaBufferSP buffer = createMediaBufferCSD(data, size);
    if (!buffer) {
        return nilMediaBuffer;
    }

    mm_status_t status = getExtraDataFromMediaBuffer(buffer);
    if (status != MM_ERROR_SUCCESS) {
        return nilMediaBuffer;
    }

    return buffer;
}

/*virtual*/mm_status_t StreamCSDMaker::getExtraDataFromMediaBuffer(MediaBufferSP &mediaBuf)
{
    FENTER();
    uint8_t *buf = NULL;
    int32_t offset;
    size_t size;

    if (!mediaBuf)
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);

    MediaMetaSP meta = mediaBuf->getMediaMeta();
    if (!meta)
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);

    size = (size_t)mediaBuf->size();
    if (!mediaBuf->getBufferInfo((uintptr_t *)&buf, &offset, NULL, 1) || !buf)
        FLEAVE_WITH_CODE(MM_ERROR_UNKNOWN);

    buf += offset;
    size -= offset;
    INFO("extractExtraData, buf %p, size %d, offset:%d\n", buf, size, offset);

    // hexDump(buf, size, 16);

    meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, buf, size);

    return MM_ERROR_SUCCESS;
}


//for decoder
int32_t StreamCSDMaker::getCSDCount()
{
    MMAutoLock locker(mLock);
    if (!mIsInited) {
        if (extractCSD() == MM_ERROR_SUCCESS) {
            mIsInited = true;
        }
    }

    return mMediaBufferCSD.size();
}


MediaBufferSP StreamCSDMaker::getMediaBufferFromCSD(int32_t index)
{
    MMAutoLock locker(mLock);
    MediaBufferSP mediaBuffer;
    if (!mIsInited) {
        if (extractCSD() == MM_ERROR_SUCCESS) {
            mIsInited = true;
        }
    }

    if (index >= (int32_t)mMediaBufferCSD.size()) {
        ERROR("invalid index %d", index);
        return mediaBuffer;
    }


    return mMediaBufferCSD[index];
}

ByteBufferSP StreamCSDMaker::getByteBufferFromCSD(int32_t index)
{
    MMAutoLock locker(mLock);
    ByteBufferSP byteBuffer;
    if (!mIsInited) {
        if (extractCSD() == MM_ERROR_SUCCESS) {
            mIsInited = true;
        }
    }

    if (index >= (int32_t)mByteBufferCSD.size()) {
        ERROR("invalid index %d", index);
        return byteBuffer;
    }

    return mByteBufferCSD[index];
}


mm_status_t StreamCSDMaker::extractCSD()
{
    uint8_t *data = NULL;
    int32_t size = 0;

    FENTER();
    mMediaBufferCSD.clear();
    mByteBufferCSD.clear();

    if (mMediaMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) {
        if (data != NULL && size > 0) {
            uint8_t *mf = NULL;
            mf = new uint8_t[size];
            if (mf == NULL) {
                ERROR("no mem, size %d", size);
                return MM_ERROR_NO_MEM;
            }
            memcpy(mf, data, size);
            hexDump(data, size, 16);
            MediaBufferSP mediaBuffer = createMediaBufferCSD(mf, size);
            if (mediaBuffer) {
                mMediaBufferCSD.push_back(mediaBuffer);
            }

            uint8_t *bf = NULL;
            bf = new uint8_t[size];
            if (bf == NULL) {
                ERROR("no mem, size %d", size);
                return MM_ERROR_NO_MEM;
            }
            memcpy(bf, data, size);
            ByteBufferSP byteBuffer = createByteBufferCSD(bf, size);
            if (byteBuffer) {
                mByteBufferCSD.push_back(byteBuffer);
            }
        }
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

/*static */bool StreamCSDMaker::releaseMediaBufferCSD(MediaBuffer* mediaBuffer)
{
    if ( !mediaBuffer->isFlagSet(MediaBuffer::MBFT_CodecData) ) {
        ERROR("not codecdata\n");
        return false;
    }

    uint8_t * buffers;
    int32_t offsets;
    int32_t strides;
    if ( !mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
        ERROR("failed to get bufferinfo\n");
        return false;
    }

    MM_RELEASE_ARRAY(buffers);
    return true;
}

/*static */MediaBufferSP StreamCSDMaker::createMediaBufferCSD(uint8_t *data, int32_t size)
{
    FENTER();
    MediaBufferSP buffer;
    if (data != NULL && size > 0) {
        //create for mediaBuffer
        buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
        if (!buffer) {
            ERROR("no mem\n");
            return buffer;
        }

        buffer->setFlag(MediaBuffer::MBFT_CodecData);

        buffer->setBufferInfo((uintptr_t*)&data, NULL, NULL, 1);
        buffer->setSize((int64_t)size);
        buffer->setDts(0);
        buffer->setPts(0);
        buffer->setDuration(0);

        buffer->addReleaseBufferFunc(releaseMediaBufferCSD);
    }

    return buffer;
}


/*static */bool StreamCSDMaker::releaseByteBufferCSD(MediaMeta::ByteBuffer *byteBuf)
{
    if (byteBuf != NULL) {
        VERBOSE("releaseByteBuffer: byteBuf->data %p", byteBuf->data);
        MM_RELEASE_ARRAY(byteBuf->data);

        delete byteBuf;

    }
    return true;
}


/*static */ByteBufferSP StreamCSDMaker::createByteBufferCSD(uint8_t *data, int32_t size)
{
    FENTER();
    ByteBufferSP byteBufferSP;
    if (data != NULL && size > 0) {
       MediaMeta::ByteBuffer *byteBuffer = new MediaMeta::ByteBuffer;
        byteBuffer->data = data;
        byteBuffer->size = size;
        byteBufferSP.reset(byteBuffer, &releaseByteBufferCSD);

    }

    return byteBufferSP;
}



/////////////////////////////////////////////////////////////////////////
//avc make csd

AVCCSDMaker::AVCCSDMaker(MediaMetaSP &metaData) : StreamCSDMaker(metaData), mIsAvcc(false), mNalLengthSize(4)
{
    FENTER();
    FLEAVE();
}


AVCCSDMaker::~AVCCSDMaker()
{
    FENTER();
    FLEAVE();
}


mm_status_t AVCCSDMaker::extractCSD()
{
    FENTER();
    uint8_t *data = NULL;
    int32_t size = 0;
    MediaBufferSP sps, pps;
    mMediaBufferCSD.clear();

    if (mMediaMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) {

        const uint8_t *spsStartPos,*ppsStartPos;
        const uint8_t *p = data;
        if (size < 7) {
            ERROR("avcC too short\n");
            return MM_ERROR_INVALID_PARAM;
        }

        if (data[0] == 1) {
            mIsAvcc = true;
        } else {
            //start with 0x00000001
            return MM_ERROR_SUCCESS;
        }

        // Decode sps from avcC

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
        mNalLengthSize = (*(p + 4) & 0x3) + 1;
        INFO("avcC: nal length size is %d", mNalLengthSize);

        int spsCnt = *(p + 5) & 0x1f; // Number of sps
        if ( spsCnt <= 0 ) {
            ERROR("no sps\n");
            return MM_ERROR_INVALID_PARAM;
        }

        p += 6;

#define CODEC_RB16(x)                           \
        ((((const uint8_t*)(x))[0] << 8) |          \
        ((const uint8_t*)(x))[1])

#define CODEC_RB32(x)                           \
        ((((const uint8_t*)(x))[0] << 24) |          \
        (((const uint8_t*)(x))[1]) << 16 |                  \
        (((const uint8_t*)(x))[2]) << 8 |                  \
        (((const uint8_t*)(x))[3]))

#define LENGTH_SIZE 4

        int spsLength = CODEC_RB16(p);
        int spsNalsize = spsLength + 2;
        //skip sequenceParameterSetLength
        spsStartPos = p + 2;
        uint8_t * spsData = new uint8_t[spsLength + LENGTH_SIZE];
        if ( !spsData ) {
            ERROR("no mem, need: %d\n", spsLength);
            return MM_ERROR_NO_MEM;
        }

        *((uint32_t*)spsData) = CODEC_RB32(&spsLength);

        memcpy(spsData + LENGTH_SIZE, spsStartPos, spsLength);
        sps = createMediaBufferCSD(spsData, spsLength + LENGTH_SIZE);
        if ( !sps ) {
            ERROR("no mem\n");
            MM_RELEASE_ARRAY(spsData);
            spsData = NULL;
            return MM_ERROR_NO_MEM;
        }

        INFO("sps size: %u\n", spsLength);

        //go on handle Extradata
        p += spsNalsize;

        // Decode pps from avcC
        int ppsCnt = *(p++); // Number of pps
        if ( ppsCnt <= 0 ) {
            WARNING("no pps\n");
            return MM_ERROR_SUCCESS;
        }

        int ppsLength = CODEC_RB16(p);
        ppsStartPos = p + 2;
        uint8_t * ppsData = new uint8_t[ppsLength + LENGTH_SIZE];
        if ( !ppsData ) {
            ERROR("no mem, need: %d\n", ppsLength);
            return MM_ERROR_NO_MEM;
        }

        *((uint32_t*)ppsData) = CODEC_RB32(&ppsLength);

        memcpy(ppsData + LENGTH_SIZE, ppsStartPos, ppsLength);

        pps = createMediaBufferCSD(ppsData, ppsLength + LENGTH_SIZE);
        if ( !pps ) {
            ERROR("no mem\n");
            MM_RELEASE_ARRAY(ppsData);
            return MM_ERROR_NO_MEM;
        }

        mMediaBufferCSD.push_back(sps);
        mMediaBufferCSD.push_back(pps);
        INFO("pps size: %u\n", ppsLength);

    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);

}


//////////////////////////////////////////
//aac make csd
AACCSDMaker::AACCSDMaker(MediaMetaSP &metaData) : StreamCSDMaker(metaData)
{
    FENTER();
    FLEAVE();
}


AACCSDMaker::~AACCSDMaker()
{
    FENTER();
    FLEAVE();
}


mm_status_t AACCSDMaker::extractCSD()
{
    uint8_t *data = NULL;
    int32_t size = 0;

    FENTER();

    mm_status_t status = MM_ERROR_UNKNOWN;
    mMediaBufferCSD.clear();

    if (mMediaMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size) ) {
        return StreamCSDMaker::extractCSD();
    } else {
        //no extraData for aac codec, need to make from sr/cc
        //for aac/mpegts/hls,applehttp file format
        int32_t profile = 0;
        int32_t sample_rate = 0;
        int32_t channels = 0;
        if (!mMediaMeta->getInt32(MEDIA_ATTR_CODECPROFILE, profile)) {
            return status;
        }

        if (!mMediaMeta->getInt32(MEDIA_ATTR_SAMPLE_RATE, sample_rate)) {
           return status;
        }

        if (!mMediaMeta->getInt32(MEDIA_ATTR_CHANNEL_COUNT, channels)) {
           return status;
        }

        MediaBufferSP mediaBuffer = makeCSDWithNoExtraData(profile, sample_rate, channels);

        if (mediaBuffer) {
            mMediaBufferCSD.push_back(mediaBuffer);
            status = MM_ERROR_SUCCESS;
        }
    }

    FLEAVE_WITH_CODE(status);
}

/*static*/int32_t AACCSDMaker::getSampleRateIndex(int32_t rate) {
    if (92017 <= rate) return 0;
    else if (75132 <= rate) return 1;
    else if (55426 <= rate) return 2;
    else if (46009 <= rate) return 3;
    else if (37566 <= rate) return 4;
    else if (27713 <= rate) return 5;
    else if (23004 <= rate) return 6;
    else if (18783 <= rate) return 7;
    else if (13856 <= rate) return 8;
    else if (11502 <= rate) return 9;
    else if (9391  <= rate) return 10;
    else                    return 11;

}

/*static*/MediaBufferSP AACCSDMaker::makeCSDWithNoExtraData(int32_t profile, int32_t sample_rate, int32_t channels) {

    MediaBufferSP mediaBuffer;

    DEBUG("profile %d, sr %d, channelConfiguration %d", profile, sample_rate, channels);

    int32_t srIndex = getSampleRateIndex(sample_rate);

#ifndef _PLATFORM_TV
    int32_t esdsSize = 2;
    uint8_t * esdsData = new uint8_t[esdsSize];
    if ( !esdsData ) {
        ERROR("no mem, need: %d\n", esdsSize);
        return mediaBuffer;
    }

    esdsData[0] =
        ((profile + 1) << 3) | (srIndex >> 1);

    esdsData[1] =
        ((srIndex << 7) & 0x80) | (channels << 3);

#else
    static const uint8_t kStaticESDS[] = {
        0x03, 22,
        0x00, 0x00,     // ES_ID
        0x00,           // streamDependenceFlag, URL_Flag, OCRstreamFlag

        0x04, 17,
        0x40,                       // Audio ISO/IEC 14496-3
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,

        0x05, 2,
        // AudioSpecificInfo follows

        // oooo offf fccc c000
        // o - audioObjectType
        // f - samplingFreqIndex
        // c - channelConfig
    };


    int32_t esdsSize = sizeof(kStaticESDS) + 2;
    uint8_t * esdsData = new uint8_t[esdsSize];
    if ( !esdsData ) {
        ERROR("no mem, need: %d\n", esdsSize);
        return mediaBuffer;
    }

    memcpy(esdsData, kStaticESDS, sizeof(kStaticESDS));

    esdsData[sizeof(kStaticESDS)] =
        ((profile + 1) << 3) | (srIndex >> 1);

    esdsData[sizeof(kStaticESDS) + 1] =
        ((srIndex << 7) & 0x80) | (channels << 3);


#endif

    mediaBuffer = createMediaBufferCSD(esdsData, esdsSize);

    hexDump(esdsData, esdsSize, 16);
    INFO("mediaBuffer %p, esds size: %u\n", mediaBuffer.get(), esdsSize);
    return mediaBuffer;

}

HEVCCSDMaker::HEVCCSDMaker(MediaMetaSP &metaData) : StreamCSDMaker(metaData), mIsHvcc(false), mNalLengthSize(4)
{
    FENTER();
    FLEAVE();
}


HEVCCSDMaker::~HEVCCSDMaker()
{
    FENTER();
    FLEAVE();
}

mm_status_t HEVCCSDMaker::extractCSD()
{
    FENTER();
    uint8_t *data = NULL;
    int32_t size = 0;
    MediaBufferSP csd0;
    mMediaBufferCSD.clear();

    if (!mMediaMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size)) {
        INFO("there is no mp4 hvcc box");
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
    }

    const uint8_t *p = data;
    DEBUG("hevcC size is %lu", size);

    if (size < 23) {
        ERROR("hvcC too short, size is %d\n", size);
        return MM_ERROR_INVALID_PARAM;
    }

    if (size > 10240) {
        ERROR("hvcC too large, size is %d\n", size);
        return MM_ERROR_INVALID_PARAM;
    }

    if (data[0] == 1) {
        mIsHvcc = true;
    } else {
        // some malformat mkv files, which is hvcC foramt with csd data[0] = 0
        INFO("csd data[0] not 1, %d", data[0]);
        const char* container = NULL;
        mMediaMeta->getString(MEDIA_ATTR_CONTAINER, container);
        if (container == NULL ||
            !strncmp(container, "hls", 3) ||
            !strncmp(container, "mpegts", 6)) {
            INFO("container is %s, this is not hvcC format", container == NULL ? "none" : container);
            return MM_ERROR_SUCCESS;
        } else {
            INFO("but container is %s, still assume hvcC format", container == NULL ? "none" : container);
            mIsHvcc = true;
        }
    }

    mNalLengthSize = (*(p + 21) & 0x3) + 1;
    INFO("hvcC: nal length size is %d", mNalLengthSize);

    p += 22;
    size -= 22;

#define CODEC_RB16(x)                           \
        ((((const uint8_t*)(x))[0] << 8) |      \
        ((const uint8_t*)(x))[1])

#define CODEC_RB32(x)                           \
        ((((const uint8_t*)(x))[0] << 24) |     \
        (((const uint8_t*)(x))[1]) << 16 |      \
        (((const uint8_t*)(x))[2]) << 8 |       \
        (((const uint8_t*)(x))[3]))

#define LENGTH_SIZE 4

    int i, j;
    int numofArrays = (char)p[0];

    p += 1;
    size -= 1;

    uint8_t * csdData = new uint8_t[size + numofArrays * LENGTH_SIZE * 5];
    uint8_t * csdPos = csdData;
    size_t csdSize = 0;

    DEBUG("hevcC has %d arrays", numofArrays);

    for (i = 0; i < numofArrays; i++) {
        if (size < 3) {
            ERROR("invalid hvcC");
            MM_RELEASE_ARRAY(csdData);
            return MM_ERROR_INVALID_PARAM;
        }
        p += 1;
        size -= 1;

        int numofNals = CODEC_RB16(p);
        p += 2;
        size -= 2;

        DEBUG("hevcC array %d has %d nals", i, numofNals);
        for (j = 0; j < numofNals; j++) {
            if (size < 2) {
                ERROR("invalid hvcC");
                MM_RELEASE_ARRAY(csdData);
                return MM_ERROR_INVALID_PARAM;
            }

            int length = CODEC_RB16(p);
            p += 2;
            size -= 2;

            DEBUG("nal lenght is %d", length);
            if (size < length) {
                ERROR("invalid hvcC");
                MM_RELEASE_ARRAY(csdData);
                return MM_ERROR_INVALID_PARAM;
            }

            *((uint32_t*)csdPos) = CODEC_RB32(&length);

            csdPos += LENGTH_SIZE;
            csdSize += LENGTH_SIZE;

            memcpy(csdPos, p, length);

            csdPos += length;
            csdSize += length;

            p += length;
            size -= length;
        }
    }

    csd0 = createMediaBufferCSD(csdData, csdSize);
    if ( !csd0) {
        ERROR("no mem\n");
        MM_RELEASE_ARRAY(csdData);
        return MM_ERROR_NO_MEM;
    }

    mMediaBufferCSD.push_back(csd0);

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

}
