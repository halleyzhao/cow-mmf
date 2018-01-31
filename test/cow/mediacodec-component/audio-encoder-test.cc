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

#include <multimedia/component.h>
#include "multimedia/media_meta.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <time.h>



#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_cpp_utils.h"
#include "components/mediacodec_encoder.h"
#include "components/file_sink.h"
#include "components/audio_source_file.h"

typedef long long    TTime;
bool g_quit = false;

static const char *g_output_url = NULL;
static const char *g_input_url_data = NULL;
static int32_t g_sample_rate = 8000;
static int32_t g_bit_rate = 12200;
static int32_t g_channel_count = 1;
static const char *g_mime_type = "audio/amr";
static int32_t g_audio_profile = 2;


const uint32_t kMaxTimeLimitSec = 3*60;       // 3 minutes



#undef DEBUG_PERF
//#define DEBUG_PERF

namespace YUNOS_MM {

#ifdef DEBUG_PERF

#define TIME_TAG(str) printTimeMs(str)
void printTimeMs(const char *str) {

    uint64_t nowMs;
    struct timeval t;
    gettimeofday(&t, NULL);
    nowMs = t.tv_sec * 1000 + t.tv_usec / 1000;

    PRINTF("%s at %" PRId64 "ms\n", str, (nowMs % 10000));
}
#else
#define TIME_TAG(str)
#endif



///////////////////////////////////////////////////////////////////////////////////
//ListenerHelper
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


} // YUNOS_MM

using namespace YUNOS_MM;

bool parseCommandLine(int argc, char **argv)
{
    static const char *shortOptions = "s:b:r:nc:o:i:hm:";

    static const struct option longOptions[] = {
        { "audio-bit-rate",               required_argument,  NULL, 'b' },
        { "audio-profile",                required_argument,  NULL, 'p' },
        { "audio-sample-rate",            required_argument,  NULL, 'r' },
        { "audio-input-file",             required_argument,  NULL, 'i' },
        { "audio-channel-count",          required_argument,  NULL, 'c' },
        { "audio-output",                 required_argument,  NULL, 'o' },
        { "audio-mime-type",              required_argument,  NULL, 'm' },
        { NULL,                           0,                  NULL,  0 }
    };

    while (true) {
        int optionIndex = 0;
        int ic = getopt_long(argc, argv, shortOptions, longOptions, &optionIndex);
        if (ic == -1) {
            break;
        }

        switch (ic) {

        case 'c':
            g_channel_count = atoi(optarg);
            if (g_channel_count != 1 && g_channel_count != 2)
                fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", g_channel_count);
            PRINTF("g_channel_count %d\n", g_channel_count);
            break;
        case 'i':
            g_input_url_data = optarg;
            PRINTF("g_input_url_data %s\n", g_input_url_data);
            break;
        case 'm':
            g_mime_type = optarg;
            PRINTF("g_mime_type %s\n", g_mime_type);
            break;
        case 'b':
            g_bit_rate = atoi(optarg);
            PRINTF("gBitRate %d\n", g_bit_rate);
            break;
        case 'r':
            g_sample_rate = atoi(optarg);
            PRINTF("g_sample_rate %d\n", g_sample_rate);
            break;
        case 'o':
            g_output_url = optarg;
            PRINTF("g_output_url %s\n", g_output_url);
            break;
        case 'p':
            g_audio_profile = atoi(optarg);
            PRINTF("g_audio_profile %d\n", g_audio_profile);
            break;
        default:
            if (ic != '?') {
                fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            }
            return false;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Must specify output file (see --help).\n");
        return -1;
    }

    return true;
}


typedef MMSharedPtr<AudioSourceFile> AudioSourceFileSP;
typedef MMSharedPtr<ListenerHelper> ListenerHelperSP;


#define COMPONENT_OP_ASYNC(COMPONENT, _OP_FUNC) do {\
    PRINTF("%sing %s\n", #_OP_FUNC, #COMPONENT);\
    mm_status_t status = COMPONENT->_OP_FUNC();\
    if ( status != MM_ERROR_SUCCESS ) {\
        if (status != MM_ERROR_ASYNC) {\
            PRINTF("failed to %s %s\n", #_OP_FUNC, #COMPONENT);\
            return -1;\
        } else {\
            PRINTF("waiting for %s %s message\n", #COMPONENT, #_OP_FUNC);\
            listenerHelper->wait();\
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
    ssize_t back_count=0;

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



int main(int argc, char *argv[])
{
    ComponentSP encoder;
    //Component *source;
    ComponentSP source;
    ComponentSP sink;
    ListenerHelperSP listenerHelper;
    listenerHelper.reset(new ListenerHelper);
    mm_status_t status;

    PRINTF("parse command line\n");
    if(!parseCommandLine(argc, argv)){
        return -1;//help or invalid params
    }

    PRINTF("create AudioSourceFile\n");
    source.reset(new AudioSourceFile(g_mime_type, true));
    if (!source) {
        PRINTF("cannot create source\n");
        return -1;
    }
    source->setListener(listenerHelper);
    if (source->init() != MM_ERROR_SUCCESS) {
        PRINTF("source init failed\n");
        return -1;
    }

    PRINTF("create encoder\n");
    encoder = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_AUDIO_AMR_NB, true);
    if (!encoder) {
        PRINTF("cannot create encoder\n");
        return -1;
    }
    encoder->setListener(listenerHelper);



    PRINTF("create File sink\n");
    sink = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_MEDIA_FILE_SINK, true);
    if (!sink) {
        PRINTF("cannot create fileSink\n");
        return -1;
    }
    sink->setListener(listenerHelper);

///////////////////////////////////////////////
    MediaMetaSP param(MediaMeta::create());
    param->setInt32(MEDIA_ATTR_SAMPLE_RATE, g_sample_rate);
    param->setInt32(MEDIA_ATTR_BIT_RATE, g_bit_rate);
    param->setInt32(MEDIA_ATTR_CHANNEL_COUNT, g_channel_count);
    source->setParameter(param);



    PRINTF("add source to encoder\n");
    status = encoder->addSource(source.get(), Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        PRINTF("encoder add source fail %d\n", status);
        return -1;
    }


    PRINTF("add sink to encoder\n");
    status = encoder->addSink(sink.get(), Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        PRINTF("encoder add sink fail %d\n", status);
        return -1;
    }

    param->setString(MEDIA_ATTR_FILE_PATH, g_output_url);
    sink->setParameter(param);


    COMPONENT_OP_ASYNC(source, prepare);
    COMPONENT_OP_ASYNC(encoder, prepare);
    COMPONENT_OP_ASYNC(sink, prepare);


    COMPONENT_OP_ASYNC(source, start);
    COMPONENT_OP_ASYNC(encoder, start);
    COMPONENT_OP_ASYNC(sink, start);

    PRINTF("\n\n\nrecording ...\n\n\n");

    RunMainLoop();

    COMPONENT_OP_ASYNC(source, stop);
    COMPONENT_OP_ASYNC(encoder, stop);
    COMPONENT_OP_ASYNC(sink, stop);


    COMPONENT_OP_ASYNC(source, reset);
    COMPONENT_OP_ASYNC(encoder, reset);
    COMPONENT_OP_ASYNC(sink, reset);


    source->uninit();


    PRINTF("record %s successfully\n", g_output_url);
    return 0;
}
