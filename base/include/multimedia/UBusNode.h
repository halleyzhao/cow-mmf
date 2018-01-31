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


#ifndef __UBusNode_H
#define __UBusNode_H

#include <dbus/DServiceManager.h>
#include <dbus/DProxy.h>
#include <dbus/DService.h>
#include <pointer/SharedPtr.h>
#include <looper/Looper.h>
#include <thread/LooperThread.h>
#include <multimedia/mm_debug.h>
#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {

template <typename UBusType, typename NodeType>
class UBusNode {
public:
    UBusNode(const char * nodeName, const char *id, void *arg, bool isServer)
                : mLooperThread(NULL),
                mLooper(NULL),
                mCondition(mLock),
                mNodeName(nodeName),
                mNode(NULL),
                mUserData(arg),
                mUniqueId(id),
                mIsServer(isServer)
    {
        MMLOGI("[%s] mIsServer %d +\n", nodeName, mIsServer);
        MMLOGV("[%s]-\n", nodeName);
    }

    virtual ~UBusNode()
    {
        MMLOGI("[%s]+\n", mNodeName.c_str());

        MMLOGV("[%s]-\n", mNodeName.c_str());
    }

    static void releaseHelper(UBusNode<UBusType, NodeType> *ubusNode)
    {
        if (!ubusNode)
            return;
        MMLOGD("+");
        ubusNode->uninit();
        delete ubusNode;
        ubusNode = NULL;
        MMLOGD("-");
    }

    bool init()
    {
        MMLOGI("[%s]+\n", mNodeName.c_str());
        try {
            mLooperThread = new yunos::LooperThread();
        } catch (...) {
            MMLOGE("[%s]no mem\n", mNodeName.c_str());
            return false;
        }

        if (mLooperThread->start(mNodeName)) {
            MMLOGE("[%s]failed to start\n", mNodeName.c_str());
            mLooperThread = NULL;
            return false;
        }

        {
            MMAutoLock lock(mLock);
            bool ret = mLooperThread->sendTask(yunos::Task(UBusNode<UBusType, NodeType>::createNode, this));
            MMASSERT(ret);
            MMLOGV("[%s]waitting\n", mNodeName.c_str());
            mCondition.wait();
        }
        MMLOGV("[%s]waitting over\n", mNodeName.c_str());

        if (!mNode) {
            MMLOGE("[%s]failed to create node\n", mNodeName.c_str());
            mLooperThread->requestExitAndWait();
            mLooperThread = NULL;
            return false;
        }

        MMLOGV("[%s]-\n", mNodeName.c_str());
        return true;
    }

    void uninit()
    {
        MMLOGI("[%s]+\n", mNodeName.c_str());
        if (!mLooperThread) {
            WARNING("mLooperThread is null, return");
            return;
        }

        // may be called from Adaptor:onDeath
        if (yunos::Looper::current() == mLooper) {
            doDestroyNode();
            // quit thread
            yunos::Looper::current()->sendTask(yunos::Looper::getQuitTask());
        } else {
            {
                MMAutoLock lock(mLock);
                bool ret = mLooperThread->sendTask(yunos::Task(UBusNode<UBusType, NodeType>::destroyNode, this));
                MMASSERT(ret);
                MMLOGV("[%s]waitting\n", mNodeName.c_str());
                mCondition.wait();
            }
            MMLOGV("[%s]waitting over\n", mNodeName.c_str());

            if (mLooperThread) {
                mLooperThread->requestExitAndWait();
            }
            MMLOGV("[%s]-\n", mNodeName.c_str());
        }
        mLooper = NULL;
    }

    static void destroyNode(UBusNode<UBusType, NodeType> * self)
    {
        MMLOGI("[%s]+\n", self->mNodeName.c_str());
        MMASSERT(self);
        self->doDestroyNode();
        MMLOGV("[%s]-\n", self->mNodeName.c_str());
    }

    void doDestroyNode()
    {
        MMLOGD("[%s] destroy +\n", mNodeName.c_str());
        MMASSERT(mManager);
        MM_RELEASE(mNode);

        if (mIsServer) {
            mManager->unregisterService(mSvc);
        }
        mManager.reset();
        mSvc.reset();

        {
            MMAutoLock lock(mLock);
            mCondition.signal();
        }
        MMLOGD("[%s] destroy -\n", mNodeName.c_str());
    }

    static void createNode(UBusNode<UBusType, NodeType> * node)
    {
        MMLOGI("[%s]+\n", node->mNodeName.c_str());
        MMASSERT(node);
        node->doCreateNode();
        MMLOGV("[%s]-\n", node->mNodeName.c_str());
    }

    void doCreateNode()
    {
        MMLOGV("[%s]+\n", mNodeName.c_str());
        mManager = yunos::DServiceManager::getInstance();
        if (!mManager) {
            MMLOGI("[%s]failed to get mManager\n", mNodeName.c_str());
            return;
        }
        mNode = NodeType::prepare(mManager, mNodeName.c_str(), mUniqueId.c_str(), mUserData, mSvc);
        mLooper = yunos::Looper::current();
        {
            MMAutoLock lock(mLock);
            mCondition.signal();
        }
        MMLOGV("[%s]-\n", mNodeName.c_str());
    }

    UBusType * node() const { return mNode; }

    // return looperThread
    yunos::SharedPtr<yunos::LooperThread> myLooperThread() { return mLooperThread; }

    // return looper
    yunos::Looper* myLooper() { return mLooper; }

protected:
    yunos::SharedPtr<yunos::LooperThread> mLooperThread;
    yunos::Looper* mLooper;
    Lock mLock;
    Condition mCondition;
    yunos::String mNodeName;
    UBusType * mNode;
    void *mUserData;
    yunos::String mUniqueId;
    yunos::SharedPtr<yunos::DService> mSvc;
    yunos::SharedPtr<yunos::DServiceManager> mManager;
    bool mIsServer;

