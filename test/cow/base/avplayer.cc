/*
 *  player.c - example player for ffmpeg
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Zhao, Halley<halley.zhao@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

//  gcc player.c `pkg-config --cflags --libs libavformat libavcodec libavutil` -o player

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <gtest/gtest.h>

#ifdef __cplusplus
extern "C" {
#endif
#define __STDC_CONSTANT_MACROS
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifdef __cplusplus
}
#endif

#include <multimedia/av_ffmpeg_helper.h>
#include "multimedia/media_buffer.h"
#include "multimedia/av_buffer_helper.h"
#include "multimedia/mm_debug.h"

MM_LOG_DEFINE_MODULE_NAME("Cow-Avplayer");

using namespace YUNOS_MM;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 28, 1)
    #define av_frame_alloc avcodec_alloc_frame
    #if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(54, 28, 0)
        #define av_frame_free avcodec_free_frame
    #else
        #define av_frame_free av_freep
    #endif
#endif

static char* input_file = NULL;

class AvplayerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(AvplayerTest, avplayerTest) {
    AVCodecContext* video_dec_ctx = NULL;
    AVCodec* video_dec = NULL;
    AVPacket* pkt;
    AVFrame *frame = NULL;
    int read_eos = 0;
    int decode_count = 0;
    int render_count = 0;
    int video_stream_index = -1;
    uint32_t i = 0;
    FILE *dump_yuv = NULL;
    int ret;
    // libav* init
    av_register_all();

    // open input file
    AVFormatContext* pFormat = NULL;
    ret = avformat_open_input(&pFormat, input_file, NULL, NULL);
    ASSERT_GE(ret, 0);

    ret = avformat_find_stream_info(pFormat, NULL) ;
    ASSERT_GE(ret, 0);

    av_dump_format(pFormat,0,input_file,0);

    // find out video stream
    for (i = 0; i < pFormat->nb_streams; i++) {
        if (pFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_dec_ctx = pFormat->streams[i]->codec;
            video_stream_index = i;
            break;
        }
    }
    INFO("video_dec_ctx=%p, video_stream_index=%d\n", video_dec_ctx, video_stream_index);
    ASSERT_TRUE(video_dec_ctx && video_stream_index>=0);

    // open video codec
    video_dec = avcodec_find_decoder(video_dec_ctx->codec_id);
    ASSERT_NE(video_dec, NULL);

    ret = avcodec_open2(video_dec_ctx, video_dec, NULL);
    ASSERT_GE(ret, 0);

    // decode frames one by one
    while (1) {
        pkt = (AVPacket *)malloc(sizeof(AVPacket));
        ASSERT_NE(pkt, NULL);
        av_init_packet(pkt);
        if(read_eos == 0 && av_read_frame(pFormat, pkt) < 0) {
            read_eos = 1;
        }
        if (read_eos) {
            pkt->data = NULL;
            pkt->size = 0;
            pkt->stream_index = video_stream_index;
        }
#if 1
        MediaBufferSP media_buffer_sp = AVBufferHelper::createMediaBuffer(pkt, true);
        AVPacket *pkt_ = NULL;
        ASSERT_TRUE(AVBufferHelper::convertToAVPacket(media_buffer_sp, &pkt_));
        ASSERT_TRUE(pkt_ == pkt);
#endif
        if (pkt->stream_index == video_stream_index) {
            frame = av_frame_alloc();
            int got_picture = 0,ret = 0;
            ret = avcodec_decode_video2(video_dec_ctx, frame, &got_picture, pkt);
            if (ret < 0) { // decode fail (or decode finished)
                DEBUG("exit ...\n");
                break;
            }

            if (read_eos && ret>=0 && !got_picture) {
                DEBUG("ret=%d, exit ...\n", ret);
                break; // eos has been processed
            }

            decode_count++;
            if (got_picture) {
                // assumed I420 format
                int height[3] = {video_dec_ctx->height, video_dec_ctx->height/2, video_dec_ctx->height/2};
                int width[3] = {video_dec_ctx->width, video_dec_ctx->width/2, video_dec_ctx->width/2};
                int plane, row;

                if (!dump_yuv) {
                    char out_file[256];
                    sprintf(out_file, "./dump_%dx%d.I420", video_dec_ctx->width, video_dec_ctx->height);
                    dump_yuv = fopen(out_file, "w");
                    ASSERT_NE(dump_yuv, NULL);
                }
#if 1
                MediaBufferSP media_buffer_sp = AVBufferHelper::createMediaBuffer(frame);
                AVFrame *frame_= NULL;
                ASSERT_TRUE(AVBufferHelper::convertToAVFrame(media_buffer_sp, &frame_));
                ASSERT_TRUE(frame_ == frame);
#endif
                for (plane=0; plane<3; plane++) {
                    for (row = 0; row<height[plane]; row++)
                        fwrite(frame->data[plane]+ row*frame->linesize[plane], width[plane], 1, dump_yuv);
                }
                render_count++;
                av_frame_free(&frame);
            }
        }
        // not necessary to av_free_packet(pkt); free(pkt); the MediaBufferSP helps
    }

    if (dump_yuv)
        fclose(dump_yuv);

    INFO("decode %s ok, decode_count=%d, render_count=%d\n", input_file, decode_count, render_count);

}


int main(int argc, char **argv) {

   if (argc<2) {
        ERROR("no input file\n");
        return -1;
    }
    input_file = argv[1];

    int ret;
    MMLOGI("hello player\n");
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        ERROR("InitGoogleTest failed!");
        return -1;
   }
   return ret;
}


