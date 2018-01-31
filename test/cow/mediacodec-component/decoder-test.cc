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
#include "multimedia/mm_surface_compat.h"


#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "components/mediacodec_decoder.h"
#include "components/av_demuxer.h"
#include "sample_offset.h"

MM_LOG_DEFINE_MODULE_NAME("Cow-MC-comp");

namespace YUNOS_MM {

class StubMCSink : public SinkComponent {

public:
    StubMCSink() {}
    virtual ~StubMCSink() {}
    virtual WriterSP getWriter(MediaType mediaType) {
        WriterSP wsp;

        if (mediaType != kMediaTypeVideo) {
            ERROR("mediaType(%d) is not supported", mediaType);
            return wsp;
        }

        Writer *writer = new StubMCWriter;
        wsp.reset(writer);

        return wsp;
    }

public:
    struct StubMCWriter : public Writer {
        StubMCWriter(){}
        virtual ~StubMCWriter(){}
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData) {return MM_ERROR_SUCCESS;}

        virtual mm_status_t write(const MediaBufferSP & buffer);
    };

public:
    virtual const char * name() const {return "stub mc sink";}
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual int64_t getCurrentPosition() {return -1ll;}
};

mm_status_t StubMCSink::StubMCWriter::write(const MediaBufferSP & buffer) {
    int32_t index = -1;

    MediaMetaSP meta = buffer->getMediaMeta();
    if (!meta.get()) {
        ERROR("no meta data");
        return MM_ERROR_UNSUPPORTED;
    }

    meta->setInt32(MEDIA_ATTR_IS_VIDEO_RENDER, true);
    meta->getInt32(MEDIA_ATTR_BUFFER_INDEX, index);

    INFO("StubMCSink: write mediabuffer size %d, pts %" PRId64 ", index %d", buffer->size(), buffer->pts(), index);

    return MM_ERROR_SUCCESS;
}

class StubMCSource : public Component {
public:
    StubMCSource() {}
    virtual ~StubMCSource() {}
    virtual mm_status_t prepare();

public:
    virtual ReaderSP getReader(MediaType mediaType) {
        ReaderSP rsp;

        if (mediaType != kMediaTypeVideo) {
            ERROR("mediaType(%d) is not supported", mediaType);
            return rsp;
        }

        Reader *reader = new StubMCReader;
        rsp.reset(reader);

        return rsp;
    }

    struct StubMCReader : public Reader {
        StubMCReader() : mNumber(0), mPts(0), mEos(false) {
            mFile = fopen("/data/src19td.ibp.264", "rb");
        }

        virtual ~StubMCReader() {
           if(mFile){
              fclose(mFile);
           }
        }

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();
        private:
            FILE *mFile;
            int mNumber;
            int64_t mPts;
            bool mEos;
    };

public:
    virtual const char * name() const {return "stub mc source";}
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}

};

mm_status_t StubMCSource::prepare() {
    return MM_ERROR_SUCCESS;
}

bool releaseMediaBuffer(MediaBuffer *mediaBuf) {

    uint8_t *pIndex = NULL;
    if (!(mediaBuf->getBufferInfo((uintptr_t *)&pIndex, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }

    MM_RELEASE_ARRAY(pIndex);

    return true;
}

mm_status_t StubMCSource::StubMCReader::read(MediaBufferSP & buffer) {
    INFO("StubMCReader: read mediabuffer");
    size_t sampleSize;
    uint8_t *buf;
    int32_t bufOffset, bufStride;
    size_t count;
    MediaBufferSP mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);

    sampleSize = sampleOffsets[mNumber + 1] - sampleOffsets[mNumber];
    mNumber++;
    mPts += 33 * 1000;

    if (mNumber == (sizeof(sampleOffsets)/sizeof(sampleOffsets[0]) - 1) || mEos || !mFile) {
        INFO("no media data left, send eos, decode %d samples", mNumber);
        mEos = true;
        mediaBuf->setPts(mPts);
        mediaBuf->setSize(0);
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
        buffer = mediaBuf;
        return MM_ERROR_SUCCESS;
    }

    buf = new uint8_t[sampleSize];
    count = fread(buf, 1, sampleSize, mFile);
    if (count != sampleSize) {
        WARNING("read file error, request size %d but return %d bytes", sampleSize, count);
    }
    if (buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 1) {
        WARNING("sample(%d) is not start with annexb prefix", mNumber - 1);
    }


    mediaBuf->setPts(mPts);
    mediaBuf->setSize(sampleSize);

    bufOffset = 0;
    bufStride = sampleSize;
    mediaBuf->setBufferInfo((uintptr_t *)&buf, &bufOffset, &bufStride, 1);
    mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);
    if (mEos) {
        INFO("source signal eos");
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    }

    buffer = mediaBuf;

    return MM_ERROR_SUCCESS;
}

