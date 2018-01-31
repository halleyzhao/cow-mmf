/**
 * Copyright (C) 2017 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <gtest/gtest.h>
#include <glib.h>

#include <multimedia/component.h>
#include "multimedia/media_meta.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "components/av_demuxer.h"
#include "components/video_decode_ffmpeg.h"
#include "components/file_sink.h"

MM_LOG_DEFINE_MODULE_NAME("VIDEO-DECODE-FFMPEG");

using namespace YUNOS_MM;

static const char *input_url = NULL;
static const char *output_url = NULL;
static int32_t g_width = 176;
static int32_t g_height = 144;


static GOptionEntry entries[] = {
    {"inputfile", 'f', 0, G_OPTION_ARG_STRING, &input_url, " set input media file url", NULL},
    {"outfile", 'o', 0, G_OPTION_ARG_STRING, &output_url, "set output media file url ", NULL},
    {"width", 'w', 0, G_OPTION_ARG_INT, &g_width, "set video surface width", NULL},
    {"height", 'h', 0, G_OPTION_ARG_INT, &g_height, "set video surface height", NULL},
    {NULL}
};

class VideoFfmpegDecodeTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(VideoFfmpegDecodeTest, videoffmpegdTest) {
    ComponentSP          decoderSP;
    ComponentSP         sink;
    //#0 parse the command
#ifdef _VIDEO_CODEC_FFMPEG
    PlaySourceComponent *source;
    mm_status_t status;

    MediaMetaSP paramSP(MediaMeta::create());
    //#1 create the source plugin
    source = new AVDemuxer;
    status = source->init();
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);
    int i = 0;
    for(i = 0; i < 200000; i++)
        AVDemuxer::AVCodecId2CodecId(i);

    if (input_url)
        source->setUri(input_url);
    INFO("source had been created,input_url:%s\n",input_url);

    //#2 create the decoder plugin
    // decoderSP = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_VIDEO_AVC, false);
    decoderSP.reset(new VideoDecodeFFmpeg());
    ASSERT_NE(decoderSP.get(), NULL);

    status = decoderSP->init();
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);

    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_WIDTH, g_width);
    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_HEIGHT,g_height);
    paramSP->setInt32(YUNOS_MM::MEDIA_ATTR_COLOR_FORMAT,(int)AV_PIX_FMT_YUV420P);
    status = decoderSP->setParameter(paramSP);
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);

   INFO("decoder had been created\n");

    //#3 create the file sink plugin
    sink = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_MEDIA_FILE_SINK, false);
    if (output_url) {
       paramSP->setString(YUNOS_MM::MEDIA_ATTR_FILE_PATH,output_url);
       status = sink->setParameter(paramSP);
       ASSERT_TRUE(status == MM_ERROR_SUCCESS);
   }
    INFO("sink had been created\n");

   //#4 source will to setup
   status = source->prepare();
   if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("prepare source ASYNC");
        usleep(100*1000);
    }
   status = source->start();
   if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("start source ASYNC");
        usleep(100*1000);
   }
   INFO("source had been setup\n");

   //#5 sink will to setup
  status = sink->start();
    if (status != MM_ERROR_SUCCESS) {
       ASSERT_TRUE(status == MM_ERROR_ASYNC);
       INFO("start source ASYNC");
       usleep(100*1000);
    }
    INFO("sink had been setup\n");

    //#6 add source && add sink
   status = decoderSP->addSource(source, Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("decoder add source ASYNC");
        usleep(100*1000);
    }
    INFO("decoder had been addSource\n");

   status = decoderSP->addSink(sink.get(), Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("decoder add sink ASYNC");
        usleep(100*1000);
    }
    INFO("decoder had been addSink\n");

    //#7 decoder will to setup
    status = decoderSP->prepare();
    if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("decoder prepare ASYNC, wait 10ms for prepare completion");
        usleep(100*1000);
    }
    INFO("decoder had been prepared\n");

    status = decoderSP->start();
    if (status != MM_ERROR_SUCCESS) {
        ASSERT_TRUE(status == MM_ERROR_ASYNC);
        INFO("decoder start ASYNC");
    }

    INFO("wait to end \n");
    sleep(10);

    decoderSP->stop();
    usleep(100*1000);

    decoderSP->reset();
    usleep(100*1000);

    decoderSP->uninit();
    usleep(100*1000);

    source->stop();
    source->uninit();
    sink->stop();
    sink->uninit();
    delete source;
    sink.reset();
#endif

}

int main(int argc, char* const argv[]) {
    if (argc < 3) {
        PRINTF("Must set inputfile and outputfile \n");
        return -1;
    }

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (char ***)&argv, &error)) {
            PRINTF("option parsing failed: %s\n", error->message);
            return -1;
    }
    g_option_context_free(context);
    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        PRINTF("InitGoogleTest failed!");
        ret = -1;
    }
    if (input_url)
        g_free(const_cast<char*>(input_url));
    if(output_url)
        g_free(const_cast<char*>(output_url));

    return ret;

}

