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

#include <multimedia/mm_debug.h>
#include <multimedia/media_meta.h>
#include <multimedia/mmparam.h>
#include <native_surface_help.h>
#include <media_surface_texture.h>

#include <MediaProxy.h>
#include <SideBandIPC.h>

#include <dbus/dbus.h>
#include <dbus/DMessage.h>
#include <dbus/DService.h>
#include <dbus/DServiceManager.h>
#include <dbus/DSignalCallback.h>
#include <dbus/DSignalRule.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaProxy)

class MediaProxyCb : public dbus::DSignalCallback {
public:
    MediaProxyCb(MediaProxy *client)
        : mOwner(client) {
        INFO("");
    }

    virtual ~MediaProxyCb() {
        INFO("");
    }

    bool handleSignal(const SharedPtr<DMessage>& msg);

private:
    MediaProxy *mOwner;
    static const char * MM_LOG_TAG;
};

DEFINE_LOGTAG(MediaProxyCb)

bool MediaProxyCb::handleSignal(const SharedPtr<DMessage> &msg) {
    if (!mOwner) {
        ERROR("no owner");
        return false;
    }

    if (!mOwner->mProxyListener) {
        ERROR("owner has no owner");
        return true;
    }

    if(msg->signalName() != mOwner->mCallbackName) {
        ERROR("unknown callback %s", msg->signalName().c_str());
        return false;
    }

    int type, param1, param2;

    type = msg->readInt32();
    param1 = msg->readInt32();
    param2 = msg->readInt32();
    if (type == 202) {
        MMParamSP param(new MMParam());
        String format = msg->readString();
        String data = msg->readString();
        String data1 = msg->readString();
        param->writeCString(format.c_str());
        param->writeCString(data.c_str());
        param->writeCString(data1.c_str());
        param->writeInt32(msg->readInt32());
        param->writeInt32(msg->readInt32());
        param->writeInt32(msg->readInt32());
        mOwner->mProxyListener->onMessage(type, param1, param2, param.get());
        return true;
    } else if (type == 5) {
        // MSG_SET_VIDEO_SIZE
        mOwner->destroyBufferMap();
    } else if (type == 501) {
        // MSG_UPDATE_TEXTURE_IMAGE
        if (mOwner->mMstListener) {
            mOwner->mMstListener->onMessage(0, param1, param2);
            DEBUG("call MST listener: param1 %d param2 %d", param1, param2);
            return true;
        }
    }

    INFO("msg %d: param1 %d param2 %d", type, param1, param2);

    mOwner->mProxyListener->onMessage(type, param1, param2, NULL);

    return true;
}

MediaProxy::MediaProxy(const SharedPtr<DService>& service,
                                     const String &path,
                                     const String &iface)
    : DProxy(service, path, iface),
      mTearDown(false),
      mProxyListener(NULL),
      mWidth(-1),
      mHeight(-1),
      mStride(-1),
      mFormat(-1),
      mUsage(-1),
      mVersion(-1),
      mNumFds(-1),
      mNumInts(-1) {
    mMstListener = NULL;
}

MediaProxy::~MediaProxy() {
    destroyBufferMap();
}

mm_status_t MediaProxy::setListener(ProxyListener * listener) {

    std::string name = service().c_str();
    size_t offset = 0, preOffset;
    do {
        preOffset = offset;
        offset = name.find('.', offset + 1);
        VERBOSE("offset is %d", offset);
    } while(offset != std::string::npos);

    mCallbackName = name.c_str() + preOffset + 1;
    mCallbackName.append(".callback");

    mCallback = new MediaProxyCb(this);
    mRule = addSignalRule(mCallbackName,
                static_cast<dbus::DSignalCallback*>(mCallback.pointer()));

    if (!mRule) {
        ERROR("fail to create rule for callback, name is %s", mCallbackName.c_str());
        return MM_ERROR_UNKNOWN;
    }

    mProxyListener = listener;
    INFO("add callback rule %s", mCallbackName.c_str());

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String("setListener"));
    CHECK_MSG(msg)

    msg->writeString(mCallbackName);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallVoid(const char *method) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    MM_METHOD_CALL_VOID(method);
}

mm_status_t MediaProxy::makeCallSetInt(const char* method, int i) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    msg->writeInt32(i);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallSetInt64(const char* method, int64_t i) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    msg->writeInt64(i);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallGetInt(const char* method, int *p) {
    if (!p || !method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg);

    MM_CHECK_RESULT();

    *p = reply->readInt32();

    return result;
}

mm_status_t MediaProxy::makeCallGetInt64(const char* method, int64_t *p) {
    if (!p || !method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg);

    MM_CHECK_RESULT();

    *p = reply->readInt64();

    return result;
}

mm_status_t MediaProxy::makeCallSetBool(const char* method, bool b) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    msg->writeBool(b);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallGetBool(const char* method, bool *p) {
    if (!p || !method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg);

    MM_CHECK_RESULT();

    *p = reply->readBool();

    return result;

}

