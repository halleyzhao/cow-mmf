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
#include "v4l2_device_client.h"
#include "multimedia/mm_ashmem.h"
//#include "media_codec.h"
/*#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_CNTRHAL_BUILD__)
#include "multimedia/mm_surface_compat.h"
#endif*/

using namespace yunos;

namespace YUNOS_MM {
static const V4l2DeviceClientSP clientNil;
static const int mTimeOut = -1;
#define MSG_NAME(MSG) case (MSG): return #MSG;

DEFINE_LOGTAG1(V4l2DeviceClient, [V4L2])
DEFINE_LOGTAG1(V4l2DeviceProxy, [V4L2])

enum {
    MSG_createNode = 0,
    MSG_destroyNode,
    MSG_setCallbackName,
    MSG_open,
    MSG_close,
    MSG_ioctl,
    MSG_poll,
    MSG_setDevicePollInterrupt,
    MSG_clearDevicePollInterrupt,
    MSG_setParameter,
    MSG_mmap,
    MSG_munmap
};


#define MSG_STR(MSG) do{ \
    return ##MSG;        \
}while(0)

#define V4L2_DEBUG(x, ...) DEBUG("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_INFO(x, ...) INFO("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_ERROR(x, ...) ERROR("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_WARNING(x, ...) WARNING("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)
#define V4L2_VERBOSE(x, ...) VERBOSE("[%s] " x, mLibName.c_str(), ##__VA_ARGS__)

#define CHECK_VALID_NODE_ID()                                        \
    uint32_t nodeId = param->readInt32();                            \
    if (mNodeId != nodeId) {                                         \
        V4L2_DEBUG("invalid nodeId %d, mNodeId %d", nodeId, mNodeId); \
        status = MM_ERROR_UNKNOWN;                                   \
    }                                                                \

////////////////////////////////////////////////////////////////////////////
//V4l2DeviceClient
V4l2DeviceClient::V4l2DeviceClient()
{
    V4L2_INFO("+\n");
}

V4l2DeviceClient::~V4l2DeviceClient()
{
    V4L2_INFO("+\n");
    mClientNode.reset();
    mNodeId = IV4l2Service::kInvalidNodeId;

}

bool V4l2DeviceClient::open(const char* name, uint32_t flags)
{
    V4L2_VERBOSE("name %s, flags %d", name,  flags);
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_open);
    param->writeInt32(mNodeId);
    param->writeCString(name);
    mMessager->sendMsg(param);
    return (bool)param->readInt32();
}

bool V4l2DeviceClient::close()
{
    V4L2_VERBOSE("close");
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_close);
    param->writeInt32(mNodeId);
    mMessager->sendMsg(param);
    return (bool)param->readInt32();
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

int32_t V4l2DeviceClient::ioctl(uint64_t request, void* arg)
{
    V4L2_VERBOSE("ioctl command: %s, arg: %p\n", IoctlCommandString(request), arg);
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_ioctl);
    param->writeInt32(mNodeId);
    param->writeInt64((int64_t)request);
    param->writeRawPointer((uint8_t*)arg);
    mMessager->sendMsg(param);
    mm_status_t status = param->readInt32();
    if (status == MM_ERROR_SUCCESS) {


        arg = (void*)param->readRawPointer();
        if (VIDIOC_QUERYBUF == request) {

        struct v4l2_buffer *v4l2buf = (struct v4l2_buffer*) arg;
        INFO("buffer->m.planes[0].length: %d, buffer->m.planes[0].m.mem_offset: %d\n",
            v4l2buf->m.planes[0].length, v4l2buf->m.planes[0].m.mem_offset);
        }
    }
    return status;
}

