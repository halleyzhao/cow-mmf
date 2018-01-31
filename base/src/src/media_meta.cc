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
#include "multimedia/mmparam.h"
#include "multimedia/media_meta.h"
#include "multimedia/mm_debug.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// libbase name space
using namespace yunos;

MM_LOG_DEFINE_MODULE_NAME("Cow-MediaMeta");
// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

namespace YUNOS_MM {
MediaMetaSP MediaMeta::create()
{
    FUNC_TRACK();
    MediaMetaSP meta;

    meta.reset(new MediaMeta);

    return meta;
}

MediaMeta::MediaMeta()
{
    FUNC_TRACK();
    // FIXME, dynamic allocation policy
    mMeta.reserve(50);
}

MediaMeta::~MediaMeta() {
    FUNC_TRACK();
    uint32_t i=0;
    while (i<mMeta.size()) {
        mMeta[i].clearData();
        i++;
    }
}

void MediaMeta::clear()
{
    uint32_t i=0;
    while (i<mMeta.size()) {
        mMeta[i].clearData();
        i++;
    }
    mMeta.clear();
}

MediaMeta::MetaItem::MetaItem()
  : mType(MT_Invalid)
  , mName(NULL)
  , mNameLength(0)
{
    FUNC_TRACK();
}

void MediaMeta::MetaItem::clearData()
{
    FUNC_TRACK();
    switch(mType) {
        case MT_String:
            free(mValue.str);
            mValue.str = NULL;
            break;
        case MT_ByteBuffer:
            free(mValue.buf.data);
            mValue.buf.data = NULL;
            mValue.buf.size = 0;
            break;
        case MT_Object:
            mObjSP.reset();
            break;
        default:
            break;
    }

    if (mName) {
        free(mName);
        mName = NULL;
    }
}

MediaMeta::MetaItem::~MetaItem()
{
    FUNC_TRACK();
}

int32_t MediaMeta::findMetaIdx(const char* field) const
{
    FUNC_TRACK();
    int idx=-1, i=-1;
    int metaSize = mMeta.size();
    uint32_t len = strlen(field);

    while (i++<metaSize-1) {
        if (mMeta[i].mNameLength != len)
            continue;
        if (!memcmp(mMeta[i].mName, field, len)) {
            idx = i;
            break;
        }
    }

    return idx;
}

bool MediaMeta::setMetaName (MetaItem& meta, const char* name)
{
    meta.mName = strdup(name);
    if (!meta.mName) {
        ERROR("fail to dump meta name\n");
        return false;
    }
    meta.mNameLength = strlen(name);

    return true;
}
#define SET_SIMPLE_VARIABLE(NAME, BASIC_TYPE, FIELD)                \
bool MediaMeta::set##NAME(const char* name, BASIC_TYPE v)           \
{                                                                   \
    FUNC_TRACK();                                                   \
    int idx = -1;                                                   \
    MetaItem meta;                                                  \
                                                                    \
    if (!name)                                                      \
        return false;                                               \
                                                                    \
    idx = findMetaIdx(name);                                        \
    if (idx>-1) {                                                   \
        MMASSERT(mMeta[idx].mType == MT_##NAME);                    \
        mMeta[idx].mValue.FIELD = v;                                \
        return true;                                                \
    }                                                               \
                                                                    \
    if (!setMetaName(meta, name))                                   \
        return false;                                               \
                                                                    \
    meta.mType = MT_##NAME;                                         \
    meta.mValue.FIELD = v;                                          \
                                                                    \
    mMeta.push_back(meta);                                          \
    return true;                                                    \
}

SET_SIMPLE_VARIABLE(Int32, int32_t, ii)
SET_SIMPLE_VARIABLE(Int64, int64_t, ld)
SET_SIMPLE_VARIABLE(Float, float, f)
SET_SIMPLE_VARIABLE(Double, double, db)
SET_SIMPLE_VARIABLE(Pointer, void*, ptr)

bool MediaMeta::setFraction(const char* name, int32_t num, int32_t denom)
{
    FUNC_TRACK();
    int idx = -1;
    MetaItem meta;

    if (!name)
        return false;

    idx = findMetaIdx(name);
    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Fraction);
        mMeta[idx].mValue.frac.num = num;
        mMeta[idx].mValue.frac.denom = denom;
        return true;
    }

