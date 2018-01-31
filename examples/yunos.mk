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
MM_ROOT_PATH:= $(LOCAL_PATH)/../

#### libpipelineplayer
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk

LOCAL_SRC_FILES:= \
    pipeline_player.cc

LOCAL_C_INCLUDES+= \
    $(properties-includes) \
    $(audioserver-includes)


LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libpipelineplayer
LOCAL_LDLIBS+= -lstdc++

REQUIRE_PROPERTIES = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)

#### libpipelinevideorecorder
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk

LOCAL_SRC_FILES:= \
    pipeline_videorecorder.cc

LOCAL_C_INCLUDES+= \
    $(WINDOWSURFACE_INCLUDE)   \
    $(cameraserver-includes)   \
    $(corefoundation-includes) \
    $(properties-includes) \
    $(audioserver-includes)


LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libpipelinevideorecorder
LOCAL_LDLIBS+= -lstdc++

REQUIRE_PROPERTIES = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)

ifeq ($(XMAKE_PLATFORM),tablet)
include ${LOCAL_PATH}/comps/yunos.mk
endif
