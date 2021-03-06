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

LOCAL_SHARED_LIBRARIES += AVDemuxer RtpMuxer

LOCAL_MODULE := rtpmuxer-test

LOCAL_SRC_FILES := rtp_muxer_test.cc

LOCAL_INST_FILES += (eng:$(MULTIMEDIA_BASE)/test/res/video/test.mp4:$(INST_TEST_RES_PATH)/test.mp4)

include $(BASE_BUILD_DIR)/build_exec