mm_status_t MediaProxy::makeCallSetPointer(const char* method, void * handle) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    //FIXME msg->writeInt64(0);
    //msg->writeInt64((int64_t)handle);
    msg->writeInt64(0);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallSetMeta(const char* method, const MediaMetaSP &meta) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    meta->writeToMsg(msg);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallGetMeta(const char* method, MediaMetaSP &meta) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg);

    if (!reply) {
        ERROR("invalid reply");
        return MM_ERROR_INVALID_PARAM;
    }
    MediaMetaSP meta1 = MediaMeta::create();
    meta1->readFromMsg(reply);
    meta = meta1;

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallSetCStr(const char* method, const char* str) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    msg->writeString(str);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallGetString(const char* method, String &string) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    MM_SEND_MSG(msg);

    MM_CHECK_RESULT();

    string = reply->readString();

    return result;
}

mm_status_t MediaProxy::makeCallSetFd(const char* method, int fd) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)
    // msg->writeInt32(fd);

    if (fd >= 0)
        msg->writeFd(fd);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

mm_status_t MediaProxy::makeCallSendFd(const char* method, int fd) {
    if (!method)
        return MM_ERROR_INVALID_PARAM;

    String socketName;

    mm_status_t status =
                makeCallGetString("sideBandChannel", socketName);

    if (status != MM_ERROR_SUCCESS) {
        ERROR("fail to setup 'side band channel', return %d", status);
        return status;
    }

    // TODO MediaProxy use SideBandIPC client, not server
    SideBandIPC p(socketName.c_str(), true);
    if (MM_ERROR_ASYNC != p.sendFdAsync(fd)) {
        ERROR("sendFdAsync fail");
        return MM_ERROR_OP_FAILED;
    }

    INFO("make call to service to retrive fd");
    SharedPtr<DMessage> msg =
            obtainMethodCallMessage(String(method));
    CHECK_MSG(msg)

    msg->writeInt32(fd);

    MM_SEND_MSG(msg);

    MM_HANDLE_REPLY();
}

class RemoteBuffer : public yunos::libgui::BaseNativeSurfaceBuffer {
public:
    RemoteBuffer(int w, int h, int stride, int format, int flag, pf_buffer_handle_t target)
        : yunos::libgui::BaseNativeSurfaceBuffer(w, h, stride, format, flag, target) {
        INFO("");
        mHandle = target;
    }

    virtual ~RemoteBuffer() {
        INFO("");

#ifdef YUNOS_ENABLE_UNIFIED_SURFACE
        native_target_close(mHandle);
        native_target_delete(const_cast<native_target_t*>(mHandle));
#else
        const int numFds = mHandle->numFds;

        int i;
        for (i=0 ; i<numFds ; i++) {
            close(mHandle->data[i]);
        }

        free((void*)mHandle);
#endif
    }

private:

    pf_buffer_handle_t mHandle;
    const char *MM_LOG_TAG = "ClientBuffer";
};

MMNativeBuffer* MediaProxy::makeCallGetAnb(const char* method, int64_t key) {
    if (!method)
        return NULL;

    int i;
    int fds[MAX_INTS];
//#ifdef USE_SIDEBAND_2
// ubus cannot IPC pass fd which is backed by dmabuf
#if 1
    INFO("use sideband channel to get MMNativeBuffer");

    String socketName;
    mm_status_t status =
                makeCallGetString("sideBandChannel2", socketName);

    if (status != MM_ERROR_SUCCESS) {
        ERROR("fail to setup 'side band channel2', return %d", status);
        return NULL;
    }

    SideBandIPC p(socketName.c_str(), false);
    if (!p.init()) {
        ERROR("fail to init side band ipc, cannot get ANB");
        return NULL;
    }

    status = makeCallVoid(method);
    if (status != MM_ERROR_SUCCESS) {
        ERROR("fail to make call %s, status %d", method, status);
        return NULL;
    }

    for (i = 0; (i < mNumFds) && (i < MAX_INTS); i++) {
        fds[i] = p.recvFd();
        INFO("readFd %d", fds[i]);
    }
#else
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    MM_SEND_MSG(msg);

    int result = MM_ERROR_UNKNOWN;
    if (!reply || (result = reply->readInt32()) != MM_ERROR_SUCCESS) {
        ERROR("fail, reply is %p, result is %d", reply.pointer(), result);
        return NULL;
    }

    for (i = 0; (i < mNumFds) && (i < MAX_INTS); i++) {
        fds[i] = reply->readFd();
        INFO("readFd %d", fds[i]);
    }

#endif

#ifdef YUNOS_ENABLE_UNIFIED_SURFACE
    native_target_t* target = native_target_create(mNumFds, mNumInts);
    if (!target) {
        ERROR("no memory");
        return NULL;
    }
    target->pounds = mVersion;
    for (i = 0; (i < mNumFds) && (i < MAX_INTS); i++)
        target->fds.data[i] = fds[i];

    for (i = 0; (i < mNumInts) && (i < MAX_INTS); i++)
        target->attributes.data[i] = mInts[i];

#else
    int size = ((3 + mNumFds + mNumInts) * sizeof(int));
    native_handle_t* handle = (native_handle_t*)malloc(size);;
    if (!handle) {
        ERROR("malloc buffer_handle_t fail");
        return NULL;
    }

    handle->version = mVersion;
    handle->numFds = mNumFds;
    handle->numInts = mNumInts;

    for (i = 0; (i < mNumFds) && (i < MAX_INTS); i++)
        handle->data[i] = fds[i];

    for (int j = 0; (j < mNumInts) && (j < MAX_INTS); j++)
        handle->data[i + j] = mInts[j];

    pf_buffer_handle_t target = handle;
#endif

    YunAllocator &allocator(YunAllocator::get());
    allocator.registerBuffer(target);

    RemoteBuffer *anb = new RemoteBuffer(mWidth, mHeight, mStride, mFormat, mUsage, target);
#ifdef YUNOS_ENABLE_UNIFIED_SURFACE
    (anb)->base.incRef(&((anb)->base));
#else
    anb->incStrong(anb);
#endif

    mBufferMap[anb] = key;
    mHandleMap[key] = anb;
    INFO("key is 0x%08x anb is %p", (uint32_t)key, (MMNativeBuffer*)anb);

    return anb;
}

