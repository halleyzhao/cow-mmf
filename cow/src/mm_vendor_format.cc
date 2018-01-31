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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <yunhal/VideoSysCapability.h>
#include <cutils/graphics.h>
#include <cutils/ywindow.h>

#include <multimedia/mm_debug.h>
#include <multimedia/mm_vendor_format.h>

using std::string;

using yunos::VideoSysCapability;
using yunos::VideoSysCapabilitySP;

MM_LOG_DEFINE_MODULE_NAME("vendor_format")

namespace YUNOS_MM {

#define HAL_PIXEL_FORMAT_NV12 0x3231564e

#define FOURCC(ch0, ch1, ch2, ch3) \
            ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
             ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))

static uint32_t mm_formatConvert(const char*fmt)
{
    DEBUG("fmt %s", fmt);
#if defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    return FOURCC('N','V','1','2');
#endif
    // refer to framework/libs/graphics/include/cutils/graphics.h
    if (!strcmp(fmt, "NV12")) {
        return FMT_PREF(NV12);
    }
    else if (!strcmp(fmt, "YV12")) {
        return FMT_PREF(YV12);
    }
    else if (!strcmp(fmt, "RGBA_8888")) {
        return FMT_PREF(RGBA_8888); //qemu
    }
#if defined(__MM_YUNOS_CNTRHAL_BUILD__) && !defined(YUNOS_ENABLE_UNIFIED_SURFACE)
    else if (!strcmp(fmt, "YCbCr_420_SP")) {
        return FMT_PREF(YCbCr_420_SP); //sprd or mstar
    }
    else if (!strcmp(fmt, "IMPLEMENTATION_DEFINED")) {
        return FMT_PREF(IMPLEMENTATION_DEFINED); // qcom
    }
#endif
    // defalut value
    return YUN_HAL_FORMAT_NV12;
}
#if (defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__))
uint32_t mm_getVendorDefaultFormat(bool decoder)
{

    VideoSysCapabilitySP cap = VideoSysCapability::create();
    ASSERT(cap);
    string fmd = cap->getDefaultFormatForDecoder();
    string fme = cap->getDefaultFormatForEncoder();
    DEBUG("fmd %s, fme %s", fmd.c_str(), fme.c_str());

    string fmt = decoder ? fmd : fme;
    return mm_formatConvert(fmt.c_str());
}
#endif


}

