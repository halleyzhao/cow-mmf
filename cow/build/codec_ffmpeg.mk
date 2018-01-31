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

MULTIMEDIA_BASE:=../../
BASE_BUILD_DIR:=$(MULTIMEDIA_BASE)/base/build
include $(BASE_BUILD_DIR)/reset_args

SRC_PATH := ../src

LOCAL_INCLUDES := $(MM_INCLUDE) \
                  ../src

LOCAL_SRC_FILES := $(SRC_PATH)/components/audio_decode_ffmpeg.cc   \
                   $(SRC_PATH)/components/audio_encode_ffmpeg.cc   \
                   $(SRC_PATH)/components/ffmpeg_codec_plugin.cc   \
                   $(SRC_PATH)/components/video_decode_ffmpeg.cc   \
                   $(SRC_PATH)/components/video_encode_ffmpeg.cc

LOCAL_CPPFLAGS += -D_VIDEO_CODEC_FFMPEG
LOCAL_CPPFLAGS:=`pkg-config --cflags libavformat libavcodec libavutil libswresample libswscale`
LOCAL_LDFLAGS:=`pkg-config --libs libavformat libavcodec libavutil libswresample libswscale`
LOCAL_INSTALL_PATH := $(INST_LIB_PATH)/cow

LOCAL_SHARED_LIBRARIES := mmbase cowbase cow-avhelper

LOCAL_MODULE := libFFmpegComponent.so

MODULE_TYPE := usr
include $(BASE_BUILD_DIR)/build_shared
