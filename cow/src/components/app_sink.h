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

#ifndef __app_sink_component_H
#define __app_sink_component_H

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

#include <multimedia/component.h>
#include <multimedia/mm_cpp_utils.h>
#include <queue>

namespace YUNOS_MM {

class APPSink : public SinkComponent {
public:
    APPSink();
    ~APPSink();

public:
    virtual mm_status_t init() { return MM_ERROR_SUCCESS; }
    virtual void uninit() {}

    virtual int64_t getCurrentPosition() { return -1; }

public:
    virtual const char * name() const { return "APPSink"; }
    COMPONENT_VERSION;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) { return MM_ERROR_SUCCESS; }
    virtual mm_status_t prepare() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t start() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t stop() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

private:
    class APPSinkReader : public Reader {
    public:
        APPSinkReader(APPSink * component);
        ~APPSinkReader();

    public:
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        APPSink * mComponent;
        DECLARE_LOGTAG()
    };
    typedef MMSharedPtr<APPSinkReader> APPSinkReaderSP;

    class APPSinkWriter : public Writer {
    public:
        APPSinkWriter(APPSink * component);
        ~APPSinkWriter();

    public:
        virtual mm_status_t write(const MediaBufferSP & buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

    private:
        APPSink * mComponent;
        DECLARE_LOGTAG()
    };
    typedef MMSharedPtr<APPSinkWriter> APPSinkWriterSP;

protected:
    mm_status_t write(const MediaBufferSP & buffer);
    mm_status_t read(MediaBufferSP & buffer);
    mm_status_t setMeta(const MediaMetaSP & metaData);
    MediaMetaSP getMeta();

private:
    void dropNonRefFrame_l(uint32_t maxQSize);
    Lock mLock;
    MediaMetaSP mMeta;
    std::queue<MediaBufferSP> mBuffers;
    bool mIsLive;
    uint32_t mDropFrameThreshHold1; // non-ref frame is dropped when cached frame count exceed mDropFrameThreshHold1
    uint32_t mDropFrameThreshHold2; // frame is dropped when cached frame count exceed mDropFrameThreshHold2

    // debug use
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;
    uint32_t mDropedBufferCount;

    MM_DISALLOW_COPY(APPSink)
    DECLARE_LOGTAG()
};

}

#endif

