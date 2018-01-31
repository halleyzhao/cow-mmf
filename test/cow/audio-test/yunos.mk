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

#### cow-audio-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := audio_test.cc

LOCAL_C_INCLUDES += \
    $(COW_COMMON_INCLUDE)       \
    $(MM_MEDIACODEC_INCLUDE)    \
    $(WINDOWSURFACE_INCLUDE)    \
    $(HYBRIS_EGLPLATFORMCOMMON) \
    $(libav-includes)           \

LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs$(COW_PLUGIN_PATH)
LOCAL_LDFLAGS += -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper libFFmpegComponent
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_SHARED_LIBRARIES += libmediacodec_yunos libMediaCodecComponent
endif
LOCAL_CPPFLAGS += -std=c++11
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

ifeq ($(USING_AUDIO_CRAS),1)
LOCAL_C_INCLUDES += $(audioserver-includes)
LOCAL_SHARED_LIBRARIES += libAudioSinkCras
else
LOCAL_C_INCLUDES +=  $(pulseaudio-includes)
LOCAL_SHARED_LIBRARIES += libAudioSinkPulse
endif
LOCAL_MODULE := cow-audio-test

include $(BUILD_EXECUTABLE)

#### cow-record-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := record_test.cc

ifeq ($(USING_AUDIO_CRAS),1)
LOCAL_C_INCLUDES += $(audioserver-includes)
LOCAL_SHARED_LIBRARIES += libAudioSrcCras
else
LOCAL_C_INCLUDES +=  $(pulseaudio-includes)
LOCAL_SHARED_LIBRARIES += libAudioSrcPulse
REQUIRE_PULSEAUDIO = 1
endif

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDFLAGS += -lstdc++
LOCAL_C_INCLUDES += $(audioserver-includes)
ifneq ($(XMAKE_PLATFORM),ivi)
LOCAL_SHARED_LIBRARIES += libAudioSinkCras
endif

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := cow-record-test

include $(BUILD_EXECUTABLE)

