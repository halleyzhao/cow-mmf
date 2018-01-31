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

MY_TEST_CURRENT_DIR:=$(call my-dir)
ifneq ($(filter $(XMAKE_BUILD_TYPE), eng userdebug),)

#### phone/tablet
ifneq ($(filter $(XMAKE_PLATFORM), phone tablet lite), )
include $(call all-subdir-makefiles)
endif

#### tv
ifeq ($(XMAKE_PLATFORM), tv)
include ${MY_TEST_CURRENT_DIR}/wakelocker/yunos.mk
include ${MY_TEST_CURRENT_DIR}/mediacodec/yunos.mk
include ${MY_TEST_CURRENT_DIR}/cow/player/yunos.mk
include ${MY_TEST_CURRENT_DIR}/cow/recorder/yunos.mk
include ${MY_TEST_CURRENT_DIR}/mediaplayer/yunos.mk
include ${MY_TEST_CURRENT_DIR}/drm/yunos.mk
include ${MY_TEST_CURRENT_DIR}/res/yunos.mk
endif

#### ivi
ifeq ($(XMAKE_PLATFORM), ivi)
    include ${MY_TEST_CURRENT_DIR}/wakelocker/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/cow/simpleplayer/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/res/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/attachbuffer/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/audio/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/base/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/cow/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/customplayer/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/drm/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/ext/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/image/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/mediacodec/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/mediaplayer/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/mediakeeper/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/mediaserver/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/pbchannel/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/res/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/instantaudio/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/syssound/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/cow/recorder/yunos.mk
    include ${MY_TEST_CURRENT_DIR}/cow/player/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/v4l2codec/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/vrvideoview/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/webrtc/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/wifidisplay/yunos.mk
    # include ${MY_TEST_CURRENT_DIR}/WindowSurface/yunos.mk
endif

endif ## ifneq ($(filter $(XMAKE_BUILD_TYPE), eng userdebug),)
