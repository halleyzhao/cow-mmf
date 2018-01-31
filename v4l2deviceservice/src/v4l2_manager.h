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

#ifndef __V4L2_MANAGER_H__
#define __V4L2_MANAGER_H__
#include <dbus/dbus.h>
#include <dbus/DProxy.h>
#include <dbus/DService.h>
#include <pointer/SharedPtr.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <dbus/DSignalCallback.h>
#include <thread/LooperThread.h>
#include "multimedia/mmparam.h"
#include "multimedia/mm_cpp_utils.h"
#include "iv4l2_service.h"
#include "v4l2_service.h"

namespace YUNOS_MM {
template <class ClientType> class UBusMessage;
template <class UBusType> class ClientNode;

class V4l2ServiceProxy;
class V4l2Manager;
typedef MMSharedPtr<V4l2Manager> V4l2ManagerSP;

class V4l2Manager {
public:
    static V4l2ManagerSP getInstance();
    virtual ~V4l2Manager();

public:
    bool isLocalNode(pid_t pid);
    mm_status_t createNode(uint32_t *nodeId);
    void destroyNode(uint32_t nodeId);

private:
    bool connect();
    mm_status_t handleMsg(YUNOS_MM::MMParamSP param);
    bool handleSignal(const yunos::SharedPtr<yunos::DMessage> &msg) { return true; }
    V4l2Manager();
    static std::string msgName(int msg);

private:
    friend class YUNOS_MM::UBusMessage<V4l2Manager>;
    MMSharedPtr<YUNOS_MM::ClientNode<V4l2ServiceProxy> > mClientNode;
    std::string mSessionId;
    YUNOS_MM::Lock mLock;
    static YUNOS_MM::Lock mInitLock;

    yunos::SharedPtr<YUNOS_MM::UBusMessage<V4l2Manager> > mMessager;

    MM_DISALLOW_COPY(V4l2Manager)
    DECLARE_LOGTAG()
};
}

#endif //__V4L2_MANAGER_H__
