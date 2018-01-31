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

#### cowplayer-test & cowplayer_test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SURFACE_HELP_SRC_FILES := ${MM_HELP_WINDOWSURFACE_SRC_FILES}
ifneq ($(XMAKE_PLATFORM),ivi)
LOCAL_SURFACE_HELP_SRC_FILES += ${MM_HELP_WINDOWSURFACE_INCLUDE}/SimpleSubWindow.cc
LOCAL_CPPFLAGS += -DMM_ENABLE_PAGEWINDOW
endif


LOCAL_C_INCLUDES += \
    $(MM_ROOT_PATH)/mediaplayer/include \
    $(libav-includes)                   \
    $(XMAKE_BUILD_MODULE_OUT)/rootfs/usr/include \
    $(graphics-includes)                 \
    $(multimedia-surfacetexture-includes) \
    $(pagewindow-includes)       \
    $(libuv-includes)            \
    $(MM_WAKELOCKER_PATH)        \
    $(MM_HELP_WINDOWSURFACE_INCLUDE)

ifeq ($(XMAKE_PLATFORM),ivi)
    LOCAL_C_INCLUDES += ../../../ext/mm_amhelper/include/
    LOCAL_C_INCLUDES += $(libdrm-includes)
endif

LOCAL_C_INCLUDES += \
    $(corefoundation-includes)

LOCAL_LDFLAGS += -lpthread  -lstdc++

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += \
        $(HYBRIS_EGLPLATFORMCOMMON)  \
        $(MM_MEDIACODEC_INCLUDE)     \
        $(power-includes)            \

    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
    LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)
endif


## surface related dep
REQUIRE_WPC = 1
REQUIRE_WAYLAND = 1
REQUIRE_EGL = 1
REQUIRE_SURFACE = 1

LOCAL_SHARED_LIBRARIES += libmediasurfacetexture
ifeq ($(YUNOS_SYSCAP_MM),true)
LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif
ifneq ($(XMAKE_PLATFORM),ivi)
REQUIRE_PAGEWINDOW = 1
endif
LOCAL_SHARED_LIBRARIES += libmmwakelocker
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    REQUIRE_LIBHYBRIS = 1
endif

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

#### cowplayer-test ## build with libmediaplayer
LOCAL_CPPFLAGS_BAK := ${LOCAL_CPPFLAGS}
LOCAL_C_INCLUDES_BAK := ${LOCAL_C_INCLUDES}
LOCAL_LDFLAGS_BAK := ${LOCAL_LDFLAGS}
LOCAL_SHARED_LIBRARIES_BAK := ${LOCAL_SHARED_LIBRARIES} libcowbase

LOCAL_SHARED_LIBRARIES := ${LOCAL_SHARED_LIBRARIES_BAK} libmediaplayer
ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

LOCAL_SRC_FILES := ${LOCAL_SURFACE_HELP_SRC_FILES} mediaplayer_test.cpp
LOCAL_MODULE := cowplayer-test
include $(BUILD_EXECUTABLE)

#### cowplayer_test ## build with libcowplayer direct
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_CPPFLAGS := ${LOCAL_CPPFLAGS_BAK}
LOCAL_C_INCLUDES := ${LOCAL_C_INCLUDES_BAK}
LOCAL_LDFLAGS := ${LOCAL_LDFLAGS_BAK}

LOCAL_SHARED_LIBRARIES := ${LOCAL_SHARED_LIBRARIES_BAK} libcowplayer
ifeq ($(YUNOS_SYSCAP_MM),true)
LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif
ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif
LOCAL_SRC_FILES := ${LOCAL_SURFACE_HELP_SRC_FILES} cowplayer_test.cpp
LOCAL_CPPFLAGS += -DUSE_COWPLAYER
USE_TEST_PIPELINE=yes
ifeq ($(USE_TEST_PIPELINE), yes)
    LOCAL_CPPFLAGS += -DUSE_TEST_PIPELINE
    LOCAL_SRC_FILES += pipeline_player_test.cc
endif

ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

ifeq ($(USING_SURFACE_FROM_BQ),1)
    LOCAL_CFLAGS_REMOVED := -march=armv7-a
    LOCAL_CXXFLAGS += -std=c++11
endif

LOCAL_MODULE := cowplayer_test
include $(BUILD_EXECUTABLE)

#### cowaudioplayer-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := cowaudioplayer_test.cpp
LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/mediaplayer/include

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase libmediaplayer
ifeq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

LOCAL_MODULE := cowaudioplayer-test

include $(BUILD_EXECUTABLE)
