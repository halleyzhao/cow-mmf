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

#ifndef fake_sink_h
#define fake_sink_h

#include <multimedia/component.h>
#include <multimedia/media_buffer.h>
#include <multimedia/clock.h>
#include "../clock_wrapper.h"


namespace YUNOS_MM {

class FakeSink : public PlaySinkComponent {
  public:
    FakeSink(const char *mimeType = NULL, bool isEncoder = false);
    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return  MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setClock(ClockSP clock);

    // FIXME, implements here
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t prepare(){ return MM_ERROR_SUCCESS;}
    virtual mm_status_t pause();
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS;}
    virtual mm_status_t reset();
    virtual mm_status_t flush();

    virtual mm_status_t setParameter(const MediaMetaSP & meta);
//    virtual mm_status_t getParameter(MediaMetaSP & meta) const;

    virtual int64_t getCurrentPosition();

    class FakeSinkWriter : public Writer {
      public:
          FakeSinkWriter(FakeSink *sink){
            mRender = sink;
          }
          virtual ~FakeSinkWriter(){}
          virtual mm_status_t write(const MediaBufferSP &buffer);
          virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
      private:
          FakeSink *mRender;
    };

  protected:
    virtual ~FakeSink();

  private:
    class VideoSinkWriter;

    Lock mLock;
    WriterSP mWriter;
    int32_t mRendedBufferCount;
    bool mVideoRenderingStarted;

    bool mPaused;
    int64_t mCurrentPosition;
    ClockWrapperSP mClockWrapper;

    std::string mMimeType;

    MM_DISALLOW_COPY(FakeSink);
};

} // end of namespace YUNOS_MM
#endif// fake_sink_h

