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


LOCAL_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../

include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES:= \
     $(LOCAL_PATH)/include \
     $(MM_INCLUDE) \
     $(MM_ROOT_PATH)/cow/include \
     $(base-includes)


ifeq ($(USING_PLAYER_SERVICE),1)
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/mediaserver/include
endif

LOCAL_C_INCLUDES += $(dbus-includes)         \
                    $(libuv-includes)        \
                    $(audioserver-includes)

LOCAL_SRC_FILES:= src/mediaplayer.cc

ifeq ($(USING_DEVICE_MANAGER),1)
	LOCAL_SRC_FILES += src/CowPlayerDMWrapper.cc
else
	LOCAL_SRC_FILES += src/CowPlayerWrapper.cc
endif

ifeq ($(USING_PLAYER_SERVICE),1)
    LOCAL_SRC_FILES += src/ProxyPlayerWrapper.cc
endif

LOCAL_MODULE:= libmediaplayer

LOCAL_LDFLAGS += -lstdc++ -ldl
LOCAL_SHARED_LIBRARIES += libmmbase

ifeq ($(USING_DEVICE_MANAGER),1)
LOCAL_SHARED_LIBRARIES += libdevmgr libjsoncpp
LOCAL_C_INCLUDES += tv/framework/nativeservice/devicemgrd/include   \
					$(jsoncpp-includes) 
endif

LOCAL_SHARED_LIBRARIES += libcowplayer libmmbase libcowbase
ifeq ($(USING_PLAYER_SERVICE),1)
   LOCAL_SHARED_LIBRARIES += libmediaclient_yunos libmediaservice_client_common
endif


include $(BUILD_SHARED_LIBRARY)



include $(CLEAR_VARS)
LOCAL_MODULE := mediaplayer_multi_prebuilt
LOCAL_MODULE_TAGS := optional

MEDIAPLAYER_INCLUDE_HEADERS := \
    $(LOCAL_PATH)/include/multimedia/mediaplayer.h:$(INST_INCLUDE_PATH)/mediaplayer.h

LOCAL_SRC_FILES:= $(MEDIAPLAYER_INCLUDE_HEADERS)

include $(BUILD_MULTI_PREBUILT)
