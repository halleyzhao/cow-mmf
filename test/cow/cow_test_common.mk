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

###cow test moudles common defines
LOCAL_INCLUDES:=${MULTIMEDIA_BASE}/include                  \
                ${MULTIMEDIA_BASE}/base/include             \
                ${MULTIMEDIA_BASE}/cow/include              \
                ${MULTIMEDIA_BASE}/cow/src/components       \
                ${MULTIMEDIA_BASE}/cow/src

LOCAL_RPATH:=-Wl,-rpath,${COW_PLUGIN_PATH}

LOCAL_CPPFLAGS += -D_VIDEO_CODEC_FFMPEG
LOCAL_LDFLAGS:= -L$(OUT_LIB_PATH)  -L$(OUT_LIB_PATH)/cow -lgtest_main -lgtest -lpthread
LOCAL_SHARED_LIBRARIES := mmbase cowbase dl stdc++ pthread
LOCAL_STATIC_LIBRARIES :=

MODULE_TYPE := eng
