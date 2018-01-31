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

#ifndef v4l2codec_device_ops_h
#define v4l2codec_device_ops_h
#include <linux/videodev2.h>
#include <stdint.h>
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
#include <yunhal/Hal.h>
#endif

/**
   * @brief extend v4l2 event definition
   * extend v4l2 event for video resolution change
   */
#ifndef V4L2_EVENT_RESOLUTION_CHANGE
    #define V4L2_EVENT_RESOLUTION_CHANGE 5
#endif

/**
  * @brief extend v4l2 buffer memory type
  * V4L2_MEMORY_DMABUF: kernel buffer handle of dma_buf
  * V4L2_MEMORY_PLUGIN_BUFFER_HANDLE: plugin gralloc buffer handle
  * V4L2_MEMORY_YUNOS_NATIVE_TARGET: yunos gfx buffer handle
  */
#ifndef V4L2_MEMORY_DMABUF
    #define V4L2_MEMORY_DMABUF      4
#endif
#ifndef V4L2_MEMORY_PLUGIN_BUFFER_HANDLE
    #define V4L2_MEMORY_PLUGIN_BUFFER_HANDLE   (V4L2_MEMORY_DMABUF+1)
#endif
#ifndef V4L2_MEMORY_YUNOS_NATIVE_TARGET
    #define V4L2_MEMORY_YUNOS_NATIVE_TARGET   (V4L2_MEMORY_DMABUF+2)
#endif

/**
  * @brief extend v4l2 buffer flag for video stream (if not present in current videodev2.h)
  * V4L2_BUF_FLAG_LAST: not clear; previously used as eos flag
  * V4L2_BUF_FLAG_EOS: the final buffer in the stream
  */
// FIXME, modify VEV4L2 to use EOS instead of LAST
#ifndef V4L2_BUF_FLAG_LAST
#define V4L2_BUF_FLAG_LAST 0x00100000
#endif
#ifndef V4L2_BUF_FLAG_EOS
#define V4L2_BUF_FLAG_EOS   0x2000
#endif
#ifndef V4L2_BUF_FLAG_NON_REF
#define V4L2_BUF_FLAG_NON_REF 0x00200000
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief function type which is implemented by v4l2codec device
  */

/**
  * @brief open v4l2codec device
  * @param name "decoder" or "encoder" for codec type
  * @param flags special flags to v4l2codec device, not used for now
  *
  * @return a unique handle for continue operation
  */
typedef int32_t (*V4l2OpenFunc)(const char* name, int32_t flags);

/**
  * @brief close v4l2codec device
  * @param fd the v4l2codec device handle
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2CloseFunc)(int32_t fd);

/**
  * @brief device operations
  * it is the complex part to operate v4l2codec device; follows the
  * @param fd the v4l2codec device handle, follows the semantic defined by Linux kernel videodev2.h
  * @param cmd ref to "I O C T L   C O D E S   F O R   V I D E O   D E V I C E S" in videodev2.h
  * @param arg corresponding parameter for cmd as defined in videodev2.h
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2IoctlFunc)(int32_t fd, int32_t cmd, void* arg);

/**
  * @brief poll v4l2codec device until there are some new event to handle
  * @param fd the v4l2codec device handle
  * @param poll_device poll v4l2codec device besides codec event, usually set to true
  * @param event_pending whether there there some event to handle
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2PollFunc)(int32_t fd, bool poll_device, bool* event_pending);

/**
  * @brief inform v4l2codec device to escape @see V4l2PollFunc
  * @param fd the v4l2codec device handle
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2SetDevicePollInterruptFunc)(int32_t fd);

/**
  * @brief re-enable v4l2codec device is poll-able
  * @param fd the v4l2codec device handle
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2ClearDevicePollInterruptFunc)(int32_t fd);

/**
  * @brief map v4l2codec device memory
  * @param addr the start memory address of v4l2codec device, set to NULL
  * @param length memory length
  * @param mmap mode, usually set to PROT_READ | PROT_WRITE
  * @flags, mmap flags, usually set to MAP_SHARED
  * @param fd the v4l2codec device handle
  * @param offset the offset return by VIDIOC_QUERYBUF in @see V4l2IoctlFunc
  *
  * @return virtual memory address of the v4l2codec buffer
  */
typedef void*   (*V4l2MmapFunc)(void* addr, size_t length, int32_t prot,
                  int32_t flags, int32_t fd, unsigned int offset);

