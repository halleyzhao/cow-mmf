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

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)

LOCAL_PATH:=$(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../../../

#### decoder-mc-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
LOCAL_SRC_FILES := decoder-test.cc \
                   $(MM_EXT_PATH)/WindowSurface/SimpleSubWindow.cc

LOCAL_C_INCLUDES += \
    $(MM_HELP_WINDOWSURFACE_INCLUDE)  \
    $(HYBRIS_EGLPLATFORMCOMMON) \
    $(MM_MEDIACODEC_INCLUDE)    \
    $(audioserver-includes)     \
    $(libav-includes)            \
    $(libhybris-includes)       \
    $(pagewindow-includes)      \
    $(libuv-includes)

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)

LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs$(COW_PLUGIN_PATH)

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libmmbase libcowbase libAVDemuxer libMediaCodecComponent libmediacodec_yunos

REQUIRE_LIBHYBRIS = 1
REQUIRE_PAGEWINDOW = 1
REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := decoder-mc-test

include $(BUILD_EXECUTABLE)

#### audio-encoder-mc-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
LOCAL_SRC_FILES := audio-encoder-test.cc


LOCAL_C_INCLUDES += \
    $(WINDOWSURFACE_INCLUDE) \
    $(HYBRIS_EGLPLATFORMCOMMON) \
    $(MM_MEDIACODEC_INCLUDE) \
    $(libav-includes)


LOCAL_CPPFLAGS +=  -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)

LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs$(COW_PLUGIN_PATH)

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase libMediaCodecComponent libmediacodec_yunos libAudioSrcFile

LOCAL_MODULE := audio-encoder-mc-test

include $(BUILD_EXECUTABLE)

endif
