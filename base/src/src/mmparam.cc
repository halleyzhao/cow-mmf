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
#include <string.h>
#include "multimedia/mmparam.h"

#include "multimedia/mm_debug.h"

namespace YUNOS_MM {

#define DATA_INIT_SIZE 32*1024

DEFINE_LOGTAG(MMParam)

MMParam::MMParam() : mDataSize(DATA_INIT_SIZE),
                        mDataWritePos(0),
                        mDataReadPos(0),
                        mStringReadPos(0),
                        mPointersReadPos(0)
{
    mData = new uint8_t[DATA_INIT_SIZE];
}

MMParam::MMParam(const MMParam & another)
{
    mData = NULL;
    copy(&another);
}

MMParam::MMParam(const MMParam * another)
{
    mData = NULL;
    copy(another);
}


MMParam::~MMParam()
{
    MM_RELEASE_ARRAY(mData);
}

template<class T>
mm_status_t MMParam::writeFixed(T val)
{
    size_t valSize = sizeof(T);
    MMLOGV("valSize: %d\n", valSize);
    if ( (mDataWritePos + valSize) > mDataSize ) {
        MMLOGE("no mem\n");
        return MM_ERROR_NO_MEM;
    }

    *reinterpret_cast<T*>(mData + mDataWritePos) = val;
    mDataWritePos += valSize;

    return MM_ERROR_SUCCESS;
}

mm_status_t MMParam::writeInt32(int32_t val)
{
    MMLOGV("val: %d", val);
    return writeFixed(val);
}

mm_status_t MMParam::writeInt64(int64_t val)
{
    MMLOGV("val: %" PRId64 "\n", val);
    return writeFixed(val);
}

mm_status_t MMParam::writeFloat(float val)
{
    MMLOGV("val: %f", val);
    return writeFixed(val);
}

mm_status_t MMParam::writeDouble(double val)
{
    MMLOGV("val: %f", val);
    return writeFixed(val);
}

mm_status_t MMParam::writeCString(const char * val)
{
    MMLOGV("val: %s", val);
    mStrings.push_back(std::string(val));

    return MM_ERROR_SUCCESS;
}

mm_status_t MMParam::writePointer(const MMRefBaseSP & pointer)
{
    mPointers.push_back(pointer);
    return MM_ERROR_SUCCESS;
}

mm_status_t MMParam::writeRawPointer(uint8_t *val)
{
    MMLOGV("val: %p", val);
    return writeFixed(val);
}

mm_status_t MMParam::readInt32(int32_t * val) const
{
    MMLOGV("\n");
    return readFixed(val);
}

mm_status_t MMParam::readInt64(int64_t * val) const
{
    MMLOGV("\n");
    return readFixed(val);
}

mm_status_t MMParam::readFloat(float * val) const
{
    MMLOGV("\n");
    return readFixed(val);
}

mm_status_t MMParam::readDouble(double * val) const
{
    MMLOGV("\n");
    return readFixed(val);
}

mm_status_t MMParam::readRawPointer(uint8_t **val) const
{
    MMLOGV("\n");
    return readFixed(val);
}

int32_t MMParam::readInt32() const
{
    int32_t val;
    mm_status_t ret = readInt32(&val);
    if ( MM_ERROR_SUCCESS == ret ) {
        return val;
    }

    return 0;
}

int64_t MMParam::readInt64() const
{
    int64_t val;
    mm_status_t ret = readInt64(&val);
    if ( MM_ERROR_SUCCESS == ret ) {
        return val;
    }

    return 0;
}

float MMParam::readFloat() const
{
    float val;
    mm_status_t ret = readFloat(&val);
    if ( MM_ERROR_SUCCESS == ret ) {
        return val;
    }

    return 0;
}

double MMParam::readDouble() const
{
    double val;
    mm_status_t ret = readDouble(&val);
    if ( MM_ERROR_SUCCESS == ret ) {
        return val;
    }

    return 0;
}

uint8_t* MMParam::readRawPointer() const
{
    uint8_t *val;
    mm_status_t ret = readRawPointer(&val);
    if ( MM_ERROR_SUCCESS == ret ) {
        return val;
    }

    return NULL;
}

template<class T>
mm_status_t MMParam::readFixed(T * val) const
{
    size_t valSize = sizeof(T);
    MMLOGV("valSize: %d\n", valSize);
    if ( mDataReadPos >= mDataWritePos ) {
        MMLOGV("no more\n");
        return MM_ERROR_NO_MORE;
    }

    *val =  *reinterpret_cast<const T*>(mData + mDataReadPos);
    mDataReadPos += valSize;
    return MM_ERROR_SUCCESS;
}

const char * MMParam::readCString() const
{
    MMLOGV("\n");
    if ( mStringReadPos >= mStrings.size() ) {
        MMLOGV("no more\n");
        return NULL;
    }

    return mStrings[mStringReadPos++].c_str();
}

MMRefBaseSP MMParam::readPointer() const
{
    if ( mPointersReadPos >= mPointers.size() ) {
        MMLOGV("no more\n");
        return MMRefBaseSP((MMRefBase*)NULL);
    }

    return (mPointers[mPointersReadPos++]);
}

const MMParam & MMParam::operator=(const MMParam & another)
{
    copy(&another);

    return *this;
}

void MMParam::copy(const MMParam * another)
{
    MMLOGV("\n");
    MM_RELEASE(mData);
    mData = new uint8_t[another->mDataSize];
    memcpy(mData , another->mData,another->mDataSize);
    mDataSize = another->mDataSize;
    mDataWritePos = another->mDataWritePos;
    mDataReadPos = 0;

    mStrings = another->mStrings;
    MMLOGV("mStrings size =%zu \n",mStrings.size());
    //mStringReadPos = another->mStringReadPos;
    mStringReadPos = 0;

    mPointers = another->mPointers;
    mPointersReadPos = 0;
    MMLOGV("over\n");
}

}