MediaMetaSP StubMCSource::StubMCReader::getMetaData() {
    MediaMetaSP metaSP = MediaMeta::create();

    if (mNumber == (sizeof(sampleOffsets) - 1) || !mFile) {
        INFO("no meta data, send eos");
        metaSP.reset();
        return metaSP;
    }

    metaSP->setString("mime", "video/avc");
    metaSP->setInt32("width", 720);
    metaSP->setInt32("height", 480);

    size_t sampleSize = sampleOffsets[mNumber + 1] - sampleOffsets[mNumber];
    mNumber++;
    uint8_t *buf = new uint8_t[sampleSize];
    size_t count = fread(buf, 1, sampleSize, mFile);
    if (count != sampleSize) {
        WARNING("read file error, request size %d but return %d bytes", sampleSize, count);
    }
    MediaMeta::ByteBuffer* csd0 = new MediaMeta::ByteBuffer;
    MediaMeta::ByteBuffer* csd1 = new MediaMeta::ByteBuffer;
    csd0->size = sampleSize;
    csd0->data = buf;
    metaSP->setPointer("csd0", csd0);

    sampleSize = sampleOffsets[mNumber + 1] - sampleOffsets[mNumber];
    mNumber++;

    buf = new uint8_t[sampleSize];
    count = fread(buf, 1, sampleSize, mFile);
    if (count != sampleSize) {
        WARNING("read file error, request size %d but return %d bytes", sampleSize, count);
    }
    csd1->size = sampleSize;
    csd1->data = buf;
    metaSP->setPointer("csd1", csd1);

    //TODO delete csd and buf somewhere
    return metaSP;
}

} // YUNOS_MM

using namespace YUNOS_MM;

#include "SimpleSubWindow.h"
WindowSurface *ws;
bool g_animate_win = false;
uint32_t g_surface_width = 1280, g_surface_height = 720;

static const char *input_url = NULL;
static void
usage(int error_code)
{
    fprintf(stderr, "Usage: decoder-test [OPTIONS]\n\n"
        "  -f\tmedia file\n"
        "  -w\tvideo surface width\n"
        "  -h\tvideo surface height\n"
        "  -?\tThis help text\n\n");

    exit(error_code);
}

void parseCommandLine(int argc, char **argv)
{
    int res;
    if (argc == 2) { // To continue support "mediacodectest input_file" usage
        input_url = argv[1];
        return;
    }

    while ((res = getopt(argc, argv, "f:x:y:h:w:a:ndmbrc:")) >= 0) {
        switch (res) {
            case 'f':
                input_url = optarg;
                break;
            case 'h':
                sscanf(optarg, "%08d", &g_surface_height);
                break;
            case 'w':
                sscanf(optarg, "%08d", &g_surface_width);
                break;
            case '?':
            default:
                usage(-1);
                break;
        }
    }

    INFO("create window size %dx%d\n", g_surface_width, g_surface_height);
}



int main(int argc, char *argv[])
{
    ComponentSP decoderSP;
    //Component *source;
    PlaySourceComponent *source;
    //Component *decoder;
    Component *sink;
    mm_status_t status;

    parseCommandLine(argc, argv);
    //source = new StubMCSource;
    source = new AVDemuxer;
    status = source->init();
    if (status != MM_ERROR_SUCCESS) {
        ERROR("init source fail %d", status);
        return -1;
    }

    source->setUri("/usr/bin/ut/res/video/trailer_short.mp4");

    if (input_url)
        source->setUri(input_url);

    decoderSP = ComponentFactory::create(NULL, YUNOS_MM::MEDIA_MIMETYPE_VIDEO_AVC, false);
    if (!decoderSP.get()) {
        ERROR("cannot load decoder");
        delete source;
        return -1;
    }

    status = decoderSP->init();
    if (status != MM_ERROR_SUCCESS) {
        ERROR("init decoder fail %d", status);
        return -1;
    }

    sink = new StubMCSink;

    status = source->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder add source fail %d", status);
            return -1;
        }

        INFO("prepare source ASYNC");
        usleep(10*1000);
    }

    status = source->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("start source fail %d", status);
            return -1;
        }

        INFO("start source ASYNC");
        usleep(10*1000);
    }

    status = decoderSP->addSource(source, Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder add source fail %d", status);
            return -1;
        }
        INFO("decoder add source ASYNC");
        usleep(10*1000);
    }

    status = decoderSP->addSink(sink, Component::kMediaTypeVideo);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder add sink fail %d", status);
            return -1;
        }
        INFO("decoder add sink ASYNC");
        usleep(10*1000);
    }

    initializePageWindow();
    createSubWindow(&ws);
    MediaMetaSP paramSP(MediaMeta::create());

    /*
    if (sizeof(void*) == 8)
        paramSP->writeInt64((int64_t)ws);
    else
        paramSP->writeInt32((int32_t)ws);
    */
    paramSP->setPointer("surface", (uint8_t *)ws);

    status = decoderSP->setParameter(paramSP);
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder set parameter fail %d", status);
            return -1;
        }
        INFO("decoder prepare ASYNC, wait 10ms for setParameter completion");
        usleep(10*1000);
    }

    status = decoderSP->prepare();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder prepare fail %d", status);
            return -1;
        }
        INFO("decoder prepare ASYNC, wait 10ms for prepare completion");
        usleep(100*1000);
    }

    status = decoderSP->start();
    if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder start fail %d", status);
            return -1;
        }
        INFO("decoder start ASYNC");
    }

    //printf("enter to stop...");
    //getchar();
    INFO("wait to end \n");
    sleep(20);
    decoderSP->stop();
    decoderSP->uninit();

    deinitializePageWindow();

    return 0;
}
