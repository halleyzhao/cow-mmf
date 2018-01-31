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


ifneq ($(XMAKE_PLATFORM),ivi)
ifneq ($(XMAKE_PRODUCT),yunhal)

LOCAL_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../

## host common
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk
LOCAL_C_INCLUDES +=                            \
     $(LOCAL_PATH)/include                     \
     $(MM_INCLUDE)                             \
     $(base-includes)

LOCAL_C_INCLUDES += $(dbus-includes)                 \
                    $(libuv-includes)

LOCAL_SRC_FILES:= src/MediaServiceName.cc        \
                  src/MediaServiceLooper.cc      \
                  src/SideBandIPC.cc

LOCAL_MODULE:= libmediaservice_client_common
LOCAL_CFLAGS += -fno-rtti
LOCAL_LDFLAGS += -lstdc++
LOCAL_SHARED_LIBRARIES += libmmbase

include $(BUILD_SHARED_LIBRARY)

## host media service
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES +=                            \
     $(LOCAL_PATH)/include                     \
     $(MM_INCLUDE)                             \
     $(base-includes)                          \
     $(MM_ROOT_PATH)/mediarecorder/include     \
     $(MM_ROOT_PATH)/mediaplayer/include       \
     $(MM_ROOT_PATH)/cow/include               \
     $(audioserver-includes)                   \
     $(permission-includes)                    \
     $(multimedia-surfacetexture-includes)     \
     $(cameraserver-includes)                  \
     $(WINDOWSURFACE_INCLUDE)                  \
     $(corefoundation-includes)                \
     $(MM_ROOT_PATH)/wifidisplay/include

LOCAL_C_INCLUDES += $(dbus-includes)                 \
                    $(libuv-includes)                \
                    $(page-includes)                 \
                    $(dynamicpagemanager-includes)

LOCAL_SRC_FILES:= src/MediaService.cc            \
                  src/MMSession.cc               \
                  src/MediaPlayerInstance.cc     \
                  src/MediaPlayerAdaptor.cc      \
                  src/MediaRecorderInstance.cc   \
                  src/MediaRecorderAdaptor.cc    \
                  src/MMDpmsProxy.cc

##create local window
LOCAL_SRC_FILES += $(MM_EXT_PATH)/WindowSurface/WindowSurfaceTestWindow.cc \
                   $(MM_EXT_PATH)/native_surface_help.cc
LOCAL_C_INCLUDES += $(MM_HELP_WINDOWSURFACE_INCLUDE) \
                    $(HYBRIS_EGLPLATFORMCOMMON)  \
                    $(pagewindow-includes)
LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof $(WINDOWSURFACE_CPPFLAGS)
LOCAL_LDFLAGS += -lpthread -ldl
##create local window

LOCAL_CFLAGS += -fno-rtti

##Enable utils/Mutex
LOCAL_CFLAGS += -DHAVE_PTHREADS
LOCAL_MODULE:= libmediaservice_yunos

LOCAL_SHARED_LIBRARIES += libmmbase libmediaservice_client_common libmediasurfacetexture libdpms-proxy
REQUIRE_LIBUV = 1
REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
REQUIRE_PERMISSION = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
LOCAL_SHARED_LIBRARIES += libcowplayer libcowrecorder##libmediaplayer #libmediarecorder
##LOCAL_SHARED_LIBRARIES += libcowaudiorecorder

LOCAL_LDFLAGS += -lstdc++

include $(BUILD_SHARED_LIBRARY)

## host media client
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES +=                            \
     $(LOCAL_PATH)/include                     \
     $(MM_INCLUDE)                             \
     $(base-includes)                          \
     $(HYBRIS_EGLPLATFORMCOMMON)               \
     $(WINDOWSURFACE_INCLUDE)                  \
     $(MM_ROOT_PATH)/cow/include               \
     $(MM_ROOT_PATH)/mediarecorder/include     \
     $(MM_ROOT_PATH)/mediaplayer/include       \
     $(multimedia-surfacetexture-includes)     \
     $(MM_ROOT_PATH)/wifidisplay/include       \
     $(MM_EXT_PATH)                            \
     $(cameraserver-includes)                  \
     $(corefoundation-includes)                \
     $(MM_HELP_WINDOWSURFACE_INCLUDE)

