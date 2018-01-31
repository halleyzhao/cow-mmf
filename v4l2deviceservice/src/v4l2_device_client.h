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

#ifndef __V4L2_CLIENT_H__
#define __V4L2_CLIENT_H__
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
#include "multimedia/mm_ashmem.h"

#include "iv4l2_device.h"
#include "v4l2_manager.h"
#include "v4l2codec_device.h"
#include <vector>

namespace YUNOS_MM {
template <class ClientType> class UBusMessage;
template <class UBusType> class ClientNode;
class Lock;
class MMAshMem;
typedef MMSharedPtr<MMAshMem> MSHMemSP;
class V4l2CodecDevice;
typedef MMSharedPtr<MMAshMem> MSHMemSP;

class V4l2DeviceProxy;
class V4l2DeviceClient;
typedef MMSharedPtr<V4l2DeviceClient> V4l2DeviceClientSP;

//////////////////////////////////////////////////////////////////////
// V4l2DeviceProxy
class V4l2DeviceProxy : public yunos::DProxy, public IV4l2Device {
public:
    V4l2DeviceProxy(const yunos::SharedPtr<yunos::DService> &service,
            yunos::String serviceName, yunos::String pathName,
            yunos::String iface, void *arg);
    virtual ~V4l2DeviceProxy();

public:
    virtual mm_status_t createNode(uint32_t *node) { return MM_ERROR_UNSUPPORTED; }
    virtual void destroyNode(uint32_t nodeId) { return; }

    virtual bool open(uint32_t nodeId, const char* name, uint32_t flags);
    virtual bool close(uint32_t nodeId);
    virtual int32_t ioctl(uint32_t nodeId, uint64_t request, void* arg);
    virtual int32_t poll(uint32_t nodeId, bool poll_device, bool* event_pending);
    virtual int32_t setDevicePollInterrupt(uint32_t nodeId);
    virtual int32_t clearDevicePollInterrupt(uint32_t nodeId);
    virtual void*   mmap(uint32_t nodeId, void* addr, size_t length, int32_t prot,
                    int32_t flags, unsigned int offset);
    virtual int32_t munmap(uint32_t nodeId, void* addr, size_t length);
    virtual int32_t setParameter(uint32_t nodeId, const char* key, const char* value);

    virtual void onDeath(const yunos::DLifecycleListener::DeathInfo& deathInfo);


private:
    friend class V4l2DeviceClient;
    std::map<BufferIdType, YUNOS_MM::MSHMemSP> mBuffers[2];
    bool mStoreMetaDataInBuffers;
    V4l2DeviceClient* mClient;
    std::string mLibName;
    
    private:
    struct BufferMap {
        uint32_t port;              // this indicate input/output port
        uint32_t buffer_index;
        uint32_t plane_index;
        uint32_t magic_memoffset;   // used to link buffer_index/plane_index and sp<IMemory> during Mmap
        YUNOS_MM::MSHMemSP memory;  // the actual memory to share between V4l2Device client & server
        void* magic_addr;           // used as addr in Munmap during call to server
    };
    std::vector<struct BufferMap> mBufferMaps;

    MM_DISALLOW_COPY(V4l2DeviceProxy)
    DECLARE_LOGTAG()
};

//////////////////////////////////////////////////////////////////////
// V4l2DeviceClient
class V4l2DeviceClient : public YUNOS_MM::V4l2CodecDevice {
public:
    V4l2DeviceClient();
    virtual ~V4l2DeviceClient();

public:
    mm_status_t handleMsg(YUNOS_MM::MMParamSP param);
    bool handleSignal(const yunos::SharedPtr<yunos::DMessage> &msg);

public:
    virtual mm_status_t createNode(uint32_t *node);
    virtual void destroyNode(uint32_t nodeId);

    virtual int32_t ioctl(uint64_t request, void* arg);
    virtual int32_t poll(bool poll_device, bool* event_pending);
    // FIXME, why we haven't used these two apis yet?
    virtual int32_t setDevicePollInterrupt();
    virtual int32_t clearDevicePollInterrupt();
    virtual void* mmap(void* addr, size_t length, uint32_t prot, uint32_t flags, uint32_t offset);
    // FIXME, not use it yet
    virtual int32_t munmap(void* addr, size_t length);
    virtual int32_t setParameter(const char* key, const char* value);
    virtual int32_t useEglImage(void* eglDisplay, void* eglContext, unsigned int buffer_index, void* egl_image){ return MM_ERROR_UNSUPPORTED; }

protected:
  virtual bool open(const char* name, uint32_t flags);
  virtual bool close();

private:
    static std::string methodName(int msg);
    static std::string eventName(int msg);

private:
    friend class YUNOS_MM::UBusMessage<V4l2DeviceClient>;
    MMSharedPtr<YUNOS_MM::ClientNode<V4l2DeviceProxy> > mClientNode;
    friend class V4l2DeviceProxy;
    std::string mSessionId;
    YUNOS_MM::Lock mLock;

    yunos::SharedPtr<YUNOS_MM::UBusMessage<V4l2DeviceClient> > mMessager;
    V4l2ManagerSP mManager;
    std::string mLibName;

    uint32_t mNodeId;
    bool mUseGraphicBuffer;
    std::string mCodecType;
    uint32_t mFlags;

    MM_DISALLOW_COPY(V4l2DeviceClient)
    DECLARE_LOGTAG()
};
}

#endif //__V4L2_CLIENT_H__
