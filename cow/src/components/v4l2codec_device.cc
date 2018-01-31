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
#include "v4l2codec_device.h"
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

#include "multimedia/mm_debug.h"
#ifdef _USE_V4L2DEVICE_CLIENT
#include "v4l2codec_device_client.h"
#endif

MM_LOG_DEFINE_MODULE_NAME("V4L2DVC");
// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

namespace YUNOS_MM {
// FIXME, use libv4l2codec_hw.so instead
static const char* s_libName = "libv4l2codec_wayland.so";

V4l2CodecDevice::V4l2CodecDevice()
    : mV4l2Fd(0),
      mLibHandle(NULL)
{
    FUNC_TRACK();
#ifndef __USING_YUNOS_MODULE_LOAD_FW__
    mV4l2CodecOps = new V4l2CodecOps;
    INIT_V4L2CODEC_OPS_SIZE_VERSION(mV4l2CodecOps);
#endif
}

V4l2CodecDevice::~V4l2CodecDevice()
{
    FUNC_TRACK();
#ifndef __USING_YUNOS_MODULE_LOAD_FW__
    if (mV4l2CodecOps)
        delete mV4l2CodecOps;
#endif
}

bool V4l2CodecDevice::open(const char* name, uint32_t flags)
{
    FUNC_TRACK();
    mV4l2Fd = 0;

    if (!mm_check_env_str("mm.v4l2.use_surface_v2", "MM_V4L2_USE_SURFACE_V2", "0")) {
        s_libName = "libv4l2codec.so";
    }
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
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

bool V4l2CodecDevice::close()
{
    FUNC_TRACK();
    if (mV4l2Fd) {
        int ioctlRet = mV4l2CodecOps->mCloseFunc(mV4l2Fd);
        ASSERT(ioctlRet != -1);
        mV4l2Fd = 0;
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
        mDevice->destroy(mDevice);
#else
        if (mLibHandle) {
            DLCLOSE(mLibHandle);
            mLibHandle = NULL;
        }
#endif
    }

    return true;
}
/* static */ void V4l2CodecDevice::releaseDevice(V4l2CodecDevice* device)
{
    FUNC_TRACK();
    if (!device)
        return;

    device->close();
    delete device;
    device = NULL;
    return;
}
/* static */ V4l2CodecDeviceSP V4l2CodecDevice::create(const char* name, uint32_t flags)
{
    FUNC_TRACK();
    V4l2CodecDeviceSP deviceSP;
#ifdef _USE_V4L2DEVICE_CLIENT
    INFO("using v4l2 device client \n");
    V4l2CodecDevice *device = new V4l2CodecDeviceClient();
#else
    INFO("using v4l2 device direct\n");
    V4l2CodecDevice *device = new V4l2CodecDevice();
#endif

    if (!device)
        return deviceSP;

    if (!device->open(name, flags))
        return deviceSP;

    deviceSP.reset(device, releaseDevice);

    return deviceSP;
}

// FIXME, use #define to transmit these func calls
int32_t V4l2CodecDevice::ioctl(uint64_t request, void* arg)
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mIoctlFunc(mV4l2Fd, request, arg);
    if (ret != 0)
        VERBOSE("ret=%d\n", ret);

    return ret;
}
int32_t V4l2CodecDevice::poll(bool poll_device, bool* event_pending)
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mPollFunc(mV4l2Fd, poll_device, event_pending);
    if (ret != 0)
        ERROR("ret=%d\n", ret);

    return ret;
}
int32_t V4l2CodecDevice::setDevicePollInterrupt()
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mSetDevicePollInterruptFunc(mV4l2Fd);
    if (ret != 0)
        ERROR("ret=%d\n", ret);

    return ret;
}
int32_t V4l2CodecDevice::clearDevicePollInterrupt()
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mClearDevicePollInterruptFunc(mV4l2Fd);
    if (ret != 0)
        WARNING("ret=%d\n", ret);

    return ret;
}
void* V4l2CodecDevice::mmap(void* addr, size_t length, uint32_t prot, uint32_t flags, uint32_t offset)
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return NULL;

    void *ret = mV4l2CodecOps->mMmapFunc(addr, length, prot, flags, mV4l2Fd, offset);
    DEBUG("ret=%p\n", ret);
    return ret;
}
int32_t V4l2CodecDevice::munmap(void* addr, size_t length)
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    int32_t ret = mV4l2CodecOps->mMunmapFunc(addr, length);
    DEBUG("ret=%d\n", ret);
    return ret;
}

int32_t V4l2CodecDevice::setParameter(const char* key, const char* value)
{
    FUNC_TRACK();
    if (!mV4l2Fd)
        return -1;

    DEBUG("key: %s, value: %s", key, value);
    int32_t ret = mV4l2CodecOps->mSetParameterFunc(mV4l2Fd, key, value);
    if (ret != 0)
        ERROR("ret=%d\n", ret);
    return ret;
}

} // namespace YUNOS_MM

