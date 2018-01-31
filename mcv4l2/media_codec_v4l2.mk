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

MULTIMEDIA_BASE:=../
BASE_BUILD_DIR:=$(MULTIMEDIA_BASE)/base/build
include $(BASE_BUILD_DIR)/reset_args

SRC_PATH := ./src

LOCAL_INCLUDES := $(MM_INCLUDE)  \
                  $(SRC_PATH)

LOCAL_SRC_FILES := $(SRC_PATH)/v4l2codec_device.cc      \
                   $(SRC_PATH)/media_notify.cc          \
                   $(SRC_PATH)/media_codec_v4l2.cc

LOCAL_SHARED_LIBRARIES := mmbase dl X11

# X11_CPP_LIBS_FLAGS :=-D_ENABLE_X11
# LOCAL_CPPFLAGS += $(X11_CPP_LIBS_FLAGS)

LOCAL_MODULE := libmediacodec_v4l2.so

MODULE_TYPE := eng
include $(BASE_BUILD_DIR)/build_shared
