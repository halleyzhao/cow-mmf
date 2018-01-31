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

#ifndef __mmparam_H
#define __mmparam_H

#include <vector>
#include <string>
#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mm_refbase.h>


namespace YUNOS_MM {


class MMParam {
public:
    MMParam();
    MMParam(const MMParam & another);
    MMParam(const MMParam * another);
    ~MMParam();

public:
    mm_status_t writeInt32(int32_t val);
    mm_status_t writeInt64(int64_t val);
    mm_status_t writeFloat(float val);
    mm_status_t writeDouble(double val);
    mm_status_t writeCString(const char * val);
    mm_status_t writePointer(const MMRefBaseSP & pointer);
    mm_status_t writeRawPointer(uint8_t *pointer);

    mm_status_t readInt32(int32_t * val) const;
    mm_status_t readInt64(int64_t * val) const;
    mm_status_t readFloat(float * val) const;
    mm_status_t readDouble(double * val) const;
    const char * readCString() const;
    MMRefBaseSP readPointer() const;
    mm_status_t readRawPointer(uint8_t **val) const;

    int32_t readInt32() const;
    int64_t readInt64() const;
    float readFloat() const;
    double readDouble() const;
    uint8_t* readRawPointer() const;

public:
    const MMParam & operator=(const MMParam & another);

private:
    template<class T>
    mm_status_t writeFixed(T val);
    template<class T>
    mm_status_t readFixed(T * val) const;

    void copy(const MMParam * another);

private:
    uint8_t * mData;
    size_t mDataSize;
    mutable size_t mDataWritePos;
    mutable size_t mDataReadPos;

    std::vector<std::string> mStrings;
    mutable size_t mStringReadPos;

    std::vector<MMRefBaseSP> mPointers;
    mutable size_t mPointersReadPos;

    DECLARE_LOGTAG()
};

typedef MMSharedPtr<MMParam> MMParamSP;

}

#endif // __mmparam_H
