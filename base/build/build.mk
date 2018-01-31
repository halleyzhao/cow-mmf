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

CURRENT_PATH:= $(call my-dir)
MM_ROOT_PATH:= $(CURRENT_PATH)/../../

LOCAL_CPPFLAGS:= -D_YUNOS_MM -DMM_LOG_LEVEL=MM_LOG_DEBUG
LOCAL_CPPFLAGS += -D_COW_SOURCE_VERSION=\"0.5.3:20171209\"
LOCAL_CPPFLAGS += -Wno-multichar

## BSP/HAL type
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_CPPFLAGS += -D__MM_YUNOS_CNTRHAL_BUILD__ -DPLUGIN_HAL
ifeq ($(XMAKE_ENABLE_CNTR_CVG),true)
    LOCAL_CPPFLAGS += -DPLUGIN_HAL_CVG
endif
else
    ifeq ($(XMAKE_PLATFORM),ivi)
        LOCAL_CPPFLAGS += -D__MM_YUNOS_LINUX_BSP_BUILD__
        LOCAL_CPPFLAGS += -DWL_EGL_PLATFORM
    else
        LOCAL_CPPFLAGS += -D__MM_YUNOS_YUNHAL_BUILD__ -DYUN_HAL
        LOCAL_CPPFLAGS += -DWL_EGL_PLATFORM
    endif
endif


LOCAL_CPPFLAGS += -std=c++11 -fpermissive
LOCAL_LDFLAGS += -z defs
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
LOCAL_LDFLAGS += -Wl,--rpath-link $(XMAKE_BUILD_OUT)/target/rootfs/usr/lib/plugin
LOCAL_LDFLAGS += -Wl,--rpath-link $(XMAKE_BUILD_OUT)/target/rootfs/usr/lib/compat
endif

# feature config
include $(MM_ROOT_PATH)/base/build/build_config.mk

ifeq ($(XMAKE_PLATFORM),tv)
    LOCAL_CPPFLAGS += -D__PLATFORM_TV__
    ifeq ($(XMAKE_BOARD),mstar)
        LOCAL_CPPFLAGS += -D__TV_BOARD_MSTAR__ -DUSE_ASHMEM_RPC -D__NO_DECODER_BUFFERING_CTRL__
        export USING_OMX_SERVICE=1
        export USING_OPUS_CODEC=1
        export USING_UV_STRIDE16=1
        export USING_DEVICE_MANAGER=1
    endif
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
endif

ifeq ($(XMAKE_PLATFORM),lite)
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
endif

ifeq ($(XMAKE_PLATFORM),phone)
    LOCAL_CPPFLAGS += -D__PLATFORM_PHONE__
    ifeq ($(XMAKE_BOARD),mtk)
        LOCAL_CPPFLAGS += -D__PHONE_BOARD_MTK__ -DUSE_ASHMEM_RPC
        export USING_UV_STRIDE16=1
    endif

    ifeq ($(XMAKE_BOARD),sprd)
        LOCAL_CPPFLAGS += -D__PHONE_BOARD_SPRD__ -DUSE_ASHMEM_RPC

        ifneq ($(YUNOS_SYSCAP_MM),true)
            export USING_PLAYBACK_CHANNEL=1
            export USING_V4L2CODEC=1
            export USING_CAMERA_API2=0
            export USING_VR_VIDEO=0
            #export USING_SOFT_VIDEO_CODEC_FOR_MS=0
            MM_UT_SURFACE_WIDTH=240
            MM_UT_SURFACE_HEIGHT=320
            MM_VIDEO_MAX_WIDTH=1920
            MM_VIDEO_MAX_HEIGHT=1080
        endif
    endif

    ifeq ($(XMAKE_BOARD),qcom)
        LOCAL_CPPFLAGS += -D__PHONE_BOARD_QCOM__ -DUSE_ASHMEM_RPC
        export USING_PLAYBACK_CHANNEL=1
        export USING_EIS=1
        export USING_VR_VIDEO=0
    endif
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
endif

ifeq ($(XMAKE_BOARD),qemu)
        LOCAL_CPPFLAGS += -D__EMULATOR__ -DUSE_ASHMEM_RPC
        MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_030)
        MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_030)
endif


