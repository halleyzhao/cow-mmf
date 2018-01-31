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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "looper/Looper.h"

#include <string/String.h>
#include <dbus/DServiceManager.h>
#include <pointer/SharedPtr.h>
#include <dbus/DMessage.h>
#include <dbus/DService.h>
#include <dbus/DProxy.h>
#include <dbus/DAdaptor.h>

#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/mm_ashmem.h"


using namespace YUNOS_MM;
using namespace yunos;
MM_LOG_DEFINE_MODULE_NAME("adaptor-ashmem");


static const char* gAshMemName = "com.yunos.mm.ashmem";
static const char* gAshMemObjectPath = "/com/yunos/mm/ashmem";
static const char* gAshMemInterface = "com.yunos.mm.ashmem.interface";


class AshMemAdaptor : public DAdaptor {
public:
    AshMemAdaptor(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface) : DAdaptor(service, path, iface) {
                       INFO("Adaptor path %s, iface %s\n", path.c_str(), iface.c_str());
    }

    virtual ~AshMemAdaptor() {
    }

    virtual bool handleMethodCall(const SharedPtr<DMessage>& msg) {
        mm_status_t ret = MM_ERROR_SUCCESS;
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        INFO("get call %s\n", msg->methodName().c_str());
        if (msg->methodName() == "create_ashmem") {
            int32_t size = msg->readInt32();
            int fd = dup(msg->readFd());
            mAshMem.reset(new MMAshMem(fd, size, false));
            ASSERT(mAshMem);

            reply->writeInt32(ret);
            sendMessage(reply);
        } else if (msg->methodName() == "test_ashmem") {
            int32_t value = msg->readInt32();
            size_t size = mAshMem->getSize();
            uint8_t *data = (uint8_t*)mAshMem->getBase();
            for (uint32_t i = 0; i < size; i++) {
                ASSERT(data[i] == value);
            }

            // dump buffer, changed by proxy
            hexDump(data, size, 32);

            memset(data, value+size, size);
            reply->writeInt32(ret);
            sendMessage(reply);
        }
        return true;
    }

private:
    MMAshMemSP mAshMem;

};

int main(int argc, char **argv) {
    // run as a dbus service.

    Looper looper;

    SharedPtr<DServiceManager> mManager;
    SharedPtr<DService> mService;
    SharedPtr<DAdaptor> mDAdaptor;

    mManager = DServiceManager::getInstance();
    INFO("mManager %p\n", mManager.pointer());
    mService = mManager->registerService(String(gAshMemName));
    mDAdaptor = new AshMemAdaptor(mService, String(gAshMemObjectPath), String(gAshMemInterface));
    mDAdaptor->publish();

    looper.run();

    INFO("end looper");
    mManager->unregisterService(mService);

    return 0;
}

