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

CURRENT_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(CURRENT_PATH)/../

REQUIRE_GTEST = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_LDFLAGS += -lglib-2.0
LOCAL_SHARED_LIBRARIES += libglib-2.0

LOCAL_C_INCLUDES += \
    $(gtest-includes) \
    $(glib-includes)
LOCAL_CPPFLAGS += -fpermissive
