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

#### media transcoding
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
include $(MM_ROOT_PATH)/test/gtest_common.mk

LOCAL_SRC_FILES:= media_trans.cc
LOCAL_SRC_FILES += remux_pipeline.cc
LOCAL_SRC_FILES += tr_pipeline.cc

LOCAL_C_INCLUDES += \
    $(MM_INCLUDE) \
    $(MM_COW_INCLUDE) \
    $(MM_ROOT_PATH)/mediaplayer/include \
    $(base-includes)                    \
    $(MM_WAKELOCKER_PATH)

LOCAL_SHARED_LIBRARIES += libmmbase libmediaplayer libmmwakelocker libcowbase libcowplayer

LOCAL_LDFLAGS += -lstdc++ -lpthread
## surface related dep
REQUIRE_WPC = 1
REQUIRE_SURFACE = 1
REQUIRE_PAGEWINDOW = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= media-trans

include $(BUILD_EXECUTABLE)

