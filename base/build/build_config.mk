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

## feature set
export USING_PLAYBACK_CHANNEL=0
export USING_PLAYBACK_CHANNEL_FUSION=0
export USING_OMX_SERVICE=0
export USING_EIS=0
export USING_MEDIA_DASH=0
# cow feature/components
ifneq ($(XMAKE_PLATFORM),ivi)
export USING_AUDIO_CRAS=1
LOCAL_CPPFLAGS += -D__AUDIO_CRAS__
endif
export USING_OPUS_CODEC=0
export USING_UV_STRIDE16=0
export USING_GUI_SURFACE=1
export USING_CAMERA=1
export USING_MEDIACODEC=0
export USING_V4L2CODEC=0
ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    export USING_MEDIACODEC=1
else
    export USING_V4L2CODEC=1
endif
export USING_PLAYER_SERVICE=1
export USING_RECORDER_SERVICE=1
export USING_CAMERA_API2=1
export USING_VR_VIDEO=1
export USING_SOFT_VIDEO_CODEC_FOR_MS=1
## create Surface from BQ (instead of create Surface directly), then we can attach buffer to BQ to render buffers
export USING_SURFACE_FROM_BQ=0
export USING_YUNOS_MODULE_LOAD_FW=1
export USING_DEVICE_MANAGER=0
export USING_VIDEOSOURCE_SW_CSC=0
#ivi, drm
export USING_DRM_SURFACE=0
#ivi, vpu
export USING_VPU_SURFACE=0
# camera api version used since pad
MM_USE_CAMERA_VERSION_010:=10
# camera api version used since ocean
MM_USE_CAMERA_VERSION_020:=20
# camera api version used since sprd
MM_USE_CAMERA_VERSION_030:=30
MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_020)
# audio api version used since pad
MM_USE_AUDIO_VERSION_010:=10
# audio api version used since ocean
MM_USE_AUDIO_VERSION_020:=20
# audio api version used since sprd
MM_USE_AUDIO_VERSION_030:=30
MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_020)
# USB camera
export USING_USB_CAMERA=0

## set width/height
MM_UT_SURFACE_WIDTH=640
MM_UT_SURFACE_HEIGHT=480
##define support video max width/height
MM_VIDEO_MAX_WIDTH=10000
MM_VIDEO_MAX_HEIGHT=10000

##define screen resolution
MM_SCREEN_WIDTH=1920
MM_SCREEN_HEIGHT=1080

ifeq ($(YUNOS_SYSCAP_MM),true)

ifeq ($(YUNOS_SYSCAP_MM.USING_PLAYBACK_CHANNEL),true)
    export USING_PLAYBACK_CHANNEL=1
else
    export USING_PLAYBACK_CHANNEL=0
endif

ifeq ($(YUNOS_SYSCAP_MM.USING_PLAYBACK_CHANNEL_FUSION),true)
    export USING_PLAYBACK_CHANNEL_FUSION=1
else
    export USING_PLAYBACK_CHANNEL_FUSION=0
endif

ifeq ($(YUNOS_SYSCAP_MM.USING_EIS),true)
    export USING_EIS=1
else
    export USING_EIS=0
endif

ifeq ($(YUNOS_SYSCAP_MM.USING_CAMERA),true)
    export USING_CAMERA=1
else
    export USING_CAMERA=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_CAMERA_API2),true)
    export USING_CAMERA_API2=1
else
    export USING_CAMERA_API2=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_MEDIA_DASH),true)
    export USING_MEDIA_DASH=1
else
    export USING_MEDIA_DASH=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_AUDIO_CRAS),true)
    export USING_AUDIO_CRAS=1
else
    export USING_AUDIO_CRAS=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_OPUS_CODEC),true)
    export USING_OPUS_CODEC=1
else
    export USING_OPUS_CODEC=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_UV_STRIDE16),true)
    export USING_UV_STRIDE16=1
else
    export USING_UV_STRIDE16=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_GUI_SURFACE),true)
    export USING_GUI_SURFACE=1
else
    export USING_GUI_SURFACE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_SURFACE_FROM_BQ),true)
    export USING_SURFACE_FROM_BQ=1
else
    export USING_SURFACE_FROM_BQ=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_MEDIACODEC),true)
    export USING_MEDIACODEC=1
else
    export USING_MEDIACODEC=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_V4L2CODEC),true)
    export USING_V4L2CODEC=1
else
    export USING_V4L2CODEC=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_OMX_SERVICE),true)
    export USING_OMX_SERVICE=1
else
    export USING_OMX_SERVICE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_VL42_SERVICE),true)
    export USING_VL42_SERVICE=1
else
    export USING_VL42_SERVICE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_PLAYER_SERVICE),true)
    export USING_PLAYER_SERVICE=1
else
    export USING_PLAYER_SERVICE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_RECORDER_SERVICE),true)
    export USING_RECORDER_SERVICE=1
else
    export USING_RECORDER_SERVICE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_VR_VIDEO),true)
    export USING_VR_VIDEO=1
else
    export USING_VR_VIDEO=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_SOFT_VIDEO_CODEC_FOR_MS),true)
    export USING_SOFT_VIDEO_CODEC_FOR_MS=1
else
    export USING_SOFT_VIDEO_CODEC_FOR_MS=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_YUNOS_MODULE_LOAD_FW),true)
    export USING_YUNOS_MODULE_LOAD_FW=1
else
    export USING_YUNOS_MODULE_LOAD_FW=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_DEVICE_MANAGER),true)
    export USING_DEVICE_MANAGER=1
else
    export USING_DEVICE_MANAGER=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_VIDEOSOURCE_SW_CSC),true)
    export USING_VIDEOSOURCE_SW_CSC=1
else
    export USING_VIDEOSOURCE_SW_CSC=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_DRM_SURFACE),true)
    export USING_DRM_SURFACE=1
else
    export USING_DRM_SURFACE=0
endif
ifeq ($(YUNOS_SYSCAP_MM.USING_VPU_SURFACE),true)
    export USING_VPU_SURFACE=1
else
    export USING_VPU_SURFACE=0
endif

ifeq ($(YUNOS_SYSCAP_MM.CAMERA_VERSION_30),true)
    MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_030)
endif

ifeq ($(YUNOS_SYSCAP_MM.CAMERA_VERSION_20),true)
    MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_020)
endif

ifeq ($(YUNOS_SYSCAP_MM.CAMERA_VERSION_10),true)
    MM_USE_CAMERA_VERSION:=$(MM_USE_CAMERA_VERSION_010)
endif
## audio version
ifeq ($(YUNOS_SYSCAP_MM.AUDIO_VERSION_30),true)
    MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_030)
endif

ifeq ($(YUNOS_SYSCAP_MM.AUDIO_VERSION_20),true)
    MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_020)
endif

ifeq ($(YUNOS_SYSCAP_MM.AUDIO_VERSION_10),true)
    MM_USE_AUDIO_VERSION:=$(MM_USE_AUDIO_VERSION_010)
endif

ifeq ($(YUNOS_SYSCAP_MM.USING_USB_CAMERA),true)
    export USING_USB_CAMERA=1
else
    export USING_USB_CAMERA=0
endif

endif