    if (!setMetaName(meta, name))
        return false;

    meta.mType = MT_Fraction;
    meta.mValue.frac.num = num;
    meta.mValue.frac.denom = denom;

    mMeta.push_back(meta);
    return true;
}

bool MediaMeta::setString(const char* name, const char* str)
{
    FUNC_TRACK();
    int idx = -1;
    MetaItem meta;
    MetaItem *pMeta = &meta;

    if (!name || !str)
        return false;

    idx = findMetaIdx(name);
    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_String);
        if (!strcmp(mMeta[idx].mValue.str, str)) {
            return true;
        }
        free(mMeta[idx].mValue.str);
        mMeta[idx].mValue.str = NULL;
        pMeta = &mMeta[idx];
    }

    pMeta->mValue.str = strdup(str);
    if (!pMeta->mValue.str) {
        ERROR("fail to alloc mem for str\n");
        // the item is left empty, but not removed
        return  false;
    }

    if (pMeta == &meta) {
        if (!setMetaName(*pMeta, name)) {
            free(pMeta->mValue.str);
            return false;
        }
        pMeta->mType = MT_String;
        mMeta.push_back(meta);
    }

    return true;
}

bool MediaMeta::setByteBuffer(const char* name, uint8_t* data, int32_t size)
{
    FUNC_TRACK();
    int idx = -1;
    MetaItem  meta;
    MetaItem *pMeta = &meta;

    if (!name || !data || !size)
        return false;

    idx = findMetaIdx(name);
    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_ByteBuffer);
        free(mMeta[idx].mValue.buf.data);
        mMeta[idx].mValue.buf.data = NULL;
        mMeta[idx].mValue.buf.size = 0;
        pMeta = &mMeta[idx];
    }

    pMeta->mValue.buf.data = (uint8_t*)malloc(size);
    if (!pMeta->mValue.buf.data) {
        ERROR("fail to alloc mem for meta data\n");
        // the item is left empty, but not removed
        return  false;
    }

    memcpy(pMeta->mValue.buf.data, data, size);
    pMeta->mValue.buf.size = size;

    if (pMeta == &meta) {
        if(!setMetaName(*pMeta, name)) {
            free(pMeta->mValue.buf.data);
            return false;
        }
        pMeta->mType = MT_ByteBuffer;
        mMeta.push_back(meta);
    }

    return true;
}

bool MediaMeta::setRect(const char* name, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    FUNC_TRACK();
    int idx = -1;
    MetaItem meta;

    if (!name)
        return false;

    idx = findMetaIdx(name);
    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Rect);
        mMeta[idx].mValue.rect.left = left;
        mMeta[idx].mValue.rect.top = top;
        mMeta[idx].mValue.rect.right = right;
        mMeta[idx].mValue.rect.bottom = bottom;
        return true;
    }

    if (!setMetaName(meta, name)) {
        return false;
    }

    meta.mType = MT_Rect;
    meta.mValue.rect.left = left;
    meta.mValue.rect.top = top;
    meta.mValue.rect.right = right;
    meta.mValue.rect.bottom = bottom;

    mMeta.push_back(meta);
    return true;
}

bool MediaMeta::setObject(const char* name, MetaBaseSP& objSP)
{
    FUNC_TRACK();
    int idx = -1;
    MetaItem meta;

    if (!name)
        return false;

    idx = findMetaIdx(name);
    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Object);
        mMeta[idx].mObjSP = objSP;
        return true;
    }

    if (!setMetaName(meta, name)) {
        return false;
    }

    meta.mType = MT_Object;
    meta.mObjSP = objSP;

    mMeta.push_back(meta);
    return true;
}

#define GET_SIMPLE_VARIABLE(NAME, BASIC_TYPE, FIELD)                \
bool MediaMeta::get##NAME(const char* name, BASIC_TYPE& v) const    \
{                                                                   \
    FUNC_TRACK();                                                   \
    int idx = findMetaIdx(name);                                    \
                                                                    \
    if (idx>-1) {                                                   \
        MMASSERT(mMeta[idx].mType == MT_##NAME);                    \
        v = mMeta[idx].mValue.FIELD;                                \
        return true;                                                \
    }                                                               \
                                                                    \
    return false;                                                   \
}

GET_SIMPLE_VARIABLE(Int32, int32_t, ii)
GET_SIMPLE_VARIABLE(Int64, int64_t, ld)
GET_SIMPLE_VARIABLE(Float, float, f)
GET_SIMPLE_VARIABLE(Double, double, db)
GET_SIMPLE_VARIABLE(Pointer, void*, ptr)

bool MediaMeta::getFraction(const char* name,int32_t& num, int32_t& denom) const
{
    FUNC_TRACK();
    int idx = findMetaIdx(name);

    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Fraction);
        num = mMeta[idx].mValue.frac.num;
        denom = mMeta[idx].mValue.frac.denom;
        return true;
    }

    return false;
}

