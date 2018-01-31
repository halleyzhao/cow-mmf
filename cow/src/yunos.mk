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
MM_ROOT_PATH:= $(LOCAL_PATH)/../../
MY_COW_LOCAL_PATH:= $(LOCAL_PATH)

#### libcowbase
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_CPPFLAGS += -D_COW_XML_PATH=\"/$(INST_ETC_PATH)\"

LOCAL_SRC_FILES:= \
    clock.cc \
    clock_wrapper.cc \
    component.cc \
    cow_xml.cc \
    component_factory.cc \
    pipeline.cc \
    pipeline_recorder_base.cc \
    pipeline_player_base.cc \
    cow_util.cc \
    make_csd.cc \
    third_helper.cc \
    mm_vendor_format.cc

LOCAL_C_INCLUDES += $(expat-includes) \
                    $(WINDOWSURFACE_INCLUDE) \
                    $(cameraserver-includes)   \
                    $(corefoundation-includes) \
                    $(audioserver-includes) \
                    $(graphics-includes)    \

ifeq ($(YUNOS_SYSCAP_MM),true)
LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif

LOCAL_LDFLAGS += -ldl -lstdc++
REQUIRE_EXPAT = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libcowbase
include $(BUILD_SHARED_LIBRARY)

#### libcow-avhelper
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_SRC_FILES:= av_buffer_helper.cc

LOCAL_C_INCLUDES += $(libav-includes)

LOCAL_LDFLAGS += -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libcow-avhelper
include $(BUILD_SHARED_LIBRARY)

####cow_multi_prebuilt
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
LOCAL_MODULE := cow_multi_prebuilt
LOCAL_MODULE_TAGS := optional
ifeq ($(XMAKE_BOARD),qemu)
    COW_COPY_FILES := $(LOCAL_PATH)/../resource/cow_plugins_emulator.xml:$(INST_ETC_PATH)/cow_plugins.xml
else
    COW_COPY_FILES := $(LOCAL_PATH)/../resource/cow_plugins.xml:$(INST_ETC_PATH)/cow_plugins.xml
    COW_COPY_FILES += $(LOCAL_PATH)/../resource/cow_plugins_hw_video.xml:$(INST_ETC_PATH)/cow_plugins_hw_video.xml
    COW_COPY_FILES += $(LOCAL_PATH)/../resource/cow_plugins_sw_video.xml:$(INST_ETC_PATH)/cow_plugins_sw_video.xml
  ifneq ($(YUNOS_SYSCAP_MM),true)
    ifeq ($(XMAKE_ENABLE_CNTR_HAL), false)
        COW_COPY_FILES += $(LOCAL_PATH)/../resource/cow_plugins_$(XMAKE_PRODUCT)_yunhal.xml:$(INST_ETC_PATH)/cow_plugins_vendor.xml
    else
        COW_COPY_FILES += $(LOCAL_PATH)/../resource/cow_plugins_$(XMAKE_PRODUCT).xml:$(INST_ETC_PATH)/cow_plugins_vendor.xml
    endif
  endif
endif

LOCAL_SRC_FILES:= $(COW_COPY_FILES)
include $(BUILD_MULTI_PREBUILT)

#### libcowplayer
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_SRC_FILES:= \
    player/pipeline_player.cc \
    player/pipeline_audioplayer.cc \
    player/cowplayer.cc


LOCAL_C_INCLUDES+= \
    $(properties-includes) \
    $(audioserver-includes)


LOCAL_SHARED_LIBRARIES += libcowbase
ifeq ($(XMAKE_BOARD),qcom)
LOCAL_SHARED_LIBRARIES += libpipelineLPA
endif
LOCAL_MODULE:= libcowplayer
LOCAL_LDLIBS+= -lstdc++

REQUIRE_PROPERTIES = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)

#### libpipelineLPA
ifeq ($(XMAKE_BOARD),qcom)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_SRC_FILES:= \
    player/pipeline_LPA.cc

LOCAL_C_INCLUDES+= \
    $(audioserver-includes)


LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libpipelineLPA
LOCAL_LDLIBS+= -lstdc++

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)
endif

#### libcowdev simple pipeline and app skeleton, to be extended by app developers
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_SRC_FILES:= \
    basicapp/pipeline_basic.cc \
    basicapp/cowapp_basic.cc

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libcow-basicapp
LOCAL_LDLIBS+= -lstdc++

include $(BUILD_SHARED_LIBRARY)

#### libcowrecorder
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../build/cow_common.mk

LOCAL_SRC_FILES:= \
    recorder/pipeline_videorecorder.cc \
    recorder/pipeline_audiorecorder.cc \
    recorder/cowrecorder.cc \
    recorder/pipeline_rtsprecorder.cc

LOCAL_C_INCLUDES += $(audioserver-includes)  \
                    $(WINDOWSURFACE_INCLUDE) \
                    $(cameraserver-includes) \
                    $(corefoundation-includes)

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libcowrecorder
LOCAL_LDLIBS += -lstdc++
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)

#### simple vr pipeline
#### libpipelinearrecorder
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/cow/build/cow_common.mk

LOCAL_SRC_FILES:= \
    pipeline_ar_recorder.cc

LOCAL_C_INCLUDES+= \
    $(properties-includes) \
    $(WINDOWSURFACE_INCLUDE) \
    $(cameraserver-includes)   \
    $(corefoundation-includes) \
    $(audioserver-includes)


LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libpipelinearrecorder
LOCAL_LDLIBS+= -lstdc++

REQUIRE_PROPERTIES = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
include $(BUILD_SHARED_LIBRARY)

##the components
include ${MY_COW_LOCAL_PATH}/components/yunos.mk

ifeq ($(XMAKE_PLATFORM),ivi)
#include ${MY_COW_LOCAL_PATH}/platform/ivi/yunos.mk
endif
