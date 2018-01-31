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

###cow moudles
all:
	make -C build -f cowbase.mk
	make -C build -f cow_av_helper.mk
	make -C build -f cowplayer.mk
	make -C build -f cowrecorder.mk
	make -C build -f av_demuxer.mk
	make -C build -f fake_sink.mk
	make -C build -f video_decode_v4l2.mk
	make -C build -f video_source.mk
	make -C build -f video_encode_v4l2.mk
	make -C build -f av_muxer.mk
	make -C build -f file_sink.mk
	make -C build -f audio_source_file.mk
	make -C build -f audio_sink_pulse.mk
	make -C build -f audio_source_pulse.mk
	make -C build -f codec_ffmpeg.mk
	make -C build -f video_source_uvc.mk
	make -C build -f jpeg_encode.mk
	make -C build -f video_sink.mk
	make -C build -f video_sink_x11.mk
	make -C build -f video_sink_egl.mk
	make -C build -f rtp_muxer.mk
clean:
	make clean -C build -f cowbase.mk
	make clean -C build -f cow_av_helper.mk
	make clean -C build -f cowplayer.mk
	make clean -C build -f cowrecorder.mk
	make clean -C build -f av_demuxer.mk
	make clean -C build -f fake_sink.mk
	make clean -C build -f video_decode_v4l2.mk
	make clean -C build -f video_source.mk
	make clean -C build -f video_encode_v4l2.mk
	make clean -C build -f av_muxer.mk
	make clean -C build -f file_sink.mk
	make clean -C build -f audio_source_file.mk
	make clean -C build -f audio_sink_pulse.mk
	make clean -C build -f audio_source_pulse.mk
	make clean -C build -f codec_ffmpeg.mk
	make clean -C build -f video_source_uvc.mk
	make clean -C build -f jpeg_encode.mk
	make clean -C build -f video_sink.mk
	make clean -C build -f video_sink_egl.mk
	make clean -C build -f video_sink_x11.mk
	make clean -C build -f rtp_muxer.mk

install:
	make install -C build -f cowbase.mk
	make install -C build -f cow_av_helper.mk
	make install -C build -f cowplayer.mk
	make install -C build -f cowrecorder.mk
	make install -C build -f av_demuxer.mk
	make install -C build -f fake_sink.mk
	make install -C build -f video_decode_v4l2.mk
	make install -C build -f video_source.mk
	make install -C build -f video_encode_v4l2.mk
	make install -C build -f av_muxer.mk
	make install -C build -f file_sink.mk
	make install -C build -f audio_source_file.mk
	make install -C build -f audio_sink_pulse.mk
	make install -C build -f audio_source_pulse.mk
	make install -C build -f codec_ffmpeg.mk
	make install -C build -f video_source_uvc.mk
	make install -C build -f jpeg_encode.mk
	make install -C build -f video_sink.mk
	make install -C build -f video_sink_x11.mk
	make install -C build -f video_sink_egl.mk
	make install -C build -f rtp_muxer.mk

