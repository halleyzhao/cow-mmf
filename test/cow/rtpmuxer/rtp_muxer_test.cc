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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <multimedia/mmmsgthread.h>

#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/media_monitor.h"
#include "components/av_demuxer.h"

MM_LOG_DEFINE_MODULE_NAME("RTP_MUXER_TEST");

using namespace YUNOS_MM;

static bool releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

/////////////////////FakeVideoDecode/////////////
class FakeVideoDecode : public FilterComponent
{

public:
    class DecodeThreadV;
    typedef MMSharedPtr <DecodeThreadV> DecodeThreadVSP;
    class DecodeThreadV : public MMThread
    {
    public:
        DecodeThreadV(FakeVideoDecode *decoder) : MMThread("FakeVideoDecodeThd")
        {
            mDecoder = decoder;
            return ;
        }
        ~DecodeThreadV() { return ; }
        void signalExit() { return; }
        void signalContinue() { return; }

    protected:
        virtual void main()
        {
            int32_t frameIdx = 0;

            while(1) {
                MediaBufferSP mediaInputBuffer;
                {
                    if (mDecoder->mIsExit) {
                        INFO("now to exit\n");
                        break;
                    }
                    mDecoder->mReader->read(mediaInputBuffer);
                }
                if (mediaInputBuffer) {
                    uint8_t *buffer = NULL;
                    int decodedSize = 0;
                    int64_t pts = ((int64_t)frameIdx * 1000 *1000) / 23976, dts;

                    mediaInputBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1);
                    decodedSize = mediaInputBuffer->size();
                    pts = mediaInputBuffer->pts();
                    dts = mediaInputBuffer->dts();
                    uint8_t * buffer2 = new uint8_t [decodedSize];
                    memcpy(buffer2, buffer, decodedSize);
                    MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
                    mediaOutputBuffer->setBufferInfo((uintptr_t *)&buffer2, NULL, &decodedSize, 1);
                    mediaOutputBuffer->setSize(decodedSize);
                    mediaOutputBuffer->setPts(pts);
                    mediaOutputBuffer->setDts(dts);
                    mediaOutputBuffer->setDuration(mediaInputBuffer->duration());
                    mediaOutputBuffer->addReleaseBufferFunc(releaseOutputBuffer);
                    mediaOutputBuffer->setMonitor(mDecoder->mMonitorWrite);

                    INFO("video info:buffer:%p,size:%d,pts:%" PRId64 ",dts:%" PRId64 ",duration:%" PRId64 "\n",
                              buffer2, decodedSize, pts, dts, mediaInputBuffer->duration());

                    mm_status_t status = mDecoder->mWriter->write(mediaOutputBuffer);
                    if (status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC) {
                        ERROR("decoder fail to write Sink");
                        break;
                    }
                    frameIdx++;
                    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
                    trafficControlWrite->waitOnFull();
                 }else { //if (mediaBuffer) {
                    //INFO("read NULL buffer from demuxer\n");
                    usleep(10*1000);
                }

                usleep(5*1000);
            }

            INFO("Video Output thread exited\n");
            return;
        }

