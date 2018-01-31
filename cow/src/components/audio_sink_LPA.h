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

#include <tr1/memory>
#include <queue>
#include <unistd.h>

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "clock_wrapper.h"
#include "multimedia/mm_audio.h"
#include "multimedia/av_buffer_helper.h"

//#define DUMP_SINK_CRAS_DATA
//#define DECODE_BEFORE_RENDER

#ifdef DECODE_BEFORE_RENDER
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
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif
#endif

namespace YUNOS_MM {

class ClockWrapper;
class Clock;
typedef std::tr1::shared_ptr <Clock> ClockSP;
typedef std::tr1::shared_ptr<ClockWrapper> ClockWrapperSP;

class AudioSinkLPA : public MMMsgThread, public PlaySinkComponent{
public:
    class ReadThread;
    typedef MMSharedPtr <ReadThread> ReadThreadSP;
    class ReadThread : public MMThread {
      public:
        ReadThread(AudioSinkLPA *sink);
        ~ReadThread();
        void signalExit();
        void signalContinue();

      protected:
        virtual void main();

      private:
        AudioSinkLPA *mAudioSink;
        bool mContinue;
    };

    AudioSinkLPA(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioSinkLPA();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual ClockSP provideClock();
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t addSource(Component * component, MediaType mediaType);
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t resume();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setAudioStreamType(int type);
    virtual mm_status_t getAudioStreamType(int *type);
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual int64_t getCurrentPosition();
    mm_status_t setVolume(double volume);
    double getVolume();
    mm_status_t setMute(bool mute);
    bool getMute();
    bool isLPASupport(const MediaMetaSP & meta);

protected:
    void audioRenderError();

private:
    virtual mm_status_t scheduleEOS(int64_t delayEOSUs);
    mm_status_t scheduleEOS2Audio();
    mm_status_t write(const MediaBufferSP & buf);
    void startPauseTimeout();
    void canclePauseTimeout();
    int64_t getCurrentPositionInternal();
    mm_status_t scheduleNewAudioRender();
    std::string mComponentName;
    class Private;
    typedef std::tr1::shared_ptr<Private> PrivateSP;
    PrivateSP mPriv;
    double mVolume;
    bool mMute;
    bool mIsLPA;
    int64_t mCurrentPosition;
    int64_t mSeekPosition;
    bool mNeedSeekForPauseTimeout;

#ifdef DECODE_BEFORE_RENDER
    virtual mm_status_t release();
    static AVSampleFormat convertAudioFormat(snd_format_t formt);
    MediaMetaSP mInputMetaData;
    MediaMetaSP mOutputMetaData;
    bool mNeedFlush;
    AVCodecContext *mAVCodecContext;
    AVCodec *mAVCodec;
    AVPacket *mAVPacket;
    AVFrame *mAVFrame;
    int32_t mCodecID;
    struct SwrContext *mAVResample;
    bool mHasResample;
    int64_t mTargetTimeUs;
#endif
    ReaderSP mReader;
    bool mIsPaused;
    bool mIsEOS;
    int64_t mCBCount;
    MonitorSP mMonitorWrite;
    Condition mCondition;
    Lock mLock;
    ReadThreadSP mReadThread;
    int32_t mPauseTimeoutGeneration;

    MM_DISALLOW_COPY(AudioSinkLPA);

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSetParameters)
    DECLARE_MSG_HANDLER(onGetParameters)
    DECLARE_MSG_HANDLER(onWrite)
    DECLARE_MSG_HANDLER(onAudioRenderError)
    DECLARE_MSG_HANDLER(onScheduleEOS)
    DECLARE_MSG_HANDLER(onScheduleEOS2Audio)
    DECLARE_MSG_HANDLER(onStartPauseTimeout)
    DECLARE_MSG_HANDLER(onScheduleNewAudioRender)

};

}



