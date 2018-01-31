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

#### webrtcp-enc-test2
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../base/build/build.mk

LOCAL_SRC_FILES:= webrtc_enc_test2.cc

LOCAL_C_INCLUDES:= \
    $(MM_INCLUDE) \
    $(base-includes) \
    $(multimedia-webrtc-includes) \
    $(cameraserver-includes) \
    $(graphics-includes)       \
    $(hal-includes)            \
    $(corefoundation-includes) \
    $(WEBRTC_SDK_LOCAL_INCLUDE)

LOCAL_CFLAGS += -DWEBRTC_MEDIA_API_2
LOCAL_SHARED_LIBRARIES += libmmbase libmmwebrtc2

REQUIRE_CAMERASVR = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= webrtcp-enc-test2
LOCAL_LDFLAGS += -lstdc++
include $(BUILD_EXECUTABLE)

#### webrtcp-dec-test
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../base/build/build.mk

LOCAL_SRC_FILES:= webrtc_dec_test2.cc
LOCAL_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES)
LOCAL_SRC_FILES += ${MM_HELP_WINDOWSURFACE_INCLUDE}/SimpleSubWindow.cc
LOCAL_CPPFLAGS += -DMM_ENABLE_PAGEWINDOW

LOCAL_C_INCLUDES:= \
    $(MM_INCLUDE) \
    $(base-includes) \
    $(multimedia-webrtc-includes) \
    $(corefoundation-includes) \
    $(MM_HELP_WINDOWSURFACE_INCLUDE) \
    $(pagewindow-includes) \
    $(graphics-includes)   \
    $(WEBRTC_SDK_LOCAL_INCLUDE)

LOCAL_CFLAGS += -DWEBRTC_MEDIA_API_2
LOCAL_SHARED_LIBRARIES += libmmbase libmmwebrtc2

REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_PAGEWINDOW = 1
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_SHARED_LIBRARIES += libmediacodec_yunos libmmwakelocker
    REQUIRE_LIBHYBRIS = 1
endif
REQUIRE_SURFACE = 1

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= webrtcp-dec-test2
LOCAL_LDFLAGS += -lstdc++ -lpthread -ldl
include $(BUILD_EXECUTABLE)

