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

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)

LOCAL_PATH:=$(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../../

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_SRC_FILES:= MediaServiceTest.cc

LOCAL_MODULE:= media-service-test

LOCAL_C_INCLUDES +=              \
    $(dbus-includes)             \
    $(libuv-includes)            \
    $(base-includes)             \
    $(MM_ROOT_PATH)/base/include \
    $(MM_ROOT_PATH)/mediaserver/include

LOCAL_SHARED_LIBRARIES += libbase libmmbase libmediaservice_client_common

LOCAL_LDFLAGS += -lpthread -lstdc++

REQUIRE_LIBUV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_EXECUTABLE)

endif
