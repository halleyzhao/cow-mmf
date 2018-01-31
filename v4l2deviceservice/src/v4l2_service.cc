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

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_debug.h"

#include "v4l2_service.h"
#include "v4l2_service_imp.h"

namespace YUNOS_MM {
DEFINE_LOGTAG1(V4l2Service, [V4L2])

using namespace yunos;

Lock V4l2Service::mNodeIdLock;
uint32_t V4l2Service::mNodeId = 1; //started from 1, 0 is invalid

////////////////////////////////////////////////////////////////////////////////
//V4l2Service
// Called by media_service
/*static */V4l2ServiceSP V4l2Service::publish()
{
    try {
        V4l2ServiceImp * imp = new V4l2ServiceImp();
        if (!imp || !imp->publish()) {
            ERROR("failed to publish\n");
            delete imp;
            return V4l2ServiceSP((V4l2Service*)NULL);
        }

        INFO("V4l2 service pulish success!!!");
        return V4l2ServiceSP((V4l2Service*)imp);
    } catch (...) {
        ERROR("no mem\n");
        return V4l2ServiceSP((V4l2Service*)NULL);
    }
}

/*static*/uint32_t V4l2Service::getNodeId()
{
    MMAutoLock locker(mNodeIdLock);

    if (mNodeId == 1 << 16) {
        return IV4l2Service::kInvalidNodeId;
    }

    return mNodeId++;
}

}
