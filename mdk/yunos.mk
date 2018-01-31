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

BUILD_MDK = 0
ifeq ($(BUILD_MDK),1)

LOCAL_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../
EXAMPLE_PATH:= example

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES += \
     $(LOCAL_PATH)/$(EXAMPLE_PATH)/include     \
     $(MM_INCLUDE)                             \
     $(MM_ROOT_PATH)/cow/include               \
     $(base-includes)                          \
     $(libav-includes)

LOCAL_SRC_FILES:= $(EXAMPLE_PATH)/src/h264Nal_source.cc

LOCAL_MODULE:= libh264NalSource

LOCAL_LDFLAGS += -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase libcowbase
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES += \
     $(LOCAL_PATH)/$(EXAMPLE_PATH)/include     \
     $(MM_INCLUDE)                             \
     $(MM_ROOT_PATH)/cow/include               \
     $(base-includes)                          \
     $(libav-includes)

LOCAL_SRC_FILES:= $(EXAMPLE_PATH)/src/example_pipeline.cc

LOCAL_MODULE:= libexamplePipeline
LOCAL_CPPFLAGS += -D__DISABLE_AUDIO_STREAM__
LOCAL_LDFLAGS += -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase libcowbase
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_SRC_FILES:= $(EXAMPLE_PATH)/test/h264Nalsource_test.cc
LOCAL_C_INCLUDES += \
      $(LOCAL_PATH)/$(EXAMPLE_PATH)/include  \
      $(MM_INCLUDE)                          \
      $(base-includes)                       \
      $(MM_ROOT_PATH)/cow/include            \
      $(MM_ROOT_PATH)/cow/src/components     \
      $(libav-includes)

LOCAL_SHARED_LIBRARIES += libmmbase libcowbase  libh264NalSource
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= h264Nalsource-test
LOCAL_LDFLAGS += -lstdc++
include $(BUILD_EXECUTABLE)


####multi_prebuilt
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
LOCAL_MODULE := exmaple_multi_prebuilt
LOCAL_MODULE_TAGS := optional
INST_PATH := etc
DEMO_COPY_FILES := $(EXAMPLE_PATH)/res/example_plugins.xml:$(INST_PATH)/example_plugins.xml \
                   res/h264.h264:$(INST_TEST_RES_PATH)/video/h264.h264
LOCAL_SRC_FILES:= $(DEMO_COPY_FILES)
include $(BUILD_MULTI_PREBUILT)


include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_SRC_FILES:= $(EXAMPLE_PATH)/test/examplepipeline_test.cc

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_SRC_FILES += \
        ../ext/WindowSurface/WindowSurfaceTestWindow.cc \
        ../ext/native_surface_help.cc
else
endif

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/$(EXAMPLE_PATH)/include  \
    $(MM_INCLUDE)                          \
    $(MM_ROOT_PATH)/mediaplayer/include    \
    $(MM_ROOT_PATH)/cow/include            \
    $(base-includes)                       \
    $(MM_WAKELOCKER_PATH)                  \
    $(power-includes)                      \
    $(MM_HELP_WINDOWSURFACE_INCLUDE)


ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += \
        $(WINDOWSURFACE_INCLUDE)     \
        $(HYBRIS_EGLPLATFORMCOMMON)  \
        $(MM_MEDIACODEC_INCLUDE)     \
        $(libhybris-includes)        \
        $(pagewindow-includes)       \
        $(libuv-includes)            \
        $(MM_MEDIACODEC_INCLUDE)

    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
    LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)

else
    LOCAL_C_INCLUDES += $(WINDOWSURFACE_INCLUDE)
endif

LOCAL_SHARED_LIBRARIES += libmmbase  libcowbase libmediaplayer libexamplePipeline

LOCAL_LDFLAGS += -lstdc++ -lpthread
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
REQUIRE_LIBHYBRIS = 1
REQUIRE_PAGEWINDOW = 1
REQUIRE_EGL = 1
REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
endif
LOCAL_MODULE:= examplepipeline-test

include $(BUILD_EXECUTABLE)
endif
