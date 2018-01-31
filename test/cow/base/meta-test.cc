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
#include <gtest/gtest.h>
#include <cstdlib>
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
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


MM_LOG_DEFINE_MODULE_NAME("Cow-MetaTest");

using namespace YUNOS_MM;
using namespace yunos;

class MetaTest {
  public:
    MetaTest(const char* name, const MediaMetaSP meta) :mField(name), mMeta(meta) {};
    virtual bool verify() =0;
  protected:
    const char* mField;
    const MediaMetaSP mMeta;
  private:
    MetaTest();
};
class MetaTestInt32 : public MetaTest {
  public:
    MetaTestInt32(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = rand();
        mMeta->setInt32(name, v);
    }
    virtual bool verify() {
        int32_t vv;
        bool ret = true;
        ret = mMeta->getInt32(mField, vv);
        if (!ret) {
            ERROR("fail to get int32_t data %s\n", mField);
            return false;
        }
        if (vv != v) {
            ERROR("int32_t data doesn't match <%d, %d>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    int32_t v;
};

class MetaTestInt64 : public MetaTest {
  public:
    MetaTestInt64(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = rand();
        mMeta->setInt64(name, v);
    }
    virtual bool verify() {
        int64_t vv;
        bool ret = true;
        ret = mMeta->getInt64(mField, vv);
        if (!ret) {
            ERROR("fail to get int64_t data %s\n", mField);
            return false;
        }
        if (vv != v) {
            ERROR("int64_t data doesn't match <%ld, %ld>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    int64_t v;
};

class MetaTestFloat : public MetaTest {
  public:
    MetaTestFloat(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = rand();
        mMeta->setFloat(name, v);
    }
    virtual bool verify() {
        float vv;
        bool ret = true;
        ret = mMeta->getFloat(mField, vv);
        if (!ret) {
            ERROR("fail to get float data %s\n", mField);
            return false;
        }
        if (vv != v) {
            ERROR("float data doesn't match <%f, %f>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    float v;
};

class MetaTestDouble: public MetaTest {
  public:
    MetaTestDouble(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = rand();
        mMeta->setDouble(name, v);
    }
    virtual bool verify() {
        double vv;
        bool ret = true;
        ret = mMeta->getDouble(mField, vv);
        if (!ret) {
            ERROR("fail to get double data %s\n", mField);
            return false;
        }
        if (vv != v) {
            ERROR("double data doesn't match <%f, %f>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    double v;
};

class MetaTestFraction: public MetaTest {
  public:
    MetaTestFraction(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v1 = rand();
        v2 = rand();
        mMeta->setFraction(name, v1, v2);
    }
    virtual bool verify() {
        int32_t vv1, vv2;
        bool ret = true;
        ret = mMeta->getFraction(mField, vv1, vv2);
        if (!ret) {
            ERROR("fail to get fraction data %s\n", mField);
            return false;
        }
        if (vv1 != v1 || vv2 != v2) {
            ERROR("fraction data doesn't match <(%d, %d), (%d, %d)>\n", vv1, vv2, v1, v2);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    int32_t v1, v2;
};

class MetaTestRect: public MetaTest {
  public:
    MetaTestRect(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v1 = rand();
        v2 = rand();
        v3 = rand();
        v4 = rand();
        mMeta->setRect(name, v1, v2, v3, v4);
    }
    virtual bool verify() {
        int32_t vv1, vv2, vv3, vv4;
        bool ret = true;
        ret = mMeta->getRect(mField, vv1, vv2, vv3, vv4);
        if (!ret) {
            ERROR("fail to get rect data %s\n", mField);
            return false;
        }
        if (vv1 != v1 || vv2 != v2 || vv3 != v3 || vv4 != v4) {
            ERROR("fraction data doesn't match <(%d, %d, %d, %d), (%d, %d, %d, %d)>\n", vv1, vv2, vv3, vv4, v1, v2, v3, v4);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    int32_t v1, v2, v3, v4;
};


class MetaTestString : public MetaTest {
  public:
    MetaTestString(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = (const char*)"TestString";
        mMeta->setString(name, v);
    }
    virtual bool verify() {
        const char* vv;
        bool ret = true;
        ret = mMeta->getString(mField, vv);
        if (!ret) {
            ERROR("fail to get string data %s\n", mField);
            return false;
        }
        if (strcmp(vv, v)) {
            ERROR("string data doesn't match <%s, %s>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    const char* v;
};

class MetaTestPointer: public MetaTest {
  public:
    MetaTestPointer(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v = &v;
        mMeta->setPointer(name, v);
    }
    virtual bool verify() {
        void *vv;
        bool ret = true;
        ret = mMeta->getPointer(mField, vv);
        if (!ret) {
            ERROR("fail to get pointer data %s\n", mField);
            return false;
        }
        if (vv != v) {
            ERROR("double data doesn't match <%p, %p>\n", vv, v);
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    void *v;
};

class MetaTestObject: public MetaTest {
  public:
    class TempObject {
    };
    typedef MMSharedPtr < TempObject > TempSP;
    class MetaTempObject : public MediaMeta::MetaBase {
      public:
        virtual void* getRawPtr() { return sp.get();};
        TempSP sp;
    };
    typedef MMSharedPtr < MetaTempObject > MetaTempSP;

  public:
    MetaTestObject(const char* name, const MediaMetaSP meta) : MetaTest(name, meta) {
        v.reset(new MetaTempObject());
        v->sp.reset(new TempObject());

        MediaMeta::MetaBaseSP t;
        t = v;
        mMeta->setObject(name, t);
    }
    virtual bool verify() {
        MediaMeta::MetaBaseSP vv;
        bool ret = true;
        ret = mMeta->getObject(mField, vv);
        if (!ret) {
            ERROR("fail to get pointer data %s\n", mField);
            return false;
        }
        if (vv->getRawPtr() != v->getRawPtr()) {
            ERROR("double data doesn't match <%p, %p>\n", vv->getRawPtr(), v->getRawPtr());
            return false;
        }

        INFO("%s verify ok\n", __PRETTY_FUNCTION__);
        return true;
    }

  private:
    MetaTempSP v;
};

#if defined(OS_YUNOS)

static const char* gMetaDataName = "com.yunos.media.metadata";
static const char* gMetaDataObjectPath = "/com/yunos/media/metadata";
static const char* gMetaDataInterface = "com.yunos.media.metadata.interface";

class MetaDataProxy : public DProxy {

public:
    MetaDataProxy(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface) : DProxy(service, path, iface){

    }

    virtual ~MetaDataProxy() {}
    bool test_metadata() {

        SharedPtr<DMessage> msg = obtainMethodCallMessage(String("test_metadata"));
        MediaMetaSP meta = MediaMeta::create();
        meta->setRect("rect", 1, 2, 3, 4);
        meta->setInt32("int32_t", 5);
        meta->setInt64("int64_t", 6);
        meta->setFloat("float", 7.0f);
        meta->setDouble("double", 8.0);
        meta->setFraction("fraction", 9, 10);
        meta->setString("string", "11");
        meta->setPointer("pointer", reinterpret_cast<void *>(0x00000012));
        int32_t size = 32;
        uint8_t buffer[size];
        memset(buffer, 0x13, size);
        meta->setByteBuffer("byteBuffer", buffer, size);

        meta->writeToMsg(msg);
        SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg);
        int32_t ret = -1;
        if (reply) {
            ret = reply->readInt32();
            INFO("ret %d\n", ret);
            return true;
        }
        return false;
    }
};

class MetaTestIpc: public MetaTest {
  public:
    MetaTestIpc(const char* name, const MediaMetaSP meta) : MetaTest(name, meta), mCondition(mLock) {

        SharedPtr<LooperThread> mLooper = new LooperThread();
        mLooper->start("MetaTest");
        if (mLooper->isRunning() != true) {
            INFO("LooperThread is not running\n");
            return;
        }
        mLooper->sendTask(Task(MetaTestIpc::createProxy, this));
        mCondition.wait();

        mMetaDataProxy->test_metadata();
    }
   static void createProxy(MetaTestIpc *instance) {

        INFO("create proxy: service(%s) path(%s) interface(%s)\n",
             gMetaDataName, gMetaDataObjectPath, gMetaDataInterface);

        instance->mManager = DServiceManager::getInstance();
        instance->mService = instance->mManager->getService(String(gMetaDataName));
        if (!instance->mService) {
            INFO("getService %s failed\n", gMetaDataName);
            return;
        }
        instance->mMetaDataProxy = new MetaDataProxy(instance->mService, String(gMetaDataObjectPath), String(gMetaDataInterface));

        instance->mCondition.signal();
    }

    virtual bool verify() {return true;};

  private:
    Lock mLock;
    YUNOS_MM::Condition mCondition;

    SharedPtr<DServiceManager> mManager;
    SharedPtr<DService> mService;
    SharedPtr<MetaDataProxy> mMetaDataProxy;
};
#endif

class MetadataTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(MetadataTest, metadataTest) {
    srand((unsigned)time(0));

    MediaMetaSP meta = MediaMeta::create();
    class MetaTestInt32 testInt32("int32", meta);
    class MetaTestInt64 testInt64("int64", meta);
    class MetaTestFloat testFloat("float", meta);
    class MetaTestDouble testDouble("double", meta);
    class MetaTestFraction testFraction("fraction", meta);
    class MetaTestRect testRect("rect", meta);
    class MetaTestString testString("string", meta);
    class MetaTestPointer testPointer("pointer", meta);
    class MetaTestObject testObject("object", meta);
#if defined(OS_YUNOS)
    class MetaTestIpc testObjectIpc("test_ipc", meta);
    #endif
    EXPECT_TRUE(testInt32.verify());
    EXPECT_TRUE(testInt64.verify());
    EXPECT_TRUE(testFloat.verify());
    EXPECT_TRUE(testDouble.verify());
    EXPECT_TRUE(testFraction.verify());
    EXPECT_TRUE(testRect.verify());
    EXPECT_TRUE(testString.verify());
    EXPECT_TRUE(testPointer.verify());
    EXPECT_TRUE(testObject.verify());
    INFO("done\n");
}

