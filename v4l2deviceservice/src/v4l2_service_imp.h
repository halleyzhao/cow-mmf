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

#ifndef __V4L2_SERVICE_IMP_H__
#define __V4L2_SERVICE_IMP_H__
#include <stdlib.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <thread/LooperThread.h>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include "iv4l2_service.h"
#include "v4l2_service.h"
#include "v4l2_device_instance.h"

namespace YUNOS_MM {
template <class UBusType> class ServiceNode;

class V4l2ServiceImp;
typedef MMSharedPtr<V4l2ServiceImp> V4l2ServiceImpSP;
class V4l2ServiceAdaptor;
typedef MMSharedPtr<V4l2ServiceAdaptor> V4l2ServiceAdaptorSP;
typedef MMSharedPtr<YUNOS_MM::ServiceNode<V4l2ServiceAdaptor> > V4l2ServiceNodeSP;


//////////////////////////////////////////////////////////////////////
// V4l2ServiceAdaptor
class V4l2ServiceAdaptor : public yunos::DAdaptor, public IV4l2Service {
public:
    V4l2ServiceAdaptor(const yunos::SharedPtr<yunos::DService> &service,
            yunos::String serviceName, yunos::String pathName,
            yunos::String iface, void *arg);
    virtual ~V4l2ServiceAdaptor();
    bool init() { return true; }

public:
    virtual bool handleMethodCall(const yunos::SharedPtr<yunos::DMessage> &msg);
    virtual bool isLocalNode(pid_t pid);
    virtual mm_status_t createNode(uint32_t *nodeId);
    virtual void destroyNode(uint32_t nodeId);
    static void rmNode(uint32_t nodeId);

private:
    friend class V4l2ServiceImp;
    yunos::String mUniqueId;
    V4l2ServiceImp *mService;
    YUNOS_MM::Lock mLock;

    DECLARE_LOGTAG();
    MM_DISALLOW_COPY(V4l2ServiceAdaptor);
};

////////////////////////////////////////////////////////////////////////////////
// V4l2ServiceImp
class V4l2ServiceImp : public V4l2Service {
public:
    V4l2ServiceImp();

    virtual ~V4l2ServiceImp();

public:
    bool publish();
    static yunos::Looper *mainLooper();

private:
    friend class V4l2ServiceAdaptor;

    V4l2ServiceNodeSP mServiceNode;
    yunos::SharedPtr<yunos::LooperThread> mLooper;
    YUNOS_MM::Lock mLock;
    yunos::String mCallbackName;
    std::map<uint32_t, V4l2DeviceInstanceSP> mNodeIdToV4l2;

    DECLARE_LOGTAG()
    MM_DISALLOW_COPY(V4l2ServiceImp)
};
}

#endif //__V4L2_SERVICE_IMP_H__
