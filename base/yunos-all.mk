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


# include $(call all-subdir-makefiles)
MY_MM_CURRENT_DIR:=$(call my-dir)

## base
include ${MY_MM_CURRENT_DIR}/base/yunos.mk
include ${MY_MM_CURRENT_DIR}/cow/yunos.mk

## module 1
include ${MY_MM_CURRENT_DIR}/ext/yunos.mk
include ${MY_MM_CURRENT_DIR}/mediasurfacetexture/yunos.mk
include ${MY_MM_CURRENT_DIR}/mediaplayer/yunos.mk
include ${MY_MM_CURRENT_DIR}/mediarecorder/yunos.mk
include ${MY_MM_CURRENT_DIR}/wifidisplay/yunos.mk
include ${MY_MM_CURRENT_DIR}/mediaserver/yunos.mk
include ${MY_MM_CURRENT_DIR}/audio/yunos.mk

## module 2
include ${MY_MM_CURRENT_DIR}/pbchannel/yunos.mk
include ${MY_MM_CURRENT_DIR}/mediakeeper/yunos.mk
include ${MY_MM_CURRENT_DIR}/instantaudio/yunos.mk
include ${MY_MM_CURRENT_DIR}/syssound/yunos.mk
include ${MY_MM_CURRENT_DIR}/image/yunos.mk
include ${MY_MM_CURRENT_DIR}/webrtc/yunos.mk

## platform specific components
ifeq ($(XMAKE_PLATFORM),ivi)
    include ${MY_MM_CURRENT_DIR}/cow/src/platform/ivi/yunos.mk
endif

## misc
include ${MY_MM_CURRENT_DIR}/drm/yunos.mk
include ${MY_MM_CURRENT_DIR}/mcv4l2/yunos.mk
# include ${MY_MM_CURRENT_DIR}/v4l2deviceservice/yunos.mk
include ${MY_MM_CURRENT_DIR}/transcoding/yunos.mk
include ${MY_MM_CURRENT_DIR}/vrvideoview/yunos.mk

## mdk
include ${MY_MM_CURRENT_DIR}/examples/yunos.mk
# include ${MY_MM_CURRENT_DIR}/examples/comps/yunos.mk ## usually, there is no libOpenCL yet
include ${MY_MM_CURRENT_DIR}/mdk/yunos.mk

## test
include ${MY_MM_CURRENT_DIR}/test/yunos.mk # webrtc is default on for pad only
ifneq ($(XMAKE_PLATFORM),tablet)  # add webrtc test manually for test
ifneq ($(XMAKE_PLATFORM),ivi)     # do not add webrtc test for ivi
  include ${MY_MM_CURRENT_DIR}/test/webrtc/y.mk
endif
endif