bool MediaMeta::getString(const char* name, const char*& str) const
{
    FUNC_TRACK();
    int idx = findMetaIdx(name);

    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_String);
        str = mMeta[idx].mValue.str;
        return true;
    }

    return false;
}

bool MediaMeta::getByteBuffer(const char* name, uint8_t*& data, int32_t& size) const
{
    FUNC_TRACK();
    int idx = findMetaIdx(name);

    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_ByteBuffer);
        data = mMeta[idx].mValue.buf.data;
        size = mMeta[idx].mValue.buf.size;
        return true;
    }

    return false;
}

bool MediaMeta::getRect(const char* name, int32_t& left, int32_t& top, int32_t& right, int32_t& bottom) const
{
    FUNC_TRACK();
    int idx = findMetaIdx(name);

    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Rect);
        left = mMeta[idx].mValue.rect.left;
        top = mMeta[idx].mValue.rect.top;
        right = mMeta[idx].mValue.rect.right;
        bottom = mMeta[idx].mValue.rect.bottom;
        return true;
    }

    return false;
}

bool MediaMeta::getObject(const char* name, MetaBaseSP& objSP) const
{
    FUNC_TRACK();
    int idx = findMetaIdx(name);

    if (idx>-1) {
        MMASSERT(mMeta[idx].mType == MT_Object);
        objSP = mMeta[idx].mObjSP;
        return true;
    }

    return false;
}

bool MediaMeta::containsKey(const char* name) const
{
    return findMetaIdx(name) >= 0;
}

#if defined(OS_YUNOS)
bool MediaMeta::writeToMsg(SharedPtr<DMessage> &msg)
{
    FUNC_TRACK();
    uint32_t i=0;
    bool success = true;

    msg->writeInt32(mMeta.size());
    for (i = 0; i < mMeta.size(); i++) {
        msg->writeInt32(mMeta[i].mType);
        msg->writeString(mMeta[i].mName);
        switch (mMeta[i].mType) {
            case MT_String:
                msg->writeString(mMeta[i].mValue.str);
                break;
            case MT_ByteBuffer:
                msg->writeByteBuffer(mMeta[i].mValue.buf.size, (int8_t *)mMeta[i].mValue.buf.data);
                break;
            case MT_Fraction:
                msg->writeInt32(mMeta[i].mValue.frac.num);
                msg->writeInt32(mMeta[i].mValue.frac.denom);
                break;
            case MT_Rect:
                msg->writeInt32(mMeta[i].mValue.rect.left);
                msg->writeInt32(mMeta[i].mValue.rect.top);
                msg->writeInt32(mMeta[i].mValue.rect.right);
                msg->writeInt32(mMeta[i].mValue.rect.bottom);
                break;
            case MT_Object:
                WARNING("not support by now");
                success = false;
                break;
            case MT_Int32:
                msg->writeInt32(mMeta[i].mValue.ii);
                break;
            case MT_Int64:
                msg->writeInt64(mMeta[i].mValue.ld);
                break;
            case MT_Float:
                msg->writeDouble((double)mMeta[i].mValue.f);
                break;
            case MT_Double:
                msg->writeDouble(mMeta[i].mValue.db);
                break;
            case MT_Pointer:
                msg->writeInt64((int64_t)mMeta[i].mValue.ptr);
                break;
            default:
                ERROR("Should not be here\n");
                success = false;
                break;
        }
    }

    return success;
}