int MediaProxy::makeCallSetAnb(const char* method, MMNativeBuffer *anb) {
    if (!method)
        return -1;

    std::map<MMNativeBuffer*, int64_t>::iterator it = mBufferMap.find(anb);
    if (it == mBufferMap.end() || !it->second) {
        ERROR("unknown buffer %p", anb);
        return -1;
    }

    mm_status_t status = makeCallSetInt64(method, it->second);

    if (status == MM_ERROR_SUCCESS)
        return 0;

    ERROR("%s fail", method);
    return -1;
}

int64_t MediaProxy::makeCallGetAnbHandle(const char* method, int i) {
    if (!method)
        return 0;

    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));
    if (!msg) {
        ERROR("obtain null message");
        return 0;
    }

    msg->writeInt32(i);

    MM_SEND_MSG(msg);

    int64_t result = 0;

    if (reply)
        result = reply->readInt64();

    if (result) {
        mWidth = reply->readInt32();
        mHeight = reply->readInt32();
        mStride = reply->readInt32();
        mFormat = reply->readInt32();
        mUsage = reply->readInt32();
        mVersion = reply->readInt32();
        mNumFds = reply->readInt32();
        mNumInts = reply->readInt32();
        DEBUG("anb(0x%08x): w %d, h %d, stride %d, format 0x%x, usage/flags %x, version/pounds %d, num fds %d, num ints %d",
             (uint32_t)result, mWidth, mHeight, mStride, mFormat, mUsage, mVersion, mNumFds, mNumInts);

        for (int x = 0; (x < mNumInts) && (x < MAX_INTS); x++)
            mInts[x] = reply->readInt32();
    }

    return result;
}

// deprecated
bool MediaProxy::handleSignal(const SharedPtr<DMessage>& msg) {
    INFO("handle signal: %s", msg->signalName().c_str());

    return false;
}

mm_status_t MediaProxy::release() {
    mTearDown = true;

    if (mRule) {
        INFO("remove the listener to service");
        removeSignalRule(mRule);
    }

    MM_METHOD_CALL_VOID("tearDown");
}

int MediaProxy::test(String &method) {
    SharedPtr<DMessage> msg = obtainMethodCallMessage(method);

    MM_SEND_MSG(msg);
    MM_HANDLE_REPLY();
}

void MediaProxy::destroyBufferMap() {

    INFO("destroy buffer map");
    std::map<MMNativeBuffer*, int64_t>::iterator it = mBufferMap.begin();

    for (; it != mBufferMap.end(); it ++) {
        YunAllocator &allocator(YunAllocator::get());
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
        allocator.unregisterBuffer(it->first->handle);
#else
        allocator.unregisterBuffer(it->first->target);
#endif

        //free((void*)it->first->handle);
        //delete static_cast<RemoteBuffer*>(it->first);
        RemoteBuffer* buffer = static_cast<RemoteBuffer*>(it->first);
        if (buffer)
#ifdef YUNOS_ENABLE_UNIFIED_SURFACE
            (buffer)->base.decRef(&((buffer)->base));
#else
            buffer->decStrong(buffer);
#endif
    }

    mBufferMap.clear();
    mHandleMap.clear();

    INFO("destroy buffer map done");
}

void MediaProxy::onDeath(const DLifecycleListener::DeathInfo& deathInfo) {
    if (mTearDown) {
        INFO("media adaptor is destroy as result of teardown invoked from client");
        return;
    }

    WARNING("media server die");

    if (mProxyListener) {
        INFO("inform client that server is die");
        mProxyListener->onServerUpdate(true);
    }

    DProxy::onDeath(deathInfo);
}

} // end of namespace YUNOS_MM
