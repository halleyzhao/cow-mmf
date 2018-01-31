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

###cow test moudles
all:
	make -C avmuxer -f avmuxer_test.mk
	make -C avdemuxer -f avdemuxer_test.mk
	make -C base -f factory_test.mk
	make -C base -f buffer_test.mk
	make -C base -f clock_test.mk
	make -C base -f meta_test.mk
	make -C base -f monitor_test.mk
	make -C recorder -f cowrecorder_test.mk
	make -C player -f cowplayer_test.mk
	make -C player -f cow_audioplayer_test.mk
	make -C video-ffmpeg -f video_ffmpeg_dtest.mk
	make -C video-ffmpeg -f video_ffmpeg_etest.mk
	make -C rtpmuxer -f rtpmuxer_test.mk

clean:
	make clean -C avmuxer -f avmuxer_test.mk
	make clean -C avdemuxer -f avdemuxer_test.mk
	make clean -C base -f factory_test.mk
	make clean -C base -f buffer_test.mk
	make clean -C base -f clock_test.mk
	make clean -C base -f meta_test.mk
	make clean -C base -f monitor_test.mk
	make clean -C recorder -f cowrecorder_test.mk
	make clean -C player -f cowplayer_test.mk
	make clean -C player -f cow_audioplayer_test.mk
	make clean -C video-ffmpeg -f video_ffmpeg_dtest.mk
	make clean -C video-ffmpeg -f video_ffmpeg_etest.mk
	make clean -C rtpmuxer -f rtpmuxer_test.mk

install:
	make install -C avmuxer -f avmuxer_test.mk
	make install -C avdemuxer -f avdemuxer_test.mk
	make install -C base -f factory_test.mk
	make install -C base -f buffer_test.mk
	make install -C base -f clock_test.mk
	make install -C base -f meta_test.mk
	make install -C base -f monitor_test.mk
	make install -C recorder -f cowrecorder_test.mk
	make install -C player -f cowplayer_test.mk
	make install -C player -f cow_audioplayer_test.mk
	make install -C video-ffmpeg -f video_ffmpeg_dtest.mk
	make install -C video-ffmpeg -f video_ffmpeg_etest.mk
	make install -C rtpmuxer -f rtpmuxer_test.mk