int32_t V4l2DeviceClient::poll(bool poll_device, bool* event_pending)
{
    V4L2_VERBOSE("poll: %d", poll_device);
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_poll);
    param->writeInt32((int32_t)mNodeId);
    param->writeInt32((int32_t)poll_device);
    mMessager->sendMsg(param);
    mm_status_t status = param->readInt32();
    if (status == MM_ERROR_SUCCESS) {
        *event_pending =  (bool)param->readInt32();
        V4L2_VERBOSE("Poll event_pending: %d ", *event_pending);
    }
    return status;
}
int32_t V4l2DeviceClient::setDevicePollInterrupt()
{
    V4L2_VERBOSE("setDevicePollInterrupt");
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_setDevicePollInterrupt);
    param->writeInt32(mNodeId);
    mMessager->sendMsg(param);
    return param->readInt32();
}
int32_t V4l2DeviceClient::clearDevicePollInterrupt()
{
    V4L2_VERBOSE("clearDevicePollInterrupt");
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_clearDevicePollInterrupt);
    param->writeInt32(mNodeId);
    mMessager->sendMsg(param);
    return param->readInt32();
}

void* V4l2DeviceClient::mmap(void* addr, size_t length, uint32_t prot, uint32_t flags, uint32_t offset)
{
    V4L2_VERBOSE("addr: %p, length: %zu, prot: 0x%x, flags: 0x%x, offset: %d\n",
            addr, length, prot, flags, offset);

    MMParamSP param(new MMParam);
    param->writeInt32(MSG_mmap);
    param->writeInt32(mNodeId);
    param->writeInt64((int64_t)addr);
    param->writeInt64((int64_t)length);
    param->writeInt32(prot);
    param->writeInt32(flags);
    param->writeInt32(offset);
    mMessager->sendMsg(param);

    void* addrReply = (void*)param->readInt64();
    return addrReply;
}

int32_t V4l2DeviceClient::munmap(void* addr, size_t length)
{
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_munmap);
    param->writeInt32(mNodeId);

    param->writeInt64((int64_t)addr);
    param->writeInt64((int64_t)length);
    mMessager->sendMsg(param);
    return param->readInt32();
}

int32_t V4l2DeviceClient::setParameter(const char* key, const char* value)
{
    V4L2_VERBOSE("key %s, value %s", key,  value);
    MMParamSP param(new MMParam);
    param->writeInt32(MSG_setParameter);
    param->writeInt32(mNodeId);
    param->writeCString(key);
    param->writeCString(value);
    mMessager->sendMsg(param);
    return param->readInt32();
}

mm_status_t V4l2DeviceClient::handleMsg(MMParamSP param)
{
    int msg = param->readInt32();
    V4L2_VERBOSE("name %s", methodName(msg).c_str());
    bool set = true;
    mm_status_t status = MM_ERROR_SUCCESS;
    CHECK_VALID_NODE_ID();
    switch (msg) {
        case MSG_open:
        {
            std::string name = param->readCString();
            uint32_t flags = (uint32_t)param->readInt32();
            status = mClientNode->node()->open(nodeId, name.c_str(), flags);
            INFO("open status = %d", status);
            break;
        }
        case MSG_close:
        {
            status = mClientNode->node()->close(nodeId);
            break;
        }
        case MSG_ioctl:
        {
            uint64_t request = (uint64_t)param->readInt64();
            void *params = (void*)param->readRawPointer();
            INFO("params = %p", params);
            status = mClientNode->node()->ioctl(nodeId, request, params);
            if (status == MM_ERROR_SUCCESS) {
                set = false;
                param->writeInt32(status);
                param->writeRawPointer((uint8_t*)params);
                INFO("params = %p", params);
            }
            break;
        }
        case MSG_poll:
        {
            bool poll_device = (bool)param->readInt32();
            bool event_pending = false;
            status = mClientNode->node()->poll(nodeId, poll_device, &event_pending);
            if (status == MM_ERROR_SUCCESS) {
                set = false;
                param->writeInt32(status);
                param->writeInt32((int32_t)event_pending);
            }
            break;
        }
        case MSG_setDevicePollInterrupt:
        {
            status = mClientNode->node()->setDevicePollInterrupt(nodeId);
            break;
        }

        case MSG_clearDevicePollInterrupt:
        {
            status = mClientNode->node()->clearDevicePollInterrupt(nodeId);
            break;
        }
        case MSG_setParameter:
        {
            std::string key = param->readCString();
            std::string value = param->readCString();
            status = mClientNode->node()->setParameter(nodeId, key.c_str(), value.c_str());
            break;
        }
        case MSG_mmap:
        {
            void* addr = (void*) param->readInt64();
            size_t length = param->readInt64();
            int32_t prot = param->readInt32();
            int32_t flags = param->readInt32();
            uint32_t offset = param->readInt32();
            void *mappedAddr = mClientNode->node()->mmap(nodeId, addr, length, prot, flags, offset);
            set = false;
            param->writeInt64((int64_t)mappedAddr);
            break;
        }
        case MSG_munmap:
        {
            void* addr = (void*) param->readInt64();
            size_t length = param->readInt64();
            status = mClientNode->node()->munmap(nodeId, addr, length);
            break;
        }
        default:
        {
            ASSERT(0 && "Should not be here\n");
            break;
        }
    }

    if (set) {
        param->writeInt32(status);
    }

    return status;
}

