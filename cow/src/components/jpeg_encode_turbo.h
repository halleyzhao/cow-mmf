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

#ifndef __JPEG_ENCODE_TURBO_H__
#define __JPEG_ENCODE_TURBO_H__

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "multimedia/mm_debug.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
//#include "multimedia/av_buffer_helper.h"
#include "multimedia/media_monitor.h"

#include "turbojpeg.h"

namespace YUNOS_MM {

class JpegEncodeTurbo;
typedef MMSharedPtr <JpegEncodeTurbo> JpegEncodeTurboSP;

class JpegEncodeTurbo : public FilterComponent, public MMMsgThread
{
public:
    class EncoderThread;
    typedef MMSharedPtr <EncoderThread> EncoderThreadSP;
    class EncoderThread : public MMThread
    {
    public:
        EncoderThread(JpegEncodeTurbo *encoder);
        ~EncoderThread();
        void signalExit();
        void signalContinue();

    protected:
        virtual void main();
    private:
        mm_status_t createJPEGFromYV12(uint8_t* yuvBuffer, int yuvSize,
                                                int pad,unsigned char** jpegBuffer,
                                                unsigned long* jpegSize);

        static mm_status_t YUYV2YV12(uint8_t *yuyv, uint8_t *yv12,
                                                            uint32_t width, uint32_t height);
    private:
        JpegEncodeTurbo *mEncoder;
        bool mContinue;
    };

    JpegEncodeTurbo(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~JpegEncodeTurbo();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t release();
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t resume(){ return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t stop();
    virtual mm_status_t pause(){ return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS;}
    virtual mm_status_t reset();
    virtual mm_status_t flush() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    // must return immediataly
    virtual mm_status_t read(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    // must return immediataly
    virtual mm_status_t write(MediaBufferSP & buf) { return MM_ERROR_UNSUPPORTED; }
    virtual MediaMetaSP getMetaData() {return MediaMetaSP((MediaMeta*)NULL);}
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData) { return MM_ERROR_UNSUPPORTED; }

private:

    tjhandle    mHandle;
    MMSharedPtr <uint8_t> mJpegBuffer;
    MMSharedPtr <uint8_t> mYUVConvertBuffer;
    int32_t     mWidth;
    int32_t     mHeight;
    int32_t     mFourcc;
    int32_t     mQuality;
    int32_t     mPhotoCount;

    std::string mComponentName;
    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;

    ReaderSP    mReader;
    WriterSP    mWriter;
    bool        mIsPaused;
    MonitorSP   mMonitorWrite;
    Condition   mCondition;
    Lock        mLock;
    EncoderThreadSP mEncoderThread;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(JpegEncodeTurbo);
};

} // end of namespace YUNOS_MM
#endif//file_sink_h

