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

#ifndef __V4L2_SERVICE_H__
#define __V4L2_SERVICE_H__

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

namespace YUNOS_MM {
class V4l2Service;
typedef MMSharedPtr<V4l2Service> V4l2ServiceSP;

class V4l2Service {
public:
    static V4l2ServiceSP publish();

public:
    virtual ~V4l2Service() {}
    static uint32_t getNodeId();

protected:
    V4l2Service() {}

protected:
    static YUNOS_MM::Lock mNodeIdLock;
    static uint32_t mNodeId;
    DECLARE_LOGTAG()
    MM_DISALLOW_COPY(V4l2Service)
};
}

#endif //__V4L2_SERVICE_H__
