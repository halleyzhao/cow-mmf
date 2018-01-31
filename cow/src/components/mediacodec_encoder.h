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

#ifndef __mediacodec_encoder_h
#define __mediacodec_encoder_h

#include "mediacodec_component.h"

namespace YUNOS_MM {

using namespace YunOSMediaCodec;

typedef MMSharedPtr < YunOSMediaCodec::Notify > NotifySP;

class MediaBufferThread : public MMMsgThread {
public:
    MediaBufferThread(const char* componentName);
    ~MediaBufferThread();

    // start thread
    mm_status_t init();
    // stop thread
    void uninit();
    // param
    // reader: from which to read MediaBuffer
    // notify: to which it notify MediaBuffer
    mm_status_t configure(Component::ReaderSP reader, YunOSMediaCodec::Notify *notify);
    // start read and notify
    mm_status_t start();
    // stop read
    mm_status_t stop();
    // clear config parameter
    mm_status_t reset();

private:
    mm_status_t processMsg(uint32_t msg, param1_type param1, param2_type param2);

private:
    bool mPaused;
    int32_t mThreadGeneration;

#define MBT_MSG_configure (msg_type)1
#define MBT_MSG_start (msg_type)2
#define MBT_MSG_stop (msg_type)3
#define MBT_MSG_reset (msg_type)4
#define MBT_MSG_acquireSourceBuffer (msg_type)5

    Component::ReaderSP mReader;
    NotifySP mNotify;
    std::string mInitName;

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onConfigure)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(acquireSourceBuffer)
};

class MediaCodecEnc : public MediaCodecComponent {

public:
    MediaCodecEnc(const char *mime);
    virtual ~MediaCodecEnc();

    virtual mm_status_t resume() {return MM_ERROR_SUCCESS;}
    virtual mm_status_t pause() {return MM_ERROR_SUCCESS;}
    virtual mm_status_t setParameter(const MediaMetaSP & meta);

private:
    MediaBufferThread *mBufferThread;
    MediaMetaSP mVideoEncParam;
    MediaMetaSP mAvcEncParam;
    MonitorSP mMonitorFPS;
    std::list<int64_t> mDecodingTimeQueue;

private:
    virtual bool handleInputBuffer(); /* handle codec input buffer */
    virtual bool handleOutputBuffer(); /* handle codec output buffer */
    virtual bool readSourceMeta();
    virtual bool writeSinkMeta();

    virtual void scheduleAcquire();
    virtual mm_status_t readSourceBuffer(MediaBufferSP &mediaBuf);
    virtual bool setupMediaCodecParam(MediaMetaSP &params);

    void dump(uint8_t *data, uint32_t size);

    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSourceBufferAvailable)

    MM_DISALLOW_COPY(MediaCodecEnc);
};

} // end of namespace YUNOS_MM
#endif //__mediacodec_encoder_h
