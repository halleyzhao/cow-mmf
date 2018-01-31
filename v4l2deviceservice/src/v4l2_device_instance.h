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

#ifndef __V4L2_DEVICE_INSTANCE_H__
#define __V4L2_DEVICE_INSTANCE_H__
//#ifdef __USING_YUNOS_MODULE_LOAD_FW__
//#undef __USING_YUNOS_MODULE_LOAD_FW__
//#endif
#include <dbus/DAdaptor.h>
#include <dbus/DService.h>
#include <thread/LooperThread.h>

#include <multimedia/mm_types.h>
#include <multimedia/mm_debug.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/mm_ashmem.h"
#include "yunhal/v4l2codec_device_ops.h"
#include "iv4l2_device.h"
/*#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include <hardware/gralloc.h>
#endif*/

namespace YUNOS_MM {
template <class UBusType> class ServiceNode;
class MMAshMem;
typedef MMSharedPtr<MMAshMem> MSHMemSP;

class V4l2DeviceInstance;
typedef MMSharedPtr<V4l2DeviceInstance> V4l2DeviceInstanceSP;
class V4l2DeviceAdaptor;
typedef MMSharedPtr<V4l2DeviceAdaptor> V4l2DeviceAdaptorSP;
class V4l2ServiceImp;
typedef MMSharedPtr<YUNOS_MM::ServiceNode<V4l2DeviceAdaptor> > V4l2DeviceAdaptorNodeSP;

class V4l2DeviceInstance : public IV4l2Device {
public:
    static V4l2DeviceInstanceSP create();
    V4l2DeviceInstance();
    virtual ~V4l2DeviceInstance();
    virtual mm_status_t createNode(uint32_t *node);
    virtual void destroyNode(uint32_t nodeId);
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

private:
    friend class V4l2DeviceAdaptor;
    YUNOS_MM::Lock mLock;
    uint32_t mNodeId;

    yunos::String mLibName;

    std::string mCodecType;
    uint32_t mFlags;
    void *mLibHandle;
    struct V4l2CodecOps *mV4l2CodecOps = NULL;
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
    VendorModule* mModule = NULL;
    VendorDevice *mDevice = NULL;
#endif
    // adaptor
    V4l2DeviceAdaptorNodeSP mServiceNode;

    MM_DISALLOW_COPY(V4l2DeviceInstance)
    DECLARE_LOGTAG()
};


//////////////////////////////////////////////////////////////////////
// V4l2DeviceAdaptor
class V4l2DeviceAdaptor : public yunos::DAdaptor {
public:
    V4l2DeviceAdaptor(const yunos::SharedPtr<yunos::DService> &service,
            yunos::String serviceName, yunos::String pathName,
            yunos::String iface, void *arg);
    virtual ~V4l2DeviceAdaptor();
    bool init() { return true; }

public:
    virtual bool handleMethodCall(const yunos::SharedPtr<yunos::DMessage> &msg);
    virtual void onDeath(const DLifecycleListener::DeathInfo& deathInfo);
    virtual void onBirth(const DLifecycleListener::DeathInfo& deathInfo);

public:
    static const char *serviceName() { return "com.yunos.v4l2"; }
    static const char *pathName() { return "/com/yunos/v4l2"; }
    static const char *iface() { return "com.yunos.v4l2.interface"; }
protected:
private:
    // void dumpInput(BufferIdType data, unsigned long rangeOffset, unsigned long rangeLength);
    // void dumpOutput(GraphicBufferPtr graphicBuffer);
private:
    V4l2DeviceInstance *mDeviceInstance;
    yunos::String mLibName;
    uint32_t mNodeId;
    std::map<BufferIdType, YUNOS_MM::MSHMemSP> mBuffers[2];
    bool mIsEncoder;

    struct BufferMapServer {
        uint32_t port;    // this indicate input/output port
        uint32_t buffer_index;
        uint32_t plane_index;
        uint32_t magic_memoffset;   // used to link buffer_index/plane_index and MSHMemSP during Mmap
        YUNOS_MM::MSHMemSP memory;  // MSHMemSP
        uint8_t* addrFromCodec;     // V4l2Device::map
    };
    std::vector<struct BufferMapServer> mBufferMaps;

    DECLARE_LOGTAG();
    MM_DISALLOW_COPY(V4l2DeviceAdaptor);
};

}

#endif //__V4L2_DEVICE_INSTANCE_H__
