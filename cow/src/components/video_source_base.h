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

#ifndef __video_source_base_H
#define __video_source_base_H

#include <multimedia/component.h>
#include <multimedia/mmmsgthread.h>
#include "multimedia/media_meta.h"

namespace YUNOS_MM {

#define VIDEO_SOURCE_DEFAULT_WIDTH     640
#define VIDEO_SOURCE_DEFAULT_HEIGHT    480
#define VIDEO_SOURCE_DEFAULT_FPS       24.0f

#define VIDEO_SOURCE_CORLOR_FMT_NV12   'NV12'
#define VIDEO_SOURCE_CORLOR_FMT_NV21   'NV21'
#define VIDEO_SOURCE_CORLOR_FMT_YV12   'YV12'
#define VIDEO_SOURCE_CORLOR_FMT_YV21   'YV21'
#define VIDEO_SOURCE_CORLOR_FMT_I420   'I420'
#define VIDEO_SOURCE_CORLOR_FMT_YUY2   'YUY2'
#define VIDEO_SOURCE_CORLOR_FMT_YUYV   'YUYV'
#define VIDEO_SOURCE_CORLOR_FMT_YVYU   'YVYU'
#define VIDEO_SOURCE_CORLOR_FMT_RGBX   'RGBX'

#define VIDEO_SOURCE_DEFAULT_CFORMAT   VIDEO_SOURCE_CORLOR_FMT_NV12

enum SourceType
{
    AUTO,
    YUVFILE,
    CAMERA_BACK,
    CAMERA_FRONT,
    WFD,
    NONE
};

class GraphicBufferReader;

template <typename frameGenerator>
class VideoSourceBase : public RecordSourceComponent, public MMMsgThread {

public:

    VideoSourceBase(const char *threadName, bool async = true, bool asyncStop = false);
    virtual ~VideoSourceBase();

    const char * name() const;
    COMPONENT_VERSION;

    // it's pull mode
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    virtual mm_status_t init();
    virtual void uninit();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t prepare();
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t resume() { mRequestIDR = true; return MM_ERROR_SUCCESS; }
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t signalEOS();
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length);

    class GraphicBufferReader : public Component::Reader {
    public:
        GraphicBufferReader(VideoSourceBase *comp);
        virtual ~GraphicBufferReader();

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        // uint32_t mFrameNum;
        // int64_t mFramePts;
        VideoSourceBase *mComponent;
    };

typedef MMSharedPtr<frameGenerator> frameGeneratorSP;

    friend class GraphicBufferReader;
protected:

    void signalBufferReturned(void*, int64_t);
    MediaBufferSP getMediaBuffer();

    mm_status_t stopInternal();
    void asyncFuncExit(Component::Event event, int param1, int param2, uint32_t rspId);
    mm_status_t processMsg(uint32_t msg,
                           param1_type param1,
                           param2_type param2,
                           param3_type param3=MMRefBaseSP((MMRefBase*)NULL));


    enum State {
        UNINITIALIZED,
        INITIALIZED,
        PREPARED,
        STARTED,
        STOPED
    };

    std::string mComponentName;
    std::string mUri;

    State mState;
    SourceType mType;

    bool mStreamEOS;
    bool mAsync;
    bool mAsyncStop;

    frameGeneratorSP mFrameSourceSP;
    //frameGenerator *frameSource;
    bool mReaderExist;

    MediaMetaSP mSourceFormat;

    Lock mLock;
    Condition mCondition;

    bool mRequestIDR;
    //DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    //DECLARE_MSG_HANDLER2(onSetParameter)
    DECLARE_MSG_HANDLER(onStop)
    //DECLARE_MSG_HANDLER(onStart)
    //DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(VideoSourceBase)
};

// Graphic Buffer Source Message
#define CAMERA_MSG_prepare (msg_type)1
#define CAMERA_MSG_pause (msg_type)2
#define CAMERA_MSG_flush (msg_type)3
#define CAMERA_MSG_setParameter (msg_type)7
#define CAMERA_MSG_stop (msg_type)8
#define CAMERA_MSG_start (msg_type)9
#define CAMERA_MSG_reset (msg_type)10
//#define CAMERA_MSG_seek (msg_type)11
#define CAMERA_MSG_END (msg_type)12

} // YUNOS_MM

#endif // __video_source_base
