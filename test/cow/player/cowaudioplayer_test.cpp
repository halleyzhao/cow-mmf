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

#include <glib.h>
#include <gtest/gtest.h>

typedef int status_t;
#include "multimedia/media_attr_str.h"

#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/mediaplayer.h"
#include "multimedia/component.h"

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("MediaPlayer-AudioTest")
typedef long	    TTime;
#define IMITATE_WEBENGINE_SEQUENCE

bool g_auto_test = false;
bool g_loop_player = false;
int32_t g_loop_count = 100;
bool g_offload = false;
static bool g_eos = false;

bool op_done_ok = false;
int64_t op_timeout = 0;

#define CHECK_FREE_GSTRING(str) do{   \
    if (str) {            \
         g_free(str);     \
    }                     \
}while(0)


#if 0
#undef DEBUG
#undef INFO
#undef WARNING
#undef ERROR
#define DEBUG(format, ...)  printf("[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  printf("[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define WARNING(format, ...)  printf("[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  printf("[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

class TestListener : public MediaPlayer::Listener {
public:
    TestListener(){}
    ~TestListener()
     {
        MMLOGV("+\n");
     }

    virtual void onMessage(int msg, int param1, int param2, const MMParam *meta)
    {
        //DEBUG("CPL-TEST %s, %d, msg %d\n", __FUNCTION__, __LINE__, msg);
        switch ( msg ) {
            case MSG_PREPARED:
                INFO("MSG_PREPARED: param1: %d\n", param1);
                if (param1 == MM_ERROR_SUCCESS) {
                    op_done_ok = true;
                }
                break;
            case MSG_STARTED:
                INFO("MSG_STARTED: param1: %d\n", param1);
                if (param1 == MM_ERROR_SUCCESS) {
                    op_done_ok = true;
                }
                break;
            case MSG_STOPPED:
                INFO("MSG_STOPPED: param1: %d\n", param1);
                op_done_ok = true;
                break;
             case MSG_SEEK_COMPLETE:
                INFO("MSG_SEEK_COMPLETE\n");
                break;
             case MSG_PLAYBACK_COMPLETE:
                INFO("MSG_PLAYBACK_COMPLETE, EOS\n");
                g_eos = true;
                break;
             case MSG_BUFFERING_UPDATE:
                VERBOSE("MSG_BUFFERING_UPDATE: param1: %d\n", param1);
                break;
            case MSG_ERROR:
                INFO("MSG_ERROR, param1: %d\n", param1);
                break;
            default:
                INFO("msg: %d, ignore\n", msg);
                break;
        }
    }
};

#define CHECK_PLAYER_OP_RET(OPERATION, RET) do {               \
    if(RET != MM_ERROR_SUCCESS && RET != MM_ERROR_ASYNC) {      \
        ERROR("%s fail, ret=%d\n", OPERATION, RET);             \
        return RET;                                             \
    }                                                           \
} while(0)

#define ENSURE_PLAYER_OP_SUCCESS_PREPARE() do { \
    op_done_ok = false;                         \
    op_timeout = 0;                             \
} while(0)

#define ENSURE_PLAYER_OP_SUCCESS(OPERATION, RET, MY_TIMEOUT) do {  \
    if(RET != MM_ERROR_SUCCESS && RET != MM_ERROR_ASYNC){           \
        ERROR("%s failed, ret=%d\n", OPERATION, RET);               \
        return RET;                                                 \
    }                                                               \
    while (!op_done_ok) {                                           \
        usleep(10000);                                              \
        op_timeout += 10000;                                        \
        if (op_timeout > (MY_TIMEOUT)*1000*1000) {                  \
            ERROR("prepare timeout");                               \
            return MM_ERROR_IVALID_OPERATION;                       \
        }                                                           \
    }                                                               \
}while (0)

