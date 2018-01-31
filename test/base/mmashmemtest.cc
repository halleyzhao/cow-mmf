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
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "multimedia/mm_ashmem.h"

#include "multimedia/mm_debug.h"
#if defined(OS_YUNOS)
#include <thread/LooperThread.h>
#include <string/String.h>
#include <dbus/DServiceManager.h>
#include <pointer/SharedPtr.h>
#include <dbus/DMessage.h>
#include <dbus/DService.h>
#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>
#endif


MM_LOG_DEFINE_MODULE_NAME("MMASHMEM")

using namespace yunos;
using namespace YUNOS_MM;


class MMAshMemTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

#if (defined OS_YUNOS )

static const char* gAshMemName = "com.yunos.mm.ashmem";
static const char* gAshMemObjectPath = "/com/yunos/mm/ashmem";
static const char* gAshMemInterface = "com.yunos.mm.ashmem.interface";

class ProxyTest : public DProxy {

public:
    ProxyTest(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface) : DProxy(service, path, iface) {

    }

    virtual ~ProxyTest() {}
    bool create_ashmem() {

        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("create_ashmem"));
        size_t size = 32;
        mAshMem = MMAshMem::create("ProxyTest", size);
        msg->writeInt32(size);
        msg->writeFd(mAshMem->getKey());
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);
        int32_t ret = -1;
        if (reply) {
            ret = reply->readInt32();
            INFO("ret %d\n", ret);
            return true;
        } else {
            ASSERT(0);
            ERROR("invalid reply");
        }
        return false;
    }

    bool test_ashmem() {

        for (int i = 0; i < 16; i++) {
            SharedPtr<DMessage> msg = obtainMethodCallMessage(String("test_ashmem"));
            uint8_t *data = (uint8_t*)mAshMem->getBase();
            size_t size = mAshMem->getSize();
            memset(data, i, size);
            msg->writeInt32(i);
            SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);
            int32_t ret = -1;
            if (reply) {
                ret = reply->readInt32();

                //dump buffer, changed by adaptor
                hexDump(data, size, 32);
                INFO("ret %d\n", ret);
            } else {
                ASSERT(0);
                ERROR("invalid reply");
                return false;
            }
        }
        return true;
    }

private:
    MMAshMemSP mAshMem;
};

class AshMemTestIpc {
public:
    AshMemTestIpc() : mCondition(mLock) {

        SharedPtr<LooperThread> mLooper = new LooperThread();
        mLooper->start("MetaTest");
        if (mLooper->isRunning() != true) {
            INFO("LooperThread is not running\n");
            return;
        }
        mLooper->sendTask(Task(AshMemTestIpc::createProxy, this));
        mCondition.wait();
    }

   static void createProxy(AshMemTestIpc *instance) {

        INFO("create proxy: service(%s) path(%s) interface(%s)\n",
             gAshMemName, gAshMemObjectPath, gAshMemInterface);

        instance->mManager = DServiceManager::getInstance();
        instance->mService = instance->mManager->getService(String(gAshMemName));
        if (!instance->mService) {
            INFO("getService %s failed\n", gAshMemName);
            instance->mCondition.signal();
            return;
        }
        instance->mProxyTest = new ProxyTest(instance->mService, String(gAshMemObjectPath), String(gAshMemInterface));

        instance->mCondition.signal();
    }

    bool create_ashmem() {
        if (!mProxyTest) {
            WARNING("proxy is invalid");
            return false;
        }
        return mProxyTest->create_ashmem();
    }

    bool test_ashmem() {
        if (!mProxyTest) {
            WARNING("proxy is invalid");
            return false;
        }
        return mProxyTest->test_ashmem();
    }

private:
    Lock mLock;
    YUNOS_MM::Condition mCondition;

    SharedPtr<DServiceManager> mManager;
    SharedPtr<DService> mService;
    SharedPtr<ProxyTest> mProxyTest;
};
#endif

TEST_F(MMAshMemTest, mmashmemtest) {

#if defined(OS_YUNOS)
    AshMemTestIpc *testObjectIpc = new AshMemTestIpc();
    EXPECT_TRUE(testObjectIpc->create_ashmem());
    EXPECT_TRUE(testObjectIpc->test_ashmem());
    delete testObjectIpc;
    testObjectIpc = NULL;
#endif

    INFO("done\n");
}

int main(int argc, char* const argv[]) {
  int ret;
  MMLOGD("testing begin\n");
  try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
   } catch (testing::internal::GoogleTestFailureException) {
        MMLOGE("InitGoogleTest failed!");
        return -1;
   } catch (...) {
        MMLOGE("unknown exception!");
        return -1;
   }
  usleep(3000000);
  MMLOGD("exit\n");
  return ret;
}

