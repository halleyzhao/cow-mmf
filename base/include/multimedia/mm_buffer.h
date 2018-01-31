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

#ifndef __mm_buffer_H
#define __mm_buffer_H

#include <multimedia/mm_refbase.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_types.h>

namespace YUNOS_MM {

class MMBuffer;
typedef MMSharedPtr<MMBuffer> MMBufferSP;

class MMBuffer : public MMRefBase {
public:
    static MMBufferSP create(size_t size);
    static MMBufferSP fromBuffer(uint8_t * buf, size_t size, bool freeByMe = false);

public:
    virtual ~MMBuffer();

public:
    // max size
    size_t getSize() const;

    // actual used size
    size_t getActualSize() const;
    mm_status_t setActualSize(size_t size);

    const uint8_t * getBuffer() const;
    uint8_t * getWritableBuffer() const;

protected:
    MMBuffer();

protected:
    uint8_t * mBuffer;
    size_t mSize;
    size_t mActualSize;
    bool mFree;

    DECLARE_LOGTAG()
    MM_DISALLOW_COPY(MMBuffer)
};


class MMIDBuffer;
typedef MMSharedPtr<MMIDBuffer> MMIDBufferSP;

class MMIDBuffer : public MMBuffer {
public:
    typedef int32_t id_type;

public:
    static MMIDBufferSP create(size_t size, id_type id);

public:
    ~MMIDBuffer() {}

public:
    id_type id() const { return mId; }

protected:
    MMIDBuffer() :mId(-1) {}

private:
    id_type mId;

    DECLARE_LOGTAG()
    MM_DISALLOW_COPY(MMIDBuffer)
};


}

#endif // __mm_buffer_H