LOCAL_C_INCLUDES += $(dbus-includes)           \
                    $(libuv-includes)          \
                    $(vr-video-includes)

LOCAL_SRC_FILES:= src/MediaClientHelper.cc     \
                  src/MediaProxy.cc            \
                  src/MediaPlayerClient.cc     \
                  src/MediaPlayerProxy.cc      \
                  src/MediaRecorderClient.cc   \
                  src/MediaRecorderProxy.cc

LOCAL_CFLAGS += -fno-rtti

##Enable utils/Mutex
LOCAL_CFLAGS += -DHAVE_PTHREADS
REQUIRE_SURFACE = 1
LOCAL_MODULE:= libmediaclient_yunos
LOCAL_SHARED_LIBRARIES += libmmbase libmediasurfacetexture

ifeq ($(USING_VR_VIDEO),1)
LOCAL_SHARED_LIBRARIES += libyvr_view
endif

REQUIRE_LIBUV = 1
ifeq ($(USING_CAMERA),1)
REQUIRE_CAMERASVR = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
LOCAL_SHARED_LIBRARIES += libmediaservice_client_common #libmediaplayer #libmediarecorder
##LOCAL_SHARED_LIBRARIES += libcowaudiorecorder

LOCAL_LDFLAGS += -lstdc++
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    REQUIRE_SURFACE = 1
    REQUIRE_LIBHYBRIS = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_SHARED_LIBRARY)

## media service main
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES:=                     \
     $(LOCAL_PATH)/include             \
     $(MM_INCLUDE)                     \
     $(base-includes)                  \
     $(multimedia-pbchannel-includes)

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
endif

LOCAL_SRC_FILES:= src/main_mediaservice.cc \
                  src/FFMpegInitor.cc

LOCAL_CFLAGS += -fno-rtti

LOCAL_MODULE:= media_service

LOCAL_SHARED_LIBRARIES += libmmbase libmediaservice_yunos

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
ifeq ($(XMAKE_ENABLE_CNTR_CVG),true)
LOCAL_SHARED_LIBRARIES += libmediaservice
endif
endif

ifeq ($(USING_AUDIO_CRAS),1)
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/syssound/include
    LOCAL_CFLAGS += -DHAVE_SYSSOUND
    LOCAL_SHARED_LIBRARIES += libsyssoundservice
endif

ifeq ($(USING_PLAYBACK_CHANNEL),1)
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/pbchannel/include
    LOCAL_CFLAGS += -DHAVE_PLAYBACK_CHANNEL_SERVICE
    LOCAL_SHARED_LIBRARIES += libpbchannelservice
endif

ifeq ($(USING_OMX_SERVICE),1)
    LOCAL_C_INCLUDES += $(multimedia-mediacodec-includes)
    LOCAL_SHARED_LIBRARIES += libomx_yunos
endif


LOCAL_CFLAGS += -DHAVE_PTHREADS
LOCAL_LDFLAGS += -lstdc++
LOCAL_C_INCLUDES += $(libav-includes)
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
ifeq ($(XMAKE_ENABLE_CNTR_CVG),true)
REQUIRE_PLUGIN_CORE = 1
endif
endif

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

include $(BUILD_EXECUTABLE)

## media service dumpsys
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_C_INCLUDES:=                     \
     $(LOCAL_PATH)/include             \
     $(MM_INCLUDE)                     \
     $(base-includes)

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
endif

LOCAL_SRC_FILES:= src/dumpsys_mediaservice.cc

LOCAL_CFLAGS += -fno-rtti

LOCAL_MODULE:= mediaservice-dumpsys

LOCAL_SHARED_LIBRARIES += libmmbase libmediaclient_yunos

LOCAL_LDFLAGS += -lstdc++

include $(BUILD_EXECUTABLE)

endif
endif
