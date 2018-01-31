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


LOCAL_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../../..

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

ifneq ($(XMAKE_PLATFORM),ivi)
LOCAL_SRC_FILES:=      \
    video_encode_plugin.cc \
    video_encode_plugin_imp.cc \
    video_decode_plugin.cc \
    video_decode_plugin_imp.cc

LOCAL_MODULE:= libmmwebrtc

LOCAL_C_INCLUDES:=         \
     $(LOCAL_PATH)/include \
     $(LOCAL_PATH)/src     \
     $(MM_INCLUDE)         \
     $(multimedia-webrtc-includes) \
     $(MM_ROOT_PATH)/cow/include \
     $(cameraserver-includes) \
     $(corefoundation-includes) \
     $(WEBRTC_SDK_INCLUDE)

LOCAL_LDFLAGS += -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase libcowbase libcow-basicapp

ADD_RECORD_PREVIEW:=1
ifeq (${ADD_RECORD_PREVIEW}, 1)
    LOCAL_CFLAGS += -DADD_RECORD_PREVIEW
    LOCAL_SURFACE_HELP_SRC_FILES := ${MM_HELP_WINDOWSURFACE_SRC_FILES}
    # REQUIRE_EGL = 1
    # REQUIRE_PAGEWINDOW = 1
    # LOCAL_SHARED_LIBRARIES += libmediasurfacetexture
endif

## create surface if not specified by app
LOCAL_SRC_FILES += $(MM_EXT_PATH)/WindowSurface/WindowSurfaceTestWindow.cc
LOCAL_SRC_FILES += $(MM_EXT_PATH)/native_surface_help.cc
LOCAL_C_INCLUDES += $(MM_HELP_WINDOWSURFACE_INCLUDE)
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1

REQUIRE_CAMERASVR = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)

endif