bool V4l2DeviceClient::handleSignal(const SharedPtr<DMessage> &msg)
{
    // no callback from v4l2 server
    int type = msg->readInt32();
    V4L2_DEBUG("callback msg %s", eventName(type).c_str());
    return true;
}

mm_status_t V4l2DeviceClient::createNode(uint32_t *node)
{
    ASSERT(node);
    *node = IV4l2Service::kInvalidNodeId;
    mm_status_t status = MM_ERROR_SUCCESS;

    mManager = V4l2Manager::getInstance();
    if (!mManager) {
        V4L2_ERROR("getInstance failed");
        return MM_ERROR_NO_MEM;
    }

    status = mManager->createNode(&mNodeId);
    if (status != MM_ERROR_SUCCESS ||
        mNodeId == IV4l2Service::kInvalidNodeId) {
        V4L2_ERROR("nodeId is invalid");
        return status;
    }

    // connect to v4l2 device adaptor
    char id[64] = {0};
    sprintf(id, "%d", mNodeId);
    mClientNode = ClientNode<V4l2DeviceProxy>::create("vdc", id, this);
    if (!mClientNode || !mClientNode->init()) {
        V4L2_ERROR("failed to init\n");
        return MM_ERROR_UNKNOWN;
    }

    mMessager = new UBusMessage<V4l2DeviceClient>(this);
    *node = mNodeId;
    V4L2_DEBUG("mNodeId %d", mNodeId);

    status = mMessager->addRule(mMessager);
    if (status != MM_ERROR_SUCCESS) {
        return status;
    }
    return 0;

}

void V4l2DeviceClient::destroyNode(uint32_t nodeId)
{
    if (mNodeId != nodeId) {
        V4L2_ERROR("invalid nodeId %d, mNodeId %d", nodeId, mNodeId);
        return;
    }
    mManager->destroyNode(nodeId);
    mMessager->removeRule();
}

std::string V4l2DeviceClient::eventName(int msg)
{
    return "unknown msg";
}

std::string V4l2DeviceClient::methodName(int msg)
{
    switch (msg) {
        MSG_NAME(MSG_createNode);
        MSG_NAME(MSG_destroyNode);
        default: return "unknown msg";
    }
    return "unknown msg";
}

////////////////////////////////////////////////////////////////////////////////////
V4l2DeviceProxy::V4l2DeviceProxy(const yunos::SharedPtr<yunos::DService>& service,
    yunos::String serviceName, yunos::String pathName, yunos::String iface, void *arg)
        : yunos::DProxy(service, pathName, iface)
        , mStoreMetaDataInBuffers(false)
        , mClient(static_cast<V4l2DeviceClient*>(arg))
{
    V4L2_INFO("pathName %s, iface %s\n", pathName.c_str(), iface.c_str());
}

V4l2DeviceProxy::~V4l2DeviceProxy()
{
    V4L2_INFO("+\n");
}

