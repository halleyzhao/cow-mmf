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


using namespace YUNOS_MM;
using namespace yunos;
MM_LOG_DEFINE_MODULE_NAME("Cow-MetaTest");


static const char* gMetaDataName = "com.yunos.media.metadata";
static const char* gMetaDataObjectPath = "/com/yunos/media/metadata";
static const char* gMetaDataInterface = "com.yunos.media.metadata.interface";


class MetaDataAdaptor : public DAdaptor {
public:
    MetaDataAdaptor(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface) : DAdaptor(service, path, iface) {
                       INFO("Adaptor path %s, iface %s\n", path.c_str(), iface.c_str());
    }

    virtual ~MetaDataAdaptor() {
    }

    virtual bool handleMethodCall(const SharedPtr<DMessage>& msg) {
        SharedPtr<DMessage> reply = DMessage::makeMethodReturn(msg);
        INFO("get call %s\n", msg->methodName().c_str());
        if (msg->methodName() == "test_metadata") {

            MediaMetaSP meta = MediaMeta::create();
            meta->readFromMsg(msg);

            int32_t a = 0, b = 0, c = 0, d = 0;
            bool ret = true;

            MMASSERT(meta->getRect("rect", a, b, c, d) == true);
            MMASSERT(a == 1);
            MMASSERT(b == 2);
            MMASSERT(c == 3);
            MMASSERT(d == 4);

            MMASSERT(meta->getInt32("int32_t", a) == true);
            MMASSERT(a == 5);

            int64_t ld = 0;
            MMASSERT(meta->getInt64("int64_t", ld) == true);
            MMASSERT(ld == 6);

            float f = 0;
            MMASSERT(meta->getFloat("float", f) == true);
            // MMASSERT(f == 7.0f);

            double db = 0;
            MMASSERT(meta->getDouble("double", db) == true);
            // MMASSERT(db == 8.0f);

            int32_t num = 0, den = 0;
            MMASSERT(meta->getFraction("fraction", num, den) == true);
            MMASSERT(num == 9);
            MMASSERT(den == 10);

            const char* str = NULL;
            MMASSERT(meta->getString("string", str) == true);
            MMASSERT(!strcmp(str, "11"));

            void *ptr = NULL;
            MMASSERT(meta->getPointer("pointer", ptr) == true);
            MMASSERT((int64_t)ptr == 0x00000012);

            //data will be released in MediaMeta:clearData
            uint8_t* data = NULL;
            int32_t size = 0;
            MMASSERT(meta->getByteBuffer("byteBuffer", data, size) == true);
            MMASSERT(size == 32);
            for (int32_t i = 0; i < size; i++) {
                INFO("%2x  ", data[i]);
                if ((i+1 % 17) == 0)
                    INFO("\n");
            }

            reply->writeInt32(ret);
            sendMessage(reply);
        }
        return true;
    }

};

int main(int argc, char **argv) {
    // run as a dbus service.
    INFO("meta-ubus-test begin\n");

    Looper looper;

    SharedPtr<DServiceManager> mManager;
    SharedPtr<DService> mService;
    SharedPtr<DAdaptor> mDAdaptor;

    mManager = DServiceManager::getInstance();
    INFO("mManager %p\n", mManager.pointer());
    mService = mManager->registerService(String(gMetaDataName));
    mDAdaptor = new MetaDataAdaptor(mService, String(gMetaDataObjectPath), String(gMetaDataInterface));
    mDAdaptor->publish();

    looper.run();

    INFO("end looper");
    mManager->unregisterService(mService);

    return 0;
}

