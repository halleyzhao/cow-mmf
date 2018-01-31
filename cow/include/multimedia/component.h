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


#ifndef __component_H
#define __component_H

#include <map>
#include <list>
#include <string>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mmlistener.h>
#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mmlistener.h>
#include <multimedia/media_buffer.h>
#include <multimedia/clock.h>
#include "multimedia/media_meta.h"

namespace YUNOS_MM {

extern MMParamSP nilParam;

class Component {
public:
    Component() {}
    virtual ~Component() {}

    class Listener {
    public:
        Listener(){}
        virtual ~Listener(){}

    public:
        virtual void onMessage(int msg, int param1, int param2, const MMParamSP obj, const Component * sender) = 0;

        MM_DISALLOW_COPY(Listener)
    };
    typedef MMSharedPtr<Listener> ListenerSP;

public:
    // match the value in MediaPlayer.h to avoid translation
    enum MediaType {
        kMediaTypeUnknown = 0,
        kMediaTypeVideo,
        kMediaTypeAudio,
        kMediaTypeSubtitle,
        kMediaTypeImage,
        kMediaTypeCount
    };

    // make sure to update the eventStr(in same order) when you update the Event below
    static const char* sEventStr[];
    //Note: new messge should be added before kEventMax
    enum Event {
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventPrepareResult,///0
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventStartResult,///1
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventStopped,///2
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventPaused,///3
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventResumed,///4
        // params:
        //   param1: MM_ERROR_SUCCESS for success, else error code.
        //   param2: not defined
        //   obj: not defined
        kEventSeekComplete,///5
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventResetComplete,///6
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventFlushComplete,///7
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventDrainComplete,///8
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        // after EOS, component behaves similar to pause (not corked but starve of data)
        // looped-playback can be supported by an additional seek(0).
        kEventEOS,///9
        // params:
        //   param1: width
        //   param2: height
        //   obj: MMParamSP, it will be the following orderly:
        //              <crop x> int32
        //              <crop y> int32
        //              <crop right> int32
        //              <crop bottom> int32
        //              <rotation degree> int32
        kEventGotVideoFormat,///10
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: MediaMetaSP
        kEventMediaInfo,///11
        // params:
        //   param1: duration in us, -1 if not determined.
        //   param2: not defined
        //   obj: not defined

        kEventInfoDuration,///12
        // params:
        //   param1: percent
        //   param2: no defined
        //   obj: not defined
        kEventInfoBufferingUpdate,///13
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: Component uses MMParamSP, it will be the following orderly:
        //              <track index> int32
        //              <time us> int64
        //              <duration us> int64
        //              <data size> int32
        //              <data> byte data
        //          CowPlayer uses MediaMetaSP
        kEventInfoSubtitleData,///14
        // params:
        //   param1: error code
        //   param2: not defined
        //   obj: not defined
        kEventError,///15
        // params:
        //   param1: Infomation code
        //   param2: specific Information event defined
        //   obj: specific Information event defined
        kEventInfo,///16
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: MMParamSP in the following order:
        //          id -- string. e.g. the component name
        //          code -- int32_t. the extend information code
        //          other component defined data
        kEventInfoExt,///17
        // params:
        //   param1: buffer index
        //   param2: not defined
        //   obj: not defined
        kEventUpdateTextureImage,///18
        // params:
        //   param1: not defined
        //   param2: not defined
        kEventRequestIDR, ///19
        // params:
        //   param1: rotation degree
        //   param2: not defined
        kEventVideoRotationDegree, ///20
        // params:
        //   param1: music spectrum
        //   param2: not defined
        kEventMusicSpectrum, ///21
        // params:
        //   param1: the position to seek.
        //   param2: not defined
        //  require a internal seek, such as LPA pause seek.
        kEventSeekRequire, ///22
        kEventMax
    };