bool MediaMeta::readFromMsg(const SharedPtr<DMessage> &msg)
{
    FUNC_TRACK();
    int32_t i=0;
    bool success = true;
    MetaItem item;

    int32_t metaSize = msg->readInt32();
    mMeta.reserve(metaSize);
    for (i = 0; i < metaSize; i++) {
        item.mType = (MetaType)msg->readInt32();
        item.mName = strdup(msg->readString().c_str());
        item.mNameLength = strlen(item.mName);
        switch (item.mType) {
            case MT_String:
                item.mValue.str = strdup(msg->readString().c_str());
                break;
            case MT_ByteBuffer:
            {
                //FIXME: buf should be released by caller
                int32_t size = -1;
                item.mValue.buf.data = (uint8_t *)msg->readByteBuffer(size);
                item.mValue.buf.size = size;
                break;
            }
            case MT_Fraction:
                item.mValue.frac.num = msg->readInt32();
                item.mValue.frac.denom = msg->readInt32();
                break;
            case MT_Rect:
                item.mValue.rect.left = msg->readInt32();
                item.mValue.rect.top = msg->readInt32();
                item.mValue.rect.right = msg->readInt32();
                item.mValue.rect.bottom = msg->readInt32();
                break;
            case MT_Object:
                WARNING("not support by now");
                success = false;
                break;
            case MT_Int32:
                item.mValue.ii = msg->readInt32();
                break;
            case MT_Int64:
                item.mValue.ld = msg->readInt64();
                break;
            case MT_Float:
                item.mValue.f = (float)msg->readDouble();
                break;
            case MT_Double:
                item.mValue.db = msg->readDouble();
                break;
            case MT_Pointer:
                item.mValue.ptr = reinterpret_cast<void *>(msg->readInt64());
                break;
            default:
                ERROR("Should not be here\n");
                success = false;
                break;
        }

        mMeta.push_back(item);
    }

    return success;
}
#endif

MediaMetaSP MediaMeta::copy() const
{
    FUNC_TRACK();
    uint32_t i=0;
    bool success = true;
    MediaMetaSP mediaMeta = create();

    for (i=0; i<mMeta.size(); i++) {
        switch (mMeta[i].mType) {
            case MT_String:
                success = mediaMeta->setString(mMeta[i].mName, mMeta[i].mValue.str);
                break;
            case MT_ByteBuffer:
                success = mediaMeta->setByteBuffer(mMeta[i].mName, mMeta[i].mValue.buf.data, mMeta[i].mValue.buf.size);
                break;
            default:
                mediaMeta->mMeta.push_back(mMeta[i]);
                mediaMeta->mMeta[i].mName = strdup(mMeta[i].mName);
                success = (mediaMeta->mMeta[i].mName != NULL) ? true : false;
                break;
        }
        if (!success)
            break;
    }

    if (!success) {
        ERROR("MediaMeta copy failed\n");
        MediaMetaSP p;
        return p;
    }

    return mediaMeta;
}

bool MediaMeta::merge(MediaMetaSP mediaMeta)
{
    FUNC_TRACK();
    uint32_t i=0;
    bool success = true;

    MMetaVec &meta = mediaMeta->mMeta;

    for (i = 0; i < meta.size(); i++) {
        switch (meta[i].mType) {
            case MT_String:
                success = setString(meta[i].mName, meta[i].mValue.str);
                break;
            case MT_ByteBuffer:
                success = setByteBuffer(meta[i].mName,
                    meta[i].mValue.buf.data,
                    meta[i].mValue.buf.size);
                break;
            case MT_Fraction:
                success = setFraction(meta[i].mName,
                    meta[i].mValue.frac.num,
                    meta[i].mValue.frac.denom);
                break;
            case MT_Rect:
                success = setRect(meta[i].mName,
                    meta[i].mValue.rect.left,
                    meta[i].mValue.rect.top,
                    meta[i].mValue.rect.right,
                    meta[i].mValue.rect.bottom);
                break;
            case MT_Object:
                success = setObject(meta[i].mName, meta[i].mObjSP);
                break;
            case MT_Int32:
                success = setInt32(meta[i].mName, meta[i].mValue.ii);
                break;
            case MT_Int64:
                success = setInt64(meta[i].mName, meta[i].mValue.ld);
                break;
            case MT_Float:
                success = setFloat(meta[i].mName, meta[i].mValue.f);
                break;
            case MT_Double:
                success = setDouble(meta[i].mName, meta[i].mValue.db);
                break;
            case MT_Pointer:
                success = setPointer(meta[i].mName, meta[i].mValue.ptr);
                break;

            default:
                ERROR("Should not be here\n");
                break;
        }
        if (!success)
            break;
    }

    if (!success) {
        ERROR("MediaMeta append failed\n");
        return false;
    }

    return true;
}

