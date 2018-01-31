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


#### clock-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES := clock-test.cc

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE := clock-test

include $(BUILD_EXECUTABLE)


#### buffer-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := avplayer.cc

LOCAL_C_INCLUDES += $(libav-includes)

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := buffer-test

include $(BUILD_EXECUTABLE)


#### meta-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := meta-test.cc

LOCAL_C_INCLUDES += $(COW_COMMON_INCLUDE) \
                    $(base-includes) \
                    $(libuv-includes)

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase libbase

LOCAL_MODULE := meta-test

include $(BUILD_EXECUTABLE)

#### meta-ubus-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := meta-ubus-test.cc

LOCAL_C_INCLUDES += $(COW_COMMON_INCLUDE) \
                    $(base-includes) \
                    $(libuv-includes)

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase libbase

LOCAL_MODULE := meta-ubus-test

include $(BUILD_EXECUTABLE)

#### factory-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := factory-test.cc

LOCAL_C_INCLUDES += $(COW_COMMON_INCLUDE)

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE := factory-test
LOCAL_LDFLAGS += -lstdc++
include $(BUILD_EXECUTABLE)


#### monitor-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk
LOCAL_SRC_FILES := monitor-test.cc

LOCAL_LDFLAGS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase

LOCAL_MODULE := monitor-test

include $(BUILD_EXECUTABLE)