ifeq ($(XMAKE_PLATFORM),tablet)
    LOCAL_CPPFLAGS += -D__PLATFORM_TABLET__
    ifeq ($(XMAKE_BOARD),intel)
        LOCAL_CPPFLAGS += -D__TABLET_BOARD_INTEL__
    endif
    ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
        LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\""
    endif
    LOCAL_CPPFLAGS += -fpermissive
    ifneq ($(YUNOS_SYSCAP_MM),true)
        MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_010)
        MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_010)
        export USING_CAMERA_API2=0
        export USING_SURFACE_FROM_BQ=1
        export USING_YUNOS_MODULE_LOAD_FW=0
    endif
endif

ifeq ($(XMAKE_PLATFORM),ivi)
  ifeq ($(XMAKE_PRODUCT),ti_ip31)
    LOCAL_CPPFLAGS += -D__PLATFORM_IVI__
    LOCAL_CPPFLAGS += -DENABLE_DEFAULT_AUDIO_CONNECTION
    export USING_AUDIO_CRAS=0
    LOCAL_CPPFLAGS += -D__AUDIO_PULSE__
    export USING_AUDIO_PULSE=1
    export USING_PLAYBACK_CHANNEL=0
    export USING_OMX_SERVICE=0
    export USING_EIS=0
    export USING_OPUS_CODEC=0
    export USING_GUI_SURFACE=0
    export USING_CAMERA=0
    export USING_MEDIACODEC=0
    export USING_PLAYER_SERVICE=0
    export USING_VR_VIDEO=0
    export USING_RECORDER_SERVICE=0
    export USING_V4L2CODEC=1
    export USING_YUNOS_MODULE_LOAD_FW=0
    export USING_V4L2CODEC=1
    export ENABLE_DEFAULT_AUDIO_CONNECTION=1
    export USING_VIDEOSOURCE_SW_CSC=1
    export USING_DRM_SURFACE=1
    MM_SCREEN_WIDTH=960
    MM_SCREEN_HEIGHT=1280
    export USING_FORCE_FFMPEG=1
  else
    ifeq ($(XMAKE_PRODUCT),nxp_imx6)
      LOCAL_CFLAGS += -DLINUX -DEGL_API_FB -DEGL_API_WL
      export USING_GUI_SURFACE=0
      export USING_CAMERA=0
      export USING_AUDIO_CRAS=1
      LOCAL_CPPFLAGS += -D__AUDIO_CRAS__
      export USING_AUDIO_PULSE=0
      MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_030)
      export USING_V4L2CODEC=1
      export USING_PLAYER_SERVICE=0
      export USING_RECORDER_SERVICE=0
      export USING_VPU_SURFACE=1
    else
      LOCAL_CPPFLAGS += -D__PLATFORM_IVI__
      export USING_AUDIO_CRAS=0
      LOCAL_CPPFLAGS += -D__AUDIO_PULSE__
      export USING_AUDIO_PULSE=1
      export USING_PLAYBACK_CHANNEL=0
      export USING_OMX_SERVICE=0
      export USING_EIS=0
      export USING_OPUS_CODEC=0
      export USING_GUI_SURFACE=0
      export USING_CAMERA=0
      export USING_MEDIACODEC=0
      export USING_PLAYER_SERVICE=0
      export USING_VR_VIDEO=0
      export USING_RECORDER_SERVICE=0
      export USING_YUNOS_MODULE_LOAD_FW=0
      export USING_FORCE_FFMPEG=1
    endif
  endif
endif
ifeq ($(USING_OMX_SERVICE),1)
    LOCAL_CPPFLAGS += -D__USING_OMX_SERVICE__
endif

ifeq ($(USING_V4L2_SERVICE),1)
    LOCAL_CPPFLAGS += -D__USING_V4L2_SERVICE__
endif

ifeq ($(USING_PLAYER_SERVICE),1)
    LOCAL_CPPFLAGS += -D__USING_PLAYER_SERVICE__
endif
ifeq ($(USING_PLAYER_SERVICE),1)
    LOCAL_CPPFLAGS += -D__USING_RECORDER_SERVICE__
endif
ifeq ($(USING_PLAYBACK_CHANNEL_FUSION),1)
    LOCAL_CPPFLAGS += -D__USING_PLAYBACK_CHANNEL_FUSION__
endif

ifeq ($(USING_CAMERA_API2),1)
    LOCAL_CPPFLAGS += -D__USING_CAMERA_API2__
endif

ifeq ($(USING_VR_VIDEO),1)
    LOCAL_CPPFLAGS += -D__USING_VR_VIDEO__
endif

ifeq ($(USING_SOFT_VIDEO_CODEC_FOR_MS),1)
    LOCAL_CPPFLAGS += -D__USEING_SOFT_VIDEO_CODEC_FOR_MS__
