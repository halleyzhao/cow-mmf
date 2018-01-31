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
#include <unistd.h>

#include "iv4l2_service.h"
#include "v4l2_service_imp.h"
#include "v4l2_device_instance.h"
#include "multimedia/UBusNode.h"
#include "multimedia/UBusHelper.h"

#ifdef __MM_YUNOS_CNTRHAL_BUILD__
#include <dlfcn/dlfcn.h>
#define _DLCLOSE hybris_dlclose
#define DLOPEN  hybris_dlopen
#define DLSYM   hybris_dlsym
#else
#include <dlfcn.h>
#define _DLCLOSE dlclose
#define DLOPEN  dlopen
#define DLSYM   dlsym
#endif
#define DLCLOSE(_handle) do {       \
        if (_handle) {              \
            _DLCLOSE(_handle);      \
            _handle = 0;            \
        }                           \
    } while(0)
#include <fcntl.h>


namespace YUNOS_MM {
DEFINE_LOGTAG1(V4l2DeviceAdaptor, [V4L2])
DEFINE_LOGTAG1(V4l2DeviceInstance, [V4L2])

using namespace yunos;

#define V4L2_DEBUG(x, ...) DEBUG("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_INFO(x, ...) INFO("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_ERROR(x, ...) ERROR("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_WARNING(x, ...) WARNING("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_VERBOSE(x, ...) VERBOSE("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)

#define CHECK_VALID_NODE_ID()                                        \
    uint32_t nodeId = msg->readInt32();                              \
    if (mNodeId != nodeId) {                                         \
        V4L2_ERROR("invalid nodeId %d, mNodeId %d", nodeId, mNodeId); \
        status = MM_ERROR_UNKNOWN;                                   \
    }                                                                \


#define V4L2_PERMISSION_NAME  "V4L2.permission.yunos.com"
static const char* s_libName = "libv4l2codec_wayland.so";

/////////////////////////////////////////////////////////////////////////////
/*static*/
V4l2DeviceInstanceSP V4l2DeviceInstance::create()
{
    V4l2DeviceInstanceSP device;
    device.reset(new V4l2DeviceInstance());
    return device;
}

V4l2DeviceInstance::V4l2DeviceInstance() : mNodeId(0)
                                            , mLibHandle(NULL)
{
#ifndef __USING_YUNOS_MODULE_LOAD_FW__
    mV4l2CodecOps = new V4l2CodecOps;
    INIT_V4L2CODEC_OPS_SIZE_VERSION(mV4l2CodecOps);
#endif
}

V4l2DeviceInstance::~V4l2DeviceInstance()
{

}

mm_status_t V4l2DeviceInstance::createNode(uint32_t *nodeId)
{
    uint32_t uniqueId = V4l2Service::getNodeId();
    if (uniqueId == IV4l2Service::kInvalidNodeId) {
        return MM_ERROR_NO_MEM;
    }
    V4L2_DEBUG("uniqueId %d", uniqueId);

    char buffer[64] = {0};
    V4l2DeviceAdaptorNodeSP serviceNode;
    sprintf(buffer, "%d", uniqueId);
    serviceNode =  ServiceNode<V4l2DeviceAdaptor>::create("vda", buffer, this);
    if (!serviceNode || !serviceNode->init()) {
        V4L2_ERROR("no mem");
        return MM_ERROR_UNKNOWN;
    }
    mServiceNode = serviceNode;
    mNodeId = uniqueId;
    ASSERT(nodeId);
    *nodeId = uniqueId;

    return MM_ERROR_SUCCESS;
}

void V4l2DeviceInstance::destroyNode(uint32_t nodeId)
{
    MMAutoLock autoLock(mLock);
}

bool V4l2DeviceInstance::open(uint32_t nodeId, const char* name, uint32_t flags)
{
    mV4l2Fd = 0;

    if (!mm_check_env_str("mm.v4l2.use_surface_v2", "MM_V4L2_USE_SURFACE_V2", "0")) {
        s_libName = "libv4l2codec.so";
    }

#ifdef __USING_YUNOS_MODULE_LOAD_FW__
#if 0
    mModule = LOAD_VENDOR_MODULE(VIDEO_VENDOR_MODULE_ID);
    if (mModule == NULL) {
        ERROR("failed to open VIDEO_VENDOR_MODULE_ID");
        return false;
    }

    unsigned i;
    VendorDevice* d = NULL;
    int32_t err = mModule->createDevice(mModule, "video", &d);
    if (err) {
        ERROR("failed to get video(err=%d)", err);
        return false;
    }
    mDevice = d;
    mV4l2CodecOps = (V4l2CodecOps*) d;
    if(mV4l2CodecOps->mSize != sizeof(V4l2CodecOps)) {
        ERROR("V4l2CodecOps interface data structure size doesn't match");
        return false;
    }
#endif
#else
    mLibHandle = DLOPEN(s_libName, RTLD_NOW | RTLD_GLOBAL);
    if (!mLibHandle) {
      ERROR("Failed to load %s, errno %s\n", s_libName, dlerror());
      return false;
    }

    V4l2codecOperationInitFunc initFunc = NULL;
    initFunc = (V4l2codecOperationInitFunc)DLSYM(mLibHandle, "v4l2codecOperationInit");
    if (!initFunc) {
        ERROR("fail to dlsym v4l2codecOperationInit\n");
        return false;
    }

    if (!initFunc(mV4l2CodecOps)) {
        ERROR("fail to init v4l2 device operation func pointers\n");
        return false;
    }

    int isVersionMatch = 0;
    IS_V4L2CODEC_OPS_VERSION_MATCH(mV4l2CodecOps->mVersion, isVersionMatch);
    if (!isVersionMatch) {
        ERROR("V4l2CodecOps interface version doesn't match\n");
        return false;
    }
    ASSERT(mV4l2CodecOps->mSize == sizeof(V4l2CodecOps));
#endif

    mV4l2Fd = mV4l2CodecOps->mOpenFunc(name, flags);
    return mV4l2Fd != 0;
}

bool V4l2DeviceInstance::close(uint32_t nodeId)
{
    if (mV4l2Fd) {
        int ioctlRet = mV4l2CodecOps->mCloseFunc(mV4l2Fd);
        ASSERT(ioctlRet != -1);
        mV4l2Fd = 0;
        DLCLOSE(mLibHandle);
    }
    return true;
}

int32_t V4l2DeviceInstance::ioctl(uint32_t nodeId, uint64_t request, void* arg)
{
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mIoctlFunc(mV4l2Fd, request, arg);
    if (ret != 0)
        VERBOSE("ret=%d\n", ret);

    return ret;
}
int32_t V4l2DeviceInstance::poll(uint32_t nodeId, bool poll_device, bool* event_pending)
{
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mPollFunc(mV4l2Fd, poll_device, event_pending);
    if (ret != 0)
        ERROR("ret=%d\n", ret);

    return ret;
}
int32_t V4l2DeviceInstance::setDevicePollInterrupt(uint32_t nodeId)
{
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mSetDevicePollInterruptFunc(mV4l2Fd);
    if (ret != 0)
        ERROR("ret=%d\n", ret);

    return ret;

}
int32_t V4l2DeviceInstance::clearDevicePollInterrupt(uint32_t nodeId)
{
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mClearDevicePollInterruptFunc(mV4l2Fd);
    if (ret != 0)
        WARNING("ret=%d\n", ret);

    return ret;
}
void* V4l2DeviceInstance::mmap(uint32_t nodeId, void* addr, size_t length, int32_t prot,
                int32_t flags, unsigned int offset)
{
    DEBUG("addr: %p, length: %zu, prot: 0x%x, flags: 0x%x, offset: %d\n",
            addr, length, prot, flags, offset);

    void *ret = mV4l2CodecOps->mMmapFunc(addr, length, prot, flags, mV4l2Fd, offset);
    DEBUG("ret=%p\n", ret);
    return ret;
}
int32_t V4l2DeviceInstance::munmap(uint32_t nodeId, void* addr, size_t length)
{
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mMunmapFunc(addr, length);
    DEBUG("ret=%d\n", ret);

    return ret;
}
int32_t V4l2DeviceInstance::setParameter(uint32_t nodeId, const char* key, const char* value)
{
    if (!mV4l2Fd)
        return -1;

    DEBUG("key: %s, value: %s", key, value);
    int32_t ret = mV4l2CodecOps->mSetParameterFunc(mV4l2Fd, key, value);
    if (ret != 0)
        ERROR("ret=%d\n", ret);
    return ret;
}


////////////////////////////////////////////////////////////////////////////////
//V4l2DeviceAdaptor
V4l2DeviceAdaptor::V4l2DeviceAdaptor(const yunos::SharedPtr<yunos::DService>& service,
    yunos::String serviceName, yunos::String pathName, yunos::String iface, void *arg)
        : yunos::DAdaptor(service, pathName, iface)
        , mDeviceInstance(static_cast<V4l2DeviceInstance*>(arg))
        , mNodeId(IV4l2Service::kInvalidNodeId)
        , mIsEncoder(false)

{
    V4L2_INFO("+\n");
    mNodeId = atoi(serviceName.substr(strlen(IV4l2Device::serviceName())).c_str());
    V4L2_DEBUG("mNodeId %d, mDeviceInstance %p", mNodeId, mDeviceInstance);

    ASSERT(mDeviceInstance);
    V4L2_INFO("-\n");
}

V4l2DeviceAdaptor::~V4l2DeviceAdaptor()
{
    V4L2_INFO("+\n");
    V4L2_INFO("-\n");
}

static const char* IoctlCommandString(int command)
{
    static const char* unknown = "Unkonwn command";
#define IOCTL_COMMAND_STRING_MAP(cmd)   {cmd, #cmd}
    static struct IoctlCommanMap{
        long unsigned int command;
        const char* cmdStr;
        } ioctlCommandMap[] = {
            IOCTL_COMMAND_STRING_MAP(VIDIOC_QUERYCAP),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_STREAMON),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_STREAMOFF),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_QUERYBUF),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_REQBUFS),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_QBUF),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_DQBUF),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_S_EXT_CTRLS),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_S_PARM),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_S_FMT),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_S_CROP),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_SUBSCRIBE_EVENT),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_DQEVENT),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_G_FMT),
            IOCTL_COMMAND_STRING_MAP(VIDIOC_G_CTRL)
        };

    // int --> long unsigned is different from int-->uint-->long unsigned
    unsigned int u_cmd = (unsigned int)command;
    long unsigned int lu_cmd = (long unsigned int) u_cmd;
    size_t i;
    for (i=0; i<sizeof(ioctlCommandMap)/sizeof(IoctlCommanMap); i++)
        if (ioctlCommandMap[i].command == lu_cmd)
            return ioctlCommandMap[i].cmdStr;

    return unknown;
}

