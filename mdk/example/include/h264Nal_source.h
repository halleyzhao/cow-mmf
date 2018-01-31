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

#ifndef example_source_plugin_h
#define example_source_plugin_h
extern "C" {
#include <libavformat/avformat.h>
}


#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_buffer.h"

using namespace YUNOS_MM;

#define AT_MSG_start (msg_type)1

    // format specific data, for future extension.
    struct VideoExtensionBuffer {
        int32_t extType;
        int32_t extSize;
        uint8_t *extData;
    };
    struct VideoDecodeBuffer {
        uint8_t *data;
        int32_t size;
        int64_t timeStamp;
        uint32_t flag;
        VideoExtensionBuffer *ext;
    };

class H264NalSource : public PlaySourceComponent, public MMMsgThread {

public:
    H264NalSource();
    virtual ~H264NalSource();
    virtual mm_status_t prepare();
    virtual mm_status_t stop();
    virtual mm_status_t start();
    bool isEOS() {return mParseToEOS;}
public:
    virtual Component::ReaderSP getReader(MediaType mediaType);

    struct H264NalSourceReader : public Reader {
        H264NalSourceReader(H264NalSource *source) {mSource = source;}
        virtual ~H264NalSourceReader() {}
        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();
    private:
        H264NalSource *mSource;
};

public:
    virtual const char * name() const {return "H264NalSource";}
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL);
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length);

 public:
    virtual const std::list<std::string> & supportedProtocols() const;
    virtual bool isSeekable() {return false;}
    virtual mm_status_t getDuration(int64_t & durationMs);
    virtual bool hasMedia(MediaType mediaType) {return true;}
    virtual MediaMetaSP getMetaData();
    virtual MMParamSP getTrackInfo() { return MMParamSP((MMParam*)NULL); }
    virtual mm_status_t selectTrack(MediaType mediaType, int index) {return MM_ERROR_UNSUPPORTED;}
    virtual int getSelectedTrack(MediaType mediaType) {return mVideoStream;}
private:
    enum state_e {
        STATE_NONE,
        STATE_PREPARING,
        STATE_PREPARED,
        STATE_STARTED,
        STATE_STOPPED
    };
    MediaBufferSP  createMediaBuffer(VideoDecodeBuffer *buf);
    bool ensureBufferData();
    bool getOneNaluInput(VideoDecodeBuffer &inputBuffer);

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onStart)
    char* mUri;
    std::list<MediaBufferSP> mAvailableSourceBuffers;
    MediaMetaSP mMetaData;
    AVFormatContext *mInFmtCtx;
    AVCodecContext *mInCodecCtx;
    AVCodec *mInCodec;
    AVPacket *mPacket;
    int mVideoStream;
    state_e mState;
    Lock mLock;
    FILE *mFp;
    int64_t mLength;
    uint32_t mLastReadOffset; // data has been consumed by decoder already
    uint32_t mAvailableData;  // available data in m_buffer
    uint8_t *mBuffer;
    bool mReadToEOS;
    bool mParseToEOS;
};

#endif// example_source_plugin_h
