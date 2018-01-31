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

#ifndef __I_V4L2_SERVICE_H__
#define __I_V4L2_SERVICE_H__

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {

class IV4l2Service {
public:
    enum V4l2Type {
        kTypeNone = 0,
        kTypeLocal = 1 << 0,
        kTypeProxy = 1 << 1
    };
    enum {
        kInvalidNodeId = 0
    };

public:
    IV4l2Service() : mV4l2Type(false) {}
    virtual ~IV4l2Service() {}

    virtual bool isLocalNode(pid_t pid) = 0;
    virtual mm_status_t createNode(uint32_t *nodeId) = 0;
    virtual void destroyNode(uint32_t nodeId) = 0;
public:
    bool mV4l2Type;

public:
    static const char *serviceName() { return "com.yunos.v4l2.service"; }
    static const char *pathName() { return "/com/yunos/v4l2/service"; }
    static const char *iface() { return "com.yunos.v4l2.service.interface"; }
};
}

#endif //__I_V4L2_SERVICE_H__
