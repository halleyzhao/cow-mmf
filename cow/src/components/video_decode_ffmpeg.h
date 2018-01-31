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

#ifndef __VIDEO_DECODE_FFMPEG_H__
#define __VIDEO_DECODE_FFMPEG_H__

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
#include <libswscale/swscale.h>
#include <libavcodec/h264_parse.h>

#ifdef __cplusplus
}
#endif
//#define WindowSurface Surface
//class Surface;

#ifndef __MM_NATIVE_BUILD__
namespace YunOSMediaCodec {
    class MediaSurfaceTexture;
}
#endif

namespace YUNOS_MM
{
#ifdef  _VIDEO_CODEC_FFMPEG
class VideoDecodeFFmpeg;
typedef MMSharedPtr <VideoDecodeFFmpeg> VideoDecoderFFmpegSP;

class VideoDecodeFFmpeg : public FilterComponent, public MMMsgThread
{

public:
    class DecodeThread;
    typedef MMSharedPtr <DecodeThread> DecodeThreadSP;
    class DecodeThread : public MMThread
    {
    public:
        DecodeThread(VideoDecodeFFmpeg *decoder);
        ~DecodeThread();
        void signalExit();
        void signalContinue();

    protected:
        virtual void main();

    private:
        VideoDecodeFFmpeg *mDecoder;
        bool mContinue;
        uint32_t mInputFrameCount, mOutputFrameCount; // debug use only
    };

    VideoDecodeFFmpeg(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~VideoDecodeFFmpeg();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType);
    virtual mm_status_t release();
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t resume();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
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
    virtual MediaMetaSP getMetaData() {return mInputMetaData;}
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData) { return MM_ERROR_UNSUPPORTED; }

private:
    AVCodecID CodecId2AVCodecId(CowCodecID id);
    MediaBufferSP createMediaBufferFromAVFrame();
    mm_status_t parseInputMeta(MediaMetaSP & meta);
    static bool releaseOutputBuffer(MediaBuffer* mediaBuffer);
    static bool releaseOutputAVBuffer(MediaBuffer* mediaBuffer);
#ifdef __USEING_SOFT_VIDEO_CODEC_FOR_MS__
    static int h264_set_extradata(AVCodecContext *avctx, int *dpbSize);
#endif

private:

    std::string mComponentName;
    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;

    ReaderSP mReader;
    WriterSP mWriter;
    bool mIsPaused;
    bool mNeedFlush;

    AVCodecContext *mAVCodecContext;
    bool mAVCodecContextByUs;
    Lock *mAVCodecContextLock;
    AVCodec *mAVCodec;
    AVPacket *mAVPacket;
    AVFrame *mAVFrame;
    int32_t mCodecID;

    AVPixelFormat mSrcFormat;
    int32_t mSrcWidth;
    int32_t mSrcHeight;
    AVPixelFormat mDstFormat;
    int32_t mDstWidth;
    int32_t mDstHeight;
    bool mNotifyWH;

    MonitorSP mMonitorWrite;
    Condition mCondition;
    Lock mLock;
    DecodeThreadSP mDecodeThread;

#ifdef __DEBUG_DUMP_RAW_VIDEO__
    DataDump mDumpRawVideo;
#endif

#ifndef __MM_NATIVE_BUILD__
    // sw decoder has no concept of surface/window, treat it as opaque void*
    void* mSurfaceTexture;
    void* mNativeWindow;
#endif

    int64_t mTargetTimeUs;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoDecodeFFmpeg);
};
#endif
}

#endif //__audio_decode_ffmpeg_h


