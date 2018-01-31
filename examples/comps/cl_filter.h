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

#ifndef __cl_filter_h__
#define __cl_filter_h__
#include <NativeWindowBuffer.h>
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <os-compatibility.h>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_surface_compat.h"
#include <cutils/yalloc.h>
// #include <cutils/native_target.h>

// copy from beignet cl_intel.h
typedef struct _cl_libva_image {
    unsigned int            bo_name;
    uint32_t                offset;
    uint32_t                width;
    uint32_t                height;
    cl_image_format         fmt;
    uint32_t                row_pitch;
    uint32_t                reserved[8];
} cl_libva_image;

namespace YUNOS_MM {
class OCLProcessor;
typedef MMSharedPtr <OCLProcessor> OCLProcessorSP;

class OCLProcessor {
    #define MAX_BUFFER_NUM 32
  public:
    OCLProcessor();
    // destruction doesn't run in waylang/egl thread, there will be crash if we delete WindowSurface here
    // now we destroy WindowSurface in egl thread, the unique one as player's listener thread (for the last loop upon MSG_STOPPED)
    ~OCLProcessor() { /* deinit(true); */ }

    bool init(/*void* nativeDisplay = NULL, void* nativeWindow = NULL*/);
    bool deinit();
    bool processBuffer(MMBufferHandleT src, MMBufferHandleT dst, uint32_t width, uint32_t height);

  private:
    int32_t mWidth;
    int32_t mHeight;
    cl_platform_id mPlatform;
    cl_device_id mDevice;
    cl_context mContext;
    cl_program mProgram;
    cl_kernel mKernel;
    cl_command_queue mQueue;
    typedef cl_mem (OCLCREATEIMAGEFROMLIBVAINTEL)(cl_context, const cl_libva_image *, cl_int *);
    OCLCREATEIMAGEFROMLIBVAINTEL *mOclCreateImageFromDrmNameIntel;
    yalloc_device_t* m_pYalloc;
    cl_mem mY_CLImage;
    cl_mem mUV_CLImage;
    cl_mem mY_CLImage2;
    cl_mem mUV_CLImage2;

    bool prepareCLImage(MMBufferHandleT src, MMBufferHandleT dst, uint32_t width, uint32_t height);
}; // endof OCLProcessor
}// end of namespace YUNOS_MM
#endif // __cl_filter_h__

