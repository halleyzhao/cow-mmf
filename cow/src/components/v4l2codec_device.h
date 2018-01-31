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
#ifndef v4l2codec_device_h
#define v4l2codec_device_h
#include "multimedia/mm_cpp_utils.h"
#include "yunhal/v4l2codec_device_ops.h"

namespace YUNOS_MM {

class V4l2CodecDevice;
typedef MMSharedPtr <V4l2CodecDevice> V4l2CodecDeviceSP;

class V4l2CodecDevice{
  public:
    virtual ~V4l2CodecDevice();
    static V4l2CodecDeviceSP create(const char* name, uint32_t flags);
    static void releaseDevice(V4l2CodecDevice* device);

    virtual int32_t ioctl(uint64_t request, void* arg);
    virtual int32_t poll(bool poll_device, bool* event_pending);
    // FIXME, why we haven't used these two apis yet?
    virtual int32_t setDevicePollInterrupt();
    virtual int32_t clearDevicePollInterrupt();
    virtual void* mmap(void* addr, size_t length, uint32_t prot, uint32_t flags, uint32_t offset);
    // FIXME, not use it yet
    virtual int32_t munmap(void* addr, size_t length);
    virtual int32_t setParameter(const char* key, const char* value);

  protected:
    int32_t mV4l2Fd;
    V4l2CodecDevice();
    virtual bool open(const char* name, uint32_t flags);
    virtual bool close();

  private:
    struct V4l2CodecOps *mV4l2CodecOps = NULL;
    void *mLibHandle;
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
    VendorModule* mModule = NULL;
    VendorDevice *mDevice = NULL;
#endif
};

} // namespace YUNOS_MM

#define CHECK_V4L2_CMD_RESULT(ret, CMD) do {    \
    if (ret == 0 )                              \
        INFO("%s success", #CMD);               \
    else if (ret == -1)                         \
        ERROR("%s failed", #CMD);               \
    else                                        \
        WARNING("%s ret: %d", #CMD, ret);       \
    } while (0)

#endif // v4l2codec_device_h

