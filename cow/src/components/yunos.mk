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
MM_ROOT_PATH:= $(LOCAL_PATH)/../../../

#### JpegEncoder
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= jpeg_encode_turbo.cc
LOCAL_C_INCLUDES += $(libjpeg-turbo-includes)

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDFLAGS += -lstdc++ -lpthread

REQUIRE_LIBJPEG_TURBO = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libJpegEncoder
include $(BUILD_SHARED_LIBRARY)

#### libAVDemuxer
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= av_demuxer.cc
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)

LOCAL_C_INCLUDES += \
    $(corefoundation-includes)


ifeq ($(YUNOS_SYSCAP_RESMANAGER),true)
    LOCAL_C_INCLUDES += $(data-includes)
    LOCAL_CPPFLAGS += -DUSE_RESMANAGER
else
    LOCAL_C_INCLUDES += $(resourcelocator-includes)
    REQUIRE_RESOURCELOCATOR = 1
endif

LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper libCowVideoCap
ifeq ($(YUNOS_SYSCAP_MM),true)
LOCAL_SHARED_LIBRARIES += libCowVideoCap
endif
LOCAL_LDLIBS:= -lm -lpthread -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

## unly code, remove it in the future
ifneq ($(XMAKE_PLATFORM),tv)
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
USE_CONNECTIVITY := true
ifeq ($(USE_CONNECTIVITY),true)
    LOCAL_C_INCLUDES += $(link-includes)
    LOCAL_SHARED_LIBRARIES += liblinkclient
    LOCAL_CPPFLAGS += -DUSE_CONNECTIVITY_PROXY_INFO
endif
endif
endif

LOCAL_MODULE:= libAVDemuxer
include $(BUILD_SHARED_LIBRARY)

#### libAVMuxer
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= av_muxer.cc
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS:= -lpthread -lstdc++

ifeq ($(USING_EIS),1)
    LOCAL_CFLAGS += -DHAVE_EIS_AUDIO_DELAY
endif

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAVMuxer
include $(BUILD_SHARED_LIBRARY)


#### libMediaCodecComponent
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= \
    mediacodec_component.cc  \
    mediacodec_decoder.cc    \
    mediacodec_encoder.cc    \
    mediacodec_plugin.cc     \

LOCAL_C_INCLUDES += \
    ${WINDOWSURFACE_INCLUDE} \
    ${graphics-includes} \
    ${HYBRIS_EGLPLATFORMCOMMON} \
    $(audioserver-includes) \
    $(multimedia-mediacodec-includes) \
    $(multimedia-mediacodec-nocomapt-includes) \
    $(multimedia-surfacetexture-includes)

LOCAL_SHARED_LIBRARIES += libcowbase libmediacodec_yunos libmediasurfacetexture libomx_common
LOCAL_LDLIBS:= -lpthread -lstdc++
LOCAL_CPPFLAGS += -std=c++11

