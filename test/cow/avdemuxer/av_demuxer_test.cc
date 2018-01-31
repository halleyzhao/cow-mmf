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
#include <semaphore.h>
#include <gtest/gtest.h>
#include <multimedia/mmthread.h>
#include <multimedia/component.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

static sem_t sgSem;
static bool sgErrorOccured = false;
static bool sgAudioEos = false;
static bool sgVideoEos = false;
static bool sgHasAudio = false;
static bool sgHasVideo = false;
static const char * MMTHREAD_NAME = "FakeSink";

class FakeSink : public PlaySinkComponent, MMThread {
public:
    FakeSink(MediaType mediaType) : MMThread(MMTHREAD_NAME)
                                  , mMediaType(mediaType), mContinue(false)
    {}
    virtual ~FakeSink(){}

public:
    virtual const char * name() const { return "FakeSink"; }
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual int64_t getCurrentPosition() { return 0; }

    virtual mm_status_t addSource(Component * component, MediaType mediaType)
    {
        MMASSERT(component != NULL);
        if ( !component ) {
            MMLOGE("invalid param\n");
            return MM_ERROR_INVALID_PARAM;
        }
        mSourceReader = component->getReader(mediaType);
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t start()
    {
        if ( mMediaType != Component::kMediaTypeAudio &&
            mMediaType != Component::kMediaTypeVideo ) {
            MMLOGE("invalid mediatype\n");
            return MM_ERROR_INVALID_PARAM;
        }
        mContinue = true;
        create();
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t stop()
    {
        mContinue = false;
        destroy();
        return MM_ERROR_SUCCESS;
    }

    virtual void main()
    {
        MMLOGV("+\n");
        int audio = mMediaType == Component::kMediaTypeAudio;
        while ( mContinue ) {
            MediaBufferSP buffer(MediaBufferSP((MediaBuffer*)NULL));
            mm_status_t ret = mSourceReader->read(buffer);
            if ( ret == MM_ERROR_SUCCESS ) {
                MMLOGV("[%s_read]size: %" PRId64 "\n", audio ? "audio" : "video", buffer->size());
                if ( buffer->isFlagSet(MediaBuffer::MBFT_EOS) ) {
                    if ( audio ) {
                        sgAudioEos = true;
                        MMLOGI("audio eos\n");
                    } else {
                        sgVideoEos = true;
                        MMLOGI("video eos\n");
                    }
                    sem_post(&sgSem);
                }
            } else if ( ret == MM_ERROR_NO_MORE ) {
                MMLOGV("[%s_read]nomore\n", audio ? "audio" : "video");
            } else if ( ret == MM_ERROR_INVALID_STATE ) {
                MMLOGV("[%s_read]source not started yet\n", audio ? "audio" : "video");
            } else {
                if ( audio ) {
                    MMLOGI("audio error\n");
                    sgErrorOccured = true;
                } else {
                    MMLOGI("video error\n");
                    sgErrorOccured = true;
                }
                sem_post(&sgSem);
                MMLOGV("[%s_read]failed: %d\n", audio ? "audio" : "video", ret);
            }
//            usleep(1000000);
            usleep(40000);
        }
        MMLOGV("-\n");
    }


protected:
    MediaType mMediaType;
    ReaderSP mSourceReader;
    bool mContinue;

    MM_DISALLOW_COPY(FakeSink);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(FakeSink)

MM_LOG_DEFINE_MODULE_NAME("AVDemuxerTest");

extern "C" Component * createComponent(const char* mimeType, bool isEncoder);
static PlaySourceComponent * createSource()
{
    return DYNAMIC_CAST<PlaySourceComponent*>(createComponent(NULL, false));
}

extern "C" void releaseComponent(Component * component);
static void destroySource(Component * component)
{
    releaseComponent(component);
}



}

using namespace YUNOS_MM;

//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/352x288-h263+amr-nb.3gp";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/640x360-vp8+vorbis.webm";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/712x368-h264+heaac.ts";
//      static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/720P-h264+mp3.avi";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/720P-mpeg4 sp+mp3.mp4";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/864x352-RealVideo4+Cooker.rmvb	";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/1080P-h264+aaclc.flv";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/1080P-wmv2+wma2.wmv";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/1416x600-dvix5+aac.mkv";
//static const char * TEST_FILE = "http://10.101.72.65/video-contents/filestyle/VGA-mpeg4 sp+aaclc.mov";
#ifndef __MM_NATIVE_BUILD__
static const char * TEST_FILE = "/usr/bin/ut/res/video/test.mp4";
#else
static const char * TEST_FILE = "./ut/res/test.mp4";
#endif
//static const char * TEST_FILE = "/usr/bin/ut/res/video/test.mp4";
//static const char * TEST_FILE = "/usr/bin/ut/res/audio/sp.mp3";
static const bool WAI_FOR_COMPLETE = true;

static PlaySourceComponent * sgSource = NULL;
static PlaySinkComponent * sgAudioSink = NULL;
static PlaySinkComponent * sgVideoSink = NULL;


static mm_status_t sgPrepareResult;

class TestListener : public Component::Listener {
public:
    TestListener(){}
    ~TestListener()
     {
        MMLOGV("+\n");
     }

    virtual void onMessage(int msg, int param1, int param2, const MMParamSP obj, const Component * sender)
    {
        switch ( msg ) {
            case Component::kEventPrepareResult:
                MMLOGI("kEventPrepareResult: param1: %d\n", param1);
                sgPrepareResult = param1;
                sem_post(&sgSem);
                break;
            case PlaySourceComponent::kEventInfoDuration:
                MMLOGI("kEventInfoDuration, param1: %d\n", param1);
                break;
            case PlaySourceComponent::kEventInfoBufferingUpdate:
                MMLOGI("kEventInfoBufferingUpdate, param1: %d\n", param1);
                break;
            case Component::kEventInfo:
                {
                    switch ( param1 ) {
                        case SourceComponent::kEventMetaDataUpdate:
                            MMLOGI("kEventMetaDataUpdate\n");
                            break;
                        case PlaySourceComponent::kEventInfoSeekable:
                            MMLOGI("kEventInfoSeekable, param2: %d\n", param2);
                            break;
                        default:
                            MMLOGI("kEventInfo, param1: %d\n", param1);
                    }
                }
                break;
            case Component::kEventEOS:
                MMLOGI("source eos\n");
                break;
            case Component::kEventError:
                MMLOGI("kEventError, param1: %d\n", param1);
                break;
            default:
                MMLOGI("msg: %d, ignore\n", msg);
                break;
        }
    }
};

static TestListener s_g_listener;

//#define NOT_ADD_AUDIO_FORCE

static void doTest()
{
    mm_status_t ret;

    MMLOGI("setting uri: %s\n", TEST_FILE);
    ret = sgSource->setUri(TEST_FILE);
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to set uri(%d)\n", ret);
        goto init;
    }
    MMLOGI("preparing\n");
    ret = sgSource->prepare();
    if ( ret != MM_ERROR_ASYNC ) {
        MMLOGE("failed to prepare(%d)\n", ret);
        goto init;
    }
    MMLOGI("waitting prepare result\n");
    sem_wait(&sgSem);

    MMLOGI("waitting prepare over, result: %d\n", sgPrepareResult);
    if ( sgPrepareResult != MM_ERROR_SUCCESS ) {
        goto prepare;
    }

    int64_t durationMs;
    ret = sgSource->getDuration(durationMs);
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to get duration\n");
        goto prepare;
    }
    MMLOGI("duration: %" PRId64 "\n", durationMs);

