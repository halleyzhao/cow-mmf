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


#ifndef make_csd_h
#define make_csd_h

#include <vector>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/media_buffer.h>
#include "multimedia/media_meta.h"

//#include "media_codec.h"


namespace YUNOS_MM {
typedef MMSharedPtr <MediaMeta::ByteBuffer> ByteBufferSP;


class StreamCSDMaker {
public:
    //For encoder
    StreamCSDMaker();

    virtual mm_status_t getExtraDataFromMediaBuffer(MediaBufferSP &mediaBuf);
    virtual MediaBufferSP getExtraDataFromRawData(uint8_t *data, size_t size);

    //For decoder
    StreamCSDMaker(MediaMetaSP &metaData);
    virtual ~StreamCSDMaker();

    int32_t getCSDCount();
    virtual MediaBufferSP getMediaBufferFromCSD(int32_t index);

    virtual ByteBufferSP getByteBufferFromCSD(int32_t index);

protected:
    //for decoder
    std::vector<MediaBufferSP> mMediaBufferCSD;
    std::vector<ByteBufferSP> mByteBufferCSD;
    MediaMetaSP mMediaMeta;

    //for decoder
    virtual mm_status_t extractCSD();

    static MediaBufferSP createMediaBufferCSD(uint8_t *data, int32_t size);

    static ByteBufferSP createByteBufferCSD(uint8_t *data, int32_t size);

    static bool releaseMediaBufferCSD(MediaBuffer* mediaBuffer);

    static bool releaseByteBufferCSD(MediaMeta::ByteBuffer *byteBuf);

private:
    bool mIsInited;
    Lock mLock;
    DECLARE_LOGTAG()

};


typedef MMSharedPtr < StreamCSDMaker > StreamMakeCSDSP;


//////////////////////////////////////////////////////////////
class AACCSDMaker : public StreamCSDMaker {
public:
    AACCSDMaker(MediaMetaSP &mMetaData);
    virtual ~AACCSDMaker();


protected:


private:
    virtual mm_status_t extractCSD();

    static MediaBufferSP makeCSDWithNoExtraData(int32_t profile, int32_t sample_rate, int32_t channels);

    static int32_t getSampleRateIndex(int32_t rate);
    DECLARE_LOGTAG()

};


///////////////////////////////////////////////////////////////
class AVCCSDMaker : public StreamCSDMaker {
public:
    //for decoder
    AVCCSDMaker(MediaMetaSP &mMetaData);

    bool isAVCc() {return mIsAvcc;}
    int getNalLengthSize() { return mNalLengthSize; }


    virtual ~AVCCSDMaker();

protected:

private:

    virtual mm_status_t extractCSD();
    bool mIsAvcc;
    int mNalLengthSize;
    DECLARE_LOGTAG()

};

class HEVCCSDMaker : public StreamCSDMaker {
public:
    //for decoder
    HEVCCSDMaker(MediaMetaSP &mMetaData);

    bool isHEVCc() {return mIsHvcc;}
    int getNalLengthSize() { return mNalLengthSize; }


    virtual ~HEVCCSDMaker();

protected:

private:

    virtual mm_status_t extractCSD();
    bool mIsHvcc;
    int mNalLengthSize;
    DECLARE_LOGTAG()

};

} // end of namespace YUNOS_MM
#endif//make_csd_h

