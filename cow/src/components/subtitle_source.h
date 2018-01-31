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

#ifndef __SUBTITLE_SOURCE_H__
#define __SUBTITLE_SOURCE_H__

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mm_debug.h"
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/media_monitor.h"
#include <multimedia/clock.h>
#include "../clock_wrapper.h"
#include <queue>
#include <algorithm>

namespace YUNOS_MM
{

class SubtitleSource : public PlaySourceComponent, public MMMsgThread
{
public:
    SubtitleSource();
    virtual ~SubtitleSource();

public:
    virtual mm_status_t init();
    virtual void uninit();
    virtual const char * name() const { return "SubtitleSource"; }
    COMPONENT_VERSION;

    virtual ReaderSP getReader(MediaType mediaType);
    virtual mm_status_t addSink(Component * component, MediaType mediaType) { return MM_ERROR_IVALID_OPERATION; }

    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t resume();
    virtual mm_status_t stop();
    virtual mm_status_t pause() { return MM_ERROR_SUCCESS; }
    virtual mm_status_t seek(int msec, int seekSequence);
    virtual mm_status_t reset();
    virtual mm_status_t flush();

    virtual mm_status_t setUri(const char * uri,
                                const std::map<std::string,
                                std::string> * headers = NULL);

    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) { return MM_ERROR_UNSUPPORTED; }
    virtual const std::list<std::string> & supportedProtocols() const;
    virtual bool isSeekable() { return true; }
    virtual mm_status_t getDuration(int64_t & durationMs) { return MM_ERROR_IVALID_OPERATION;}
    virtual bool hasMedia(MediaType mediaType) { return true;}
    virtual MediaMetaSP getMetaData() { return MediaMetaSP((MediaMeta*)NULL); }
    virtual MMParamSP getTrackInfo() { return nilParam; }
    virtual mm_status_t selectTrack(MediaType mediaType, int index) { return MM_ERROR_SUCCESS; }
    virtual int getSelectedTrack(MediaType mediaType) { return 0;}
    virtual mm_status_t setClock(ClockSP clock);

private:
    mm_status_t internalStop();
    void releaseBuffers();
    mm_status_t createPriv();
    struct SubtitleBuffer {
        uint32_t index;
        uint64_t startPositionMs;
        uint64_t endPositionMs;
        std::string text;
    };
    typedef MMSharedPtr <SubtitleBuffer> SubtitleBufferSP;

    class SubtitleSourceReader : public Reader
    {
     public:
        SubtitleSourceReader(SubtitleSource *source, MediaType type)
        {
            mSource = source;
            mType = type;
        }
        ~SubtitleSourceReader() { }

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

    private:
        SubtitleSource *mSource;
        MediaType mType;
        DECLARE_LOGTAG()
    };

    class ReaderThread : public MMThread
    {
    public:
        ReaderThread(SubtitleSource *source);
        ~ReaderThread();
        void signalExit();
        void signalContinue();
        static bool releaseInputBuffer(MediaBuffer* mediaBuffer);

    protected:
        virtual void main();

    private:
        SubtitleSource *mSource;
        bool mContinue;
        DECLARE_LOGTAG()
    };
    typedef MMSharedPtr<ReaderThread> ReaderThreadSP;

    class Private;
    typedef std::tr1::shared_ptr<Private> PrivateSP;
    class Private {
      public:
        static PrivateSP create(char* uri);
        static bool comp (SubtitleBufferSP a,SubtitleBufferSP b) { return (a->startPositionMs < b->startPositionMs); }
        void sortByStartTime() {sort(mSubtitleSource->mSubtitleBuffers.begin(),mSubtitleSource->mSubtitleBuffers.end(), comp); }
        mm_status_t init(SubtitleSource *subtitlesource);
        virtual void parseTime(const char *time, size_t pos, uint64_t& start, uint64_t& end);
        virtual mm_status_t parseFile(const char *uri) { return MM_ERROR_UNSUPPORTED; }
        virtual mm_status_t removeFont() { return MM_ERROR_UNSUPPORTED; }
        void uninit() {}
        ~Private() {}
        Private() : mSubtitleSource(NULL) {}

        SubtitleSource *mSubtitleSource;
        MM_DISALLOW_COPY(Private);
    };

private:
    std::queue<MediaBufferSP> mBuffers;
    std::vector<SubtitleBufferSP> mSubtitleBuffers;

    Lock mLock;
    Condition mCondition;
    bool mIsPaused;

    ReaderThreadSP mReaderThread;

    std::string mUri;
    uint32_t mCurrentIndex;
    class ParseSrt;
    class ParseSub;
    class ParseVTT;
    class ParseASS;
    class ParseTXT;
    class ParseSMI;
    PrivateSP mPriv;
    ClockWrapperSP mClockWrapper;

private:
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onPrepare)
    DECLARE_MSG_HANDLER(onStart)
    DECLARE_MSG_HANDLER(onResume)
    DECLARE_MSG_HANDLER(onFlush)
    DECLARE_MSG_HANDLER(onStop)
    DECLARE_MSG_HANDLER(onReset)
    MM_DISALLOW_COPY(SubtitleSource);

private:
    DECLARE_LOGTAG()
};

typedef MMSharedPtr<SubtitleSource> SubtitleSourceSP;

}

#endif // __SUBTITLE_SOURCE_H__