//class MyAudioPlayer:public MediaPlayer::Listener
class MyAudioPlayer
{
public:
    MyAudioPlayer():
        mpUrl(NULL)
    {
        DEBUG();
        if (g_offload)
            mpPlayer = MediaPlayer::create(MediaPlayer::PlayerType_LPA);
        else
            mpPlayer = MediaPlayer::create(MediaPlayer::PlayerType_COWAudio);
        mpListener= new TestListener();
        mMeta = MediaMeta::create();
    }
    virtual ~MyAudioPlayer(){
        DEBUG();
        //mpPlayer->removeListener();
        MediaPlayer::destroy(mpPlayer);
        //delete mpPlayer;
        delete mpListener;
        DEBUG("~MyAudioPlayer done");
    }

    mm_status_t quit()
    {
        mm_status_t ret = MM_ERROR_SUCCESS;
        DEBUG();
        // stop and wait stop is done
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->stop();
        ENSURE_PLAYER_OP_SUCCESS("stop", ret, 10);

        mpPlayer->reset();
        // mpPlayer->release();
        return ret;
    }

    mm_status_t pause()
    {
        DEBUG();
        return mpPlayer->pause();
    }

    mm_status_t resume(){
        DEBUG();
        return mpPlayer->start();
    }

    mm_status_t  seek(TTime tar){
        DEBUG();
        return mpPlayer->seek((int64_t)tar);
    }

    mm_status_t setLoop(){
        static bool loop = true;

        int ret= mpPlayer->setLoop(loop);
        loop = !loop;
        return ret;
    }

    void test(){
        DEBUG();
        mm_status_t status = MM_ERROR_UNKNOWN;
        int msec = -2;
        status = mpPlayer->getCurrentPosition(&msec);

        DEBUG("got msec=%d, status: %d\n", msec, status);
    }

    mm_status_t play(const char* url){
        DEBUG("url: %s", url);
        mpUrl=url;
        mm_status_t ret;
        g_eos = false;
        std::map<std::string,std::string> header;
        ret = mpPlayer->setDataSource(url,&header);
        if(ret != MM_ERROR_SUCCESS && ret != MM_ERROR_ASYNC){
            DEBUG("player setDataSource fail, ret=%d\n", ret);
            return ret;
         }

        ret = mpPlayer->setListener(mpListener);
        CHECK_PLAYER_OP_RET("setListener", ret);

        ret = mpPlayer->setAudioStreamType(MediaPlayer::AS_TYPE_MUSIC);
        CHECK_PLAYER_OP_RET("setAudioStreamType", ret);

        DEBUG("use prepareAsync\n");
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->prepareAsync();
        ENSURE_PLAYER_OP_SUCCESS("prepare", ret, 5);

#ifdef IMITATE_WEBENGINE_SEQUENCE
        int msec = -1;
        ret = mpPlayer->getDuration(&msec);
        CHECK_PLAYER_OP_RET("getDuration", ret);

        ret = mpPlayer->seek(0);
        CHECK_PLAYER_OP_RET("seek", ret);
#endif
        DEBUG();
        ENSURE_PLAYER_OP_SUCCESS_PREPARE();
        ret = mpPlayer->start();
        ENSURE_PLAYER_OP_SUCCESS("start", ret, 5);

#ifdef IMITATE_WEBENGINE_SEQUENCE
        ret = mpPlayer->getCurrentPosition(&msec);
        CHECK_PLAYER_OP_RET("getCurrentPosition", ret);
#endif

         return MM_ERROR_SUCCESS;
    }

    MediaPlayer* getPlayer(){
        return mpPlayer;
    }

private:
    const char* mpUrl;
    MediaPlayer* mpPlayer;
    TestListener* mpListener;
    MediaMetaSP mMeta;
};

MyAudioPlayer* gPlayer;


#define MAX_SOURCE_NAME_LEN 256
#define MAX_SOURCE_NUM 16

char g_source[MAX_SOURCE_NAME_LEN];
int get_source(const char* par)
{
    if(strlen(par) >= MAX_SOURCE_NAME_LEN){
        ERROR("get_source err: too long source name\n");
        return -1;
    }

    int rval = 0;
    const char* ptr = par;
    int filename_len = 0;

    for(; *ptr != '\0'; ptr++){
        if(*ptr == ' ')
            break;
    }
    filename_len = ptr - par;

    DEBUG("filename_len: %d\n", filename_len);

    strncpy(g_source, par, filename_len);
    //g_source[filename_len];
    //const char *tmp = "http://d.tv.taobao.com/TB2kh.pbFXXXXbfXpXXXXXXXXXX/0.m3u8?auth_key=9A05722F7B23C53BBE52BC9F2BD80D03-1448716836&ttr=free";
    //memcpy(g_source, tmp, strlen(tmp));
    DEBUG("get_source %s, filename_len:%d, parsed:%s.\n", par, filename_len, g_source);
    return rval;
}

