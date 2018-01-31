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
MM_ROOT_PATH:= $(LOCAL_PATH)/../../

#### mediaplayer with customized video sink test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES:= custom_player_test.cc
LOCAL_SRC_FILES += custom_pipeline.cc
ifneq ($(XMAKE_PLATFORM),ivi)
LOCAL_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES)
endif

LOCAL_C_INCLUDES += \
    $(MM_INCLUDE) \
    $(MM_COW_INCLUDE) \
    $(MM_ROOT_PATH)/mediaplayer/include \
    $(base-includes)                    \
    $(MM_WAKELOCKER_PATH)               \
    $(libuv-includes)                   \
    $(pagewindow-includes)              \
    $(MM_HELP_WINDOWSURFACE_INCLUDE)

LOCAL_SHARED_LIBRARIES += libmmbase libmediaplayer libmmwakelocker libcowbase libcowplayer

LOCAL_LDFLAGS += -lstdc++ -lpthread
## surface related dep
ifneq ($(XMAKE_PLATFORM),ivi)
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_SURFACE = 1
REQUIRE_PAGEWINDOW = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

USE_CUSTOM_VIDEO_SINK:= 1
ifeq ($(USE_CUSTOM_VIDEO_SINK), 1)
    LOCAL_SRC_FILES += custom_video_sink.cc
    #LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs$(COW_PLUGIN_PATH)
    LOCAL_SHARED_LIBRARIES += libVideoSinkBasic
endif
endif

LOCAL_MODULE:= custom-player-test

include $(BUILD_EXECUTABLE)

