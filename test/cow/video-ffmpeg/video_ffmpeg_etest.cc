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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <stdint.h>
#include <cstdlib>
#include <glib.h>
#include <gtest/gtest.h>

#include <multimedia/component.h>
#include "multimedia/media_meta.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_cpp_utils.h"
#include "components/file_sink.h"


MM_LOG_DEFINE_MODULE_NAME("VideoFFmpegEncoderTest");

static const char *g_output_url = NULL;
static const char *g_input_url = NULL;
static const char *g_filemode = NULL;
static const char *g_sizestr = NULL;

static int32_t g_bitrate = 100*1000;
static int32_t g_fps = 30;

static int32_t g_input_width = 320;
static int32_t g_input_height = 240;
static int32_t g_input_format = 'YV12';

static int32_t g_output_width = 320;
static int32_t g_output_height = 240;
static int32_t g_output_format =  'YV12';

static bool g_quit = false;
static int g_timeout = 0;

namespace YUNOS_MM {

inline void printMsgInfo(int event, int param1, const char* _sender=NULL)
{
    const char* sender = _sender ? _sender : "--";

    if (event>=0 && event <Component::kEventInfo)
        PRINTF("got event %s from %s\n", Component::sEventStr[event], sender);
    else if (event == Component::kEventInfo && (param1 >=Component::kEventInfoDiscontinuety && param1 <= Component::kEventInfoMediaRenderStarted))
        PRINTF("got kEventInfo param1=%s from %s\n", Component::sEventInfoStr[param1], sender);
    else
        PRINTF("got event=%d, param1=%d from %s\n", event, param1, sender);

}

class ListenerHelper : public Component::Listener
{
  public:
    ListenerHelper() : mContinueCondition(mLock)
    {
    }
    ~ListenerHelper()
    {
    }

    void onMessage(int msg, int param1, int param2, const MMParamSP obj, const Component * sender)
    {

        MMAutoLock locker(mLock);
        mContinueCondition.broadcast();

        printMsgInfo(msg, param1, sender->name());

    }

    void wait()
    {
        MMAutoLock locker(mLock);
        mContinueCondition.wait();
    }


  private:
    Lock mLock;
    Condition mContinueCondition;

    MM_DISALLOW_COPY(ListenerHelper)
};

}

using namespace YUNOS_MM;

static bool parseWidthHeight(const char* str, int32_t* pWidth,
        int32_t* pHeight) {
    long width, height;
    char* end;

    // Must specify base 10, or "0x0" gets parsed differently.
    width = strtol(str, &end, 10);
    if (end == str || *end != 'x' || *(end+1) == '\0') {
        // invalid chars in width, or missing 'x', or missing height
        return false;
    }
    height = strtol(end + 1, &end, 10);
    if (*end != '\0') {
        // invalid chars in height
        return false;
    }

    *pWidth = width;
    *pHeight = height;
    return true;
}

