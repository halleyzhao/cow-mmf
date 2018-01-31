#############################################################################
# Copyright (C) 2015-2017 Alibaba Group Holding Limited. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#############################################################################


LOCAL_PATH:=$(call my-dir)
MM_ROOT_PATH:= $(YUNOS_ROOT)/framework/nativeservice/multimediad
ifneq ($(XMAKE_PLATFORM),ivi)
#### v4l2dec
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := v4l2decode.cc                               \
                   ./util.cc                                   \
                   ./input/decodeinput.cc                      \
                   ./input/decodeinputavformat.cc              \
                  mm_vendor_compat.cc


LOCAL_C_INCLUDES += \
    $(MM_INCLUDE)   \
    $(graphics-includes)    \
    $(wayland-includes)     \
    ${gui-includes}         \
    $(libav-includes)       \
    $(wpc-includes)         \
    $(weston-includes)      \
    $(yunhal-includes)      \
    $(corefoundation-includes) \

ifeq ($(XMAKE_BOARD),intel)
LOCAL_C_INCLUDES += $(kernel-includes)
endif

LOCAL_CPPFLAGS += -D__ENABLE_AVFORMAT__

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif

LOCAL_SHARED_LIBRARIES += libwltoolkit libcowbase
ifeq ($(YUNOS_SYSCAP_MM),true)
 LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),false)
LOCAL_C_INCLUDES += \
    $(YALLOC_INCLUDE)
endif


## surface related dep
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_SURFACE = 1
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := v4l2dec-test

## FIXME, temp disable v4l2dec for surface_v2 support
include $(BUILD_EXECUTABLE)



#### v4l2enc
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := v4l2encode.cc                               \
                   ./util.cc                                  \
                   ./input/encodeinput.cc                      \
                   ./input/encodeInputBQYunOS.cc               \
                   mm_vendor_compat.cc

LOCAL_C_INCLUDES += \
    $(MM_INCLUDE)   \
    $(graphics-includes)    \
    $(wayland-includes)     \
    ${gui-includes}         \
    $(libav-includes)       \
    $(wpc-includes)         \
    $(weston-includes)      \
    $(yunhal-includes)      \
    $(corefoundation-includes) \
    $(LOCAL_PATH)/../../

ifeq ($(XMAKE_BOARD),intel)
LOCAL_C_INCLUDES += $(kernel-includes)
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),false)
LOCAL_C_INCLUDES += \
    $(YALLOC_INCLUDE)
endif


LOCAL_LDFLAGS += -lpthread -ldl -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase libcowbase
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif
LOCAL_SHARED_LIBRARIES += libwltoolkit

ifeq ($(YUNOS_SYSCAP_MM),true)
 LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif


## surface related dep
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_SURFACE = 1
REQUIRE_LIBAV = 1
REQUIRE_SURFACE = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := v4l2enc-test

## FIXME, temp disable v4l2dec for surface_v2 support
include $(BUILD_EXECUTABLE)

endif