char* trim(char *str)
{
    char *ptr = str;
    int i;

    // skip leading spaces
    for (; *ptr != '\0'; ptr++)
        if (*ptr != ' ')
            break;

    //skip (-a    xxx) to xxx
    for(; *ptr != '\0'; ptr++){
        if(*ptr != ' ')
            continue;
        for(; *ptr != '\0'; ptr++){
            if(*ptr == ' ')
                continue;
            break;
        }
        break;
    }

    // remove trailing blanks, tabs, newlines
    for (i = strlen(str) - 1; i >= 0; i--)
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n')
            break;

    str[i + 1] = '\0';
    return ptr;
}

bool g_quit = false;

uint32_t g_testCount = 10;
char getRandomOp()
{
    const int randomOpMin = 0;
    const int randomOpMax = 4;
    char randomOps[]={'p','r','s','l','t'};
    uint32_t i = rand()%(randomOpMax-randomOpMin+1)+randomOpMin;
    sleep(2);
    return randomOps[i];
}

int64_t getRandomSeekTo()
{
    MediaPlayer* mp = gPlayer->getPlayer();
    int randomSeekMin = 0;
    int randomSeekMax = 0;
    mp->getDuration(&randomSeekMax);
    DEBUG("max auto seek to =%d\n",randomSeekMax);
    uint32_t i = rand()%(randomSeekMax-randomSeekMin+1)+randomSeekMin;
    return i;
}

void RunMainLoop()
{
    char ch;
    char buffer[128] = {0};
    static int flag_stdin = 0;
    int par;
    int64_t target;
    //
    char buffer_back[128] = {0};
    ssize_t back_count = 0;

    ssize_t count = 0;

    flag_stdin = fcntl(STDIN_FILENO, F_GETFL);
    if(fcntl(STDIN_FILENO, F_SETFL, flag_stdin | O_NONBLOCK)== -1)
        ERROR("stdin_fileno set error");

    uint32_t  testcount =0;
    if(g_auto_test){
        uint64_t seed = (unsigned)time(0);
        INFO("auto test seed =%" PRId64, seed);
        srand(seed);
    }
    while (1)
    {
        if(g_quit)
            break;
        if(!g_auto_test){
            memset(buffer, 0, sizeof(buffer));
            if ((count = read(STDIN_FILENO, buffer, sizeof(buffer)-1) < 0)){
                if (g_eos) {
                    g_eos = false;
                    buffer[0] = 'q';
                }
                 else {
                    usleep(100000);
                    continue;
                }
            }
            buffer[sizeof(buffer) - 1] = '\0';
            count = strlen(buffer);
            DEBUG("read return %zu bytes, %s\n", count, buffer);
            ch = buffer[0];
            if(ch == '-'){
                if (count < 2)
                    continue;
                 else
                    ch = buffer[1];
            }
           if(ch == '\n' && buffer_back[0] != 0){
                DEBUG("Repeat Cmd:%s", buffer_back);//buffer_back has a line change
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
                //DEBUG("deb%sg\n", buffer_back);
                memcpy(buffer_back, buffer, sizeof(buffer));
                back_count = count;
           }
        }
        else{ //auto test get rand op
           if(testcount <= g_testCount && !g_quit){
                ch = getRandomOp();
                DEBUG("auto ch = %c\n",ch);
                testcount ++;
            }
            else{ //auto test exit
                 ch = 'q';
                 DEBUG("auto quit\n ");
            }
        }
        //g_last_cmd = ch;
        switch (ch) {
        case 'q':    // exit
            g_quit = true;
            gPlayer->quit();
            break;

        case 'p':
            gPlayer->pause();
            break;

        case 'r':
            gPlayer->resume();
            break;

        case 's':
            if(!g_auto_test){
                if(sscanf(trim(buffer), "%d", &par) != 1){
                    ERROR("Please set the seek target in 90k unit\n");
                    break;
                }
                target = par;
            }
            else {
                target = getRandomSeekTo();
                DEBUG("auto seek to %" PRId64 "\n",target);
            }
            gPlayer->seek(target);
            break;

        case 'l':
            gPlayer->setLoop();
            break;

        case 't':
            gPlayer->test();
            break;

        case '-':
            //some misc cmdline
            break;

        default:
            ERROR("Unkonw cmd line:%c\n", ch);
            break;
        }
    }

    if(fcntl(STDIN_FILENO,F_SETFL,flag_stdin) == -1)
        ERROR("stdin_fileno set error");
    DEBUG("\nExit RunMainLoop on yunostest.\n");
}