    DECLARE_LOGTAG()
};

template <typename UBusType, typename NodeType>
/*static*/ const char * UBusNode<UBusType, NodeType>::MM_LOG_TAG = "UBusNode";

template <class UBusType> class ServiceNode;


template <typename UBusType>
class ServiceNode : public UBusNode<UBusType, ServiceNode<UBusType> > {
public:
    ServiceNode(const char * nodeName, const char* id = NULL, void *arg = NULL)
        : UBusNode<UBusType, ServiceNode<UBusType> >(nodeName, id, arg, true)
    {
        MMLOGI("[%s]+\n", nodeName);
        MMLOGV("[%s]-\n", nodeName);
    }

    virtual ~ServiceNode()
    {
        MMLOGI("[%s]+\n", UBusNode<UBusType, ServiceNode<UBusType> >::mNodeName.c_str());
        MMLOGV("[%s]\n", UBusNode<UBusType, ServiceNode<UBusType> >::mNodeName.c_str());
    }

    static MMSharedPtr<ServiceNode<UBusType> > create(const char * nodeName, const char* id = NULL, void *arg = NULL)
    {
        MMSharedPtr<ServiceNode<UBusType> > node(new ServiceNode<UBusType>(nodeName, id, arg),
            UBusNode<UBusType, ServiceNode<UBusType> >::releaseHelper);

        return node;
    }
public:
    static UBusType * prepare(yunos::SharedPtr<yunos::DServiceManager> mgr,
        const char * nodeName, const char* id, void *arg,
        yunos::SharedPtr<yunos::DService> &svc)
    {
        MMLOGI("[%s]+\n", nodeName);

        yunos::String serviceName(UBusType::serviceName());
        yunos::String pathName(UBusType::pathName());
        yunos::String iface = serviceName;
        if (id) {
            serviceName.append(id);
            pathName.append(id);
            iface = serviceName;
        }
        iface.append(".interface");
        MMLOGI("id %s, serviceName %s, pathName %s, iface %s",
            id, serviceName.c_str(), pathName.c_str(), iface.c_str());

        svc = mgr->registerService(serviceName);
        if (!svc) {
            MMLOGI("[%s]failed to register service\n", nodeName);
            return NULL;
        }

        try {
            UBusType * adp = new UBusType(svc, serviceName, pathName, iface, arg);
            if (!adp->init()) {
                MMLOGE("[%s]failed to init\n", nodeName);
                delete adp;
                throw 1;
            }
            adp->publish();
            MMLOGI("[%s] adaptor create success\n", serviceName.c_str());
            return adp;
        } catch (...) {
            MMLOGE("[%s]no mem\n", nodeName);
            mgr->unregisterService(svc);
            return NULL;
        }
    }
public:
    DECLARE_LOGTAG()
};

template <typename UBusType>
/*static*/ const char * ServiceNode<UBusType>::MM_LOG_TAG = "UBusSNode";

template <class UBusType> class ClientNode;
//typedef MMSharedPtr<ClientNode<class UBusType> > UBusClientNodeSP;


template <typename UBusType>
class ClientNode : public UBusNode<UBusType, ClientNode<UBusType> > {
public:
    ClientNode(const char * nodeName, const char* id = NULL, void *arg = NULL)
        : UBusNode<UBusType, ClientNode<UBusType> >(nodeName, id, arg, false)
    {
        MMLOGI("[%s]+\n", nodeName);
        MMLOGV("[%s]-\n", nodeName);
    }

    virtual ~ClientNode()
    {
        MMLOGI("[%s]+\n", UBusNode<UBusType, ClientNode<UBusType> >::mNodeName.c_str());
        MMLOGV("[%s]-\n", UBusNode<UBusType, ClientNode<UBusType> >::mNodeName.c_str());
    }

    static MMSharedPtr<ClientNode<UBusType> > create(const char * nodeName, const char* id = NULL, void *arg = NULL)
    {
        MMSharedPtr<ClientNode<UBusType> > node(new ClientNode<UBusType>(nodeName, id, arg),
            UBusNode<UBusType, ClientNode<UBusType> >::releaseHelper);

        return node;
    }

public:
    static UBusType * prepare(yunos::SharedPtr<yunos::DServiceManager> mgr,
        const char * nodeName, const char *id, void *arg,
        yunos::SharedPtr<yunos::DService> &svc)
    {
        MMLOGI("[%s]+\n", nodeName);

        yunos::String serviceName(UBusType::serviceName());
        yunos::String pathName(UBusType::pathName());
        yunos::String iface = serviceName;
        if (id) {
            serviceName.append(id);
            pathName.append(id);
            iface = serviceName;
        }
        iface.append(".interface");

        MMLOGI("id %s, serviceName %s, pathName %s, iface %s",
            id, serviceName.c_str(), pathName.c_str(), iface.c_str());

        svc = mgr->getService(serviceName);
        if (!svc) {
            MMLOGE("[%s]failed to get service\n", nodeName);
            return NULL;
        }

        try {
            UBusType * clt = new UBusType(svc, serviceName, pathName, iface, arg);
            if (!clt) {
                MMLOGE("[%s] create failed, no mem\n", serviceName.c_str());
                throw 1;
            }
            MMLOGI("[%s] proxy create success\n", serviceName.c_str());
            return clt;
        } catch (...) {
            MMLOGE("[%s]no mem\n", nodeName);
            return NULL;
        }
    }

    DECLARE_LOGTAG()
};

template <typename UBusType>
/*static*/ const char * ClientNode<UBusType>::MM_LOG_TAG = "UBusCNode";

}

#endif
