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

#include <multimedia/mm_buffer.h>
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(MMBuffer)
DEFINE_LOGTAG(MMIDBuffer)


#define DO_CREATE(_size, _t, _T, _STP) do {\
    MMLOGV("size: %u\n", _size);\
    try {\
        _t = _STP(new _T());\
    } catch (...) {\
        MMLOGE("no mem\n");\
        _t = _STP((_T*)NULL);\
        break;\
    }\
    if ( !_t ) {\
        MMLOGE("no mem\n");\
        break;\
    }\
\
    try {\
        _t->mBuffer = new uint8_t[_size];\
    } catch (...) {\
        MMLOGE("no mem\n");\
        _t = _STP((_T*)NULL);\
        break;\
    }\
    if ( !_t->mBuffer ) {\
        MMLOGE("no mem\n");\
        break;\
    }\
    _t->mSize = _size;\
    _t->mActualSize = 0;\
    _t->mFree = true;\
}while(0)

/*static */MMBufferSP MMBuffer::create(size_t size)
{
    MMBufferSP buf;
    DO_CREATE(size, buf, MMBuffer, MMBufferSP);

    return buf;
}

/*static */MMBufferSP MMBuffer::fromBuffer(uint8_t * buf, size_t size, bool freeByMe/* = true*/)
{
    MMBufferSP me;
    try {
        me.reset(new MMBuffer());
    } catch (...) {
        MMLOGE("no mem\n");
        return MMBufferSP((MMBuffer*)NULL);
    }
    me->mBuffer = buf;
    me->mSize = me->mActualSize = size;
    me->mFree = freeByMe;
    return me;
}

MMBuffer::MMBuffer() : mBuffer(NULL), mSize(0), mActualSize(0), mFree(true)
{
}

MMBuffer::~MMBuffer()
{
    if (mFree)
        MM_RELEASE_ARRAY(mBuffer);
}

size_t MMBuffer::getSize() const
{
    return mSize;
}

size_t MMBuffer::getActualSize() const
{
    return mActualSize;
}

mm_status_t MMBuffer::setActualSize(size_t size)
{
    if ( size > mSize ) {
        MMLOGE("size: %zu too big(max: %zu)\n", size, mSize);
        return MM_ERROR_INVALID_PARAM;
    }

    mActualSize = size;
    return MM_ERROR_SUCCESS;
}

const uint8_t * MMBuffer::getBuffer() const
{
    return mBuffer;
}

uint8_t * MMBuffer::getWritableBuffer() const
{
    return mBuffer;
}

/*static */MMIDBufferSP MMIDBuffer::create(size_t size, id_type id)
{
    MMIDBufferSP buf;
    DO_CREATE(size, buf, MMIDBuffer, MMIDBufferSP);

    if ( buf ) {
        buf->mId = id;
    }

    return buf;
}

}

