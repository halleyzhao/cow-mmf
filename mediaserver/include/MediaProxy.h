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

#include <string/String.h>
#include <dbus/DProxy.h>
#include <dbus/DError.h>

#include <multimedia/mm_cpp_utils.h>

#include <MediaMethodID.h>

#include <map>

#ifndef __media_proxy_h
#define __media_proxy_h

#if defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(YUNOS_ENABLE_UNIFIED_SURFACE)
struct NativeSurfaceBuffer;
#define ProxyClientBuffer NativeSurfaceBuffer
#elif __MM_YUNOS_CNTRHAL_BUILD__
struct ANativeWindowBuffer; //ctnr
#define ProxyClientBuffer ANativeWindowBuffer //ctnr
#elif defined __MM_YUNOS_LINUX_BSP_BUILD__
#define ProxyClientBuffer void
#endif

namespace YunOSMediaCodec {
class MediaSurfaceTexture;
class SurfaceTextureListener;
}

namespace yunos {
    class DService;
    class DMessage;
};

namespace YUNOS_MM {

using namespace yunos;

class MMParam;
class MediaProxyCb;
class DSignalRule;

#define MM_MSG_TIMEOUT 8000

class MediaProxy : public DProxy {

public:
    MediaProxy(const SharedPtr<DService>& service,
                       const String& path,
                       const String& iface);

    virtual ~MediaProxy();

    //virtual bool handleMethodCall(const SharedPtr<DMessage>& msg);
    virtual bool handleSignal(const SharedPtr<DMessage>& msg);

#ifdef SINGLE_THREAD_PROXY
    virtual int64_t handleMethod(MMParam &param) { return (int64_t)MM_ERROR_SUCCESS; }
    void setMeta1(const MediaMetaSP meta) { mMeta1 = meta; }
    void setMeta2(MediaMetaSP meta) { mMeta2 = meta; }
#endif

    class ProxyListener {
    public:
        ProxyListener() {}
        virtual ~ProxyListener() {}

        virtual void onMessage(int msg, int param1, int param2, const MMParam *obj) = 0;
        virtual void onServerUpdate(bool die) = 0;
    };

    mm_status_t setListener(ProxyListener * listener);

    mm_status_t release();
    int test(String &method);

    virtual void onDeath(const DLifecycleListener::DeathInfo& deathInfo);

protected:

#define CHECK_MSG(msg)                                                                    \
    if (!msg) {                                                                           \
        ERROR("obtain null message");                                                     \
        return MM_ERROR_NO_SUCH_FILE;                                                     \
    }

#define MM_SEND_MSG(msg)                                                                  \
    DError err;                                                                           \
    SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg, MM_MSG_TIMEOUT, &err);  \

#define MM_HANDLE_REPLY()                                        \
    if (reply) {                                                 \
        int result = reply->readInt32();                         \
        VERBOSE("%s return: %d", __func__, result);              \
        return result;                                           \
    } else {                                                     \
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());        \
        if (err.type() == DError::ErrorType::BUS_ERROR_ACCESS_DENIED) { \
            ERROR("permission is not allowed");                         \
            return MM_ERROR_PERMISSION_DENIED;                       \
        } else {                                                        \
            return MM_ERROR_UNKNOWN;                                    \
        }                                                               \
    }

#define MM_CHECK_RESULT()                                              \
    int result = MM_ERROR_UNKNOWN;                                     \
    if (!reply) {                                                      \
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());        \
        return result;                                                 \
    } else if ((result = reply->readInt32()) != MM_ERROR_SUCCESS)  {    \
        ERROR("result %d", result);                                    \
        return result;                                                 \
    }                                                                  \

#define MM_METHOD_CALL_VOID(method)                                                       \
    SharedPtr<DMessage> msg = obtainMethodCallMessage(String(method));                    \
    CHECK_MSG(msg)                                                                        \
    DError err;                                                                           \
    SharedPtr<DMessage> reply = sendMessageWithReplyBlocking(msg, MM_MSG_TIMEOUT, &err);  \
    MM_HANDLE_REPLY();

#define MAX_INTS 64

    mm_status_t makeCallVoid(const char* method);
    mm_status_t makeCallSetInt(const char* method, int i);
    mm_status_t makeCallSetInt64(const char* method, int64_t i);
    mm_status_t makeCallGetInt(const char* method, int* p);
    mm_status_t makeCallGetInt64(const char* method, int64_t* p);
    mm_status_t makeCallSetBool(const char* method, bool b);
    mm_status_t makeCallGetBool(const char* method, bool* p);
    mm_status_t makeCallSetPointer(const char* method, void* p);
    mm_status_t makeCallSetMeta(const char* method, const MediaMetaSP &meta);
    mm_status_t makeCallGetMeta(const char* method, MediaMetaSP &meta);
    mm_status_t makeCallSetCStr(const char* method, const char* str);
    mm_status_t makeCallGetString(const char* method, String &string);
    // NOTE, kdbus cannot IPC socket fd
    mm_status_t makeCallSetFd(const char* method, int fd);

    mm_status_t makeCallSendFd(const char* method, int fd);

    int64_t makeCallGetAnbHandle(const char* method, int i);
    ProxyClientBuffer* makeCallGetAnb(const char* method, int64_t handle);
    int makeCallSetAnb(const char* method, ProxyClientBuffer *anb);

protected:
    // String mInterface;
    bool mTearDown;
    ProxyListener *mProxyListener;
    YunOSMediaCodec::SurfaceTextureListener* mMstListener;

    friend class MediaProxyCb;

    SharedPtr<MediaProxyCb> mCallback;
    SharedPtr<dbus::DSignalRule> mRule;
    String mCallbackName;

    // handle is the anb pointer of remote service
    std::map<ProxyClientBuffer*, int64_t> mBufferMap;
    std::map<int64_t, ProxyClientBuffer*> mHandleMap;

    // ANB meta data
    int mWidth;
    int mHeight;
    int mStride;
    int mFormat;
    int mUsage;
    int mVersion;
    int mNumFds;
    int mNumInts;
    int mInts[MAX_INTS];

#ifdef SINGLE_THREAD_PROXY
    const MediaMetaSP mMeta1;
    MediaMetaSP mMeta2;
#endif

private:
    void destroyBufferMap();

    static const char * MM_LOG_TAG;

    MediaProxy(const MediaProxy &);
    MediaProxy & operator=(const MediaProxy &);
};

} // end of YUNOS_MM
#endif
