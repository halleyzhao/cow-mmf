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

MULTIMEDIA_BASE:=../../
BASE_BUILD_DIR:=$(MULTIMEDIA_BASE)/base/build
include $(BASE_BUILD_DIR)/reset_args

LOCAL_INCLUDES := $(MM_INCLUDE) \
                  ./include

LOCAL_SHARED_LIBRARIES := pthread
LOCAL_CPPFLAGS := -g -D__ENABLE_AVFORMAT__
LOCAL_LDFLAGS:= `pkg-config --cflags --libs libavformat libavcodec libavutil`

LOCAL_MODULE := libmmbase.so
SRC_PATH := ./src
LOCAL_SRC_FILES := $(SRC_PATH)/media_buffer.cc   \
                   $(SRC_PATH)/media_monitor.cc   \
                   $(SRC_PATH)/media_attr_str.cc   \
                   $(SRC_PATH)/media_meta.cc        \
                   $(SRC_PATH)/mm_buffer.cc          \
                   $(SRC_PATH)/mm_debug.cc            \
                   $(SRC_PATH)/mm_cpp_utils.cc       \
                   $(SRC_PATH)/mmthread.cc           \
                   $(SRC_PATH)/mmmsgthread.cc        \
                   $(SRC_PATH)/mmnotify.cc           \
                   $(SRC_PATH)/mmparam.cc 

MODULE_TYPE := usr
include $(BASE_BUILD_DIR)/build_shared
