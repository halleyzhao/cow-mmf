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

YAMI_INSTALL_DIR=${YAMI_ROOT_DIR}
YAMI_INCLUDES= ${YAMI_INSTALL_DIR}/vaapi/include                   \
               ${YAMI_INSTALL_DIR}/libyami/include/libyami
YAMI_LIBS= va  va-drm yami
SRC_PATH := ../src

LOCAL_INCLUDES := $(MM_INCLUDE)                        \
                  $(SRC_PATH)                          \
                  $(SRC_PATH)/components               \
                  $(YAMI_INCLUDES)

LOCAL_SRC_FILES := $(SRC_PATH)/components/video_decode_yami.cc

LOCAL_SHARED_LIBRARIES := mmbase cowbase $(YAMI_LIBS)
LOCAL_LDFLAGS:= -L${YAMI_INSTALL_DIR}/vaapi/lib  -L${YAMI_INSTALL_DIR}/libyami/lib
LOCAL_INSTALL_PATH := $(INST_LIB_PATH)/cow

LOCAL_MODULE := libVideoDecodeYami.so

MODULE_TYPE := usr
include $(BASE_BUILD_DIR)/build_shared