bool MediaMeta::writeToMMParam(MMParam* param)
{
    FUNC_TRACK();
    uint32_t i=0;
    bool success = true;

    param->writeInt32(mMeta.size());
    for (i = 0; i < mMeta.size(); i++) {
        param->writeInt32(mMeta[i].mType);
        param->writeCString(mMeta[i].mName);
        switch (mMeta[i].mType) {
            case MT_String:
                param->writeCString(mMeta[i].mValue.str);
                break;
            case MT_ByteBuffer:
                // param->writeByteBuffer(mMeta[i].mValue.buf.size, (int8_t *)mMeta[i].mValue.buf.data);
                ASSERT(0 && "MMParam don't support writeByteBuffer method");
                success = false;
                break;
            case MT_Fraction:
                param->writeInt32(mMeta[i].mValue.frac.num);
                param->writeInt32(mMeta[i].mValue.frac.denom);
                break;
            case MT_Rect:
                param->writeInt32(mMeta[i].mValue.rect.left);
                param->writeInt32(mMeta[i].mValue.rect.top);
                param->writeInt32(mMeta[i].mValue.rect.right);
                param->writeInt32(mMeta[i].mValue.rect.bottom);
                break;
            case MT_Object:
                ASSERT(0 && "MMParam don't support writeObject method");
                success = false;
                break;
            case MT_Int32:
                param->writeInt32(mMeta[i].mValue.ii);
                break;
            case MT_Int64:
                param->writeInt64(mMeta[i].mValue.ld);
                break;
            case MT_Float:
                param->writeDouble((double)mMeta[i].mValue.f);
                break;
            case MT_Double:
                param->writeDouble(mMeta[i].mValue.db);
                break;
            case MT_Pointer:
                param->writeInt64((int64_t)mMeta[i].mValue.ptr);
                break;
            default:
                ERROR("Should not be here\n");
                success = false;
                break;
        }
    }

    return success;
}

bool MediaMeta::readFromMMParam(MMParam *param)
{
    FUNC_TRACK();
    int32_t i=0;
    bool success = true;
    MetaItem item;

    int32_t metaSize = param->readInt32();
    mMeta.reserve(metaSize);
    for (i = 0; i < metaSize; i++) {
        item.mType = (MetaType)param->readInt32();
        item.mName = strdup(param->readCString());
        item.mNameLength = strlen(item.mName);
        switch (item.mType) {
            case MT_String:
                item.mValue.str = strdup(param->readCString());
                break;
            case MT_ByteBuffer:
            {
                ASSERT(0 && "MMParam don't support readByteBuffer method");
                success = false;
                break;
            }
            case MT_Fraction:
                item.mValue.frac.num = param->readInt32();
                item.mValue.frac.denom = param->readInt32();
                break;
            case MT_Rect:
                item.mValue.rect.left = param->readInt32();
                item.mValue.rect.top = param->readInt32();
                item.mValue.rect.right = param->readInt32();
                item.mValue.rect.bottom = param->readInt32();
                break;
            case MT_Object:
                ASSERT(0 && "MMParam don't support readObject method");
                success = false;
                break;
            case MT_Int32:
                item.mValue.ii = param->readInt32();
                break;
            case MT_Int64:
                item.mValue.ld = param->readInt64();
                break;
            case MT_Float:
                item.mValue.f = (float)param->readDouble();
                break;
            case MT_Double:
                item.mValue.db = param->readDouble();
                break;
            case MT_Pointer:
                item.mValue.ptr = reinterpret_cast<void *>(param->readInt64());
                break;
            default:
                ERROR("Should not be here\n");
                success = false;
                break;
        }

        mMeta.push_back(item);
    }

    return success;
}


