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

#include "fake_sink.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("FakeSink")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

class FakeSink::VideoSinkWriter : public Component::Writer {
  public:
    VideoSinkWriter(FakeSink* sink)
        : mSink(sink)
    {}
    virtual ~VideoSinkWriter(){}
    virtual mm_status_t write(const MediaBufferSP &buffer);
    virtual mm_status_t setMetaData(const MediaMetaSP & metaData);

  private:
    FakeSink *mSink;
};

mm_status_t FakeSink::VideoSinkWriter::write(const MediaBufferSP &buffer) {
    FUNC_TRACK();
    mSink->mRendedBufferCount++;
    return MM_ERROR_SUCCESS;
}

mm_status_t FakeSink::VideoSinkWriter::setMetaData(const MediaMetaSP & metaData) {
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
FakeSink::FakeSink(const char *mimeType, bool isEncoder)
        : mRendedBufferCount(0)
        , mVideoRenderingStarted(false)
        , mPaused(true)
        , mCurrentPosition(-1)
        , mMimeType(mimeType)
{
    FUNC_TRACK();

    mMimeType = std::string(mimeType);
    ASSERT(isEncoder == false);

    mClockWrapper.reset(new ClockWrapper(ClockWrapper::kFlagVideoSink));
    mWriter.reset(new FakeSink::VideoSinkWriter(this));
}

FakeSink::~FakeSink() {
    FUNC_TRACK();
}

mm_status_t FakeSink::setClock(ClockSP clock) {
    FUNC_TRACK();
    mm_status_t ret = mClockWrapper->setClock(clock);

    return ret;
}

mm_status_t FakeSink::start() {
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    //resume from pause state
    mClockWrapper->resume();
    mPaused = false;

    if (!mVideoRenderingStarted) {
        INFO("notifyVideoRenderingStart");
        mVideoRenderingStarted = true;
        notify(kEventInfo, kEventInfoVideoRenderStart, 0, nilParam);
        notify(kEventInfo, kEventInfoMediaRenderStarted, 0, nilParam);
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t FakeSink::stop() {
    FUNC_TRACK();

    mClockWrapper->flush();
    return MM_ERROR_SUCCESS;
}

mm_status_t FakeSink::pause() {
    FUNC_TRACK();
    MMAutoLock locker(mLock);

    mClockWrapper->pause();
    mPaused = true;

    return MM_ERROR_SUCCESS;
}

mm_status_t FakeSink::reset() {
    FUNC_TRACK();
    return stop();
}

mm_status_t FakeSink::flush() {
    FUNC_TRACK();
    mClockWrapper->flush();
    return MM_ERROR_SUCCESS;
}

const char* FakeSink::name() const{
    return "FakeSink";
}

mm_status_t FakeSink::setParameter(const MediaMetaSP & meta) {
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

int64_t FakeSink::getCurrentPosition() {
    MMAutoLock locker(mLock);
    if (mClockWrapper && mClockWrapper->getCurrentPosition(mCurrentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        mCurrentPosition = -1ll;
    }
    INFO("getCurrentPosition %" PRId64 "", mCurrentPosition);
    return mCurrentPosition;
}

Component::WriterSP FakeSink::getWriter(MediaType mediaType) {
    FUNC_TRACK();
    Component::WriterSP mWriter(new FakeSink::FakeSinkWriter(this));
    return mWriter;
}

mm_status_t FakeSink::FakeSinkWriter::write(const MediaBufferSP & buf)
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

mm_status_t FakeSink::FakeSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}

extern "C" {
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::FakeSink *sinkComponent = new YUNOS_MM::FakeSink(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}


void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}
}

}