#define BUFFER_ADDRESS_OFFSET  4
#define BUFFER_HANDLE_OFFSET 8
bool V4l2DeviceAdaptor::handleMethodCall(const yunos::SharedPtr<yunos::DMessage>& msg)
{
    // static PermissionCheck perm(String(MEDIA_CONTROL_PERMISSION_NAME));
    mm_status_t status = MM_ERROR_SUCCESS;
    yunos::SharedPtr<yunos::DMessage> reply = yunos::DMessage::makeMethodReturn(msg);

    V4L2_INFO("v4l2(pid: %d, interface: %s) call %s",
        msg->getPid(), interface().c_str(), msg->methodName().c_str());

    CHECK_VALID_NODE_ID();
    if (msg->methodName() == "open") {
        yunos::String name = msg->readString();
        uint32_t flags = (uint32_t)msg->readInt32();
        bool ret = mDeviceInstance->open(nodeId, name.c_str(), flags);
        reply->writeBool(ret);
        V4L2_DEBUG("name %s", name.c_str());
    } else if (msg->methodName() == "close") {
        bool ret = mDeviceInstance->close(nodeId);
        reply->writeBool(ret);
        V4L2_DEBUG("close");
    } else if (msg->methodName() == "ioctl") {
        uint64_t request = (uint64_t)msg->readInt64();
        void *arg = NULL, *arg2 = NULL;
        uint8_t *buf = NULL, *buf2 = NULL;
        static int32_t inputBufferCount = 0;
        static int32_t outputBufferCount = 0;
        int32_t sz = -1;
        INFO("ioctl command: %s, arg: %p\n", IoctlCommandString(request), arg);
        // parse input data
        switch(request) {
            case VIDIOC_DQBUF:
            case VIDIOC_QBUF: {
                arg = malloc(sizeof(struct v4l2_buffer));
                ASSERT(arg);

                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_buffer));
                memcpy(arg, buf, sizeof(struct v4l2_buffer));

                struct v4l2_buffer *v4l2buf = (struct v4l2_buffer*) arg;
                arg2 = malloc(sizeof(struct v4l2_plane) * v4l2buf->length);

                ASSERT(arg2);
                v4l2buf->m.planes = (struct v4l2_plane*)arg2;

                sz = -1;
                buf2 = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_plane) * v4l2buf->length);
                memcpy(v4l2buf->m.planes, buf2, sizeof(struct v4l2_plane) * v4l2buf->length);

                //v4l2buf->m.planes = planes;

                DEBUG("v4l2buf->memory: %d, v4l2buf->type: %d", v4l2buf->memory, v4l2buf->type);
                // noop for DQBUF
                if (request == VIDIOC_DQBUF)
                    break;

                switch (v4l2buf->memory) {
                    case V4L2_MEMORY_MMAP: {
                        // 1. noop for output port
                        if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
                            break;
                        // 2. raw data can't be used directly across process boundary, leverage MSHMemSP
                        uint32_t plane = 0;
                        for (plane=0; plane<v4l2buf->length; plane++) {
                            uint32_t j=0;
                            for (j=0; j<mBufferMaps.size(); j++) {
                                if (mBufferMaps[j].port == v4l2buf->type &&
                                    mBufferMaps[j].buffer_index == v4l2buf->index &&
                                    mBufferMaps[j].plane_index == plane) {
                                    ASSERT(mBufferMaps[plane].memory->getBase());
                                    memcpy(mBufferMaps[j].addrFromCodec, mBufferMaps[j].memory->getBase(), v4l2buf->m.planes[plane].bytesused);

#ifdef DUMP_DATA_DEBUG
                                    DEBUG("dump_input_data, inputBufferCount=%d, plane: %d\n", inputBufferCount, plane);
                                    hexDump((uint8_t*)(mBufferMaps[j].memory->getBase()), v4l2buf->m.planes[plane].bytesused, 32);
#endif
                                }
                            }
                        }
                    }
                    break;
                    case V4L2_MEMORY_PLUGIN_BUFFER_HANDLE: {
                        DEBUG("camera_data_dump in V4l2DeviceServer, buffer_handle_t: %p", (void*)(v4l2buf->m.planes[0].m.userptr));
                    }
                    break;
                    case V4L2_MEMORY_USERPTR: {
                        uint32_t  port = v4l2buf->type;
                        uint32_t buffer_index = v4l2buf->index;
                        uint32_t plane = 0;
                        for (plane = 0; plane < v4l2buf->length; plane++) {
                            // 1. userptr isn't valid across process boundary, use our internal MSHMemSP
                            // 1.1 find the MSHMemSP to pass user_ptr data across process boundary
                            uint32_t mapIndex = 0;
                            for (mapIndex=0; mapIndex<mBufferMaps.size(); mapIndex++) {
                                if (mBufferMaps[mapIndex].port == port &&
                                    mBufferMaps[mapIndex].buffer_index == buffer_index &&
                                    mBufferMaps[mapIndex].plane_index == plane) {
                                    DEBUG("mapIndex: %d, port: %d, buffer_index: %d, plane: %d\n",
                                        mapIndex, port, buffer_index, plane);
                                    break;
                                }
                            }
                            // 1.2 if there is not a MSHMemSP for this user_ptr yet, create it now
                            if (mapIndex == mBufferMaps.size()) {
                                struct BufferMapServer map;
                                map.port = port;
                                map.buffer_index = buffer_index;
                                map.plane_index = plane;
                                //map.memory = interface_cast<IMemory>(data.readStrongBinder());
                                ASSERT(map.memory->getBase());
                                DEBUG("push map port: %d, buffer_index: %d, plane_index: %d\n", port, buffer_index, plane);
                                mBufferMaps.push_back(map);
                            }
                            ASSERT(mBufferMaps[mapIndex].memory->getBase() );

                            // 2. use the pointer from MSHMemSP
                            v4l2buf->m.planes[plane].m.userptr = (unsigned long)mBufferMaps[mapIndex].memory->getBase();

#ifdef DUMP_DATA_DEBUG
                            if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                                DEBUG("dump_input_data, inputBufferCount=%d, plane: %d\n", inputBufferCount, plane);
                                hexDump((uint8_t*)(v4l2buf->m.planes[plane].m.userptr), v4l2buf->m.planes[plane].bytesused, 32);
                            }
#endif
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            break;
            case VIDIOC_QUERYCAP:
                arg = malloc(sizeof(struct v4l2_capability));
                ASSERT(arg);
            break;
            case VIDIOC_STREAMON:
            case VIDIOC_STREAMOFF: {
                arg = malloc(sizeof(__u32));
                ASSERT(arg);
                __u32 *type = (__u32*) arg;
                *type = msg->readInt32();
            }
            break;
            case VIDIOC_REQBUFS: {
                arg = malloc(sizeof(struct v4l2_requestbuffers));
                ASSERT(arg);
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_requestbuffers));
                memcpy(arg, buf, sizeof(struct v4l2_requestbuffers));
                mBufferMaps.clear();
            }
            break;
            case VIDIOC_QUERYBUF: {
                arg = malloc(sizeof(struct v4l2_buffer));
                ASSERT(arg);
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_buffer));
                memcpy(arg, buf, sizeof(struct v4l2_buffer));
                struct v4l2_buffer *v4l2buf = (struct v4l2_buffer*) arg;
                arg2 = malloc(sizeof(struct v4l2_plane) * v4l2buf->length);
                ASSERT(arg2);
                v4l2buf->m.planes = (struct v4l2_plane*)arg2;
                sz = -1;
                buf2 = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_plane) * v4l2buf->length);
                memcpy(v4l2buf->m.planes, buf2, sizeof(struct v4l2_plane) * v4l2buf->length);
            }
            break;
            case VIDIOC_S_EXT_CTRLS: {
            }
            break;
            case VIDIOC_S_PARM: {
            }
            break;
            case VIDIOC_S_FMT: {
                arg = malloc(sizeof(struct v4l2_format));
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_format));
                memcpy(arg, buf, sizeof(struct v4l2_format));
            }
            break;
            case VIDIOC_G_FMT: {
                arg = malloc(sizeof(struct v4l2_format));
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_format));
                memcpy(arg, buf, sizeof(struct v4l2_format));
            }
            break;
            case VIDIOC_G_CTRL: {
                arg = malloc(sizeof(struct v4l2_control));
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_control));
                memcpy(arg, buf, sizeof(struct v4l2_control));
            }
            break;
            case VIDIOC_DQEVENT: {
                arg = malloc(sizeof(struct v4l2_event));
                buf = (uint8_t *)msg->readByteBuffer(sz);
                ASSERT((uint32_t)sz == sizeof(struct v4l2_event));
                memcpy(arg, buf, sizeof(struct v4l2_event));
            }
            break;
            case VIDIOC_S_CROP:
            default:
                ERROR ("unknown command");
                return false;
            break;
        }

        if (buf) {
            delete [] buf;
            buf = NULL;
        }
        if (buf2) {
            delete [] buf2;
            buf2 = NULL;
        }
        status = mDeviceInstance->ioctl(nodeId, request, arg);
        reply->writeInt32(status);
        if (status != 0) {
            ERROR("status=%d\n", status);
            return false;
        }

        // parse output data
        switch(request) {
            case VIDIOC_DQBUF:
            case VIDIOC_QBUF: {
                struct v4l2_buffer* v4l2buf = static_cast<struct v4l2_buffer*>(arg);
                reply->writeByteBuffer(sizeof(struct v4l2_buffer), (int8_t *)arg);
                ASSERT(arg2 == v4l2buf->m.planes);
                reply->writeByteBuffer(sizeof(struct v4l2_plane) * v4l2buf->length, (int8_t *)v4l2buf->m.planes);

                // DEBUG
                if (request == VIDIOC_QBUF && v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
                    inputBufferCount++;
                else if (request == VIDIOC_DQBUF && v4l2buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
                    outputBufferCount++;

                // noop for QBUF
                if (request == VIDIOC_QBUF)
                    break;
                // noop for input port
                if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
                    break;
                // only operate on raw memory data
                if (v4l2buf->memory != V4L2_MEMORY_MMAP && v4l2buf->memory != V4L2_MEMORY_USERPTR)
                    break;

                uint32_t plane = 0;
                for (plane=0; plane<v4l2buf->length; plane++) {
                    uint32_t j=0;
                    for (j=0; j<mBufferMaps.size(); j++) {
                        if (mBufferMaps[j].port == v4l2buf->type &&
                            mBufferMaps[j].buffer_index == v4l2buf->index &&
                            mBufferMaps[j].plane_index == plane) {
                            ASSERT(mBufferMaps[plane].memory->getBase());
                            // copy V4L2Codec MMAP data to MSHMemSP
                            if (v4l2buf->memory == V4L2_MEMORY_MMAP)
                                memcpy(mBufferMaps[j].memory->getBase(), mBufferMaps[j].addrFromCodec, v4l2buf->m.planes[plane].bytesused);

                            break;
                        }
                    }

#ifdef DUMP_DATA_DEBUG
                    encodedDataDump.dump(mBufferMaps[j].memory->getBase(), v4l2buf->m.planes[plane].bytesused);
                    DEBUG("dump_output_data, outputBufferCount=%d\n", outputBufferCount-1);
                    hexDump((uint8_t*)(mBufferMaps[j].memory->getBase()), v4l2buf->m.planes[plane].bytesused, 32);
#endif
                }
            }
            break;
            case VIDIOC_QUERYCAP:
                reply->writeByteBuffer(sizeof(struct v4l2_capability), (int8_t *)arg);
            break;
            case VIDIOC_STREAMON:
            case VIDIOC_STREAMOFF: {
                __u32 type = * ((__u32*)arg);
                reply->writeInt32((int32_t)type);
            }
            break;
            case VIDIOC_REQBUFS: {
                reply->writeByteBuffer(sizeof(struct v4l2_requestbuffers), (int8_t *)arg);
            }
            break;
            case VIDIOC_QUERYBUF: {
                reply->writeByteBuffer(sizeof(struct v4l2_buffer), (int8_t *)arg);

                struct v4l2_buffer *v4l2buf = (struct v4l2_buffer*) arg;
                ASSERT(arg2 == v4l2buf->m.planes);
                reply->writeByteBuffer(sizeof(struct v4l2_plane) * v4l2buf->length, (int8_t *)v4l2buf->m.planes);
                DEBUG("buffer->m.planes[0].length: %d, buffer->m.planes[0].m.mem_offset: %d\n",
                    v4l2buf->m.planes[0].length, v4l2buf->m.planes[0].m.mem_offset);
            }
            break;
            case VIDIOC_S_EXT_CTRLS: {
            }
            break;
            case VIDIOC_S_PARM: {
            }
            break;
            case VIDIOC_S_FMT: {
                reply->writeByteBuffer(sizeof(struct v4l2_format), (int8_t *)arg);
            }
            break;
            case VIDIOC_G_FMT: {
                reply->writeByteBuffer(sizeof(struct v4l2_format), (int8_t *)arg);
            }
            break;
            case VIDIOC_G_CTRL: {
                reply->writeByteBuffer(sizeof(struct v4l2_control), (int8_t *)arg);
            }
            break;
            case VIDIOC_DQEVENT: {
                reply->writeByteBuffer(sizeof(struct v4l2_event), (int8_t *)arg);
            }
            break;
            case VIDIOC_S_CROP:
            default:
                ERROR("unknown command");
            break;
        }

        if (arg) {
            free(arg);
            arg = NULL;
        }
         if (arg2) {
            free(arg2);
            arg2 = NULL;
        }
    } else if (msg->methodName() == "poll") {
        bool poll_device = (bool)msg->readInt32();
        bool event_pending = false;

        status = mDeviceInstance->poll(nodeId, poll_device, &event_pending);
        reply->writeInt32(status);
        if (status == MM_ERROR_SUCCESS) {
            reply->writeInt32((int32_t)event_pending);
        }
    } else if (msg->methodName() == "setDevicePollInterrupt") {
        status = mDeviceInstance->setDevicePollInterrupt(nodeId);
        reply->writeInt32(status);
        V4L2_DEBUG("setDevicePollInterrupt");
    } else if (msg->methodName() == "clearDevicePollInterrupt") {
        status = mDeviceInstance->clearDevicePollInterrupt(nodeId);
        reply->writeInt32(status);
        V4L2_DEBUG("clearDevicePollInterrupt");
    } else if (msg->methodName() == "setParameter") {
        yunos::String key = msg->readString();
        yunos::String value = msg->readString();
        status = mDeviceInstance->setParameter(nodeId, key.c_str(), value.c_str());
        reply->writeInt32(status);
        V4L2_DEBUG("setParameter");
    } else if (msg->methodName() == "mmap") {
        void* addr = (void*) msg->readInt64();
        size_t length = msg->readInt64();
        int32_t prot = msg->readInt32();
        int32_t flags = msg->readInt32();
        uint32_t offset = msg->readInt32();

        struct BufferMapServer map;
        map.port = msg->readInt32();
        map.buffer_index = msg->readInt32();
        map.plane_index = msg->readInt32();
        map.magic_memoffset = msg->readInt32();
        size_t size = (size_t)msg->readInt32();
        key_t key = (key_t)dup(msg->readFd());
        MSHMemSP ashmem;
        ashmem.reset(new MMAshMem(key, size, false));
        // void *mem = ashmem->getBase();
        void *mappedAddr = NULL;
        if (!ashmem) {
            V4L2_ERROR("no mem");
            status = MM_ERROR_NO_MEM;
        } else {
           mappedAddr = mDeviceInstance->mmap(nodeId, addr, length, prot, flags, offset);
        }

        ASSERT(mappedAddr);
        map.memory = ashmem;
        map.addrFromCodec = (uint8_t*)mappedAddr;
        mBufferMaps.push_back(map);
        reply->writeInt32(status);
        if (status == MM_ERROR_SUCCESS) {
            reply->writeInt64(int64_t(mappedAddr));
        }
        V4L2_DEBUG("mmap");
    } else if (msg->methodName() == "munmap") {
        void* addr = (void*)msg->readInt64();
        size_t length = (size_t)msg->readInt64();
        status = mDeviceInstance->munmap(nodeId, addr, length);
        uint32_t i=0;
        for (i=0; i<mBufferMaps.size(); i++) {
            if (mBufferMaps[i].addrFromCodec == addr) {
                mBufferMaps[i].memory.reset();
                break;
            }
        }
        ASSERT(i < mBufferMaps.size());
        reply->writeInt32(status);
        V4L2_DEBUG("munmap");
    } else {
        V4L2_DEBUG("unknown command");
    }
    sendMessage(reply);
    return true;
}

// onDeath is run in V4l2DeviceAdaptor messge thread
// send a message and handle it V4l2ServiceAdaptor thread, avoiding to race condition
void V4l2DeviceAdaptor::onDeath(const DLifecycleListener::DeathInfo& deathInfo)
{
    V4L2_INFO("client die: %s, mSeq %d, release shared memroy and handleMap",
        deathInfo.mName.c_str(), deathInfo.mSeq);

    V4L2_INFO("shared memory, port[0]: %d, port[1]: %d",
        mBuffers[0].size(), mBuffers[1].size());
    mBuffers[0].clear();
    mBuffers[1].clear();
    V4l2ServiceImp::mainLooper()->sendTask(Task(V4l2ServiceAdaptor::rmNode, mNodeId));
}

void V4l2DeviceAdaptor::onBirth(const DLifecycleListener::DeathInfo& deathInfo)
{
    V4L2_INFO("client birth, nName %s, mSeq %d",
        deathInfo.mName.c_str(), deathInfo.mSeq);
}


}


