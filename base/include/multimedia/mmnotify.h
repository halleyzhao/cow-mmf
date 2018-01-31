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

#ifndef __MMNotify_H
#define __MMNotify_H

#include <vector>
#include "multimedia/mm_types.h"

namespace YUNOS_MM {

class MMMsgThread;

class MMNotify
{
public:
    MMNotify();
    MMNotify(const MMNotify &notify);

    virtual ~MMNotify();

    //int what() const {return mWhat};
    void setWhat(uint32_t what);
    bool postAsync(int64_t delayUs = 0);

    bool registerThreadHandler(MMMsgThread*);

    // handle message parameter
public:
    virtual bool setInt32(const char *name, int32_t val) = 0;
    virtual bool setInt64(const char *name, int64_t val) = 0;
    virtual bool setFloat(const char *name, float val) = 0;
    virtual bool setPointer(const char *name, void *val) = 0;

public:
    virtual bool findInt32(const char *name, int32_t *val) = 0;
    virtual bool findInt64(const char *name, int64_t *val) = 0;
    virtual bool findFloat(const char *name, float *val) = 0;
    virtual bool findPointer(const char *name, void **val) = 0;

private:
    uint32_t mWhat;

    // should be share pointer instead ??
    MMMsgThread *mMMMsgthread;
};

}
#endif //__MMNotify_H
