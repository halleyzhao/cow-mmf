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

##!!!!! it is used by yunos.mk in fact !!!!!##
CURRENT_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(CURRENT_PATH)/../../

include $(MM_ROOT_PATH)/base/build/build.mk

COW_COMMON_INCLUDE:= \
    ${MM_INCLUDE} \
    ${MM_COW_INCLUDE} \
    ${LOCAL_PATH}/../include

LOCAL_C_INCLUDES += $(COW_COMMON_INCLUDE)

LOCAL_CPPFLAGS += -D_COW_PLUGIN_PATH=\"$(COW_PLUGIN_PATH)\"
LOCAL_CPPFLAGS += -D_VIDEO_CODEC_FFMPEG

LOCAL_RPATH += $(COW_PLUGIN_PATH)

LOCAL_SHARED_LIBRARIES += libmmbase