endif

ifeq ($(XMAKE_ENABLE_LOW_MEMORY),true)
    LOCAL_CPPFLAGS += -D__ENABLE_LOW_MEMORY__
endif

ifeq ($(USING_SURFACE_FROM_BQ),1)
    LOCAL_CPPFLAGS += -D__USING_SURFACE_FROM_BQ__
endif
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
    LOCAL_CPPFLAGS += -D__USING_YUNOS_MODULE_LOAD_FW__
endif

ifeq ($(USING_UV_STRIDE16),1)
    LOCAL_CPPFLAGS += -D__USEING_UV_STRIDE16__
endif

ifeq ($(USING_DEVICE_MANAGER),1)
    LOCAL_CPPFLAGS += -D__USEING_DEVICE_MANAGER__
endif

ifeq ($(XMAKE_PROD_MODE), on)
    LOCAL_CFLAGS +=   -D__MM_PROD_MODE_ON__
    LOCAL_CXXFLAGS += -D__MM_PROD_MODE_ON__
endif

ifeq ($(USING_VIDEOSOURCE_SW_CSC),1)
    LOCAL_CPPFLAGS += -DMM_VIDEOSOURCE_SW_CSC
endif

ifeq ($(USING_DRM_SURFACE),1)
    LOCAL_CPPFLAGS += -D__MM_BUILD_DRM_SURFACE__
endif

ifeq ($(USING_VPU_SURFACE),1)
    LOCAL_CPPFLAGS += -D__MM_BUILD_VPU_SURFACE__
endif

ifeq ($(USING_AUDIO_CRAS),1)
    LOCAL_CPPFLAGS += -D__AUDIO_CRAS__
endif

ifeq ($(YUNOS_SYSCAP_MM),true)
    LOCAL_CPPFLAGS += -D__USING_SYSCAP_MM__
endif

ifeq ($(USING_USB_CAMERA),1)
    LOCAL_CPPFLAGS += -D__USING_USB_CAMERA__
endif

LOCAL_CPPFLAGS += -DMM_USE_CAMERA_VERSION=$(MM_USE_CAMERA_VERSION)
LOCAL_CPPFLAGS += -DMM_USE_AUDIO_VERSION=$(MM_USE_AUDIO_VERSION)
LOCAL_CPPFLAGS += -DMM_UT_SURFACE_WIDTH=$(MM_UT_SURFACE_WIDTH)
LOCAL_CPPFLAGS += -DMM_UT_SURFACE_HEIGHT=$(MM_UT_SURFACE_HEIGHT)
LOCAL_CPPFLAGS += -DMM_VIDEO_MAX_WIDTH=$(MM_VIDEO_MAX_WIDTH)
LOCAL_CPPFLAGS += -DMM_VIDEO_MAX_HEIGHT=$(MM_VIDEO_MAX_HEIGHT)
LOCAL_CPPFLAGS += -DMM_SCREEN_WIDTH=$(MM_SCREEN_WIDTH)
LOCAL_CPPFLAGS += -DMM_SCREEN_HEIGHT=$(MM_SCREEN_HEIGHT)
COW_PLUGIN_PATH := /usr/lib/cow

#### native surface include path
WINDOWSURFACE_INCLUDE := $(graphics-includes) \
                         $(wpc-includes) \
                         $(wayland-includes) \
                         ${gui-includes}

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
else
    YALLOC_INCLUDE := $(YUNOS_ROOT)/framework/libs/graphics/yalloc/include
    ifeq ($(XMAKE_BOARD),intel)
        YALLOC_INCLUDE += $(YUNOS_ROOT)/vendor/intel/graphics/yalloc/include/
    endif
    WINDOWSURFACE_INCLUDE += ${YALLOC_INCLUDE}
endif

MM_EXT_PATH := $(MM_ROOT_PATH)/ext

#### native window/surface include path
MM_HELP_WINDOWSURFACE_INCLUDE := $(WINDOWSURFACE_INCLUDE) \
                                 $(MM_ROOT_PATH)/ext
MM_HELP_WINDOWSURFACE_SRC_FILES := $(MM_ROOT_PATH)/ext/native_surface_help.cc