LOCAL_MODULE:= libMediaCodecComponent
ifeq ($(USING_MEDIACODEC),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libVideoSinkBasic
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_CPPFLAGS += -D_CREATE_VIDEOSINKBASIC -fpermissive
LOCAL_SRC_FILES:= video_sink.cc
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS:= -lstdc++

LOCAL_MODULE:= libVideoSinkBasic
include $(BUILD_SHARED_LIBRARY)

#### libVideoSinkSurface
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive

LOCAL_C_INCLUDES += \
    $(WINDOWSURFACE_INCLUDE) \
    $(HYBRIS_EGLPLATFORMCOMMON) \
    $(libhybris-includes)

LOCAL_C_INCLUDES += $(pagewindow-includes) \
                    $(WINDOWSURFACE_INCLUDE) \
                    $(libuv-includes)

LOCAL_C_INCLUDES += $(multimedia-mediacodec-includes)       \
                    $(multimedia-surfacetexture-includes)

LOCAL_SRC_FILES:= video_sink.cc video_sink_surface.cc
LOCAL_SHARED_LIBRARIES += libcowbase libmediasurfacetexture
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
LOCAL_LDLIBS += -lstdc++

LOCAL_MODULE:= libVideoSinkSurface
ifeq ($(USING_GUI_SURFACE),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libAudioSinkPulse
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= audio_sink_pulse.cc

LOCAL_C_INCLUDES += $(pulseaudio-includes) \
                    $(audioserver-includes)
ifeq ($(ENABLE_DEFAULT_AUDIO_CONNECTION),1)
LOCAL_C_INCLUDES += ../../../ext/mm_amhelper/include
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDFLAGS += -lstdc++

REQUIRE_PULSEAUDIO = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAudioSinkPulse
ifeq ($(USING_AUDIO_PULSE),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libAudioSinkCras
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= audio_sink_cras.cc

LOCAL_C_INCLUDES += $(audioserver-includes)     \
                    $(corefoundation-includes)

LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDFLAGS += -lstdc++
REQUIRE_AUDIOSERVER = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAudioSinkCras
ifeq ($(USING_AUDIO_CRAS),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libAudioSinkLPA
ifeq ($(XMAKE_BOARD),qcom)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= audio_sink_LPA.cc

LOCAL_C_INCLUDES += $(audioserver-includes)     \
                    $(libav-includes) \
                    $(corefoundation-includes)

LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDFLAGS += -lstdc++
REQUIRE_AUDIOSERVER = 1
REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAudioSinkLPA
include $(BUILD_SHARED_LIBRARY)
endif

#### libAudioSrcCras
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= audio_src_cras.cc

LOCAL_C_INCLUDES += $(audioserver-includes)  \
                    $(corefoundation-includes)

LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDFLAGS += -lstdc++
REQUIRE_AUDIOSERVER = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAudioSrcCras
ifeq ($(USING_AUDIO_CRAS),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libAudioSrcPulse
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= audio_src_pulse.cc

LOCAL_C_INCLUDES += $(pulseaudio-includes) \
                    $(audioserver-includes)

ifeq ($(ENABLE_DEFAULT_AUDIO_CONNECTION),1)
LOCAL_C_INCLUDES += ../../../ext/mm_amhelper/include
LOCAL_SHARED_LIBRARIES += libmmamhelper
endif

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDFLAGS += -lstdc++

REQUIRE_PULSEAUDIO = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAudioSrcPulse
ifeq ($(USING_AUDIO_PULSE),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libFFmpegComponent
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:=   \
    audio_decode_ffmpeg.cc \
    audio_encode_ffmpeg.cc \
    video_decode_ffmpeg.cc \
    video_encode_ffmpeg.cc \
    ffmpeg_codec_plugin.cc

LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS += -lpthread -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libFFmpegComponent
include $(BUILD_SHARED_LIBRARY)

#### libAudioCodecOpus
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:=   \
    audio_codec_opus.cc \

LOCAL_C_INCLUDES += $(YUNOS_ROOT)/third_party/opus/include

LOCAL_SHARED_LIBRARIES += libcowbase libopus
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_MODULE:= libAudioCodecOpus
ifeq ($(USING_OPUS_CODEC),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libFakeSink
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= fake_sink.cc
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_MODULE:= libFakeSink
include $(BUILD_SHARED_LIBRARY)

#### libVideoFilterExample
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= video_filter_example.cc
LOCAL_SHARED_LIBRARIES += libcowbase libgui
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
LOCAL_SHARED_LIBRARIES += libhardware
endif
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_C_INCLUDES +=     $(graphics-includes)       \
                        $(hal-includes)            \
                        $(WINDOWSURFACE_INCLUDE)   \

LOCAL_MODULE:= libVideoFilterExample
ifeq ($(USING_GUI_SURFACE),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libVideoTestSource
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES := video_test_source.cc \

ifneq ($(XMAKE_PLATFORM),ivi)
    ifeq ($(USING_GUI_SURFACE),1)
    LOCAL_SRC_FILES += $(MM_EXT_PATH)/native_surface_help.cc \
                   $(MM_EXT_PATH)/WindowSurface/WindowSurfaceTestWindow.cc
    endif
endif
# LOCAL_SRC_FILES += $(MM_HELP_WINDOWSURFACE_SRC_FILES)

LOCAL_C_INCLUDES += \
    $(MM_HELP_WINDOWSURFACE_INCLUDE) \
    $(pagewindow-includes)       \
    $(MM_MEDIACODEC_INCLUDE)     \
    $(multimedia-surfacetexture-includes)

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof
LOCAL_CPPFLAGS += -std=c++11

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase libmediasurfacetexture libhal

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += \
        $(HYBRIS_EGLPLATFORMCOMMON)  \
        $(libhybris-includes)
    REQUIRE_LIBHYBRIS = 1
endif

REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := libVideoTestSource
include $(BUILD_SHARED_LIBRARY)

#### libVideoSourceFile
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES := video_source_file.cc

LOCAL_C_INCLUDES += $(MM_HELP_WINDOWSURFACE_INCLUDE)
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += $(HYBRIS_EGLPLATFORMCOMMON)
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
    LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof
endif
LOCAL_LDLIBS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase

LOCAL_MODULE := libVideoSourceFile
include $(BUILD_SHARED_LIBRARY)

#### vpe filter
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk

LOCAL_SRC_FILES:= vpe_filter.cc csc_filter.cc

LOCAL_C_INCLUDES+=  ${TI-LIBDCE-INCLUDES}    \
                    $(ti-ipc-includes)       \
                    $(libdrm-includes)       \
                    ${WINDOWSURFACE_INCLUDE} \
                    $(libav-includes)        \
                    $(graphics-includes)     \
                    $(wayland-includes)


LOCAL_SHARED_LIBRARIES += libmmbase
LOCAL_LDFLAGS += -lstdc++ -lpthread

REQUIRE_LIBAV = 1
REQUIRE_LIBDCE = 1
REQUIRE_LIBDRM = 1
REQUIRE_LIBMMRPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libCscFilter
ifeq ($(USING_VIDEOSOURCE_SW_CSC),1)
include $(BUILD_SHARED_LIBRARY)
endif

### libVideoSourceSurface
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES := video_source_surface.cc    \
                   video_consumer_base.cc

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
LOCAL_SRC_FILES += video_consumer_droid.cc
else
    ifeq ($(USING_DRM_SURFACE),1)
    LOCAL_SRC_FILES += video_consumer_drm.cc
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/cow/src/platform/ivi/
    LOCAL_SHARED_LIBRARIES += libCscFilter
    else
    LOCAL_SRC_FILES += video_consumer.cc

    LOCAL_CXXFLAGS += -DYUN_HAL
    endif

    LOCAL_C_INCLUDES += $(graphics-includes)  \
                        $(gui-includes)
endif

LOCAL_C_INCLUDES += \
    $(WINDOWSURFACE_INCLUDE)     \
    $(HYBRIS_EGLPLATFORMCOMMON)  \

#LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof

LOCAL_LDFLAGS += -lpthread -ldl -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase

REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
REQUIRE_WPC = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := libVideoSourceSurface
ifeq ($(XMAKE_ENABLE_UNIFIED_SURFACE),false)
include $(BUILD_SHARED_LIBRARY)
endif

#### libVideoSourceCamera
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES := video_capture_source.cc \
                   video_capture_source_time_lapse.cc \
                   video_source_camera.cc

export SOURCE_BUFFER_DUMP=0
ifeq ($(SOURCE_BUFFER_DUMP),1)
LOCAL_CPPFLAGS += -D__SOURCE_BUFFER_DUMP__
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
LOCAL_C_INCLUDES += \
    $(libhybris-includes)      \
    $(corefoundation-includes) \
    $(cameraserver-includes)   \
    $(graphics-includes)       \
    $(hal-includes)            \
    $(WINDOWSURFACE_INCLUDE)   \
    $(multimedia-mediacodec-nocomapt-includes) \
    $(multimedia-mediacodec-includes)

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof
ifeq ($(SOURCE_BUFFER_DUMP),1)
    LOCAL_SHARED_LIBRARIES += libhardware
endif

else
LOCAL_C_INCLUDES += $(WINDOWSURFACE_INCLUDE) \
                    $(cameraserver-includes)   \
                    $(corefoundation-includes)

ifeq ($(SOURCE_BUFFER_DUMP),1)
    REQUIRE_SURFACE = 1
endif
endif

ifeq ($(USING_EIS),1)
    LOCAL_CFLAGS += -DHAVE_EIS_AUDIO_DELAY
endif

LOCAL_CPPFLAGS += \
    -DHAVE_POSIX \
    -DCRAS_AUDIO_UIO \
    -DHAVE_PRCTL \
    -std=c++11

LOCAL_LDLIBS += -ldl -lpthread -lstdc++

LOCAL_SHARED_LIBRARIES += libcowbase libpthread-stubs

REQUIRE_CAMERASVR = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := libVideoSourceCamera
ifeq ($(USING_CAMERA),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libFileSink
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= file_sink.cc

LOCAL_CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_MODULE:= libFileSink
include $(BUILD_SHARED_LIBRARY)

#### libAudioSrcFile
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)
LOCAL_C_INCLUDES += $(audioserver-includes)

LOCAL_SRC_FILES:= audio_source_file.cc
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_MODULE:= libAudioSrcFile
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
include $(BUILD_SHARED_LIBRARY)
endif

#### libMediaFission
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES := media_fission.cc
LOCAL_LDLIBS += -lpthread -lstdc++
LOCAL_SHARED_LIBRARIES += libcowbase

LOCAL_MODULE := libMediaFission
include $(BUILD_SHARED_LIBRARY)

#### libVideoDecodeV4l2
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES :=  v4l2codec_device.cc      \
                    video_decode_v4l2.cc     \
                    ../make_csd.cc  ## move it to libmmbase after resolve media_codec_common.h bytebuffer issue

ifeq ($(XMAKE_BOARD),sprd)
LOCAL_SRC_FILES += \
    $(MM_EXT_PATH)/WindowSurface/WindowSurfaceTestWindow.cc
endif

LOCAL_C_INCLUDES += $(MM_HELP_WINDOWSURFACE_INCLUDE) \
                    $(graphics-includes)   \
                    $(multimedia-surfacetexture-includes)    \
                    $(MM_MEDIACODEC_INCLUDE) \
                    $(wayland-includes)     \
                    ${gui-includes}         \
                    $(wpc-includes)         \
                    $(weston-includes)      \
                    $(yunhal-includes)      \

ifeq ($(XMAKE_BOARD),intel)
LOCAL_C_INCLUDES += $(kernel-includes)
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += \
        $(HYBRIS_EGLPLATFORMCOMMON)         \
        $(libhybris-includes)               \
        $(pagewindow-includes)
endif

ifeq ($(XMAKE_PLATFORM),ivi)
    LOCAL_C_INCLUDES += $(MM_ROOT_PATH)/cow/src/platform/ivi/
endif

LOCAL_LDLIBS += -lpthread -ldl -lstdc++
#LOCAL_CFLAGS_REMOVED:= -march=armv7-a
LOCAL_CXXFLAGS += -std=c++11
LOCAL_SHARED_LIBRARIES += libcowbase libmediasurfacetexture
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif

REQUIRE_WPC = 1

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
    LOCAL_CPPFLAGS += -D_USE_PLUGIN_BUFFER_HANDLE
    LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof

    REQUIRE_LIBHYBRIS = 1
else
    LOCAL_LDLIBS += -lwayland-client
endif
REQUIRE_WAYLAND = 1
REQUIRE_SURFACE = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := libVideoDecodeV4l2
ifeq ($(USING_V4L2CODEC),1)
include $(BUILD_SHARED_LIBRARY)
endif

#### libVideoEncodeV4l2
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES :=  v4l2codec_device.cc      \
                    video_encode_v4l2.cc     \

LOCAL_C_INCLUDES += $(MM_HELP_WINDOWSURFACE_INCLUDE) \
                    $(graphics-includes)   \
                    $(MM_MEDIACODEC_INCLUDE) \
                    $(yunhal-includes)

LOCAL_C_INCLUDES+=  $(libdrm-includes)      \

ifeq ($(XMAKE_BOARD),intel)
LOCAL_C_INCLUDES += $(kernel-includes)
endif

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_C_INCLUDES += \
        $(HYBRIS_EGLPLATFORMCOMMON)         \
        $(libhybris-includes)               \
        $(pagewindow-includes)
endif

LOCAL_LDLIBS += -lpthread -ldl -lstdc++
LOCAL_CPPFLAGS += -std=c++11
LOCAL_SHARED_LIBRARIES += libcowbase
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif

REQUIRE_WPC = 1

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\"" -fpermissive
    LOCAL_CPPFLAGS += -D_USE_PLUGIN_BUFFER_HANDLE
    LOCAL_CPPFLAGS += -Wno-deprecated-declarations -Wno-invalid-offsetof

    REQUIRE_LIBHYBRIS = 1
    REQUIRE_SURFACE = 1
endif
REQUIRE_SURFACE = 1
ifeq ($(USING_DRM_SURFACE),1)
REQUIRE_LIBDCE = 1
REQUIRE_LIBDRM = 1
endif
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE := libVideoEncodeV4l2
ifeq ($(USING_V4L2CODEC),1)
include $(BUILD_SHARED_LIBRARY)
endif

########libRtpMuxer
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= rtp_muxer.cc
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)  \
                    $(MM_WFD_INCLUDE)
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS:= -lpthread -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libRtpMuxer
include $(BUILD_SHARED_LIBRARY)

########libRtpdemuxer
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)

LOCAL_SRC_FILES:= rtp_demuxer.cc
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS += -lpthread -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libRtpDemuxer
include $(BUILD_SHARED_LIBRARY)

#### libAPPPlaySource
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= app_play_source.cc
LOCAL_C_INCLUDES +=
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS:= -lm -lstdc++

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAPPPlaySource
include $(BUILD_SHARED_LIBRARY)

#### libAPPSink
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= app_sink.cc
LOCAL_C_INCLUDES +=
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS:= -lm -lstdc++

include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libAPPSink
include $(BUILD_SHARED_LIBRARY)

#### libSubtitleSink
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_C_INCLUDES += \
    $(multimedia-mediakeeper-includes)

LOCAL_SRC_FILES:= subtitle_sink.cc \
                  mediacollector_char_converter.cc
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_MODULE:= libSubtitleSink
LOCAL_LDLIBS:= -lstdc++
include $(BUILD_SHARED_LIBRARY)

#### libSubtitleSource
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= subtitle_source.cc
LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS:= -lstdc++

LOCAL_MODULE:= libSubtitleSource
include $(BUILD_SHARED_LIBRARY)

#### libExternalCaptureSource
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= external_capture_source.cc
LOCAL_C_INCLUDES += $(libav-includes) \
                    $(audioserver-includes)
LOCAL_SHARED_LIBRARIES += libcowbase libcow-avhelper
LOCAL_LDLIBS:= -lm -lpthread -lstdc++

REQUIRE_LIBAV = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libExternalCaptureSource
include $(BUILD_SHARED_LIBRARY)

