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

#include <unistd.h>
#include <algorithm>
#include <dbus/DSignalRule.h>
#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
#include "multimedia/UBusHelper.h"
#include "multimedia/UBusNode.h"
#include "multimedia/UBusMessage.h"
#include "v4l2_manager.h"

using namespace yunos;

namespace YUNOS_MM {
enum {
    MSG_isLocalNode = 0,
    MSG_createNode,
    MSG_destroyNode
};


#define MSG_STR(MSG) do{\
    return ##MSG; \
}while(0)
static int const mTimeOut = -1;


//////////////////////////////////////////////////////////////////////
// V4l2ServiceProxy
class V4l2ServiceProxy : public yunos::DProxy, public IV4l2Service {
public:
    V4l2ServiceProxy(const yunos::SharedPtr<yunos::DService> &service,
            yunos::String serviceName, yunos::String pathName,
            yunos::String iface, void *arg);
    virtual ~V4l2ServiceProxy();

public:
    virtual bool isLocalNode(pid_t pid);
    virtual mm_status_t createNode(uint32_t *nodeId);
    virtual void destroyNode(uint32_t nodeId);

public:
    yunos::Looper *looper() { return mLooper; }

private:
    yunos::Looper *mLooper;
    MM_DISALLOW_COPY(V4l2ServiceProxy)
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG1(V4l2Manager, [V4L2])
DEFINE_LOGTAG1(V4l2ServiceProxy, [V4L2])


////////////////////////////////////////////////////////////////////////////
//V4l2Manager
Lock V4l2Manager::mInitLock;
static V4l2ManagerSP mV4l2Manager;
/*static */V4l2ManagerSP V4l2Manager::getInstance()
{
    INFO("+\n");
    MMAutoLock locker(mInitLock);
    
    try {
        if (mV4l2Manager) {
            VERBOSE("already exist, return %p\n", mV4l2Manager.get());
            return mV4l2Manager;
        }
        V4l2Manager* manager = new V4l2Manager();
        if (!manager->connect()) {
            ERROR("failed to connect\n");
            delete manager;
            return V4l2ManagerSP((V4l2Manager*)NULL);
        }
        mV4l2Manager.reset(manager);
        return mV4l2Manager;
    } catch (...) {
        ERROR("no mem\n");
        return V4l2ManagerSP((V4l2Manager*)NULL);
    }
}

V4l2Manager::V4l2Manager()
{
    INFO("+\n");
}

V4l2Manager::~V4l2Manager()
{
    INFO("+\n");
    mClientNode.reset();
}

bool V4l2Manager::connect()
{
    INFO("+\n");
    try {
        mClientNode.reset(new ClientNode<V4l2ServiceProxy>("vsp"));
        if (!mClientNode->init()) {
            ERROR("failed to init\n");
            return false;
        }

        INFO("success\n");
        mMessager = new UBusMessage<V4l2Manager>(this);
        return true;
    } catch (...) {
        ERROR("no mem\n");
        return false;
    }
}

mm_status_t V4l2Manager::handleMsg(MMParamSP param)
{
    int msg = param->readInt32();
    DEBUG("name %s", msgName(msg).c_str());

    mm_status_t status = MM_ERROR_SUCCESS;
    switch (msg) {
        case MSG_isLocalNode:
        {
            pid_t pid = param->readInt32();
            bool isLocal = mClientNode->node()->isLocalNode(pid);
            param->writeInt32(isLocal);
            break;
        }
        case MSG_createNode:
        {
            uint32_t nodeId = IV4l2Service::kInvalidNodeId;
            mm_status_t status = mClientNode->node()->createNode(&nodeId);
            if (status != MM_ERROR_SUCCESS ||
                nodeId == IV4l2Service::kInvalidNodeId) {
                ERROR("nodeId is invalid");
                status = MM_ERROR_UNKNOWN;
            }

            param->writeInt32(nodeId);
            break;
        }
        case MSG_destroyNode:
        {
            uint32_t nodeId = param->readInt32();
            mClientNode->node()->destroyNode(nodeId);
            break;
        }

        default:
        {
            ASSERT(0 && "Should not be here\n");
            break;
        }
    }

    return status;
}

bool V4l2Manager::isLocalNode(pid_t pid)
{
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_isLocalNode);
    param->writeInt32((int32_t)pid);
    mMessager->sendMsg(param);
    return param->readInt32();
}

mm_status_t V4l2Manager::createNode(uint32_t *nodeId)
{
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_createNode);
    mm_status_t status = mMessager->sendMsg(param);
    *nodeId = param->readInt32();
    return status;
}

void V4l2Manager::destroyNode(uint32_t nodeId)
{
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_destroyNode);
    param->writeInt32(nodeId);
    mMessager->sendMsg(param);
	return;
}


std::string V4l2Manager::msgName(int msg)
{
#define MSG_NAME(MSG) case (MSG): return #MSG;
    switch (msg) {
        MSG_NAME(MSG_isLocalNode);
        MSG_NAME(MSG_createNode);
        MSG_NAME(MSG_destroyNode);
        default: return "unknown msg";
    }
    return "unknown msg";
}

////////////////////////////////////////////////////////////////////////////////////
V4l2ServiceProxy::V4l2ServiceProxy(const yunos::SharedPtr<yunos::DService>& service,
    yunos::String serviceName, yunos::String pathName, yunos::String iface, void *arg)
        : yunos::DProxy(service, yunos::String(IV4l2Service::pathName())
        , yunos::String(IV4l2Service::iface()))
{
    INFO("+\n");
    mLooper = Looper::current();
    ASSERT(mLooper);
    DEBUG("mLooper %p", mLooper);
}

V4l2ServiceProxy::~V4l2ServiceProxy()
{
    INFO("+\n");
}

bool V4l2ServiceProxy::isLocalNode(pid_t pid)
{
    CHECK_OBTAIN_MSG_RETURN1("isLocalNode", false);
    msg->writeInt32((int32_t)pid);
    CHECK_REPLY_MSG_RETURN_BOOL();
}

mm_status_t V4l2ServiceProxy::createNode(uint32_t *nodeId)
{
    CHECK_OBTAIN_MSG_RETURN1("createNode", IV4l2Service::kInvalidNodeId);
    HANDLE_INVALID_REPLY_RETURN_INT();

    *nodeId = reply->readInt32();
    DEBUG("nodeId %d", *nodeId);
    return MM_ERROR_SUCCESS;
}

void V4l2ServiceProxy::destroyNode(uint32_t nodeId)
{
    CHECK_OBTAIN_MSG_RETURN0("destroyNode");
    msg->writeInt32(nodeId);
    HANDLE_REPLY_MSG_RETURN0();
}
}
