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
#ifndef media_meta_h
#define media_meta_h

#include <stdint.h>
#include <vector>
#if defined(OS_YUNOS)
#include "pointer/BasePtr.h"
#include "pointer/SharedPtr.h"
#include <dbus/DMessage.h>
#endif

#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mm_refbase.h>


namespace YUNOS_MM {
class MMParam;
class MediaMeta;
typedef MMSharedPtr <MediaMeta> MediaMetaSP;
class MediaMeta : public MMRefBase {
  public:
    /* a base class for the ones to be held as shared_ptr in MediaMeta
     * in order to accommodate an object; extend this base class and add a new member as shared_ptr of the object.
     */
    class MetaBase {
      public:
        MetaBase() {};
        virtual ~MetaBase() {};
        virtual void* getRawPtr() = 0;
    };
    typedef MMSharedPtr < MetaBase > MetaBaseSP;

    void clear();

    static MediaMetaSP create();
    bool setInt32(const char* name, int32_t v);
    bool setInt64(const char* name, int64_t v);
    bool setFloat(const char* name, float v);
    bool setDouble(const char* name, double v);
    bool setFraction(const char* name, int32_t num, int32_t denom);
    bool setPointer(const char* name, void* ptr);
    bool setString(const char* name, const char* str);
    bool setByteBuffer(const char* name, uint8_t* data, int32_t size);
    bool setRect(const char* name, int32_t left, int32_t top, int32_t right, int32_t bottom);
    bool setObject(const char* name, MetaBaseSP &objSP);

    bool getInt32(const char* name, int32_t& v) const;
    bool getInt64(const char* name, int64_t& v) const;
    bool getFloat(const char* name, float& v) const;
    bool getDouble(const char* name, double& v) const;
    bool getFraction(const char* name, int32_t& num, int32_t& denom) const;
    bool getPointer(const char* name, void*& ptr) const;
    bool getString(const char* name, const char*& str) const;
    bool getByteBuffer(const char* name, uint8_t*& data, int32_t& size) const;
    bool getRect(const char* name, int32_t& left, int32_t& top, int32_t& right, int32_t& bottom) const;
    bool getObject(const char* name, MetaBaseSP& objSP) const;
    //check item reprerented by key whether exist
    bool containsKey(const char* name) const;

#if defined(OS_YUNOS)
    //ipc methods
    bool writeToMsg(yunos::SharedPtr<yunos::DMessage> &msg);
    bool readFromMsg(const yunos::SharedPtr<yunos::DMessage> &msg);
    #endif

    bool writeToMMParam(MMParam* param);
    bool readFromMMParam(MMParam* param);

    /* do not support removeField() for now. vector is required to be locked if removing one element from it, it is heavy.
     * anyway, it seems few chance to remove a field from MediaMeta.
     * as a quick work around, we can set the MediaMeta.name to NULL and type to MT_Invalid to 'remove' the element
     */
    // bool removeField(const char* name); // for objects, no additional cleanup action; share_ptr is suggested

    /* when a new MediaBuffer is created, copy MediaMeta from existing ones.
     * MediaMeta is required to update after it is processed, a new one is required to not taint the original one
     */
    MediaMetaSP copy() const;  // share pointer

    int32_t size() const { return mMeta.size(); }

    bool empty() const { return mMeta.empty(); }
    /*merge items of mediaMeta to myself*/
    bool merge(MediaMetaSP mediaMeta);
    void dump() const;
    ~MediaMeta();

    struct ByteBuffer {
        size_t  size;
        uint8_t *data;
    };

    struct Fraction {
        int32_t num;
        int32_t denom;
    };

    struct Rect {
         int32_t left, top, right, bottom;
    };

    union MetaValue {
        int8_t ic;
        uint8_t uc;
        int16_t is;
        uint16_t us;
        int32_t ii;
        uint32_t ui;
        float   f;
        double db;
        void *ptr;
        char *str;
        int64_t ld;
        ByteBuffer buf;
        Fraction frac;
        Rect rect;
    };

    enum MetaType {
        MT_Invalid,
        MT_Int32,
        MT_Int64,
        MT_Float,
        MT_Double,
        MT_Pointer,
        MT_String,
        MT_ByteBuffer,
        MT_Fraction,
        MT_Rect,
        MT_Object,
    };

    class MetaItem {
      public:
        MetaType mType;
        MetaValue mValue;
        char *mName;
        uint32_t mNameLength;
        MetaBaseSP mObjSP;

        MetaItem();
        ~MetaItem();
        void clearData();
    };
    typedef std::vector<MetaItem> MMetaVec;

    class iterator {
    public:
        iterator();
        iterator(MMetaVec & vec, int i);
        iterator(const iterator & another);
        ~iterator();

    public:
        iterator operator++();
        iterator operator++(int);
        MetaItem & operator*();
        iterator & operator=(const iterator & another);
        bool operator!=(const iterator & another);

    private:
        MMetaVec * mVec;
        int mI;
    };

    class const_iterator {
    public:
        const_iterator();
        const_iterator(const MMetaVec & vec, int i);
        const_iterator(const const_iterator & another);
        ~const_iterator();

    public:
        const_iterator operator++();
        const_iterator operator++(int);
        const MetaItem & operator*();
        const_iterator & operator=(const const_iterator & another);
        bool operator!=(const const_iterator & another);

    private:
        const MMetaVec * mVec;
        int mI;
    };

    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;

  private:
    MMetaVec mMeta;

    int32_t findMetaIdx(const char* field) const;
    bool setMetaName(MetaItem& meta, const char* name);
    MediaMeta();
    MM_DISALLOW_COPY(MediaMeta)
};

} // namespace YUNOS_MM

#endif // media_meta_h

