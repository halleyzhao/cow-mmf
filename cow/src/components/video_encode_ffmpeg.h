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

#ifndef __VIDEO_ENCODE_FFMPEG_H__
#define __VIDEO_ENCODE_FFMPEG_H__

#include <map>

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_debug.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/av_buffer_helper.h"
#include "multimedia/media_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifdef __MM_NATIVE_BUILD__
#include <libavutil/time.h>
#else
#include <libavutil/avtime.h>
#endif
#include <libavutil/opt.h>
#include <libavutil/common.h>
#include "multimedia/av_buffer_helper.h"
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

namespace YUNOS_MM
{
#ifdef  _VIDEO_CODEC_FFMPEG

class VideoEncodeFFmpeg;
typedef MMSharedPtr <VideoEncodeFFmpeg> VideoEncodeFFmpegSP;

class VideoEncodeFFmpeg : public FilterComponent, public MMMsgThread
{

public:
    class EncodeThread;
    typedef MMSharedPtr<EncodeThread> EncodeThreadSP;
    class EncodeThread : public MMThread
    {
    public:
        EncodeThread(VideoEncodeFFmpeg *encoder);
        ~EncodeThread();
        void signalExit();
        void signalContinue();

    protected:
        virtual void main();

    private:
        VideoEncodeFFmpeg *mEncoder;
        bool mContinue;
        typedef enum {
            eEosNormal,
            eEOSInput,
            eEOSOutput,
        } EosState;
        EosState mEos;
    };

    VideoEncodeFFmpeg(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~VideoEncodeFFmpeg();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t release();
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    //virtual mm_status_t getParameter(MediaMetaSP & meta) const;

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
    mm_status_t	configEncoder();
    mm_status_t	openEncoder();
    mm_status_t encodeFrame(uint8_t *data, int64_t & pts, int64_t & dts,
                            unsigned char **out, int *outSize);
    void closeEncoder();
    mm_status_t parseMetaFormat(const MediaMetaSP & meta, bool isInput);

private:

    std::string mComponentName;
    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;

    ReaderSP mReader;
    WriterSP mWriter;
    bool mIsPaused;

    AVCodecID mCodecID;
    AVCodecContext *mAVCodecContext;
    AVCodec *mAVCodec;
    AVPacket *mAVPacket;
    AVFrame *mAVFrame;
    uint8_t  *mScaledBuffer;
    AVPixelFormat mInputFormat;
    int32_t mInputWidth;
    int32_t mInputHeight;
    AVPixelFormat mEncodeFormat;
    int32_t mEncodeWidth;
    int32_t mEncodeHeight;
    float mFrameFps;
    int32_t mBitRate;
    int32_t mFlags;
    uint32_t mCRF;
    uint32_t mPreset;

    MonitorSP mMonitorWrite;
    Condition mCondition;
    Lock mLock;
    EncodeThreadSP mEncodeThread;

    // debug use
    uint32_t mInputBufferCount;
    uint32_t mOutputBufferCount;

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoEncodeFFmpeg);
};
#endif
}

#endif //__audio_Encode_ffmpeg_h