/**
  * @brief unmap v4l2codec device buffer
  * @param addr virtual address of maped buffer from @see V4l2MmapFunc
  * @param length memory length
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2MunmapFunc)(void* addr, size_t length);

/**
  * @brief pass additional information to v4l2codec device
  * @param fd the v4l2codec device handle
  * @param key parameter name
  * @param value parameter value
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2SetParameterFunc)(int32_t fd, const char* key, const char* value);

/**
  * @brief associate v4l2_buffer to given egl_image
  * not used by YunOS for now, used to be used by chromeos
  * @param fd the v4l2codec device handle
  * @param egl_display EGLDisplay
  * @param egl_context EGLContext
  * @param buffer_index index of decoder output buffer
  * @egl_image EGLImage
  *
  * @return 0 for success, otherwise failure
  */
typedef int32_t (*V4l2UseEglImageFunc)(int32_t fd, void* egl_display, void* egl_context,
                  uint32_t buffer_index, void* egl_image);

/**
  * @brief version domain to make sure that device and app are at the same page
  */
#define V4L2CODEC_VENDOR_STRING_SIZE  16
#define V4L2CODEC_VERSION_MAJOR        0
#define V4L2CODEC_VERSION_MINOR        1
#define V4L2CODEC_VERSION_REVISION     0
#define V4L2CODEC_VERSION_STEP         0

typedef union V4l2CodecVersion{
    struct {
        uint8_t mMajor;
        uint8_t mMinor;
        uint8_t mRevision;
        uint8_t mStep;
    } mDetail;
    uint32_t mVersion;
} V4l2CodecVersion;

typedef struct V4l2CodecOps {
#ifdef __USING_YUNOS_MODULE_LOAD_FW__
    VendorDevice common;
#endif
    uint32_t mSize;
#ifndef __USING_YUNOS_MODULE_LOAD_FW__
    V4l2CodecVersion mVersion;
    char mVendorString[V4L2CODEC_VENDOR_STRING_SIZE];   // for example yami-0.4.0
#endif
    V4l2OpenFunc mOpenFunc;
    V4l2CloseFunc mCloseFunc;
    V4l2IoctlFunc mIoctlFunc;
    V4l2PollFunc mPollFunc;
    V4l2SetDevicePollInterruptFunc mSetDevicePollInterruptFunc;
    V4l2ClearDevicePollInterruptFunc mClearDevicePollInterruptFunc;
    V4l2MmapFunc mMmapFunc;
    V4l2MunmapFunc mMunmapFunc;
    V4l2SetParameterFunc mSetParameterFunc;
    V4l2UseEglImageFunc mUseEglImageFunc;
} V4l2CodecOps;

#define INIT_V4L2CODEC_OPS_VERSION(version) do {                \
    (version).mDetail.mMajor = V4L2CODEC_VERSION_MAJOR;         \
    (version).mDetail.mMinor = V4L2CODEC_VERSION_MINOR;         \
    (version).mDetail.mRevision = V4L2CODEC_VERSION_REVISION;   \
    (version).mDetail.mStep = V4L2CODEC_VERSION_STEP;           \
} while(0)

// do not cast ops to V4l2CodecOps*; assume caller use it correctly (it will not pass compile if not)
#define INIT_V4L2CODEC_OPS_SIZE_VERSION(ops) do {               \
    if((ops) == NULL) break;                                    \
    memset(ops, 0, sizeof(V4l2CodecOps));                       \
    (ops)->mSize = sizeof(V4l2CodecOps);                        \
    INIT_V4L2CODEC_OPS_VERSION((ops)->mVersion);                \
} while(0)

#define IS_V4L2CODEC_OPS_VERSION_MATCH(version, isMatch) do {   \
    V4l2CodecVersion v;                                         \
    INIT_V4L2CODEC_OPS_VERSION(v);                              \
    if (version.mVersion == v.mVersion) {                       \
        isMatch = 1;                                            \
    } else {                                                    \
        isMatch = 0;                                            \
    }                                                           \
} while(0)

/**
  * @brief initialize v4l2codec device
  * the function pointer define in @see V4l2CodecOps will be hooked by v4l2codec device
  * @param OpFuncs @see V4l2CodecOps
  *
  * @return true for success
  */
typedef bool (*V4l2codecOperationInitFunc)(struct V4l2CodecOps *OpFuncs);
// fill all the func ptrs implemented by platform
bool v4l2codecOperationInit(V4l2CodecOps *OpFuncs);

/**
  * @brief data flow refers to the unit test
  */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // v4l2codec_device_ops_h