MM_HELP_WINDOWSURFACE_INCLUDE += $(MM_ROOT_PATH)/ext/WindowSurface
MM_HELP_WINDOWSURFACE_SRC_FILES_V2 := $(MM_EXT_PATH)/WindowSurface/WindowSurfaceTestWindow.cc
MM_HELP_WINDOWSURFACE_SRC_FILES_V2 += $(MM_EXT_PATH)/egl/egl_texture.cc
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    MM_HELP_WINDOWSURFACE_SRC_FILES_V2 += $(MM_EXT_PATH)/WindowSurface/SimpleSubWindow.cc
endif
MM_HELP_WINDOWSURFACE_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES_V2)

WINDOWSURFACE_CPPFLAGS := -std=c++11

HYBRIS_EGLPLATFORMCOMMON := $(wayland-includes) \
                            $(libhybris-includes)

MM_INCLUDE := \
     . \
     $(MM_ROOT_PATH)/base/include \
     $(MM_ROOT_PATH)/base/include/multimedia \
     $(base-includes)

ifeq ($(USING_DRM_SURFACE),1)
TI-LIBDCE-INCLUDES:= $(YUNOS_ROOT)/third_party/libdce                       \
                    $(YUNOS_ROOT)/third_party/libdce/packages               \
                    $(YUNOS_ROOT)/third_party/libdce/packages/codec_engine  \
                    $(YUNOS_ROOT)/third_party/libdce/packages/ivahd_codecs  \
                    $(YUNOS_ROOT)/third_party/libdce/packages/xdais         \
                    $(YUNOS_ROOT)/third_party/libdce/packages/xdctools      \
                    $(YUNOS_ROOT)/third_party/libdce/packages/framework_components
endif

MM_PULSEAUDIO_INCLUDE := $(MM_ROOT_PATH)/audio/src/pulseaudio/include

MM_MEDIACODEC_INCLUDE := $(multimedia-mediacodec-includes)

MM_WFD_INCLUDE := ${MM_ROOT_PATH}/wifidisplay/include

MM_MEDIACODEC_NOTUSE_COMPAT_INCLUDE := $(multimedia-mediacodec-nocomapt-includes)

MM_COW_INCLUDE := \
    $(MM_ROOT_PATH)/cow/src \
    $(MM_ROOT_PATH)/cow/src/components \
    $(MM_ROOT_PATH)/cow/include

MM_WAKELOCKER_PATH := $(MM_ROOT_PATH)/test/wakelocker $(power-includes)
## TODO, move it to xmake build/core/includes.mk
WEBRTC_SDK_INCLUDE := ${MM_ROOT_PATH}/ext/webrtc/include
WEBRTC_SDK_LOCAL_INCLUDE := ${WEBRTC_SDK_INCLUDE}
ifeq ($(XMAKE_PLATFORM),tablet)
    WEBRTC_SDK_INCLUDE := third_party/webrtc/sdk-bin/include
endif
ifneq ($(XMAKE_PLATFORM),tablet)
    multimedia-webrtc-includes := ${MM_ROOT_PATH}/webrtc/include
endif

INST_TEST_RES_PATH := usr/bin/ut/res
INST_INCLUDE_PATH := usr/include/multimedia
INST_ETC_PATH := etc
INST_TEST_SH_PATH := usr/bin/ut

ifeq ($(XMAKE_ENABLE_BASE_3),true)
  LOCAL_SHARED_LIBRARIES += libbase liblog
else
  LOCAL_REQUIRED_MODULES += base
  LOCAL_LDFLAGS += -lbase -llog
endif

###initialize requre modules flags
REQUIRE_PULSEAUDIO := 0
REQUIRE_PULSEAUDIO_MODULE := 0
REQUIRE_PROPERTIES := 0
REQUIRE_LIBHYBRIS := 0
REQUIRE_LIBUV := 0
REQUIRE_LIBAV := 0
REQUIRE_LIBJPEG_TURBO := 0
REQUIRE_EXPAT := 0
REQUIRE_AUDIOSERVER := 0
REQUIRE_EXIF := 0
REQUIRE_PNG := 0
REQUIRE_GIF := 0
REQUIRE_GTEST := 0
REQUIRE_PAGEWINDOW := 0
REQUIRE_POWER := 0
REQUIRE_ZLIB := 0
REQUIRE_WAYLAND := 0
REQUIRE_SURFACE := 0
REQUIRE_WPC := 0
REQUIRE_EGL := 0
REQUIRE_COMPAT := 0
REQUIRE_CAMERASVR := 0
REQUIRE_PLUGIN_CORE := 0
REQUIRE_LIBDCE := 0
REQUIRE_LIBDRM := 0
REQUIRE_LIBMMRPC := 0
