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

MULTIMEDIA_BASE:=../../../
BASE_BUILD_DIR:=$(MULTIMEDIA_BASE)/base/build
include $(BASE_BUILD_DIR)/reset_args
include ../cow_test_common.mk

LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof
LOCAL_CPPFLAGS += $(GLIB_G_OPTION_CFLAGS)
LOCAL_LDFLAGS += $(GLIB_G_OPTION_LDFLAGS)
LOCAL_SHARED_LIBRARIES += cowrecorder mediarecorder

LOCAL_MODULE := cowrecorder-test

LOCAL_INCLUDES := $(MM_INCLUDE) \
                  $(MULTIMEDIA_BASE)/mediarecorder/include \
                  $(MULTIMEDIA_BASE)/cow/include           \
                  $(MM_HELP_WINDOWSURFACE_INCLUDE)

LOCAL_SRC_FILES := cowrecorder_test.cc

include $(BASE_BUILD_DIR)/build_exec