    if ( 1 ) {
        MMParamSP ti = sgSource->getTrackInfo();
        if ( !ti ) {
            MMLOGE("failed to get track info\n");
            goto start;
        }

        int32_t streamCount = ti->readInt32();
        MMLOGI("streamCount: %d\n", streamCount);
        for ( int32_t i = 0; i < streamCount; ++i ) {
            int32_t trackType = ti->readInt32();
            int32_t trackCount = ti->readInt32();
            MMLOGI("\ttrackType: %d, count: %d\n", trackType, trackCount);
            for ( int32_t j = 0; j < trackCount; ++j ) {
                int32_t id = ti->readInt32();
                int32_t codecId = ti->readInt32();
                const char * codecName = ti->readCString();
                const char * mime = ti->readCString();
                const char * title = ti->readCString();
                const char * lang = ti->readCString();
                MMLOGI("\t\tid: %d, codecId: %d, codecName: %s, mime: %s, title: %s, lang: %s\n",
                    id, codecId, codecName, mime, title, lang);
            }
        }
    }

    {
        bool seekable = sgSource->isSeekable();
        sgHasAudio = sgSource->hasMedia(Component::kMediaTypeAudio);
        sgHasVideo = sgSource->hasMedia(Component::kMediaTypeVideo);
        MMLOGI("seekable: %d, hasAudio: %d, hasVideo: %d\n", seekable, sgHasAudio, sgHasVideo);

        MediaMetaSP meta = sgSource->getMetaData();
        if ( meta ) {
            meta->dump();
        } else {
            MMLOGI("no meta\n");
        }

        if ( sgHasAudio ) {
#ifndef NOT_ADD_AUDIO_FORCE
            mm_status_t ret = sgAudioSink->addSource(sgSource, Component::kMediaTypeAudio);

            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to audio sinck\n");
                goto start;
            }
#endif
        }
        MMLOGI("add source to audio sink success\n");