typedef MMSharedPtr<ListenerHelper> ListenerHelperSP;
#define COMPONENT_OP_ASYNC(COMPONENT, _OP_FUNC) do {\
    PRINTF("%sing %s\n", #_OP_FUNC, #COMPONENT);\
    mm_status_t status = COMPONENT->_OP_FUNC();\
    if ( status != MM_ERROR_SUCCESS ) {\
        if (status != MM_ERROR_ASYNC) {\
            PRINTF("failed to %s %s\n", #_OP_FUNC, #COMPONENT);\
            return;\
        } else {\
            PRINTF("waiting for %s %s message\n", #COMPONENT, #_OP_FUNC);\
            usleep(1000*1000);\
        }\
    } \
    PRINTF("%s %s success\n", #_OP_FUNC, #COMPONENT);\
}while(0)

void RunMainLoop()
{
    char ch;
    char buffer[128] = {0};
    static int flag_stdin = 0;
    //
    char buffer_back[128] = {0};
    ssize_t back_count = 0;

    ssize_t count = 0;

    flag_stdin = fcntl(STDIN_FILENO, F_GETFL);
    if(fcntl(STDIN_FILENO, F_SETFL, flag_stdin&(~O_NONBLOCK))== -1)
        printf("stdin_fileno set error");

    while (1)
    {
        if(g_quit)
            break;
        memset(buffer, 0, sizeof(buffer));
        if ((count = read(STDIN_FILENO, buffer, sizeof(buffer)-1) < 0)){
            printf("read from stdin err, count is %d\n", count);
            continue;
        }

        buffer[sizeof(buffer) - 1] = '\0';
        count = strlen(buffer);
        PRINTF("read return %zu bytes, %s\n", count, buffer);
        ch = buffer[0];
        if(ch == '-'){
            if (count < 2)
                continue;
             else
                ch = buffer[1];
        }
       if(ch == '\n' && buffer_back[0] != 0){
            printf("Repeat Cmd:%s", buffer_back);//buffer_back has a line change
            memcpy(buffer, buffer_back, sizeof(buffer));
            ch = buffer[0];
            if(ch == '-'){
                if (back_count < 2)
                    continue;
                else
                    ch = buffer[1];
            }
       }else{
            memset(buffer_back, 0, sizeof(buffer_back));
            //printf("deb%sg\n", buffer_back);
            memcpy(buffer_back, buffer, sizeof(buffer));
            back_count = count;
       }
       switch (ch) {
        case 'q':    // exit
            g_quit = true;
            break;

        case '-':
            //some misc cmdline
            break;

        default:
            printf("Unkonw cmd line:%c\n", ch);
            break;
        }
    }

    if(fcntl(STDIN_FILENO,F_SETFL,flag_stdin) == -1)
        printf("stdin_fileno set error");
    printf("\nExit RunMainLoop on yunostest.\n");
}


static GOptionEntry entries[] = {
    {"infile", 'i', 0, G_OPTION_ARG_STRING, &g_input_url, " set input file url", NULL},
    {"outfile", 'o', 0, G_OPTION_ARG_STRING, &g_output_url, "set output file url ", NULL},
    {"filemode", 'm', 0, G_OPTION_ARG_STRING, &g_filemode, "set  file mode ", NULL},
    {"size", 's', 0, G_OPTION_ARG_STRING, &g_sizestr, "set output file url ", NULL},
    {"format", 'f', 0, G_OPTION_ARG_INT, &g_output_format, "set output file color format", NULL},
    {"fps", 'r', 0, G_OPTION_ARG_INT, &g_fps, "set output file fps", NULL},
    {"bitrate", 'b', 0, G_OPTION_ARG_INT, &g_bitrate, "set output file bitrate", NULL},
    {"timeout", 't', 0, G_OPTION_ARG_INT, &g_timeout, "Set timeout", NULL},
    {NULL}
};

class VideoFfmpegEncodeTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(VideoFfmpegEncodeTest, videoffmpegeTest) {
    ComponentSP encoder;
    ComponentSP source;
    ComponentSP sink;
    mm_status_t status;
    ListenerHelperSP listenerHelper;
    listenerHelper.reset(new ListenerHelper);

    /////////////source///////////////
    source = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_VIDEO_FILE_SOURCE,false);
    ASSERT_NE(source.get(), NULL);

    source->setListener(listenerHelper);
    ASSERT_TRUE(source->init() == MM_ERROR_SUCCESS);
    PRINTF("created source plugin\n");

    /////////////encoder///////////////
    encoder = ComponentFactory::create("FFmpeg", YUNOS_MM::MEDIA_MIMETYPE_VIDEO_MPEG4, true);
    ASSERT_NE(encoder.get(), NULL);

    encoder->setListener(listenerHelper);
    ASSERT_TRUE(encoder->init() == MM_ERROR_SUCCESS);
    PRINTF("created encoder plugin\n");

    /////////////sink/////////////////
    sink = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_MEDIA_FILE_SINK,false);
    ASSERT_NE(sink.get(), NULL);

    sink->setListener(listenerHelper);
    ASSERT_TRUE(sink->init() == MM_ERROR_SUCCESS);
    PRINTF("created sink plugin\n");

    ///////set parameters of source/////
    MediaMetaSP param(MediaMeta::create());

    param->setString("type","file");
    param->setString("uri", g_input_url);
    param->setInt32("width", g_input_width);
    param->setInt32("height", g_input_height);
    param->setInt32("color-format",g_input_format);
    param->setFloat("frame-rate",g_fps);
    ASSERT_TRUE(source->setParameter(param) == MM_ERROR_SUCCESS);
    PRINTF("set parameters for source\n");

    ///////set parameters of encoder/////////
    param->setInt32("width", g_output_width);
    param->setInt32("height", g_output_height);
    param->setInt32("color-format",g_output_format);
    param->setFloat("frame-rate",g_fps);
    param->setInt32("bitrate",g_bitrate);
    ASSERT_TRUE(encoder->setParameter(param) == MM_ERROR_SUCCESS);
    PRINTF("set parameters for encoder\n");

    ////////set parameters of filesink/////////
    if ( !g_filemode ) {
        g_filemode = (char*) malloc(4);
        if (!g_filemode) {
            PRINTF("malloc the memory for g_filemode error\n");
            return;
        }
        strcpy(const_cast<char*>(g_filemode), "mp4");
    }
    param->setString(MEDIA_ATTR_OUTPUT_FORMAT,g_filemode);
    param->setString(MEDIA_ATTR_FILE_PATH, g_output_url);
    ASSERT_TRUE(sink->setParameter(param) == MM_ERROR_SUCCESS);
    PRINTF("set parameters for sink\n");

    status = encoder->addSource(source.get(), Component::kMediaTypeVideo);
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);
    PRINTF("add source of encoder\n");

    //////add sink into encoder/////////
    status = encoder->addSink(sink.get(), Component::kMediaTypeVideo);
    ASSERT_TRUE(status == MM_ERROR_SUCCESS);
    PRINTF("add sink of encoder\n");

    COMPONENT_OP_ASYNC(source, prepare);
    COMPONENT_OP_ASYNC(encoder, prepare);
    COMPONENT_OP_ASYNC(sink, prepare);

    COMPONENT_OP_ASYNC(source, start);
    COMPONENT_OP_ASYNC(encoder, start);
    COMPONENT_OP_ASYNC(sink, start);

    PRINTF("\n\n\nrecording ...\n\n\n");


    PRINTF("wait a while \n");
    PRINTF("g_timeout = %d\n",g_timeout);
    if (g_timeout > 0) {
        usleep(g_timeout*1000*1000);
    } else {
        RunMainLoop();
    }

    COMPONENT_OP_ASYNC(encoder, stop);
    COMPONENT_OP_ASYNC(source, stop);
    COMPONENT_OP_ASYNC(sink, stop);


    COMPONENT_OP_ASYNC(source, reset);
    COMPONENT_OP_ASYNC(encoder, reset);
    COMPONENT_OP_ASYNC(sink, reset);

    source->uninit();

    PRINTF("take picture %s successfully\n", g_output_url);

}


