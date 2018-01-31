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

#ifndef __MM_ASHMEM_H__
#define __MM_ASHMEM_H__
#include <sys/types.h>
#include <sys/shm.h>
#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

namespace YUNOS_MM {
class MMAshMemImpl;
class MMAshMem;
typedef MMSharedPtr<MMAshMem> MMAshMemSP;

// Platform abstraction for shared memory.  Provides a C++ wrapper
// around the OS primitive for a memory mapped file.
class MMAshMem {
public:
    /* static functions */
    /**
    *   @brief Returns MMAshMem object, when pass name and size.
    *   @note it is used for create share buffer with name&size,
        @after create valid MMAshMem object pointer,use getKey to get key,
        @and pass key to remote process,
        @after create valid MMAshMem object pointer,use getBase to get shared buffer pointer,
        @which is ready for read/write data.
        @if return MMAshMem object pointer is valid, after finish use it, you should delete it.
    *   @return one valid pointer to MMAshMem object or NULL.
    */
    static MMAshMemSP create(const char* name, size_t size);

    /**
    *   @brief create MMAshMem object by provided key and size and readonly or not.
    *   @note that key maybe come from another MMAshMem which even in different process.
    *   @return MMAshMem object.
    */
    MMAshMem(key_t key, size_t size, bool readonly=true, bool needrm=false);

    /**
    *   @brief destroy MMAshMem, its key and shard memory buffer will be unmap and delete.
    */
    virtual ~MMAshMem();

    /**
    *   @brief get shared memory key, it can be passed to another process to use.
    */
    key_t getKey() const;

    /**
    *   @brief get shared memory pointer, if it's not null, you can read/write content for it.
    */
    void* getBase();

    /**
    *   @brief get size for shared memory.
    */
    size_t getSize() const;

private:
    MMAshMem(const MMAshMem &other);
    void operator=(const MMAshMem &other);
    MMAshMemImpl* mImpl;

    DECLARE_LOGTAG();
};

};
#endif
