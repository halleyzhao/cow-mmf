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

#ifndef __mm_surface_compat_h
#define __mm_surface_compat_h
/* it is necessary header file to operate Surface on BufferPipe interface.
 *  in order to create viewable Window/Surface, native_surface_help.h is required.
 */
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
        #include <unistd.h>
        //#include <WindowSurface.h>
        #include <Surface.h>
        #include <cutils/graphics.h>
        #include <BufferPipe.h>
        #ifndef __MM_YUNOS_YUNHAL_BUILD__
        #include <gb_wrapper.h>
        #endif
        typedef yunos::libgui::BaseNativeSurface WindowSurface;
        // use MMNativeBuffer in MM internal code, for external interface we use YNativeSurfaceBuffer
        typedef YNativeSurfaceBuffer MMNativeBuffer;
        typedef yunos::libgui::BufferPipeProducer MMBQProducer;
        typedef std::shared_ptr<yunos::libgui::BufferPipeProducer> MMBQProducerPtr;
        typedef std::shared_ptr<yunos::libgui::IProducerObserver> MMProducerListenerPtr;

        #define WINDOW_API(command)    native_##command
        #define GET_ANATIVEWINDOW(w)    (static_cast<WindowSurface*>(w))
    #define CAST_ANATIVEWINDOW(w)   (static_cast<WindowSurface*>(w))

    #if defined(YUNOS_ENABLE_UNIFIED_SURFACE) && defined(__MM_YUNOS_CNTRHAL_BUILD__)
        #include <AbstractNativeSurface.h>
    #endif
    #if defined(YUNOS_ENABLE_UNIFIED_SURFACE) || defined(__MM_YUNOS_YUNHAL_BUILD__)
        typedef gb_target_t MMBufferHandleT;
        typedef native_target_t MMNativeHandleT;
    #elif defined(__MM_YUNOS_CNTRHAL_BUILD__)
        typedef buffer_handle_t MMBufferHandleT;
        typedef native_handle_t MMNativeHandleT;
    #endif
#elif defined __MM_YUNOS_LINUX_BSP_BUILD__
    //typedef void MMNativeBuffer;
    #include <WindowSurface.h>
    #include <stdint.h>
    #include <multimedia/media_native_buffer.h>

    #ifdef __MM_BUILD_DRM_SURFACE__
        typedef MMWlDrmBuffer MMNativeBuffer;
        typedef MMWlDrmBuffer* MMBufferHandleT;
    #endif
    #ifdef __MM_BUILD_VPU_SURFACE__
        typedef MMVpuBuffer MMNativeBuffer;
        typedef MMVpuBuffer* MMBufferHandleT;
    #endif

    typedef Surface WindowSurface;
    typedef void YNativeSurfaceBuffer;
    #define GET_ANATIVEWINDOW(w)    (static_cast<WindowSurface*>(w))
    typedef struct Rect_t {
        int left;
        int top;
        int right;
        int bottom;
    } Rect;
    #define DRM_NAME_USAGE_TI_HW_ENCODE 0x01
    #define DRM_NAME_USAGE_HW_DECODE_PAD_H264 0x02
    #define DRM_NAME_USAGE_HW_DECODE_PAD_MPEG4 0x04
    #define DRM_NAME_USAGE_HW_DECODE_PAD_MPEG2 0x08
    #define DRM_NAME_USAGE_HW_DECODE_PAD_VC1 0x10
#else
    class Surface;
    #define WindowSurface void
#endif

#include "multimedia/mm_vendor_format.h"

#define CAST_BASE_BUFFER_PTR(x) static_cast<yunos::libgui::BaseNativeSurfaceBuffer*>(x)

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__))
inline int mm_cancelBuffer(WindowSurface* surface, MMNativeBuffer *buffer, int fence)
{
    int ret = -1;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    ret = surface->dropBuffer(CAST_BASE_BUFFER_PTR(buffer), fence);
#else
    // error
#endif
    return ret;
}
inline int mm_queueBuffer(WindowSurface* surface, MMNativeBuffer *buffer, int fence)
{
    int ret = -1;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    ret = surface->submitBuffer(CAST_BASE_BUFFER_PTR(buffer), fence);
#else
    // error
#endif
    return ret;
}
inline int mm_dequeueBuffer(WindowSurface* surface, MMNativeBuffer **buffer, int *fence)
{
    int ret = -1;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    yunos::libgui::BaseNativeSurfaceBuffer* temp;
    ret = surface->obtainBuffer(&temp, fence);
    *buffer = static_cast<MMNativeBuffer*>(temp);
#else
    // error
#endif
    return ret;
}

inline int mm_dequeueBufferWait(WindowSurface* surface, MMNativeBuffer **buffer)
{
    int ret = -1;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    ret = native_obtain_buffer_and_wait(surface, buffer);
#else
    // error
#endif
    return ret;
}

inline int mm_setSurfaceUsage(WindowSurface* surface, uint32_t usage)
{
    int ret = -1;
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    ret = native_set_flag(surface, usage);
#else
    // error
#endif
    return ret;
}
#endif

#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
inline unsigned long mm_getBufferHandle(MMNativeBuffer* buffer)
{
#if defined(__MM_YUNOS_CNTRHAL_BUILD__)
#ifndef YUNOS_ENABLE_UNIFIED_SURFACE
    return reinterpret_cast<unsigned long>(buffer->handle);
#else
    AbstractSurfaceBuffer* surfaceBuf = static_cast<AbstractSurfaceBuffer*>(buffer);
    ANativeWindowBuffer* nativeWindowBuf = surfaceBuf->getANativeBuffer();
    return reinterpret_cast<unsigned long>(nativeWindowBuf->handle);
#endif
#elif defined(__MM_YUNOS_YUNHAL_BUILD__)
    return reinterpret_cast<unsigned long>(buffer->target);
#elif defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    return reinterpret_cast<unsigned long>(buffer);
#else
    return 0xffffffff;
#endif
}
#endif

#endif // __mm_surface_compat_h