bool V4l2DeviceProxy::open(uint32_t nodeId, const char* name, uint32_t flags)
{
    CHECK_OBTAIN_MSG_RETURN1("open", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    msg->writeString(String(name));
    msg->writeInt32(flags);
    CHECK_REPLY_MSG_RETURN_BOOL();
}

bool V4l2DeviceProxy::close(uint32_t nodeId)
{
    CHECK_OBTAIN_MSG_RETURN1("close", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    CHECK_REPLY_MSG_RETURN_BOOL();
}

int32_t V4l2DeviceProxy::ioctl(uint32_t nodeId, uint64_t request, void* arg)
{
    CHECK_OBTAIN_MSG_RETURN1("ioctl", MM_ERROR_NO_MEM);
    static int32_t inputBufferCount = 0;
    static int32_t outputBufferCount = 0;
    int32_t sz = -1;
    uint8_t *buf = NULL, *buf2 = NULL;
    INFO("ioctl command: %s, arg: %p\n", IoctlCommandString(request), arg);
    msg->writeInt32(nodeId);
    msg->writeInt64((int64_t)request);
    switch(request) {
        case VIDIOC_DQBUF:
        case VIDIOC_QBUF: {
            struct v4l2_buffer* v4l2buf = (struct v4l2_buffer*)arg;
            msg->writeByteBuffer(sizeof(struct v4l2_buffer), (int8_t *)v4l2buf);

            uint32_t length = v4l2buf->length;
            struct v4l2_plane *planes = v4l2buf->m.planes;
            msg->writeByteBuffer(sizeof(struct v4l2_plane) * length, (int8_t *)planes);

            INFO("v4l2buf->memory: %d, v4l2buf->type: %d\n", v4l2buf->memory, v4l2buf->type);
            if (request == VIDIOC_QBUF) {
                switch(v4l2buf->memory) {
                    case V4L2_MEMORY_PLUGIN_BUFFER_HANDLE: {
                        //buffer_handle_t handle = (buffer_handle_t)(planes[0].m.userptr);
                        //INFO("camera_data_dump in V4l2DeviceClient: %p", handle);
                    }
                    break;
                    case V4L2_MEMORY_MMAP: {
                        // the MSHMemSP has been setup during VIDIOC_REQBUFS/VIDIOC_QUERYBUF
                        // nothing in client side, while server side requires a copy
                    }
                    break;
                    case V4L2_MEMORY_USERPTR: {
                        uint32_t  port = v4l2buf->type;
                        uint32_t buffer_index = v4l2buf->index;
                        uint32_t plane = 0;
                        INFO("port: %d, mBufferMaps.size(): %zu\n", port, mBufferMaps.size());
                        for (plane = 0; plane < v4l2buf->length; plane++) {
                            // find the MSHMemSP to pass user_ptr data across process boundary
                            uint32_t mapIndex = 0;
                            for (mapIndex=0; mapIndex<mBufferMaps.size(); mapIndex++) {
                                if (mBufferMaps[mapIndex].port == port &&
                                    mBufferMaps[mapIndex].buffer_index == buffer_index &&
                                    mBufferMaps[mapIndex].plane_index == plane) {
                                    INFO("mapIndex: %d, port: %d, buffer_index: %d, plane: %d\n",
                                        mapIndex, port, buffer_index, plane);
                                    break;
                                }
                            }
                            if (mapIndex == mBufferMaps.size()) {
                                // if there is not a MSHMemSP for this user_ptr yet, create it now
                                struct BufferMap map;
                                map.port = port;
                                map.buffer_index = buffer_index;
                                map.plane_index = plane;
                                // FIXME
                                //map.memory = mMemoryDealer->allocate(MAX_COMPRESSED_FRAME_SIZE);
                                INFO("push map port: %d, buffer_index: %d, plane_index: %d\n", port, buffer_index, plane);
                                mBufferMaps.push_back(map);
                            }
                            ASSERT(mBufferMaps[mapIndex].memory->getBase());

                            // associate user_ptr with MSHMemSP
                            mBufferMaps[mapIndex].magic_addr = (void*)(planes[plane].m.userptr);

                            // copy user_ptr input data to MSHMemSP
                            if (port == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                                memcpy(mBufferMaps[mapIndex].memory->getBase(), (void*)(planes[plane].m.userptr), planes[plane].bytesused);
#ifdef DUMP_DATA_DEBUG
                                DEBUG("dump_input_data, inputBufferCount: %d, plane: %d\n", inputBufferCount, plane);
                                hexDump((uint8_t*)(mBufferMaps[mapIndex].memory->getBase()), planes[plane].bytesused, 32);
#endif
                            }
                            //data.writeStrongBinder(mBufferMaps[mapIndex].memory->asBinder());
                        } // DQBUF copies data after server return, see below
                    }
                    break;
                    default:
                    break;
                }
            }
        }
        break;
        case VIDIOC_QUERYCAP:
            // no input, but output with struct v4l2_capability.
            break;
        case VIDIOC_STREAMON:
        case VIDIOC_STREAMOFF: {
            __u32 type = * ((__u32*)arg);
            msg->writeInt32(type);
        }
        break;
        case VIDIOC_REQBUFS: {
            msg->writeByteBuffer(sizeof(struct v4l2_requestbuffers), (int8_t *)arg);
        }
        break;
        case VIDIOC_QUERYBUF: {
            struct v4l2_buffer* v4l2buf = (struct v4l2_buffer*)arg;
            msg->writeByteBuffer(sizeof(struct v4l2_buffer), (int8_t *)v4l2buf);

            uint32_t length = v4l2buf->length;
            struct v4l2_plane *planes = v4l2buf->m.planes;
            msg->writeByteBuffer(sizeof(struct v4l2_plane) * length, (int8_t *)planes);

        }
        break;
        case VIDIOC_S_EXT_CTRLS: {
        }
        break;
        case VIDIOC_S_PARM: {
        }
        break;
        case VIDIOC_S_FMT: {
            msg->writeByteBuffer(sizeof(struct v4l2_format), (int8_t *)arg);
        }
        break;
        case VIDIOC_G_FMT: {
            msg->writeByteBuffer(sizeof(struct v4l2_format), (int8_t *)arg);
        }
        break;
        case VIDIOC_G_CTRL: {
            msg->writeByteBuffer(sizeof(struct v4l2_control), (int8_t *)arg);
        }
        break;
        case VIDIOC_DQEVENT: {
            msg->writeByteBuffer(sizeof(struct v4l2_event), (int8_t *)arg);
        }
        break;
        case VIDIOC_S_CROP:
        default:
            ERROR("unknown command");
        break;
    }
    HANDLE_INVALID_REPLY_RETURN_INT();


    mm_status_t status = MM_ERROR_SUCCESS;
    status = reply->readInt32();
    if (status != MM_ERROR_SUCCESS) {
        ERROR("%s fail, return: %d\n", IoctlCommandString(request), status);
        return status;
    }

    // parse return value
    switch(request) {
    case VIDIOC_DQBUF:
    case VIDIOC_QBUF: {
        struct v4l2_buffer* v4l2buf = (struct v4l2_buffer*)arg;
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_buffer));
        memcpy(arg, buf, sizeof(struct v4l2_buffer));

        sz = -1;
        uint8_t* buffer1 = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_plane) * v4l2buf->length);
        memcpy(v4l2buf->m.planes, buffer1, sizeof(struct v4l2_plane) * v4l2buf->length);
        delete [] buffer1;
        buffer1 = NULL;

        // DEBUG
        if (request == VIDIOC_QBUF && v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
            inputBufferCount++;
        else if (request == VIDIOC_DQBUF && v4l2buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
            outputBufferCount++;

        DEBUG("v4l2buf->memory: %d, v4l2buf->type: %d", v4l2buf->memory, v4l2buf->type);
        // noop for QBUF
        if (request == VIDIOC_QBUF)
            break;
        // noop for input port
        if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
            break;
        struct v4l2_plane *planes = v4l2buf->m.planes;
        uint32_t  port = v4l2buf->type;
        uint32_t buffer_index = v4l2buf->index;
        uint32_t plane = 0;
        for (plane = 0; plane < v4l2buf->length; plane++) {
            // find the MSHMemSP to get user_ptr data across process boundary
            uint32_t mapIndex = 0;
            for (mapIndex=0; mapIndex<mBufferMaps.size(); mapIndex++) {
                if (mBufferMaps[mapIndex].port == port &&
                    mBufferMaps[mapIndex].buffer_index == buffer_index &&
                    mBufferMaps[mapIndex].plane_index == plane)
                    break;
            }

            ASSERT(mBufferMaps[mapIndex].memory->getBase());
            if (v4l2buf->memory == V4L2_MEMORY_USERPTR) {
                memcpy(mBufferMaps[mapIndex].magic_addr, mBufferMaps[mapIndex].memory->getBase(), planes[plane].bytesused);
                planes[plane].m.userptr = (unsigned long)mBufferMaps[mapIndex].magic_addr;
            }
#ifdef DUMP_DATA_DEBUG
            encodedDataDump.dump(mBufferMaps[mapIndex].memory->getBase(), planes[plane].bytesused);
            DEBUG("dump_output_data, outputBufferCount: %d, plane: %d\n", outputBufferCount-1, plane);
            hexDump(mBufferMaps[mapIndex].memory->getBase(), planes[plane].bytesused, 32);
#endif
        }
    }
    break;
    case VIDIOC_QUERYCAP: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_capability));
        memcpy(arg, buf, sizeof(struct v4l2_capability));
    }
    break;
    case VIDIOC_STREAMON:
    case VIDIOC_STREAMOFF: {
        __u32 *type = (__u32*) arg;
        *type = reply->readInt32();
    }
    break;
    case VIDIOC_REQBUFS: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_requestbuffers));
        memcpy(arg, buf, sizeof(struct v4l2_requestbuffers));
    }
    break;
    case VIDIOC_QUERYBUF: {
        void *arg1 = NULL, *arg2 = NULL;

        arg1 = malloc(sizeof(struct v4l2_buffer));
        ASSERT(arg1);
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_buffer));

        memcpy(arg1, buf, sizeof(struct v4l2_buffer));

        struct v4l2_buffer *v4l2buf = (struct v4l2_buffer*) arg1;

        arg2 = malloc(sizeof(struct v4l2_plane) * v4l2buf->length);
        ASSERT(arg2);
        v4l2buf->m.planes = (struct v4l2_plane*)arg2;
        sz = -1;
        buf2 = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_plane) * v4l2buf->length);

        memcpy(v4l2buf->m.planes, buf2, sizeof(struct v4l2_plane) * v4l2buf->length);
        memcpy(arg, arg1, sizeof(struct v4l2_requestbuffers));

        struct v4l2_buffer *v4l2buf1 = (struct v4l2_buffer*) arg;
        memcpy(v4l2buf1->m.planes, buf2, sizeof(struct v4l2_plane) * v4l2buf->length);

        struct BufferMap map;
        map.port = v4l2buf->type;
        map.buffer_index = v4l2buf->index;
        uint32_t i = 0;
        for (i=0; i< v4l2buf->length; i++) {
            map.plane_index = i;
            map.magic_memoffset = v4l2buf->m.planes[i].m.mem_offset;
            mBufferMaps.push_back(map);
        }
    }
    break;
    case VIDIOC_S_EXT_CTRLS: {
    }
    break;
    case VIDIOC_S_PARM: {
    }
    break;
    case VIDIOC_S_FMT: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_format));
        memcpy(arg, buf, sizeof(struct v4l2_format));
    }
    break;
    case VIDIOC_G_FMT: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_format));
        memcpy(arg, buf, sizeof(struct v4l2_format));
    }
    break;
    case VIDIOC_G_CTRL: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_control));
        memcpy(arg, buf, sizeof(struct v4l2_control));
    }
    break;
    case VIDIOC_DQEVENT: {
        buf = (uint8_t *)reply->readByteBuffer(sz);
        ASSERT((uint32_t)sz == sizeof(struct v4l2_event));
        memcpy(arg, buf, sizeof(struct v4l2_event));
    }
    break;
    case VIDIOC_S_CROP:
    default:
        ERROR("unknown command");
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
    return status;
}

