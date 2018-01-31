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

#include <multimedia/mmthread.h>
#include <multimedia/component.h>
#include <dlfcn.h>
#include <multimedia/media_attr_str.h>
#include <gtest/gtest.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

#define RECORD_TYPE_AV 1

static sem_t sgSem;
static bool sgErrorOccured = false;
static bool sgHasAudio = false;
static bool sgHasVideo = false;



#define FUNC_ENTER() MMLOGI("+\n")
#define FUNC_LEAVE() MMLOGV("-\n")

//#define FORCE_NO_AUDIO
#define FORCE_NO_VIDEO
#define GENERATE_MP4_FILE_FORMAT
//#define GENERATE_AAC_FILE_FORMAT

#ifdef FORCE_NO_AUDIO
static bool sgEncoderAudioEOS = true;
#else
static bool sgEncoderAudioEOS = false;
#endif

#ifdef FORCE_NO_VIDEO
static bool sgEncoderVideoEOS = true;
#else
static bool sgEncoderVideoEOS = false;
#endif
static const char * MMTHREAD_NAME = "MyThread";


class FakeSink : public SinkComponent {
public:
    FakeSink()
    {
        MMLOGV("+\n");
        MMLOGV("-\n");
    }
    virtual ~FakeSink()
    {
        MMLOGV("+\n");
        MMLOGV("-\n");
    }

public:
    class FakeSinkWriter : public Writer {
    public:
        FakeSinkWriter() {
            MMLOGV("+\n");
            #ifndef __MM_NATIVE_BUILD__
            mOutputFp = fopen("/data/avmuxertest.mp4", "w");
            #else
            mOutputFp = fopen("./ut/res/avmuxertest.mp4", "w");
            #endif
            if ( !mOutputFp ) {
                MMLOGE("failed to open output file\n");
            }
            MMLOGV("-\n");
        }
        ~FakeSinkWriter() {
            MMLOGV("+\n");
            if ( mOutputFp )
                fclose(mOutputFp);
            MMLOGV("-\n");
        }
        virtual mm_status_t write(const MediaBufferSP & buffer) {
            if ( !mOutputFp ) {
                MMLOGE("no output file\n");
                return MM_ERROR_OP_FAILED;
            }

            uint8_t * data;
            int64_t size = buffer->size();
            if ( size < 0 ) {
                MMLOGE("size < 0: %" PRId64 "\n");
                return MM_ERROR_OP_FAILED;
            }
            buffer->getBufferInfo((uintptr_t*)&data, 0, 0, 1);
            if ( !data ) {
                MMLOGE("data is null\n");
                return MM_ERROR_OP_FAILED;
            }

            MediaMetaSP meta = buffer->getMediaMeta();
            int64_t seekOffset = -1;
            int seekWhence = -1;
            if ( meta->getInt64(MEDIA_ATTR_SEEK_OFFSET, seekOffset) &&
                meta->getInt32(MEDIA_ATTR_SEEK_WHENCE, seekWhence) &&
                seekOffset >= 0 &&
                seekWhence >= 0 ) {
                fseek(mOutputFp, seekOffset, seekWhence);
            }

            MMLOGV("av_seek: offset: %" PRId64 ", whence: %d\n", seekOffset, seekWhence);

            size_t written = fwrite(data, 1, size, mOutputFp);
            MMLOGV("write to file: %u written\n", written);

            return MM_ERROR_SUCCESS;
        }
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData) {return MM_ERROR_SUCCESS;}

    private:
        FILE * mOutputFp;
    };

public:
    virtual const char * name() const { return "FakeSink"; }
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP(new FakeSinkWriter()); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_SUCCESS;}
    virtual int64_t getCurrentPosition() {return -1ll;}

protected:

    MM_DISALLOW_COPY(FakeSink);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(FakeSink)

