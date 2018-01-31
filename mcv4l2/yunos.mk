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

ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)

LOCAL_PATH:=$(call my-dir)
MM_ROOT_PATH:= $(LOCAL_PATH)/../

#### libmediacodec_v4l2.so
include $(CLEAR_VARS)
include $(MM_ROOT_PATH)/base/build/build.mk

LOCAL_SRC_FILES:= \
    ./src/v4l2codec_device.cc     \
    ./src/media_codec_v4l2.cc


LOCAL_SRC_FILES += \
    $(YUNOS_ROOT_ORIGIN)/framework/libs/multimedia/mediacodec/src/media_notify.cc


LOCAL_C_INCLUDES += ./src/                      \
                    $(MM_INCLUDE)               \
                    $(MM_HELP_WINDOWSURFACE_INCLUDE) \
                    $(HYBRIS_EGLPLATFORMCOMMON)     \
                    $(YUNOS_ROOT)/framework/libs/multimedia/mediacodec/src

LOCAL_CPPFLAGS += -DMP_HYBRIS_EGLPLATFORM="\"wayland\""
LOCAL_CPPFLAGS += -D_USE_PLUGIN_BUFFER_HANDLE
LOCAL_SHARED_LIBRARIES += libmmbase
ifeq ($(USING_YUNOS_MODULE_LOAD_FW),1)
LOCAL_SHARED_LIBRARIES += libhal
endif
LOCAL_LDFLAGS += -lstdc++ -lpthread

REQUIRE_LIBHYBRIS = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libmediacodec_v4l2
include $(BUILD_SHARED_LIBRARY)

endif
