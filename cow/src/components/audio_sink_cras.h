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

#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "clock_wrapper.h"

namespace YUNOS_MM {

//#define DUMP_SINK_CRAS_DATA

class ClockWrapper;
class Clock;
typedef std::tr1::shared_ptr <Clock> ClockSP;
typedef std::tr1::shared_ptr<ClockWrapper> ClockWrapperSP;

class AudioSinkCras : public MMMsgThread, public PlaySinkComponent{
public:

    AudioSinkCras(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioSinkCras();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType);
    virtual ClockSP provideClock();
    virtual mm_status_t init();
    virtual void uninit();

    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
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

    class AudioSinkWriter : public Writer {
    public:
        AudioSinkWriter(AudioSinkCras *sink){
            mRender = sink;
        }
        virtual ~AudioSinkWriter(){}
        virtual mm_status_t write(const MediaBufferSP &buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
    private:
        AudioSinkCras *mRender;
    };

protected:
    void audioRenderError();

private:
    virtual mm_status_t scheduleEOS(int64_t delayEOSUs);
    mm_status_t getOutputSampleRate();
    std::string mComponentName;
    class Private;
    typedef std::tr1::shared_ptr<Private> PrivateSP;
    PrivateSP mPriv;
    double mVolume;
    bool mMute;
    int64_t mCurrentPosition;

    MM_DISALLOW_COPY(AudioSinkCras);

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onWrite)
    DECLARE_MSG_HANDLER(onAudioRenderError)
    DECLARE_MSG_HANDLER(onScheduleEOS)

};

}



