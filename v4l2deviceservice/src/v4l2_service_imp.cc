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

#include <algorithm>
#include <Permission.h>

#include "v4l2_service.h"
#include "v4l2_service_imp.h"
#include "multimedia/UBusNode.h"
#include "multimedia/UBusHelper.h"
#include <unistd.h>

namespace YUNOS_MM {
DEFINE_LOGTAG1(V4l2ServiceImp, [V4L2])
DEFINE_LOGTAG1(V4l2ServiceAdaptor, [V4L2])

using namespace yunos;

#define V4L2_PERMISSION_NAME  "V4L2.permission.yunos.com"

// static
V4l2ServiceAdaptor *gV4l2Service = NULL;
Looper *gV4l2ServiceLoop = NULL;

////////////////////////////////////////////////////////////////////////////////
//V4l2ServiceImp
V4l2ServiceImp::V4l2ServiceImp()
{

}

V4l2ServiceImp::~V4l2ServiceImp()
{
    mServiceNode.reset();
}

bool V4l2ServiceImp::publish()
{
    try {
        mServiceNode = ServiceNode<V4l2ServiceAdaptor>::create("vsa", NULL, this);
        if (!mServiceNode->init()) {
            ERROR("init failed");
            mServiceNode.reset();
            return false;
        }
        gV4l2Service = mServiceNode->node();
        gV4l2ServiceLoop = mServiceNode->myLooper();
        INFO("V4l2ServiceAdaptor pulish success\n");
        return true;
    } catch (...) {
        ERROR("no mem\n");
        return false;
    }
}


/*static*/Looper *V4l2ServiceImp::mainLooper()
{
    return gV4l2ServiceLoop;
}



////////////////////////////////////////////////////////////////////////////////
//V4l2ServiceAdaptor
V4l2ServiceAdaptor::V4l2ServiceAdaptor(const yunos::SharedPtr<yunos::DService>& service,
    yunos::String serviceName, yunos::String pathName, yunos::String iface, void *arg)
        : yunos::DAdaptor(service, yunos::String(IV4l2Service::pathName())
        , yunos::String(IV4l2Service::iface()))
        , mService(static_cast<V4l2ServiceImp*>(arg))

{
    INFO("+\n");
    ASSERT(mService);
    INFO("-\n");
}

V4l2ServiceAdaptor::~V4l2ServiceAdaptor()
{
    INFO("+\n");
    INFO("-\n");
}

bool V4l2ServiceAdaptor::handleMethodCall(const yunos::SharedPtr<yunos::DMessage>& msg)
{
    // static PermissionCheck perm(String(MEDIA_CONTROL_PERMISSION_NAME));
    mm_status_t status = MM_ERROR_SUCCESS;
    yunos::SharedPtr<yunos::DMessage> reply = yunos::DMessage::makeMethodReturn(msg);

    INFO("V4l2(pid: %d, interface: %s) call %s",
        msg->getPid(), interface().c_str(), msg->methodName().c_str());

    if (msg->methodName() == "isLocalNode") {
        pid_t pid = msg->readInt32();
        INFO("pid: %d, ubus pid: %d", pid, msg->getPid());
        ASSERT(pid == msg->getPid());
        bool isLocal = isLocalNode(pid);
        DEBUG("isLocal node %d", isLocal);
        reply->writeBool(isLocal);
    } else if (msg->methodName() == "createNode") {
        uint32_t nodeId = IV4l2Service::kInvalidNodeId;
        createNode(&nodeId);
        DEBUG("nodeId %d", nodeId);
        reply->writeInt32(nodeId);
    } else if (msg->methodName() == "destroyNode") {
        uint32_t nodeId = msg->readInt32();
        destroyNode(nodeId);
    } else {
        ERROR("unknown call: %s\n", msg->methodName().c_str());
        status = MM_ERROR_UNSUPPORTED;
        reply->writeInt32(status);
    }

    sendMessage(reply);
    return true;
}

bool V4l2ServiceAdaptor::isLocalNode(pid_t pid)
{
    return getpid() == pid;
}

mm_status_t V4l2ServiceAdaptor::createNode(uint32_t *nodeId)
{
    ASSERT(nodeId);
    *nodeId = kInvalidNodeId;
    mm_status_t status = MM_ERROR_SUCCESS;

    // mNodeId is thread safe
   V4l2DeviceInstanceSP v4l2Device = V4l2DeviceInstance::create();
    if (!v4l2Device) {
        return MM_ERROR_UNKNOWN;
    }

    status = v4l2Device->createNode(nodeId);
    if (status == MM_ERROR_SUCCESS) {
        mService->mNodeIdToV4l2.insert(std::pair<uint32_t, V4l2DeviceInstanceSP>(*nodeId, v4l2Device));
    }
    return status;
}

void V4l2ServiceAdaptor::destroyNode(uint32_t nodeId)
{
    MMAutoLock locker(mLock);
    auto it1 = mService->mNodeIdToV4l2.find(nodeId);
    if (it1 == mService->mNodeIdToV4l2.end()) {
        DEBUG("cannot find record %d\n", nodeId);
        return;
    }
    it1->second->destroyNode(nodeId);
    mService->mNodeIdToV4l2.erase(it1);
}

/*static*/ void V4l2ServiceAdaptor::rmNode(uint32_t nodeId) {
    if (!gV4l2Service || !gV4l2ServiceLoop) {
        return;
    }
    DEBUG("nodeId %d", nodeId);
    gV4l2Service->destroyNode(nodeId);
}


}


