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

namespace YUNOS_MM {

//#define DUMP_SRC_PULSE_DATA


class AudioSrcPulse : public MMMsgThread, public RecordSourceComponent{
public:

    AudioSrcPulse(const char *mimeType = NULL, bool isEncoder = false);
    virtual ~AudioSrcPulse();

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType){ return WriterSP((Writer*)NULL); }
    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t init();
    virtual void uninit();
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

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
    virtual mm_status_t signalEOS();

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) {return MM_ERROR_UNSUPPORTED;}

    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;

private:
    class Private;
    std::string mComponentName;
    typedef MMSharedPtr<Private> PrivateSP;
    PrivateSP mPriv;

    MM_DISALLOW_COPY(AudioSrcPulse);

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

};

}



