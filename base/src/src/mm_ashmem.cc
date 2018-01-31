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

#define LOG_TAG "MMAshMem"
#include <unistd.h>

#ifdef USE_ASHMEM_RPC
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "multimedia/ashmem.h"
#endif

#include "multimedia/mm_ashmem.h"
#include <multimedia/mm_debug.h>
#include <multimedia/mm_cpp_utils.h>

namespace YUNOS_MM {
DEFINE_LOGTAG(MMAshMem)

class MMAshMemImpl {
public:
    /**
     *   @brief MMAshMemImpl as wrapper for MMAshMem to provide more cleaner interface.
     *   @create MMAshMemImpl object by provided key and size and readonly or not.
     *   @note that key maybe come from another MMAshMem which even in different process.
     *   @return VCMSHMMemImpl object.
     */
    MMAshMemImpl(key_t key, size_t size, bool readonly=true, bool needrm=false);

    /**
    *   @brief destroy MMAshMemImpl, its key and shard memory buffer will be unmap and delete.
    */
    virtual ~MMAshMemImpl();

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
    MMAshMemImpl(const MMAshMemImpl &other);
    void operator=(const MMAshMemImpl &other);
    key_t   mKey;
    int32_t mSID;
    size_t  mSize;
    void*   mBuffer;
    bool    mReadOnly;
    bool    mNeedRM;
    DECLARE_LOGTAG();
};

DEFINE_LOGTAG(MMAshMemImpl)

MMAshMemImpl::MMAshMemImpl(key_t key, size_t size, bool readonly, bool needrm)
    : mKey(key),
      mSID(-1),
      mSize(size),
      mBuffer(NULL),
      mReadOnly(readonly),
      mNeedRM(needrm)
{
}

MMAshMemImpl::~MMAshMemImpl() {
    if (mBuffer) {
#ifdef USE_ASHMEM_RPC
        munmap(mBuffer, mSize);
        close(mKey);
        INFO("destroy ashmem key %d.\n", mKey);
#else
        shmdt(mBuffer);
#endif
        mBuffer = NULL;
    }
    if (mNeedRM) {
#ifdef USE_ASHMEM_RPC
#else
        shmctl(mSID, IPC_RMID, NULL);
#endif
    }
}

void* MMAshMemImpl::getBase() {
    if (mSize <= 0)
        return NULL;
    if (mBuffer) {
        return mBuffer;
    }
#ifdef USE_ASHMEM_RPC
    void* ret = mmap(NULL, mSize, PROT_READ | PROT_WRITE, MAP_SHARED, mKey, 0);
    if (ret == MAP_FAILED) {
        ERROR("ERROR ashmem map fail:%d %s", errno, strerror(errno));
        return NULL;
    }
#else
    mSID = shmget(mKey, mSize, IPC_CREAT | (mReadOnly ? 0400 : 0777));
    if (mSID < 0) {
        ERROR("failed to get shmid.\n");
        return NULL;
    }
    void* ret = shmat(mSID, NULL, mReadOnly ? SHM_RDONLY : 0);
    if (ret == (void *)-1) {
        ERROR("failed to shm map.\n");
        return NULL;
    }
#endif
    mBuffer = ret;
    return ret;
}

key_t MMAshMemImpl::getKey() const {
    return mKey;
}

size_t MMAshMemImpl::getSize() const {
    return mSize;
}

//static
MMAshMemSP MMAshMem::create(const char* name, size_t size)
{
    MMAshMemSP mem;
    if (size <= 0)
        return mem;

    key_t shmkey;
#ifdef USE_ASHMEM_RPC
    shmkey = ashmem_create_block(name, size);
    ashmem_set_prot_block(shmkey, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (shmkey < 0) {
        ERROR("failed to create ashmem.\n");
        exit(shmkey);
    }

    INFO("create ashmem key %d.\n", shmkey);
#else
    unsigned loops = 0;
    int shmid = -1;
    do {
        shmkey = getpid() + (rand() % 1000);
        shmid = shmget(shmkey, size, IPC_CREAT | IPC_EXCL | 0777);
    } while (shmid < 0 && loops++ < 100);
    if (shmid < 0) {
        ERROR("failed to get shmid.\n");
        return mem;
    }
#endif
    mem.reset(new MMAshMem(shmkey, size, false, true));
    return mem;
}

MMAshMem::MMAshMem(key_t key, size_t size, bool readonly, bool needrm)
{
    mImpl = new MMAshMemImpl(key, size, readonly, needrm);
    // try get base buffer prepare first.
    mImpl->getBase();
}

MMAshMem::~MMAshMem() {
    if (mImpl)
        delete mImpl;
}

void* MMAshMem::getBase() {
    void* ret = NULL;
    if (mImpl)
        ret = mImpl->getBase();
    return ret;
}

key_t MMAshMem::getKey() const {
    key_t key = 0;
    if (mImpl)
        key = mImpl->getKey();
    return key;
}

size_t MMAshMem::getSize() const {
    size_t size = 0;
    if (mImpl)
        size = mImpl->getSize();
    return size;
}

};

