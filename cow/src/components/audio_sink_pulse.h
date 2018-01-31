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

#include <queue>

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/clock.h"
#include "../clock_wrapper.h"

#ifndef __COW_COMPONENT_AUDIO_SINK_PULSE
#define __COW_COMPONENT_AUDIO_SINK_PULSE

namespace YUNOS_MM {

//#define DUMP_SINK_PULSE_DATA

class AudioSinkPulse : public MMMsgThread, public PlaySinkComponent{
public:

    AudioSinkPulse(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioSinkPulse();

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
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta) const;
    virtual int64_t getCurrentPosition();

    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;


    class AudioSinkWriter : public Writer {
    public:
        AudioSinkWriter(AudioSinkPulse *sink){
            mRender = sink;
        }
        virtual ~AudioSinkWriter(){}
        virtual mm_status_t write(const MediaBufferSP &buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
    private:
        AudioSinkPulse *mRender;
    };

private:

    std::string mComponentName;
    class Private;
    typedef MMSharedPtr<Private> PrivateSP;
    PrivateSP mPriv;

    MM_DISALLOW_COPY(AudioSinkPulse);

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onPause)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onSeek)
    DECLARE_MSG_HANDLER(onReset)
    DECLARE_MSG_HANDLER(onSetParameters)
    DECLARE_MSG_HANDLER(onGetParameters)
    DECLARE_MSG_HANDLER(onWrite)
    DECLARE_MSG_HANDLER(onSetMetaData)

};

}


#endif
