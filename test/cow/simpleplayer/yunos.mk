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
MM_ROOT_PATH:= $(LOCAL_PATH)/../../../

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES:= simple_player.cc
# LOCAL_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES)

LOCAL_C_INCLUDES += \
    $(MM_INCLUDE) \
    $(MM_ROOT_PATH)/cow/include \
    $(base-includes)                    \

#     $(libuv-includes)                   \
#     $(pagewindow-includes)              \
#     $(MM_HELP_WINDOWSURFACE_INCLUDE)

ifeq ($(XMAKE_PLATFORM),ivi)
    LOCAL_C_INCLUDES += ../../../ext/mm_amhelper/include/
endif

LOCAL_SHARED_LIBRARIES += libmmbase libcowplayer libcowbase

ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

LOCAL_LDFLAGS += -lstdc++ -lpthread
## surface related dep
# REQUIRE_WPC = 1
# REQUIRE_WAYLAND = 1
# REQUIRE_EGL = 1
# REQUIRE_SURFACE = 1
# REQUIRE_PAGEWINDOW = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
LOCAL_MODULE:= simple_cowplayer

include $(BUILD_EXECUTABLE)


