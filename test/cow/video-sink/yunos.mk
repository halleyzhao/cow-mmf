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

#### video-sink-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := video_sink_test.cc

LOCAL_C_INCLUDES += \
    $(WINDOWSURFACE_INCLUDE) \
    $(MM_MEDIACODEC_INCLUDE) \
    $(HYBRIS_EGLPLATFORMCOMMON)

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_SHARED_LIBRARIES += libmediacodec_yunos
endif

LOCAL_MODULE := video-sink-test

include $(BUILD_EXECUTABLE)
