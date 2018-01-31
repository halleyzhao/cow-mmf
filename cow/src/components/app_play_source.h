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

#ifndef __app_play_source_component_H
#define __app_play_source_component_H

extern "C" {
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <inttypes.h>
}

#include <vector>

#include <multimedia/component.h>
#include <multimedia/mm_cpp_utils.h>
#include <queue>

namespace YUNOS_MM {

class APPPlaySource : public PlaySourceComponent {
public:
    APPPlaySource();
    ~APPPlaySource();

public:
    virtual mm_status_t init() { return MM_ERROR_SUCCESS; }
    virtual void uninit() {}

    virtual const std::list<std::string> & supportedProtocols() const;
    virtual bool isSeekable() { return false; }
    virtual mm_status_t getDuration(int64_t & durationMs) {return MM_ERROR_UNSUPPORTED;}
    virtual bool hasMedia(MediaType mediaType) { return false; }
    virtual MediaMetaSP getMetaData() { return mMeta; }
    virtual MMParamSP getTrackInfo() {return MMParamSP((MMParam*)NULL);}
    virtual mm_status_t selectTrack(MediaType mediaType, int index){return MM_ERROR_UNSUPPORTED;}
    virtual int getSelectedTrack(MediaType mediaType){return -1;}

    virtual mm_status_t pushData(MediaBufferSP & buffer);

public:
    virtual const char * name() const { return "APPPlaySource"; }
    COMPONENT_VERSION;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }
    virtual WriterSP getWriter(MediaType mediaType);

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t prepare() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t start() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t stop() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t drain() { return MM_ERROR_SUCCESS; }

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) { return MM_ERROR_UNSUPPORTED; }

private:
    class APPPlaySourceReader : public Reader {
    public:
        APPPlaySourceReader(APPPlaySource * component, MediaType mediaType);
        ~APPPlaySourceReader();

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        APPPlaySource * mComponent;
        MediaType mMediaType;
        DECLARE_LOGTAG()
    };
    typedef MMSharedPtr<APPPlaySourceReader> APPPlaySourceReaderSP;

    class APPPlaySourceWriter : public Writer {
    public:
        APPPlaySourceWriter(APPPlaySource * component);
        ~APPPlaySourceWriter();

    public:
        virtual mm_status_t write(const MediaBufferSP & buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

    private:
        APPPlaySource * mComponent;
        DECLARE_LOGTAG()
    };
    typedef MMSharedPtr<APPPlaySourceWriter> APPPlaySourceWriterSP;

    struct Stream {
        Stream();
        ~Stream();

        void flush();

        MediaMetaSP mMeta;
        std::queue<MediaBufferSP> mBuffers;
        bool mDropBuffer;
        uint32_t mInputBufferCount;
        uint32_t mOutputBufferCount;
        uint32_t mDropBufferCount;
        MM_DISALLOW_COPY(Stream)
        DECLARE_LOGTAG()
    };

protected:
    mm_status_t write(const MediaBufferSP & buffer);
    mm_status_t read(MediaBufferSP & buffer, MediaType mediatype);
    mm_status_t setMeta(const MediaMetaSP & metaData);
    MediaMetaSP getMeta(MediaType mediatype);

    MediaType getMediaTypeByMeta(const MediaMetaSP & meta);
    Stream * getStreamByMediaType(MediaType mediaType);

private:
    Lock mLock;
    MediaMetaSP mMeta;

    Stream mStreams[kMediaTypeCount];

    MM_DISALLOW_COPY(APPPlaySource)
    DECLARE_LOGTAG()
};

}

#endif