    static const char* sEventInfoStr[];
    // kEventInfo params
    enum EventInfo {
        // params:
        //   param1: kEventInfoDiscontinuety
        //   param2: not defined
        //   obj: not defined
        kEventInfoDiscontinuety,
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventInfoVideoRenderStart,
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventInfoMediaRenderStarted,
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventInfoSeekPreviewDone,
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventMaxFileSizeReached,
        // params:
        //   param1: not defined
        //   param2: not defined
        //   obj: not defined
        kEventMaxDurationReached,
        // params:
        //   param1: kEventCostMemorySize
        //   param2: cost memory size
        //   obj: not defined
        kEventCostMemorySize,
        kEventInfoSourceStart = 50,
        kEventInfoSourceMax = 99,
        kEventInfoFilterStart = 100,
        kEventInfoFilterMax = 149,
        kEventInfoSinkStart = 150,
        kEventInfoSinkMax = 199
    };
    /*
        Reader::read() & Writer::write() are non-block API, costs at most 2 times of frame duration.
        - it should NOT block. either pipeline or peer needn't hanle the case when read/write is blocked. if there is, it is a bug (of component who provides Reader/Write).
        - in reality, component is possible to wait for new data in read/write but no more than 2 times of frame duration. it usually happens
           in some component where there is hw clock to generate/consume data in a timely manner.
           anyway, it's the component's internal detail, should NOT impact the caller component
        - read/write can retrun MM_ERROR_AGAIN or MM_ERROR_XXX to report status
    */
    struct Reader {
        Reader(){}
        virtual ~Reader(){}
        virtual mm_status_t read(MediaBufferSP & buffer) = 0;
        virtual MediaMetaSP getMetaData() = 0;
    };
    typedef MMSharedPtr<Reader> ReaderSP;

    struct Writer {
        Writer(){}
        virtual ~Writer(){}
        virtual mm_status_t write(const MediaBufferSP & buffer) = 0;
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData) = 0;
    };
    typedef MMSharedPtr<Writer> WriterSP;

public:
    // component's name
    virtual const char * name() const = 0;

    //component's version
    virtual const char * version() const = 0;
#define COMPONENT_VERSION                   \
    virtual const char * version() const    \
    {                                       \
        return _COW_SOURCE_VERSION;         \
    }

    virtual ReaderSP getReader(MediaType mediaType) = 0;
    virtual WriterSP getWriter(MediaType mediaType) = 0;

    // donot use shared_ptr to record peer component because it may cause cycle reference and cannot be freed automaicaly.
    // addSource means current component will actively pull data from upstream @component
    virtual mm_status_t addSource(Component * component, MediaType mediaType) = 0;
    // donot use shared_ptr to record peer component because it may cause cycle reference and cannot be freed automaicaly.
    // addSink means current component will actively push data to downstream @component
    virtual mm_status_t addSink(Component * component, MediaType mediaType) = 0;

    virtual mm_status_t setListener(ListenerSP listener) { mListener = listener; return MM_ERROR_SUCCESS; }

    virtual mm_status_t init() { return MM_ERROR_SUCCESS; }
    virtual void uninit() {}

    // parameter defined by subclass
    virtual mm_status_t setParameter(const MediaMetaSP & meta) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getParameter(MediaMetaSP & meta) const { return MM_ERROR_UNSUPPORTED; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    // symmetrical to reset();
    // after prepare is done (MM_ERROR_SUCCESS in sync mode, or kEventPrepareResult with MM_ERROR_SUCCESS in async mode),
    // component is ready to be connected by addSource()/addSink(); then start() etc
    virtual mm_status_t prepare() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventStartResult.
    //              others: error.
    // symmetrical to stop, alloc some internal resources. threads, hw resource etc
    virtual mm_status_t start() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventStopped.
    //              others: error.
    // symmetrical to start, release some internal only resources. threads, hw resource etc.
    virtual mm_status_t stop() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventPaused.
    //              others: error.
    //  symmetrical to  resume, usually update the status and cork the data flow
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResumed.
    //              others: error.
    // symmetrical to pause, usually update the status and uncork the data flow
    virtual mm_status_t resume() { return MM_ERROR_SUCCESS; }

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventSeekComplete.
    //              others: error.
    // component does flush() implicitly during seek if needed
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResetComplete.
    //              others: error.
    // symmetrical to prepare();
    // component disconnects each other, release the resource referenced from peers;
    // for example, the ReaderSP/WriteSP which is got during addSource()/addSink().
    // while keep the (exportable) resource create by itself, (its resource may still be referenced by others since reset() can be done in async mode)
    virtual mm_status_t reset() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventFlushComplete.
    //              others: error.
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; }
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventDrainComplete.
    //              others: error.
    virtual mm_status_t drain() { return MM_ERROR_SUCCESS; }

