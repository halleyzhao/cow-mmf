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

##rtpmuxer-test
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk

LOCAL_SRC_FILES := rtp_muxer_test.cc
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)

LOCAL_LDFLAGS += -lpthread
LOCAL_SHARED_LIBRARIES += libcowbase libAVDemuxer libRtpMuxer

LOCAL_MODULE := rtpmuxer-test
LOCAL_LDFLAGS += -lstdc++
include $(BUILD_EXECUTABLE)