int main(int argc, char* const argv[]) {

    GError *error = NULL;
    GOptionContext *context;
    int ret = -1;
    char * p = NULL;
    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (char ***)&argv, &error)) {
        PRINTF("option parsing failed: %s\n", error->message);
        goto _end;
    }
    g_option_context_free(context);

    if(!g_input_url && !g_output_url){
        PRINTF("Must set inputfile and outputfile \n");
        goto _end;
    }
    p = strstr(const_cast<char*>(g_input_url), "file://");
    if ( p != g_input_url ) {
        PRINTF("please insert the prefix:'file://' %s\n", g_input_url);
        goto _end;
    }

    if(g_sizestr){
           if (!parseWidthHeight(g_sizestr, &g_input_width, &g_input_height)) {
                PRINTF("Invalid size '%s', must be width x height\n", g_sizestr);
                goto _end;
            }
            if (g_input_width == 0 || g_input_height == 0) {
                PRINTF("Invalid size %ux%u, width and height may not be zero\n",
                                g_input_width, g_input_height);
               goto _end;
            }
    }
    PRINTF("g_timeout = %d\n",g_timeout);
    try {
       ::testing::InitGoogleTest(&argc, (char **)argv);
       ret = RUN_ALL_TESTS();
    } catch (...) {
       PRINTF("InitGoogleTest failed!");
       goto _end;
    }
_end:
    if (g_output_url)
        g_free(const_cast<char *>(g_output_url));
    if (g_input_url)
        g_free(const_cast<char *>(g_input_url));
    if (g_filemode)
        g_free(const_cast<char *>(g_filemode));
    if (g_sizestr)
        g_free(const_cast<char *>(g_sizestr));
    return ret;

}