    virtual mm_status_t pushData(MediaBufferSP & buffer) { return MM_ERROR_UNSUPPORTED; }

    // for api extension
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply) { return MM_ERROR_UNSUPPORTED; }

    virtual mm_status_t setClock(ClockSP clock) { return MM_ERROR_SUCCESS; }
    virtual ClockSP provideClock() { return ClockSP((Clock*)NULL); }

    virtual mm_status_t setAudioConnectionId(const char * connectionId) { return MM_ERROR_UNSUPPORTED; }
    virtual const char * getAudioConnectionId() const { return ""; }

protected:
    mm_status_t notify(int msg, int param1, int param2, const MMParamSP obj)
    {
        if ( !mListener ) {
            return MM_ERROR_NOT_INITED;
        }

        mListener->onMessage(msg, param1, param2, obj, this);
        return MM_ERROR_SUCCESS;
    }

private:
    ListenerSP mListener;

    MM_DISALLOW_COPY(Component);
};

typedef MMSharedPtr<Component> ComponentSP;

class SourceComponent : public Component {
public:
    SourceComponent(){}
    virtual ~SourceComponent(){}

public:
    // kEventInfo params
    enum EventInfo {
        // params:
        //   param1: kEventMetaDataUpdate
        //   param2: not defined
        //   obj: not defined
        kEventMetaDataUpdate = Component::kEventInfoSourceStart,
        kEventInfoSubClassStart
    };

    // set/get Parameter keys
    // prepare timeout in usec
    // type: set/get
    // value: int32_t
    static const char * PARAM_KEY_PREPARE_TIMEOUT;
    // read timeout in usec
    // type: set/get
    // value: int32_t
    static const char * PARAM_KEY_READ_TIMEOUT;

public:
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL) = 0;
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) = 0;

    /*
     * <stream count> int32
     * for every stream:
     *     <track type> int32
     *     <track count> int32
     *     for every track:
     *         <track id> int32
     *         <codecId> int32
     *         <codecName> string
     *         <MIME> string
     *         <title> string
     *         <lang> string
     *     end for every track
     * end for every stream
     */
    virtual MMParamSP getTrackInfo() { return MMParamSP((MMParam*)NULL); }
    // -1: auto
    virtual mm_status_t selectTrack(MediaType mediaType, int index) { return MM_ERROR_UNSUPPORTED; }
    virtual int getSelectedTrack(MediaType mediaType) { return -1; };


    MM_DISALLOW_COPY(SourceComponent);
};

class PlaySourceComponent : public SourceComponent {
public:
    PlaySourceComponent(){}
    virtual ~PlaySourceComponent(){}

public:
    // kEventInfo params
    enum EventInfo {
        // params:
        //   param1: kEventInfoSeekable
        //   param2: none zero if seekable, else not seekable.
        //   obj: not defined
        kEventInfoSeekable = SourceComponent::kEventInfoSubClassStart + 1,
        kEventInfoSubClassStart
    };

    // buffering time in usec
    // type: set/get
    // value: int64_t -> buffering time
    static const char * PARAM_KEY_BUFFERING_TIME;

public:
    virtual const std::list<std::string> & supportedProtocols() const = 0;
    virtual bool isSeekable() = 0;
    virtual mm_status_t getDuration(int64_t & durationMs) = 0;

    virtual bool hasMedia(MediaType mediaType) = 0;

    virtual MediaMetaSP getMetaData() = 0;


    MM_DISALLOW_COPY(PlaySourceComponent);
};
typedef MMSharedPtr<PlaySourceComponent> PlaySourceComponentSP;


