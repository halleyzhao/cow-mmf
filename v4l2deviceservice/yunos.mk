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
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../base/build/build.mk

LOCAL_C_INCLUDES +=               \
    $(multimedia-base-includes)   \
    $(LOCAL_PATH)/src             \
    $(permission-includes)        \
    $(base-includes)              \
    $(yunhal-includes)            \
    $(libdrm-includes)            \
    $(dbus-includes)

ifeq ($(XMAKE_BOARD),intel)
LOCAL_C_INCLUDES += $(kernel-includes)
endif
ifeq ($(XMAKE_PLATFORM),ivi)
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/cow/src/platform/ivi/
endif

LOCAL_SRC_FILES +=          \
    src/v4l2_service.cc      \
    src/v4l2_service_imp.cc  \
    src/v4l2codec_device.cc  \
    src/v4l2_manager.cc      \
    src/v4l2_device_client.cc \
    src/v4l2_device_instance.cc \

ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif
LOCAL_SHARED_LIBRARIES += libbase libmmbase
LOCAL_LDFLAGS += -lpthread -lstdc++ -ldl
LOCAL_CPPFLAGS+= -std=c++11

LOCAL_MODULE:= libv4l2_yunos

include $(BUILD_SHARED_LIBRARY)