void MediaMeta::dump() const
{
    uint32_t i = 0;
    DEBUG("######## MediaMeta dump debug\n");
    for (i=0; i< mMeta.size(); i++) {
        DEBUG("meta index=%d\t\t", i);
        switch(mMeta[i].mType) {
            case MT_Invalid:
                DEBUG("empty item\n");
                break;
            case MT_Int32:
                DEBUG("(%s, int32_t:%d)\n", mMeta[i].mName, mMeta[i].mValue.ii);
                break;
            case MT_Int64:
                DEBUG("(%s, int64_t:%" PRId64 ")\n", mMeta[i].mName, mMeta[i].mValue.ld);
                break;
            case MT_Float:
                DEBUG("(%s, float:%f)\n", mMeta[i].mName, mMeta[i].mValue.f);
                break;
            case MT_Double:
                DEBUG("(%s, double:%f)\n", mMeta[i].mName, mMeta[i].mValue.db);
                break;
            case MT_Pointer:
                DEBUG("(%s, pointer:%p)\n", mMeta[i].mName, mMeta[i].mValue.ptr);
                break;
            case MT_String:
                DEBUG("(%s, string:%s)\n", mMeta[i].mName, mMeta[i].mValue.str);
                break;
            case MT_ByteBuffer:
                DEBUG("(%s, bytebuffer(%p, %zu))\n", mMeta[i].mName, mMeta[i].mValue.buf.data, mMeta[i].mValue.buf.size);
                break;
            case MT_Fraction:
                DEBUG("(%s, fraction:(%d/%d))\n", mMeta[i].mName, mMeta[i].mValue.frac.num, mMeta[i].mValue.frac.denom);
                break;
            case MT_Rect:
                DEBUG("(%s, rect:(%d, %d, %d, %d))\n", mMeta[i].mName, mMeta[i].mValue.rect.left, mMeta[i].mValue.rect.top, mMeta[i].mValue.rect.right, mMeta[i].mValue.rect.bottom);
                break;
            case MT_Object:
                DEBUG("(%s, object:%p)\n", mMeta[i].mName, mMeta[i].mObjSP.get());
                break;
            default:
                DEBUG("unknown item\n");
                break;
        }
    }
}

MediaMeta::iterator::iterator() : mVec(NULL), mI(-1)
{
}

MediaMeta::iterator::iterator(MMetaVec & vec, int i) : mVec(&vec), mI(i)
{
}

MediaMeta::iterator::iterator(const iterator & another) : mVec(another.mVec), mI(another.mI)
{
}

MediaMeta::iterator::~iterator()
{
}

MediaMeta::iterator MediaMeta::iterator::operator++()
{
    ++mI;
    return *this;
}

MediaMeta::iterator MediaMeta::iterator::operator++(int)
{
    ++mI;
    return *this;
}

MediaMeta::MetaItem & MediaMeta::iterator::operator*()
{
    return (*mVec)[mI];
}

MediaMeta::iterator & MediaMeta::iterator::operator=(const iterator & another)
{
    mI = another.mI;
    mVec = another.mVec;
    return *this;
}

bool MediaMeta::iterator::operator!=(const iterator & another)
{
    return (mI != another.mI);
}

MediaMeta::const_iterator::const_iterator() : mVec(NULL), mI(-1)
{
}

MediaMeta::const_iterator::const_iterator(const MMetaVec & vec, int i) : mVec(&vec), mI(i)
{
}

MediaMeta::const_iterator::const_iterator(const const_iterator & another) : mVec(another.mVec), mI(another.mI)
{
}

MediaMeta::const_iterator::~const_iterator()
{
}

MediaMeta::const_iterator MediaMeta::const_iterator::operator++()
{
    ++mI;
    return *this;
}

MediaMeta::const_iterator MediaMeta::const_iterator::operator++(int)
{
    ++mI;
    return *this;
}

const MediaMeta::MetaItem & MediaMeta::const_iterator::operator*()
{
    return (*mVec)[mI];
}

MediaMeta::const_iterator & MediaMeta::const_iterator::operator=(const const_iterator & another)
{
    mI = another.mI;
    mVec = another.mVec;
    return *this;
}

bool MediaMeta::const_iterator::operator!=(const const_iterator & another)
{
    return (mI != another.mI);
}

MediaMeta::iterator MediaMeta::begin()
{
    return MediaMeta::iterator(mMeta, 0);
}

MediaMeta::const_iterator MediaMeta::begin() const
{
    return MediaMeta::const_iterator(mMeta, 0);
}

MediaMeta::iterator MediaMeta::end()
{
    return MediaMeta::iterator(mMeta, (int)mMeta.size());
}

MediaMeta::const_iterator MediaMeta::end() const
{
    return MediaMeta::const_iterator(mMeta, (int)mMeta.size());
}


}