class FakeEncoder : public FilterComponent {
public:
    FakeEncoder(MediaType mediaType) : mMediaType(mediaType) ,mThread(NULL)
    {
        MMLOGI("+\n");
        MMLOGI("-\n");
    }
    ~FakeEncoder()
    {
        MMLOGI("+\n");
        MMLOGI("-\n");
    }

public:
    virtual const char * name() const { return "MuxReader"; }
    COMPONENT_VERSION;
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }

    virtual mm_status_t addSource(Component * component, MediaType mediaType) {
        mSouce = component->getReader(mMediaType);
        mMeta = mSouce->getMetaData();
        mMeta->dump();
        return MM_ERROR_SUCCESS;
    }
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {
        mSink = component->getWriter(mMediaType);
        mSink->setMetaData(mMeta);
        return MM_ERROR_SUCCESS;
    }


    struct MyThread : public MMThread {
        MyThread(MediaType mediaType, FakeEncoder * owner) :
            MMThread(MMTHREAD_NAME),
             mContinue(false),
            mMediaType(mediaType),
            mOwner(owner)
        {
        }
        ~MyThread(){}

        mm_status_t start() {
            FUNC_ENTER();
            mContinue = true;
            if ( create() ) {
                MMLOGE("failed to create thread\n");
                return MM_ERROR_NO_MEM;
            }

            usleep(100000);
            FUNC_LEAVE();
            return MM_ERROR_SUCCESS;
        }

        mm_status_t stop() {
            FUNC_ENTER();
            mContinue = false;
            destroy();
            FUNC_LEAVE();
            return MM_ERROR_SUCCESS;
        }

        virtual void main() {
            uint8_t * codecData;
            int32_t codecDataSize;
            if ( mOwner->mMeta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, codecData, codecDataSize) ) {
                MMLOGI("media_%d has codecdata\n", mMediaType);
                MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
                if (!buffer) {
                    MMLOGE("no mem\n");
                    return;
                }
                MediaMetaSP meta = buffer->getMediaMeta();
                meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecData, codecDataSize);

                buffer->setFlag(MediaBuffer::MBFT_CodecData);

                //buffer->setBufferInfo((uintptr_t*)&codecData, NULL, NULL, 1);
                //buffer->setSize((int64_t)codecDataSize);
                buffer->setDts(0);
                buffer->setPts(0);
                buffer->setDuration(0);

                mOwner->mSink->write(buffer);
            } else {
                MMLOGI("media_%d no codecdata\n", mMediaType);
            }

            while ( mContinue ) {
                MediaBufferSP buffer;
                mm_status_t ret = mOwner->mSouce->read(buffer);
                if ( ret != MM_ERROR_SUCCESS ) {
                    MMLOGE("failed to read\n");
                    usleep(10000);
                    continue;
                }

                ret = mOwner->mSink->write(buffer);
                if ( ret != MM_ERROR_SUCCESS ) {
                    MMLOGE("failed to write\n");
                    usleep(1000000);
                    continue;
                }

                if ( buffer->isFlagSet(MediaBuffer::MBFT_EOS) ) {
                    if ( mMediaType == Component::kMediaTypeAudio ) {
                        MMLOGI("read_write_%d, eos\n", mMediaType);
                        sgEncoderAudioEOS = true;
                        sem_post(&sgSem);
                        return;
                    }
                    if ( mMediaType == Component::kMediaTypeVideo ) {
                        MMLOGI("read_write_%d, eos\n", mMediaType);
                        sgEncoderVideoEOS = true;
                        sem_post(&sgSem);
                        return;
                    }
                }

                MMLOGV("read_write_%d, one success\n", mMediaType);
                usleep(10000);
            }
        }

        bool mContinue;
        MediaType mMediaType;
        FakeEncoder * mOwner;
        DECLARE_LOGTAG()
    };

    virtual mm_status_t start() {
        mThread = new MyThread(mMediaType, this);
        mThread->start();
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t stop() {
        mThread->stop();
        MM_RELEASE(mThread);
        return MM_ERROR_SUCCESS;
    }

protected:
    MediaType mMediaType;
    ReaderSP mSouce;
    WriterSP mSink;
    MyThread * mThread;
    MediaMetaSP mMeta;
    MM_DISALLOW_COPY(FakeEncoder);
    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(FakeEncoder)
DEFINE_LOGTAG(FakeEncoder::MyThread)




MM_LOG_DEFINE_MODULE_NAME("AVMuxerTest");

}

