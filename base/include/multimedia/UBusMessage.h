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

#ifndef __UBUS_MESSAGE_H__
#define __UBUS_MESSAGE_H__
#include <sys/syscall.h>
#include <unistd.h>

#include "UBusNode.h"
#include <dbus/dbus.h>
#include <dbus/DProxy.h>
#include <dbus/DService.h>
#include <pointer/SharedPtr.h>
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <dbus/DSignalCallback.h>
#include <dbus/DSignalRule.h>

#include "multimedia/mm_types.h"
#include "multimedia/mm_errors.h"
#include "multimedia/media_meta.h"
#include "multimedia/mmparam.h"


namespace YUNOS_MM {

template <class ClientType>
class UBusMessage : public yunos::dbus::DSignalCallback {
public:
    UBusMessage(ClientType *client) : mOwner(client)
                                  , mMsgCondition(mMsgLock)
    {
        ASSERT(mOwner);
        INFO("+\n");
    }
    virtual ~UBusMessage() { INFO("+\n"); }

public:
    mm_status_t sendMsg(MMParamSP param)
    {
    #if 1
        yunos::Looper *callingLooper = yunos::Looper::current();
        yunos::Looper *currentLooper = mOwner->mClientNode->myLooper();
    #else
        // thread id is not portable
        int callingTid = syscall(SYS_gettid);
        int currentTid = mOwner->mClientNode->myLooperThread()->getId();

        VERBOSE("thread equal, callingTid %d\n", callingTid);
    #endif
        if (callingLooper == currentLooper) {
            VERBOSE("equal looper %p", currentLooper);
            // calling from the current loooper, handle msg direct.
            mm_status_t status = mOwner->handleMsg(param);
            if (status != MM_ERROR_SUCCESS) {
                ERROR("send msg failed, status %d", status);
            }
            return status;
        } else {
            // DEBUG("thread not equal, %d, %d\n", callingTid, currentTid);
            VERBOSE("looper is not equal, callingLooper %p, currentLooper %p\n",
                callingLooper, currentLooper);
        }
        MMAutoLock lock(mMsgLock);
        bool ret = mOwner->mClientNode->myLooperThread()->sendTask(yunos::Task(UBusMessage<ClientType>::doSendMsg, this, param));
        if (ret) {
            mMsgCondition.wait();
        } else {
            ERROR("send message task failed, ret %d", ret);
            return MM_ERROR_UNKNOWN;
        }
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t addRule(yunos::SharedPtr<UBusMessage<ClientType> > self)
    {
        const char *serviceName = mOwner->mClientNode->node()->service().c_str();
        mCallbackName.append(serviceName);
        mCallbackName.append(".callback");

        mRule =  mOwner->mClientNode->node()->addSignalRule(mCallbackName, self);
        if (!mRule) {
            ERROR("fail to create rule for callback");
            return MM_ERROR_UNKNOWN;
        }
        DEBUG("add rule for callback success, mCallbackName %s", mCallbackName.c_str());
        return MM_ERROR_SUCCESS;
    }

    virtual void removeRule()
    {
        mOwner->mClientNode->node()->removeSignalRule(mRule);
        DEBUG("remove rule for callback successs, mCallbackName %s", mCallbackName.c_str());
    }


    virtual bool handleSignal(const yunos::SharedPtr<yunos::DMessage>& msg)
    {
        if (!mOwner) {
            ERROR("no owner");
            return false;
        }

        if(msg->signalName() != mCallbackName) {
            ERROR("unknown callback %s", msg->signalName().c_str());
            return false;
        }

        return mOwner->handleSignal(msg);
    }

    yunos::String callbackName() { return mCallbackName; }
public:
    static void doSendMsg(UBusMessage<ClientType>* self, MMParamSP param)
    {
        MMAutoLock lock(self->mMsgLock);
        mm_status_t status = self->mOwner->handleMsg(param);
        self->mMsgCondition.signal();
        if (status != MM_ERROR_SUCCESS) {
            ERROR("handle msg failed, status %d", status);
        }
    }

protected:
    ClientType *mOwner;
    yunos::String mCallbackName;
    yunos::SharedPtr<yunos::dbus::DSignalRule> mRule;
    Lock mMsgLock;
    Condition mMsgCondition;

    MM_DISALLOW_COPY(UBusMessage<ClientType>)
    DECLARE_LOGTAG()
};

template <typename ClientType>
/*static*/ const char * UBusMessage<ClientType>::MM_LOG_TAG = "UBusMessage[MS]";

}

#endif //__UBUS_MESSAGE_H__