const static char *file_name = NULL;
static gboolean autotest = FALSE;
static gboolean loopplay = FALSE;
static int loopcount = -1;
static gboolean offload = FALSE;

static GOptionEntry entries[] = {
    {"add", 'a', 0, G_OPTION_ARG_STRING, &file_name, " set the file name to play", NULL},
    {"autotest", 't', 0, G_OPTION_ARG_NONE, &autotest, "use auto test ", NULL},
    {"loopPlayer", 'l', 0, G_OPTION_ARG_NONE, &loopplay, "set loop play", NULL},
    {"loopCount", 'c', 0, G_OPTION_ARG_INT, &loopcount, "set loop play count", NULL},
    {"lowpoweraudio", 'o', 0, G_OPTION_ARG_NONE, &offload, "set offload mode", NULL},
    {NULL}
};

class CowAudioplayerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(CowAudioplayerTest, cowaudioplaytest) {
    int ret;
    int32_t loopedCount = 1;

REC_LOOP:

    gPlayer = new MyAudioPlayer();

    DEBUG();
    ret = gPlayer->play(g_source);
    EXPECT_TRUE(ret == MM_ERROR_SUCCESS);
    DEBUG();

    if (g_loop_player) {
        usleep(10*1000*1000);//player 10s, then call quit
        gPlayer->quit();
    } else {
        RunMainLoop();
    }
    usleep(10000);
    DEBUG("delete gPlayer...\n");
    delete gPlayer;
    DEBUG("delete gPlayer done, loopedCount %d\n\n\n", loopedCount);
    printf("delete gPlayer done, loopedCount %d\n", loopedCount);

    if (g_loop_player) {
        if (loopedCount++ < g_loop_count) {
             usleep(2*1000*1000);
             goto REC_LOOP;
        }
    }

}

int main(int argc, char* const argv[]) {
    DEBUG("MediaPlayer audio test for YunOS!\n");
    if (argc < 2) {
        ERROR("Must specify play file \n");
        return 0;
    }

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(MM_LOG_TAG);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_help_enabled(context, TRUE);

    if (!g_option_context_parse(context, &argc, (gchar***)&argv, &error)) {
            ERROR("option parsing failed: %s\n", error->message);
            return -1;
    }
    g_option_context_free(context);

    if(file_name){
        DEBUG("set file name  %s\n", file_name);
        if (get_source(file_name) < 0){
                ERROR("invalid file name\n");
                return -1;
        }
    }

    if(autotest){
        DEBUG("set auto test\n");
        g_auto_test = true;
    }

    if(loopplay){
        DEBUG("set loop play \n");
        g_loop_player = true;
    }

    if(loopcount >0){
        DEBUG("set loop play count =%d \n",loopcount);
        g_loop_count = loopcount;
    }

    if(offload){
        DEBUG("set offload mode \n");
        g_offload = true;
    }

    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char**)argv);
        ret = RUN_ALL_TESTS();
     } catch (...) {
        ERROR("InitGoogleTest failed!");
        ret = -1;
    }

    CHECK_FREE_GSTRING((char*)file_name);
    DEBUG("cowaudioplayyer-test done");
    return ret;

}