    private:
        FakeVideoDecode *mDecoder;
    };

    FakeVideoDecode(const char *mimeType = NULL, bool isEncoder = false)
    {
        mIsExit = true;
        mMonitorWrite.reset(new TrafficControl(1,10,"FakeVideoDecodeWrite"));
        return;

    }
    virtual ~FakeVideoDecode() { return ; }
    virtual const char * name() const { return "FakeVideoDecoder"; }
    COMPONENT_VERSION;

    virtual mm_status_t addSource(Component * component, MediaType mediaType)
    {
        if (component && mediaType == kMediaTypeVideo) {
            mReader = component->getReader(kMediaTypeVideo);
            if (mReader) {
                MediaMetaSP metaData;
                metaData = mReader->getMetaData();
                if (metaData) {
                    mMetaData = metaData->copy();
                    return MM_ERROR_SUCCESS;
                }
            }
        }
        return MM_ERROR_IVALID_OPERATION;
    }

    virtual mm_status_t addSink(Component * component, MediaType mediaType)
    {
        //#0 set the write meta data
        if (component && mediaType == kMediaTypeVideo) {
            mWriter = component->getWriter(kMediaTypeVideo);
            //#0 Width|Height|pixFmt|gopSize|bitRate
            mMetaData->setInt32(MEDIA_ATTR_CODECID, 'H264');
            mMetaData->setInt32(MEDIA_ATTR_WIDTH, 640);
            mMetaData->setInt32(MEDIA_ATTR_HEIGHT, 360);
            mMetaData->setInt32(MEDIA_ATTR_COLOR_FOURCC, 'YV12');
            mMetaData->setInt32(MEDIA_ATTR_GOP_SIZE, 12);
            mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, 764836);

            //#1 timeBase|extraData
            mMetaData->setFraction(MEDIA_ATTR_TIMEBASE, 16384, 785645);
            mMetaData->setFraction(MEDIA_ATTR_TIMEBASE2, 1, 1000000);
            unsigned char buf[40];
            FILE *fp;
            fp = fopen("/home/haibinwuhb/work/cow_player/39.bin", "rb");
            fread(buf,1,39,fp);
            fclose(fp);
            mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, buf, 39);
            INFO("now start to send video parameters\n");
            mWriter->setMetaData(mMetaData);
        }

        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t start()
    {
        mIsExit = false;
        if (!mDecodeThread) {
        // create thread to decode buffer
            mDecodeThread.reset (new DecodeThreadV(this), MMThread::releaseHelper);
            mDecodeThread->create();
        }
        usleep(500*1000);
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t stop()
    {
        TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
        trafficControlWrite->unblockWait();

        mIsExit = true;
        usleep(500*1000);
        if (mDecodeThread) {
            mDecodeThread.reset();
        }
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t release() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t init() { return MM_ERROR_SUCCESS; }
    virtual void uninit() { return; }
    virtual mm_status_t prepare() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t setParameter(const MediaMetaSP & meta) { return MM_ERROR_SUCCESS; }
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t read(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t write(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual MediaMetaSP getMetaData() {return MediaMetaSP((MediaMeta*)NULL);}
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData) { return MM_ERROR_UNSUPPORTED; }

private:

    MediaMetaSP mMetaData;
    DecodeThreadVSP mDecodeThread;
    MonitorSP mMonitorWrite;

    ReaderSP mReader;
    WriterSP mWriter;
    bool     mIsExit;
    MM_DISALLOW_COPY(FakeVideoDecode);
};

///////////AudioDecode////////////////////////////
class FakeAudioDecode : public FilterComponent
{

public:
    class DecodeThreadA;
    typedef MMSharedPtr <DecodeThreadA> DecodeThreadASP;
    class DecodeThreadA : public MMThread
    {
    public:
        DecodeThreadA(FakeAudioDecode *decoder) : MMThread("FakeAudioDecodeThd")
        {
            mDecoder = decoder;
        }
        ~DecodeThreadA() { return; }

    protected:
        virtual void main()
        {
            while(1) {
                MediaBufferSP mediaInputBuffer;
                {
                    if (mDecoder->mIsExit) {
                        INFO("now to exit\n");
                        break;
                    }
                    mDecoder->mReader->read(mediaInputBuffer);
                }
                if (mediaInputBuffer) {
                    uint8_t *buffer = NULL;
                    int decodedSize = 0;
                    int64_t pts,dts;

                    mediaInputBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1);
                    decodedSize = mediaInputBuffer->size();
                    pts = mediaInputBuffer->pts();
                    dts = mediaInputBuffer->dts();

                    uint8_t *buffer2 = new uint8_t [decodedSize];
                    memcpy(buffer2, buffer, decodedSize);
                    MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
                    mediaOutputBuffer->setBufferInfo((uintptr_t *)&buffer2, NULL, &decodedSize, 1);
                    mediaOutputBuffer->setSize(decodedSize);
                    mediaOutputBuffer->setPts(pts);
                    mediaOutputBuffer->setDts(dts);
                    mediaOutputBuffer->setDuration(mediaInputBuffer->duration());
                    mediaOutputBuffer->addReleaseBufferFunc(releaseOutputBuffer);
                    mediaOutputBuffer->setMonitor(mDecoder->mMonitorWrite);

                    INFO("audio info:buffer:%p, size:%d, pts:%" PRId64 ", dts:%" PRId64 ",duration:%" PRId64 "\n",
                              buffer2, decodedSize, pts, dts, mediaInputBuffer->duration());

                    mm_status_t status = mDecoder->mWriter->write(mediaOutputBuffer);
                    if (status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC) {
                        ERROR("decoder fail to write Sink");
                        break;
                    }
                    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
                    trafficControlWrite->waitOnFull();
                 }else { //if (mediaBuffer) {
                    //INFO("read NULL buffer from demuxer\n");
                    usleep(10*1000);
                }

                usleep(5*1000);
            }

            INFO("Audio Output thread exited\n");
            return;
        }

    private:
        FakeAudioDecode *mDecoder;
    };

public:

    FakeAudioDecode(const char *mimeType = NULL, bool isEncoder = false)
    {
        mIsExit = false;
        mMonitorWrite.reset(new TrafficControl(1, 64, "FakeAudioDecodeWrite"));
        return;
    }
    virtual ~FakeAudioDecode() { return; }

    virtual const char * name() const { return "TestAudioDecoder"; }
    COMPONENT_VERSION;

    virtual mm_status_t addSource(Component * component, MediaType mediaType)
    {
        if (component && mediaType == kMediaTypeAudio) {
            mReader = component->getReader(kMediaTypeAudio);
            if (mReader) {
                MediaMetaSP metaData;
                metaData = mReader->getMetaData();
                if (metaData) {
                    mMetaData = metaData->copy();
                    return MM_ERROR_SUCCESS;
                }
            }
        }
        return MM_ERROR_IVALID_OPERATION;
    }

    virtual mm_status_t addSink(Component * component, MediaType mediaType)
    {
        //#0 set the write meta data
        if (component && mediaType == kMediaTypeAudio) {
            mWriter = component->getWriter(kMediaTypeAudio);
            mMetaData->setInt32(MEDIA_ATTR_CODECID, 'AAC');
            mMetaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, 'FLTP');
            mMetaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, 44100);
            mMetaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, 2);
            mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, 128175);
            mMetaData->setFraction(MEDIA_ATTR_TIMEBASE2, 1, 1000000);

            //#1 extraData
            unsigned char buf[40];
            FILE *fp;
            fp = fopen("/home/haibinwuhb/work/cow_player/5.bin", "rb");
            fread(buf,1,5,fp);
            fclose(fp);
            mMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, buf, 5);
            INFO("now start to send audio parameters\n");
            mWriter->setMetaData(mMetaData);
        }

        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t start()
    {
        mIsExit = false;
        if (!mDecodeThread) {
        // create thread to decode buffer
            mDecodeThread.reset (new DecodeThreadA(this), MMThread::releaseHelper);
            mDecodeThread->create();
        }
        usleep(500*1000);
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t stop()
    {
        TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
        trafficControlWrite->unblockWait();

        mIsExit = true;
        usleep(500*1000);
        if (mDecodeThread) {
            mDecodeThread.reset();
        }
        return MM_ERROR_SUCCESS;
    }

    virtual mm_status_t prepare() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t release() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t init() { return MM_ERROR_SUCCESS; }
    virtual void uninit() { return; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t setParameter(const MediaMetaSP & meta) { return MM_ERROR_SUCCESS; }
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t read(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t write(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual MediaMetaSP getMetaData() { return MediaMetaSP((MediaMeta*)NULL); }
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData) { return MM_ERROR_UNSUPPORTED; }

private:

    MediaMetaSP mMetaData;
    DecodeThreadASP mDecodeThread;
    MonitorSP mMonitorWrite;

    ReaderSP mReader;
    WriterSP mWriter;
    bool mIsExit;

    MM_DISALLOW_COPY(FakeAudioDecode);
};

static const char *input_url = NULL;
static const char *output_url = NULL;

int main(int argc, char *argv[])
{
    PlaySourceComponent *avDemuxer;
    ComponentSP rtpMuxer;
    FakeAudioDecode *audioDecoder;
    FakeVideoDecode *videoDecoder;
    mm_status_t status;

    //#0 parse the command
    if (argc == 3) {
        input_url = argv[1];
        output_url = argv[2];
    } else {
        INFO("input parameters are failed\n");
        return -1;
    }
    INFO("input file:%s,output file:%s\n", input_url,output_url);

    //#1 create the avDemuxer plugin
    avDemuxer = new AVDemuxer();
    status = avDemuxer->init();
    if (status != MM_ERROR_SUCCESS) {
        INFO("init source fail %d", status);
        return -1;
    }
    if (input_url)
        avDemuxer->setUri(input_url);
    INFO("avDemuxer had been created,input_url:%s\n",input_url);

    //#2 create the video decoder plugin
    videoDecoder = new FakeVideoDecode();
    status = videoDecoder->init();
    INFO("video decoder had been created\n");

    //#3 create the audio decoder plugin
    audioDecoder = new FakeAudioDecode();
    status = audioDecoder->init();
    INFO("audio decoder had been created\n");

    //#4 create the upload stream plugin
    rtpMuxer = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_MEDIA_RTP_MUXER, false);
    if (!rtpMuxer.get()) {
        INFO("cannot load decoder");
        return -1;
    }
    status = rtpMuxer->init();
    MediaMetaSP paramSP(MediaMeta::create());
    paramSP->setString(YUNOS_MM::MEDIA_ATTR_FILE_PATH, output_url);
    INFO("ggggggggggggg:%s\n", output_url);
    status = rtpMuxer->setParameter(paramSP);

    //#5 link every plugin
    //video::addSource
    status = videoDecoder->addSource(avDemuxer, Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("video decoder add source fail %d", status);
            return -1;
        }
        INFO("video decoder add source ASYNC");
        usleep(10*1000);
    }
    INFO("video decoder had been addSource\n");
    //video::addSink
    status = videoDecoder->addSink(rtpMuxer.get(), Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("video decoder add sink fail %d", status);
            return -1;
        }
        INFO("video decoder add addSink ASYNC");
        usleep(10*1000);
    }
    INFO("video decoder had been addSink\n");
    //audio::addSource
    status = audioDecoder->addSource(avDemuxer, Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("audio decoder add source fail %d", status);
            return -1;
        }
        INFO("audio decoder add source ASYNC");
        usleep(10*1000);
    }
    INFO("audio decoder had been addSource\n");
    //audio::addSink
    status = audioDecoder->addSink(rtpMuxer.get(), Component::kMediaTypeAudio);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("audio decoder add sink fail %d", status);
            return -1;
        }
        INFO("audio decoder add addSink ASYNC");
        usleep(10*1000);
    }
    INFO("audio decoder had been addSink\n");

    //#6 start rtpMuxer
    status = rtpMuxer->prepare();
    if (status != MM_ERROR_SUCCESS) {
       if (status != MM_ERROR_ASYNC) {
           INFO("rtpMuxer prepare fail %d", status);
           return -1;
       }
       INFO("rtpMuxer prepare ASYNC");
       usleep(10*1000);
    }
    INFO("rtpMuxer had been prepare\n");

    status = rtpMuxer->start();
    if (status != MM_ERROR_SUCCESS) {
      if (status != MM_ERROR_ASYNC) {
          INFO("rtpMuxer start fail %d\n", status);
          return -1;
      }

      INFO("rtpMuxer start ASYNC");
      usleep(10*1000);
    }
    INFO("rtpMuxer had been start\n");

    //#7 start videoDecoder
    status = videoDecoder->prepare();
    if (status != MM_ERROR_SUCCESS) {
       if (status != MM_ERROR_ASYNC) {
           INFO("videoDecoder prepare fail %d", status);
           return -1;
       }

       INFO("videoDecoder prepare ASYNC");
       usleep(10*1000);
    }
    INFO("videoDecoder had been prepare\n");

    status = videoDecoder->start();
    if (status != MM_ERROR_SUCCESS) {
      if (status != MM_ERROR_ASYNC) {
          INFO("videoDecoder start fail %d\n", status);
          return -1;
      }

      INFO("videoDecoder start ASYNC");
      usleep(10*1000);
    }
    INFO("videoDecoder had been start\n");

    //#8 start audioDecoder
    status = audioDecoder->prepare();
    if (status != MM_ERROR_SUCCESS) {
       if (status != MM_ERROR_ASYNC) {
           INFO("audioDecoder prepare fail %d", status);
           return -1;
       }

       INFO("audioDecoder prepare ASYNC");
       usleep(10*1000);
    }
    INFO("audioDecoder had been prepare\n");

    status = audioDecoder->start();
    if (status != MM_ERROR_SUCCESS) {
      if (status != MM_ERROR_ASYNC) {
          INFO("audioDecoder start fail %d\n", status);
          return -1;
      }

      INFO("audioDecoder start ASYNC");
      usleep(10*1000);
    }
    INFO("audioDecoder had been start\n");

    //#9 start avDemuxer
    status = avDemuxer->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("avDemuxer prepare fail %d\n", status);
            return -1;
        }

        INFO("avDemuxer prepare\n");
        usleep(10*1000);
    }
    INFO("avDemuxer had been prepare\n");

    status = avDemuxer->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            INFO("avDemuxer start fail %d\n", status);
            return -1;
        }

        INFO("avDemuxer start ASYNC");
        usleep(10*1000);
   }
   INFO("avDemuxer had been start\n");

   //#10 stop the plugins
   INFO("wait to end \n");
   usleep(1000*1000*1000);

   avDemuxer->stop();
   usleep(100*1000);
   INFO("avDemuxer had been stop\n");

   videoDecoder->stop();
   usleep(100*1000);
   INFO("videoDecoder had been stop\n");

   audioDecoder->stop();
   usleep(100*1000);
   INFO("audioDecoder had been stop\n");

   rtpMuxer->stop();
   usleep(100*1000);
   INFO("rtpMuxer had been stop\n");

   //#11 release
   avDemuxer->uninit();
   videoDecoder->uninit();
   audioDecoder->uninit();
   rtpMuxer->uninit();
   INFO("all had been uninit\n");

   delete avDemuxer;
   delete videoDecoder;
   delete audioDecoder;
   rtpMuxer.reset();
   INFO("all had been delete\n");

   return 0;
}
