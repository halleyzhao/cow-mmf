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


LOCAL_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES:= \
     $(LOCAL_PATH)/include \
     $(MM_INCLUDE)         \
     $(base-includes)      \
     $(properties-includes)

LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/cow/include         \
                    $(MM_ROOT_PATH)/mediaserver/include \
                    $(WINDOWSURFACE_INCLUDE)            \
                    $(cameraserver-includes)            \
                    $(corefoundation-includes)          \
                    $(dbus-includes)                    \
                    $(libuv-includes)

LOCAL_SRC_FILES:= src/mediarecorder.cc         \
                  src/cowrecorder_wrapper.cc

ifeq ($(USING_RECORDER_SERVICE),1)
LOCAL_SRC_FILES +=  \
                  src/proxyrecorder_wrapper.cc
endif

LOCAL_CFLAGS += -fno-rtti

LOCAL_MODULE:= libmediarecorder

LOCAL_SHARED_LIBRARIES += libmmbase libcowrecorder
#LOCAL_SHARED_LIBRARIES += libmmbase libcowrecorder libmediaclient_yunos libmediaservice_client_common
ifeq ($(USING_RECORDER_SERVICE),1)
LOCAL_SHARED_LIBRARIES += libmediaclient_yunos libmediaservice_client_common
endif

LOCAL_LDFLAGS += -lstdc++ -ldl

REQUIRE_PROPERTIES = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := mediarecorder_multi_prebuilt
LOCAL_MODULE_TAGS := optional

MEDIARECORDER_INCLUDE_HEADERS := \
    $(LOCAL_PATH)/include/multimedia/mediarecorder.h:$(INST_INCLUDE_PATH)/mediarecorder.h

LOCAL_SRC_FILES:= $(MEDIARECORDER_INCLUDE_HEADERS)

include $(BUILD_MULTI_PREBUILT)
