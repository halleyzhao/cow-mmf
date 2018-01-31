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

#### TI codec
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= video_decode_ducati.cc

LOCAL_C_INCLUDES+=  ${TI-LIBDCE-INCLUDES}   \
                    $(ti-ipc-includes)      \
                    $(libdrm-includes)      \
                    $(wayland-includes)

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDFLAGS += -lstdc++ -lpthread

REQUIRE_LIBDCE = 1
REQUIRE_LIBDRM = 1
REQUIRE_LIBMMRPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libVideoDecodeDucati
ifeq ($(USING_DRM_SURFACE),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### wayland sink
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= wayland-drm-protocol.c \
    video_sink_wayland.cc

LOCAL_C_INCLUDES+=  $(wayland-includes)

LOCAL_SHARED_LIBRARIES += libcowbase libVideoSinkBasic
LOCAL_LDFLAGS += -lstdc++ -lpthread

REQUIRE_WAYLAND = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libVideoSinkWayland
ifeq ($(USING_DRM_SURFACE),1)
include $(BUILD_SHARED_LIBRARY)
endif