int32_t V4l2DeviceProxy::poll(uint32_t nodeId, bool poll_device, bool* event_pending)
{
    CHECK_OBTAIN_MSG_RETURN1("poll", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    msg->writeInt32((int32_t)poll_device);
    HANDLE_INVALID_REPLY_RETURN_INT();

    mm_status_t status = MM_ERROR_SUCCESS;
    status = reply->readInt32();
    V4L2_DEBUG("status %d", status);
    if (status == MM_ERROR_SUCCESS) {
        *event_pending = (bool)reply->readInt32();
    }
    return status;
}

int32_t V4l2DeviceProxy::setDevicePollInterrupt(uint32_t nodeId)
{
    CHECK_OBTAIN_MSG_RETURN1("setDevicePollInterrupt", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    CHECK_REPLY_MSG_RETURN_INT();
}

int32_t V4l2DeviceProxy::clearDevicePollInterrupt(uint32_t nodeId)
{
    CHECK_OBTAIN_MSG_RETURN1("clearDevicePollInterrupt", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    CHECK_REPLY_MSG_RETURN_INT();
}

#define BUFFER_HANDLE_RESERVED_SIZE 1024
#define MAX_COMPRESSED_FRAME_SIZE   1048675 // 1M
void* V4l2DeviceProxy::mmap(uint32_t nodeId, void* addr, size_t length, int32_t prot,
                int32_t flags, unsigned int offset)
{

    CHECK_OBTAIN_MSG_RETURN1("mmap", NULL);
    msg->writeInt32(nodeId);

    uint32_t  i = 0;
    for (i=0; i<mBufferMaps.size(); i++) {
        if (mBufferMaps[i].magic_memoffset == offset)
            break;
    }
    ASSERT(i < mBufferMaps.size());

    MSHMemSP ashmem;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
    if (mStoreMetaDataInBuffers) {
        ashmem = MMAshMem::create("OMXProxy", MAX_COMPRESSED_FRAME_SIZE + BUFFER_HANDLE_RESERVED_SIZE);
    } else
#endif
    {
        ashmem = MMAshMem::create("OMXProxy", MAX_COMPRESSED_FRAME_SIZE);
    }

    if (ashmem == NULL) {
        V4L2_ERROR("no mem");
        return NULL;
    }
    mBufferMaps[i].memory = ashmem;

    msg->writeInt64((int64_t)addr);
    msg->writeInt64((int64_t)length);
    msg->writeInt32(prot);
    msg->writeInt32(flags);
    msg->writeInt32(offset);

    msg->writeInt32(mBufferMaps[i].port);
    msg->writeInt32(mBufferMaps[i].buffer_index);
    msg->writeInt32(mBufferMaps[i].plane_index);
    msg->writeInt32(mBufferMaps[i].magic_memoffset);
    msg->writeInt32((int32_t)MAX_COMPRESSED_FRAME_SIZE);
    msg->writeFd(ashmem->getKey());
    HANDLE_INVALID_REPLY_RETURN1(NULL);

    mm_status_t status = MM_ERROR_SUCCESS;
    status = reply->readInt32();
    void* magic_addr = NULL;
    V4L2_DEBUG("status %d", status);
    if (status == MM_ERROR_SUCCESS) {
        magic_addr = (void*)reply->readInt64();
    }
    mBufferMaps[i].magic_addr = magic_addr;

    void *ret = ashmem->getBase();
    return ret;

}

int32_t V4l2DeviceProxy::munmap(uint32_t nodeId, void* addr, size_t length)
{
    CHECK_OBTAIN_MSG_RETURN1("munmap", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    msg->writeInt64((int64_t)addr);
    msg->writeInt64((int64_t)length);
    CHECK_REPLY_MSG_RETURN_INT();
}

int32_t V4l2DeviceProxy::setParameter(uint32_t nodeId, const char* key, const char* value)
{
    CHECK_OBTAIN_MSG_RETURN1("setParameter", MM_ERROR_NO_MEM);
    msg->writeInt32(nodeId);
    msg->writeString(String(key));
    msg->writeString(String(value));
    CHECK_REPLY_MSG_RETURN_INT();
}

// Called in msg thread
void V4l2DeviceProxy::onDeath(const DLifecycleListener::DeathInfo& deathInfo)
{
    INFO("media_service die: %s, mSeq %d",
        deathInfo.mName.c_str(), deathInfo.mSeq);

    if (!mClient) {
        WARNING("v4l2 client is already destroyed");
        return;
    }

    DProxy::onDeath(deathInfo);
}

}
