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

#### libVideoFilterGL
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../cow/build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= video_filter_gl.cc egl_gl_fbo.cc

LOCAL_SHARED_LIBRARIES += libcowbase
LOCAL_LDLIBS += -lpthread -lstdc++

LOCAL_C_INCLUDES +=     $(graphics-includes)       \
                        $(hal-includes)            \
                        $(WINDOWSURFACE_INCLUDE)   \

REQUIRE_WAYLAND = 1
REQUIRE_WPC = 1
REQUIRE_EGL = 1
REQUIRE_SURFACE = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk

LOCAL_MODULE:= libVideoFilterGL
include $(BUILD_SHARED_LIBRARY)

#### libVideoFilterCL
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../cow/build/cow_common.mk
LOCAL_MODULE_PATH = $(COW_PLUGIN_PATH)

LOCAL_SRC_FILES:= video_filter_cl.cc cl_filter.cc

LOCAL_SHARED_LIBRARIES += libcowbase
## FIXME, uses LOCAL_SHARED_LIBRARIES instead of -lOpenCL
LOCAL_LDLIBS += -lpthread -lstdc++ -lOpenCL

LOCAL_C_INCLUDES +=     $(graphics-includes)        \
                        $(hal-includes)             \
                        $(WINDOWSURFACE_INCLUDE)    \
                        $(YUNOS_ROOT)/yunhal/droid/graphics/opencl

# REQUIRE_WAYLAND = 1
# REQUIRE_WPC = 1
# REQUIRE_EGL = 1
# REQUIRE_SURFACE = 1
include $(MM_ROOT_PATH)/base/build/xmake_req_libs.mk
LOCAL_SHARED_LIBRARIES += libgfx-cutils

LOCAL_SRC_FILES += buffer_pool_surface.cc
LOCAL_C_INCLUDES += $(multimedia-surfacetexture-includes)
LOCAL_SHARED_LIBRARIES += libmediasurfacetexture

LOCAL_MODULE:= libVideoFilterCL
include $(BUILD_SHARED_LIBRARY)


#### example_cow_components_prebuilt
include $(CLEAR_VARS)
LOCAL_MODULE := example_cow_components_prebuilt
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(LOCAL_PATH)/cow_plugins_example.xml:$(INST_ETC_PATH)/cow_plugins_example.xml
include $(BUILD_MULTI_PREBUILT)