using namespace YUNOS_MM;

typedef Component * (*createComponent_t)(const char* mimeType, bool isEncoder);
typedef void (*destroyComponent_t)(Component * component);

//static const char * TEST_FILE = "/usr/bin/ut/res/audio/sp.mp3";
//static const char * TEST_FILE = "/usr/bin/ut/res/video/test.mp4";

#ifdef GENERATE_MP4_FILE_FORMAT
#ifndef __MM_NATIVE_BUILD__
static const char * TEST_FILE = "/usr/bin/ut/res/video/h264.mp4";
#else
static const char * TEST_FILE = "./ut/res/h264.mp4";
#endif
#endif

#ifdef GENERATE_AAC_FILE_FORMAT
#ifndef __MM_NATIVE_BUILD__
static const char * TEST_FILE = "/usr/bin/ut/res/audio/aac.aac";
#else
static const char * TEST_FILE = "./ut/res/aac.aac";
#endif
#endif


static const bool WAI_FOR_COMPLETE = true;

static PlaySourceComponent * sgSource = NULL;
static void * sgSourceH = NULL;
#ifndef FORCE_NO_AUDIO
static FilterComponent * sgAudioEncoder = NULL;
#endif
#ifndef FORCE_NO_VIDEO
static FilterComponent * sgVideoEncoder = NULL;
#endif
static FilterComponent * sgMuxer = NULL;
static void * sgMuxerH = NULL;
static SinkComponent * sgSink = NULL;


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


class AVMuxerListener : public Component::Listener {
public:
    AVMuxerListener(){}
    ~AVMuxerListener()
     {
        MMLOGV("+\n");
     }

    virtual void onMessage(int msg, int param1, int param2, const MMParamSP obj, const Component * sender)
    {
        switch ( msg ) {
            case Component::kEventResetComplete:
                MMLOGI("kEventResetComplete: param1: %d\n", param1);
                sem_post(&sgSem);
                break;
            case Component::kEventStopped:
                MMLOGI("kEventStopped, param1: %d\n", param1);
                sem_post(&sgSem);
                break;
            default:
                MMLOGI("msg: %d, ignore\n", msg);
                break;
        }
    }
};

static AVMuxerListener s_g_avmuxerlistener;

static Component * createCompoent(const char * libname, void *& h)
{
    h = dlopen(libname, RTLD_NOW);
    if ( !h ) {
        MMLOGE("failed to dlopen %s, error: %s", libname, dlerror());
        return NULL;
    }

    createComponent_t func = (createComponent_t)dlsym(h, "createComponent");
    if ( !func ) {
        MMLOGE("failed to find create func for %d\n", libname);
        dlclose(h);
        return NULL;
    }

    Component * c = func(0,0);
    c->init();
    return c;
}


static void releaseCompoent(void * h, Component * c)
{
    destroyComponent_t func = (destroyComponent_t)dlsym(h, "releaseComponent");
    if ( !func ) {
        MMLOGE("failed to find release func for %d\n");
        dlclose(h);
        return;
    }

    c->uninit();
    func(c);
 //   dlclose(h);
}

#define CREATE_COMPOENT(_c, _h, _who, _type) do {\
    _c = (_type*)createCompoent(_who, _h);\
    if ( !_c ) {\
        MMLOGE("failed to create\n");\
        goto init;\
    }\
}while(0)

