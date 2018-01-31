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

#ifndef __I_V4L2_DEVICE_H__
#define __I_V4L2_DEVICE_H__

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mm_cpp_utils.h"
//#include "graphic_buffer.h"

namespace YUNOS_MM {
typedef void* BufferIdType;
typedef void* V4L2_PTR;

class IV4l2Device {

public:
    IV4l2Device() : mV4l2Fd(0) {}
    virtual ~IV4l2Device() {}
    virtual mm_status_t createNode(uint32_t *node) = 0;
    virtual void destroyNode(uint32_t nodeId) = 0;
    virtual bool open(uint32_t nodeId, const char* name, uint32_t flags) = 0;
    virtual bool close(uint32_t nodeId) = 0;
    virtual int32_t ioctl(uint32_t nodeId, uint64_t request, void* arg) = 0;
    virtual int32_t poll(uint32_t nodeId, bool poll_device, bool* event_pending) = 0;
    virtual int32_t setDevicePollInterrupt(uint32_t nodeId) = 0;
    virtual int32_t clearDevicePollInterrupt(uint32_t nodeId) = 0;
    virtual void*   mmap(uint32_t nodeId, void* addr, size_t length, int32_t prot,
                    int32_t flags, unsigned int offset) = 0;
    virtual int32_t munmap(uint32_t nodeId, void* addr, size_t length) = 0;
    virtual int32_t setParameter(uint32_t nodeId, const char* key, const char* value) = 0;

protected:
    int32_t mV4l2Fd;

public:
    static const char *serviceName() { return "com.yunos.v4l2"; }
    static const char *pathName() { return "/com/yunos/v4l2"; }
    static const char *iface() { return "com.yunos.v4l2.interface"; }
};
}

#endif //__I_V4L2_DEVICE_H__