class RecordSourceComponent : public SourceComponent {
public:
    RecordSourceComponent(){}
    virtual ~RecordSourceComponent(){}

public:
    virtual mm_status_t signalEOS() = 0;


    MM_DISALLOW_COPY(RecordSourceComponent);
};


class FilterComponent : public Component {
public:
    FilterComponent(){}
    virtual ~FilterComponent(){}

public:
    // kEventInfo params
    enum EventInfo {
        kEventNoUse = Component::kEventInfoFilterStart,
        kEventInfoSubClassStart
    };

public:

    MM_DISALLOW_COPY(FilterComponent);
};


class SinkComponent : public Component {
public:
    SinkComponent(){}
    virtual ~SinkComponent(){}
    virtual int64_t getCurrentPosition() = 0;

public:
    // kEventInfo params
    enum EventInfo {
        kEventNoUse = Component::kEventInfoSinkStart,
        kEventInfoSubClassStart
    };

public:
    virtual ReaderSP getReader(MediaType mediaType) { return ReaderSP((Reader*)NULL); }
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    MM_DISALLOW_COPY(SinkComponent);
};

class PlaySinkComponent : public SinkComponent {
public:
    PlaySinkComponent(){}
    virtual ~PlaySinkComponent(){}

public:
    // kEventInfo params
    enum EventInfo {
        kEventNoUse = SinkComponent::kEventInfoSubClassStart,
        kEventInfoSubClassStart
    };

public:
    virtual mm_status_t setVolume(double volume) { return MM_ERROR_UNSUPPORTED; }
    virtual double getVolume() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setMute(bool mute) { return MM_ERROR_UNSUPPORTED; }
    virtual bool getMute() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setAudioStreamType(int type) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getAudioStreamType(int *type) { return MM_ERROR_UNSUPPORTED; }

    MM_DISALLOW_COPY(PlaySinkComponent);
};

}

namespace dash {
    namespace mpd {
        class IMPD;
    };
};

namespace YUNOS_MM {

class DashSourceComponent : public PlaySourceComponent {
public:
    DashSourceComponent(){}
    virtual ~DashSourceComponent(){}

public:

    // set segment buffer to PlaySource
    // type: set
    // value: pointer -> SegmentBuffer object
    static const char * PARAM_KEY_SEGMENT_BUFFER;

public:
    virtual const std::list<std::string> & supportedProtocols() const = 0;
    virtual bool isSeekable() = 0;
    virtual mm_status_t getDuration(int64_t & durationMs) = 0;

    virtual bool hasMedia(MediaType mediaType) = 0;
    virtual mm_status_t setMPD(dash::mpd::IMPD *mpd) = 0;
    virtual dash::mpd::IMPD *getMPD() = 0;

    virtual MediaMetaSP getMetaData() = 0;

    /*
     * <dynamic> int32
     * if (dynamic != 0) {
     *     <min update peroid> int32
     *     <available end> int32
     * }
     * <peroid count> int32
     * for every peroid
     *     <start> int32
     *     <duration> int32
     *     <stream count> int32
     *     for every stream:
     *         <media type> int32
     *         <adaptationSet count> int32
     *         for every adaptationSet:
     *             <lang> string
     *             <group> int
     *             <representation count> int32
     *             for every representation:
     *                 <MIME> string
     *                 <bandwidth> int32
     *                 <codecNum> int32
     *                 for every codec:
     *                     <codecName> string
     *             end for every representation
     *         end for every adaptationSet
     *     end for every stream
     * end for every peroid
     */

    virtual MMParamSP getMediaRepresentationInfo() = 0;
    virtual mm_status_t selectMediaRepresentation(int period, MediaType mediaType, int adaptationSet, int index) = 0;
    virtual int getSelectedMediaRepresentation(int period, MediaType mediaType, int &adaptationSet) = 0;

    virtual MMParamSP getTrackInfo() = 0;
    virtual mm_status_t selectTrack(MediaType mediaType, int index) = 0;
    virtual int getSelectedTrack(MediaType mediaType) = 0;

    MM_DISALLOW_COPY(DashSourceComponent);
};
typedef MMSharedPtr<DashSourceComponent> DashSourceComponentSP;

}

#endif // __component_H
