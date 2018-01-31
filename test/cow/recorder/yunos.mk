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

#### cowrecorder-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := cowrecorder_test.cc

ifneq ($(XMAKE_PLATFORM),ivi)
LOCAL_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES)
LOCAL_SRC_FILES += ${MM_HELP_WINDOWSURFACE_INCLUDE}/SimpleSubWindow.cc
LOCAL_CPPFLAGS += -DMM_ENABLE_PAGEWINDOW
endif

LOCAL_C_INCLUDES += \
    $(MM_HELP_WINDOWSURFACE_INCLUDE)      \
    $(MM_ROOT_PATH)/mediarecorder/include \
    $(pagewindow-includes)       \
    $(libuv-includes)            \
    $(corefoundation-includes)    \
    $(cameraserver-includes)      \
    $(graphics-includes)	\
    $(MM_ROOT_PATH)/examples \
    $(audioserver-includes)

ifeq ($(XMAKE_PLATFORM),ivi)
    LOCAL_C_INCLUDES += ../../../ext/mm_amhelper/include/
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
LOCAL_C_INCLUDES += \
    $(HYBRIS_EGLPLATFORMCOMMON)  \
    $(libhybris-includes)        \
    $(MM_WAKELOCKER_PATH)         \
    $(power-includes)

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)
endif

LOCAL_CPPFLAGS += -std=c++11
ifeq ($(USING_EIS),1)
    LOCAL_CFLAGS += -DHAVE_EIS_AUDIO_DELAY
endif

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase libmediarecorder libpipelinearrecorder

ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

## surface related dep
ifneq ($(XMAKE_PLATFORM),ivi)
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_PAGEWINDOW = 1
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_SHARED_LIBRARIES += libmmwakelocker
endif
REQUIRE_SURFACE = 1
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := cowrecorder-test

include $(BUILD_EXECUTABLE)