        if ( sgHasVideo ) {
            mm_status_t ret = sgVideoSink->addSource(sgSource, Component::kMediaTypeVideo);
            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to video sinck\n");
                goto start;
            }
        }
        MMLOGI("add source to video sink success\n");
    }


    MMLOGI("starting\n");
    if ( sgHasAudio ) {
#ifndef NOT_ADD_AUDIO_FORCE
        ret = sgAudioSink->start();
        if ( ret != MM_ERROR_SUCCESS ) {
            MMLOGE("failed to start audio sink(%d)\n", ret);
            goto start;
        }
#endif
    }

    if ( sgHasVideo ) {
        ret = sgVideoSink->start();
        if ( ret != MM_ERROR_SUCCESS ) {
            MMLOGE("failed to start video sink(%d)\n", ret);
            goto start;
        }
    }

    MMLOGI("seek it to 0 after prepare\n");
    sgSource->seek(0, 0);

    ret = sgSource->start();
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to start(%d)\n", ret);
        goto start;
    }

    if ( WAI_FOR_COMPLETE ) {
        MMLOGI("sleep for a while\n");
        usleep(5000000);
        MMLOGI("seek it\n");
        sgSource->seek(10000, 1);
        usleep(5000000);
        MMLOGI("pause it\n");
        sgSource->pause();
        MMLOGI("seek it\n");
        sgSource->seek(2000, 2);
        MMLOGI("start it\n");
        sgSource->start();
        MMLOGI("wait for complete\n");
        while ( !sgErrorOccured ) {
            if ( sgAudioEos && sgVideoEos ) break;
            sem_wait(&sgSem);
        }
        MMLOGI("eos or error\n");
    } else {
        MMLOGI("wait for a while\n");
        usleep(10000000);
    }

    MMLOGI("stopping\n");
    ret = sgSource->stop();
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to prepare(%d)\n", ret);
        goto start;
    }
    if ( sgHasVideo )
        sgVideoSink->stop();
    if ( sgHasAudio )
        sgAudioSink->stop();

start:
    sgSource->reset();

prepare:

init:
    return;
}


class AvdemuxerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(AvdemuxerTest, avdemuxTest) {
    Component::ListenerSP listener(new TestListener());

    MMLOGI("Hello AVDemuxer\n");
    sem_init(&sgSem, 0, 0);

    sgSource = createSource();
    EXPECT_NE(sgSource, NULL);
    if ( !sgSource ) {
        MMLOGE("failed to create source\n");
        goto init;
    }
    mm_status_t ret;
    ret =sgSource->init();
    EXPECT_TRUE(ret == MM_ERROR_SUCCESS);
    if ( ret != MM_ERROR_SUCCESS ) {
        MMLOGE("failed to init(%d)\n", ret);
        goto init;
    }

    sgAudioSink = new FakeSink(Component::kMediaTypeAudio);
    EXPECT_NE(sgAudioSink, NULL);
    if ( !sgAudioSink ) {
        MMLOGE("failed to create audio sink\n");
        goto init;
    }

    sgVideoSink = new FakeSink(Component::kMediaTypeVideo);
    EXPECT_NE(sgVideoSink, NULL);
    if ( !sgVideoSink ) {
        MMLOGE("failed to create video sink\n");
        goto init;
    }

    sgSource->setListener(listener);

    {
        const std::list<std::string> & supported = sgSource->supportedProtocols();
        MMLOGI("all supported protocols:\n");
        std::list<std::string>::const_iterator it;
        int i;
        for ( i = 0, it = supported.begin(); it != supported.end(); ++it, ++i ) {
            MMLOGI("[supported][%d]%s\n", i, (*it).c_str());
        }
    }

    for ( int i = 0; i < 5; ++i ) {
        MMLOGI("testing time: %d\n", i);
        doTest();
    }

init:
    MM_RELEASE(sgAudioSink);
    MM_RELEASE(sgVideoSink);
    if ( sgSource ) {
        sgSource->uninit();
        destroySource(sgSource);
        sgSource = NULL;
    }
    sem_destroy(&sgSem);
    MMLOGI("bye\n");

}