#define RELEASE_C(_h, _c) do {\
    if ( _c ) {\
        MMLOGI("releasing %s\n", #_c);\
        releaseCompoent(_h, _c);\
        MMLOGI("releasing %s over\n", #_c);\
    }\
}while(0)

#define START_C(_c) do {\
    MMLOGI("starting %s\n", #_c);\
    mm_status_t ret = _c->start();\
    if ( ret != MM_ERROR_SUCCESS ) {\
        MMLOGE("failed to start %s\n", #_c);\
        goto start;\
    }\
}while(0)

#define STOP_C(_c) do {\
    MMLOGI("stopping %s\n", #_c);\
    mm_status_t ret = _c->stop();\
    if ( ret == MM_ERROR_SUCCESS ) {\
        MMLOGE("%s stopped\n", #_c);\
        break;\
    }\
    if (ret == MM_ERROR_ASYNC) {\
        MMLOGI("waitting for %s stop complete\n", #_c);\
        sem_wait(&sgSem);\
        MMLOGI("waitting for %s stop complete over\n", #_c);\
        break;\
    }\
    MMLOGE("failed to stop %s\n", #_c);\
}while(0)

#define RESET_C(_c) do {\
    MMLOGI("reseting %s\n", #_c);\
    mm_status_t ret = _c->reset();\
    if ( ret != MM_ERROR_SUCCESS ) {\
        MMLOGE("failed to reset %s\n", #_c);\
    }\
}while(0)

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
        MediaMetaSP muxparam(MediaMeta::create());
#ifdef GENERATE_MP4_FILE_FORMAT
        muxparam->setString("output-format", "mp4");
#endif
#ifdef GENERATE_AAC_FILE_FORMAT
        muxparam-setString("output-format", "adts");
#endif
        sgMuxer->setParameter(muxparam);
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

#ifndef FORCE_NO_AUDIO
        if ( sgHasAudio ) {
            MMLOGI("adding sgSource to audio encoder\n");
            mm_status_t ret = sgAudioEncoder->addSource(sgSource, Component::kMediaTypeAudio);
            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to audio sinck\n");
                goto start;
            }
            MMLOGI("adding sgMuxer to audio encoder\n");
            ret = sgAudioEncoder->addSink(sgMuxer, Component::kMediaTypeAudio);
            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to audio sinck\n");
                goto start;
            }
        }
#endif
#ifndef FORCE_NO_VIDEO
        if ( sgHasVideo ) {
            MMLOGI("adding sgSource to video encoder\n");
            mm_status_t ret = sgVideoEncoder->addSource(sgSource, Component::kMediaTypeVideo);
            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to video sinck\n");
                goto start;
            }
            MMLOGI("adding sgMuxer to video encoder\n");
            ret = sgVideoEncoder->addSink(sgMuxer, Component::kMediaTypeVideo);
            if ( ret != MM_ERROR_SUCCESS ) {
                MMLOGE("failed to add source to video sinck\n");
                goto start;
            }
        }
#endif
        MMLOGI("adding sgSink to video sgMuxer\n");
        mm_status_t ret = sgMuxer->addSink(sgSink, Component::kMediaTypeUnknown);
        if ( ret != MM_ERROR_SUCCESS ) {
            MMLOGE("failed to add source to video sinck\n");
            goto start;
        }
    }


    MMLOGI("starting\n");
    {
        START_C(sgSource);
        START_C(sgSink);
        START_C(sgMuxer);
#ifndef FORCE_NO_AUDIO
        if ( sgHasAudio ) {
            START_C(sgAudioEncoder);
        } else {
            sgEncoderAudioEOS = true;
        }
#endif
#ifndef FORCE_NO_VIDEO
        if ( sgHasVideo ) {
            START_C(sgVideoEncoder);
        } else {
            sgEncoderVideoEOS = true;
        }
#endif
    }

    if ( WAI_FOR_COMPLETE ) {
        MMLOGI("wait for all media complete\n");
        while ( 1 ) {
            if ( sgEncoderAudioEOS && sgEncoderVideoEOS && !sgErrorOccured ) {
                MMLOGI("all media eos or error\n");
                break;
            }
            MMLOGI("wait eos: audio: %d, video: %d\n", sgEncoderAudioEOS, sgEncoderVideoEOS);
            sem_wait(&sgSem);
        }
        MMLOGI("all media eos or error\n");
    } else {
        MMLOGI("wait for a while\n");
        usleep(100000000);
    }

    MMLOGI("stopping\n");
    {
#ifndef FORCE_NO_AUDIO
        if ( sgHasAudio ) {
            STOP_C(sgAudioEncoder);
        }
#endif
#ifndef FORCE_NO_VIDEO
        if ( sgHasVideo ) {
            STOP_C(sgVideoEncoder);
        }
#endif
        STOP_C(sgSource);
        STOP_C(sgMuxer);
        STOP_C(sgSink);
    }


start:
    RESET_C(sgSource);
#ifndef FORCE_NO_AUDIO
    if ( sgHasAudio ) {
        RESET_C(sgAudioEncoder);
    }
#endif
#ifndef FORCE_NO_VIDEO
    if ( sgHasVideo ) {
        RESET_C(sgVideoEncoder);
    }
#endif
    RESET_C(sgMuxer);
    RESET_C(sgSink);

    MMLOGI("waitting for reset complete\n");
    sem_wait(&sgSem);
    MMLOGI("waitting for reset complete over\n");

prepare:

init:
    return;
}


class AvmuxerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};


TEST_F(AvmuxerTest, avmuxTest) {
    Component::ListenerSP listener(new TestListener());
    Component::ListenerSP avmuxerListener(new AVMuxerListener());

    MMLOGI("Hello AVMuxer\n");
    sem_init(&sgSem, 0, 0);

    CREATE_COMPOENT(sgSource, sgSourceH, "libAVDemuxer.so", PlaySourceComponent);
    CREATE_COMPOENT(sgMuxer, sgMuxerH, "libAVMuxer.so", FilterComponent);
#ifndef FORCE_NO_AUDIO
    sgAudioEncoder = new FakeEncoder(Component::kMediaTypeAudio);
    ASSERT_NE(sgAudioEncoder, NULL);
#endif
#ifndef FORCE_NO_VIDEO
    sgVideoEncoder = new FakeEncoder(Component::kMediaTypeVideo);
    ASSERT_NE(sgVideoEncoder, NULL);
#endif
    sgSink = new FakeSink();
    ASSERT_NE(sgSink, NULL);
    int ret ;
    ret = sgSource->setListener(listener);
    EXPECT_TRUE(ret == MM_ERROR_SUCCESS);
 
    ret = sgMuxer->setListener(avmuxerListener);
    EXPECT_TRUE(ret == MM_ERROR_SUCCESS);

    {
        const std::list<std::string> & supported = sgSource->supportedProtocols();
        MMLOGI("all supported protocols:\n");
        std::list<std::string>::const_iterator it;
        int i;
        for ( i = 0, it = supported.begin(); it != supported.end(); ++it, ++i ) {
            MMLOGI("[supported][%d]%s\n", i, (*it).c_str());
        }
    }

    for ( int i = 0; i < 1; ++i ) {
        MMLOGI("testing time: %d\n", i);
        doTest();
    }

init:
    RELEASE_C(sgSourceH, sgSource);
    RELEASE_C(sgMuxerH, sgMuxer);
#ifndef FORCE_NO_AUDIO
    MM_RELEASE(sgAudioEncoder);
#endif
#ifndef FORCE_NO_VIDEO
    MM_RELEASE(sgVideoEncoder);
#endif
    MM_RELEASE(sgSink);

    sem_destroy(&sgSem);
    MMLOGI("bye\n");

}


