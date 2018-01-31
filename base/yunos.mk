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


TOP_PATH:=$(call my-dir)
LOCAL_PATH:= $(TOP_PATH)
MM_ROOT_PATH:= $(LOCAL_PATH)/../

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_SRC_FILES:= \
    src/src/media_buffer.cc \
    src/src/media_monitor.cc \
    src/src/mmthread.cc \
    src/src/mmmsgthread.cc \
    src/src/mmnotify.cc \
    src/src/mmparam.cc \
    src/src/mm_debug.cc \
    src/src/mm_cpp_utils.cc \
    src/src/mm_buffer.cc \
    src/src/media_attr_str.cc \
    src/src/media_meta.cc \
    src/src/mm_ashmem.cc \
    src/src/ashmem.cc \


LOCAL_MODULE:= libmmbase

LOCAL_C_INCLUDES:= \
    $(MM_INCLUDE) \
    $(LOCAL_PATH)/include \
    $(base-includes) \
    $(properties-includes)

LOCAL_LDFLAGS += -lpthread -lstdc++

REQUIRE_PROPERTIES = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := mmbase_header_prebuilt
LOCAL_MODULE_TAGS := optional

MMBASE_INCLUDE_HEADERS := \
    include/multimedia/mm_cpp_utils.h:$(INST_INCLUDE_PATH)/mm_cpp_utils.h \
    include/multimedia/mm_debug.h:$(INST_INCLUDE_PATH)/mm_debug.h \
    include/multimedia/mm_errors.h:$(INST_INCLUDE_PATH)/mm_errors.h \
    include/multimedia/mm_types.h:$(INST_INCLUDE_PATH)/mm_types.h \
    include/multimedia/mm_audio.h:$(INST_INCLUDE_PATH)/mm_audio.h \
    include/multimedia/mm_camera_compat.h:$(INST_INCLUDE_PATH)/mm_camera_compat.h \
    include/multimedia/mm_audio_compat.h:$(INST_INCLUDE_PATH)/mm_audio_compat.h \
    include/multimedia/mmparam.h:$(INST_INCLUDE_PATH)/mmparam.h \
    include/multimedia/mmthread.h:$(INST_INCLUDE_PATH)/mmthread.h \
    include/multimedia/mmmsgthread.h:$(INST_INCLUDE_PATH)/mmmsgthread.h \
    include/multimedia/mmlistener.h:$(INST_INCLUDE_PATH)/mmlistener.h \
    include/multimedia/mmnotify.h:$(INST_INCLUDE_PATH)/mmnotify.h \
    include/multimedia/mm_buffer.h:$(INST_INCLUDE_PATH)/mm_buffer.h \
    include/multimedia/mm_refbase.h:$(INST_INCLUDE_PATH)/mm_refbase.h \
    include/multimedia/elapsedtimer.h:$(INST_INCLUDE_PATH)/elapsedtimer.h \
    include/multimedia/media_attr_str.h:$(INST_INCLUDE_PATH)/media_attr_str.h \
    include/multimedia/media_profile.h:$(INST_INCLUDE_PATH)/media_profile.h \
    include/multimedia/media_meta.h:$(INST_INCLUDE_PATH)/media_meta.h \
    include/multimedia/media_buffer.h:$(INST_INCLUDE_PATH)/media_buffer.h \
    include/multimedia/media_monitor.h:$(INST_INCLUDE_PATH)/media_monitor.h \
    include/multimedia/mm_ashmem.h:$(INST_INCLUDE_PATH)/mm_ashmem.h
LOCAL_SRC_FILES:= $(MMBASE_INCLUDE_HEADERS)

include $(BUILD_MULTI_PREBUILT)
