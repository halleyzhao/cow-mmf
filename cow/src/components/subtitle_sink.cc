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

#include "subtitle_sink.h"
#include <math.h>
#include <multimedia/component.h>
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>
#include <mediacollector_char_converter.h>


namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("STS")

static const char * COMPONENT_NAME = "SubtitleSink";
static const char * MMTHREAD_NAME = "SubtitleSink::ReadThread";


#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ADFF_MSG_prepare (msg_type)1
#define ADFF_MSG_start (msg_type)2
#define ADFF_MSG_resume (msg_type)3
#define ADFF_MSG_pause (msg_type)4
#define ADFF_MSG_stop (msg_type)5
#define ADFF_MSG_flush (msg_type)6
#define ADFF_MSG_seek (msg_type)7
#define ADFF_MSG_reset (msg_type)8
#define ADFF_MSG_setParameters (msg_type)9
#define ADFF_MSG_getParameters (msg_type)10
#define ADFF_MSG_renderSubtile (msg_type)11

BEGIN_MSG_LOOP(SubtitleSink)
    MSG_ITEM(ADFF_MSG_prepare, onPrepare)
    MSG_ITEM(ADFF_MSG_start, onStart)
    MSG_ITEM(ADFF_MSG_resume, onResume)
    MSG_ITEM(ADFF_MSG_pause, onPause)
    MSG_ITEM(ADFF_MSG_stop, onStop)
    MSG_ITEM(ADFF_MSG_flush, onFlush)
    MSG_ITEM(ADFF_MSG_seek, onSeek)
    MSG_ITEM(ADFF_MSG_reset, onReset)
    MSG_ITEM(ADFF_MSG_setParameters, onSetParameters)
    MSG_ITEM(ADFF_MSG_getParameters, onGetParameters)
    MSG_ITEM(ADFF_MSG_renderSubtile, onRenderSubtitle)
END_MSG_LOOP()

// ReadThread
SubtitleSink::ReadThread::ReadThread(SubtitleSink* subtitlesink)
    : MMThread(MMTHREAD_NAME)
    , mSubtitleSink(subtitlesink)
    , mContinue(true)
{
    ENTER();
    EXIT();
}

SubtitleSink::ReadThread::~ReadThread()
{
    ENTER();
    EXIT();
}

void SubtitleSink::ReadThread::signalExit()
{
    ENTER();
    MMAutoLock locker(mSubtitleSink->mLock);
    mContinue = false;
    mSubtitleSink->mCondition.signal();
    EXIT();
}

void SubtitleSink::ReadThread::signalContinue()
{
    ENTER();
    mSubtitleSink->mCondition.signal();
    EXIT();
}

// Read Buffer
void SubtitleSink::ReadThread::main()
{
    ENTER();

    while(1) {
        // MediaBufferSP mediaBuffer;
        MMAutoLock locker(mSubtitleSink->mLock);
        if (!mContinue) {
            break;
        }
        if (mSubtitleSink->mIsPaused) {
            INFO("pause wait");
            mSubtitleSink->mCondition.wait();
            INFO("pause wait wakeup");
            continue;
        }
        mm_status_t ret = MM_ERROR_SUCCESS;
        if (!mSubtitleSink->mMediaBuffer)
            mSubtitleSink->mReader->read(mSubtitleSink->mMediaBuffer);

        if (mSubtitleSink->mMediaBuffer && ret == MM_ERROR_SUCCESS) {

            // mSubtitleSink->mSubtitleQueue.push(mediaBuffer);
            int64_t pts = mSubtitleSink->mMediaBuffer->pts();
            if (MM_LIKELY(!mSubtitleSink->mMediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS))) {
                if (pts >= 0) {
                    mSubtitleSink->mClockWrapper->setAnchorTime(pts, Clock::getNowUs());
                    mSubtitleSink->mClockWrapper->updateMaxAnchorTime(pts+1000000);
                }
            } else {
                mSubtitleSink->mIsPaused = true;
                mSubtitleSink->notify(kEventEOS, 0, 0, nilParam);
                MMLOGV("EOS, not set anchor\n");
                continue;
            }

            int64_t lateUs = mSubtitleSink->mClockWrapper->getMediaLateUs(pts);
            int64_t delayUs = -1ll;
            if (mSubtitleSink->mFirstPTS) {
                mSubtitleSink->mFirstPTS = false;
                delayUs = pts;
            } else if (lateUs <= -10*1000ll){
                delayUs = -lateUs;
                if (delayUs < (int64_t)mSubtitleSink->mLastDuration) {
                    delayUs = (int64_t)mSubtitleSink->mLastDuration;
                 }
            } else {
                delayUs = 0;
            }

            mSubtitleSink->mCondition.timedWait(delayUs);
            if (mSubtitleSink->mIsPaused) {
                continue;
            }

            MMParamSP param(new MMParam());
            if ( !param ) {
                MMLOGE("no mem\n");
                mSubtitleSink->notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
                return;
            }
            int stream_index = 0;
            MediaMetaSP meta = mSubtitleSink->mMediaBuffer->getMediaMeta();
            if (meta) {
                meta->getInt32(MEDIA_ATTR_STREAM_INDEX, stream_index);
            }

            uint8_t *sourceBuf = NULL;
            mSubtitleSink->mMediaBuffer->getBufferInfo((uintptr_t*)&sourceBuf, NULL, NULL, 1);
            // std::string subtitleText = std::string(sourceBuf);
            char * dst = NULL;
#ifdef __PLATFORM_TV__
            mm_status_t ret = CharConverter::convert(CharConverter::kCharSetUTF8, (const char*)sourceBuf, dst);
            INFO("convert ret = %d", ret);
            param->writeCString("UTF-8");
#else
            mm_status_t ret = CharConverter::convert(CharConverter::kCharSetGBK, (const char*)sourceBuf, dst);
            INFO("convert ret = %d", ret);
            if (dst)
                param->writeCString("GBK");
            else
                param->writeCString("UTF-8");
#endif
            if (dst) {
                param->writeCString(dst);
                param->writeCString(dst);
                if (mm_check_env_str("mm.subtitle.dump","MM_SUBTITLE_DUMP", "1", false)) {
                    mSubtitleSink->bufferDump(dst, strlen(dst));
                }
           } else {
                param->writeCString((const char*)sourceBuf);
                param->writeCString((const char*)sourceBuf);
                if (mm_check_env_str("mm.subtitle.dump","MM_SUBTITLE_DUMP", "1", false)) {
                    mSubtitleSink->bufferDump(sourceBuf, strlen(sourceBuf));
                }
            }
            param->writeInt32(stream_index);
            param->writeInt32(mSubtitleSink->mMediaBuffer->pts()/1000);
            param->writeInt32(mSubtitleSink->mMediaBuffer->duration()/1000);
            mSubtitleSink->mLastDuration = mSubtitleSink->mMediaBuffer->duration();
            mSubtitleSink->notify(kEventInfoSubtitleData, 0, 0, param);
            mSubtitleSink->mMediaBuffer.reset();

            // if (mSubtitleSink->mSubtitleQueue.size() == 1) {
            //    INFO("start show subtitle");
            //    mSubtitleSink->postMsg(ADFF_MSG_renderSubtile, 0, NULL);
            // }

        }else {
            // INFO("read NULL buffer from demuxer\n");
            mSubtitleSink->mFirstPTS = false;
            mSubtitleSink->mCondition.timedWait(1000*1000);
        }

    }

    INFO("subtitle read thread exited\n");
    EXIT();
}
//ReadThread  end

SubtitleSink::SubtitleSink(const char *mimeType, bool isEncoder) : MMMsgThread(COMPONENT_NAME),
                                                mComponentName(COMPONENT_NAME),
                                                mIsPaused(true),
                                                mFirstPTS(true),
                                                mCondition(mLock),
                                                mLastDuration(0)
{
    mClockWrapper.reset(new ClockWrapper(ClockWrapper::kFlagVideoSink));
    mMediaBuffer.reset();
    DEBUG("mimeType: %s", mimeType);
}

SubtitleSink::~SubtitleSink()
{
    ENTER();
    EXIT();
}

void SubtitleSink::bufferDump(unsigned char *buf, int len)
{
#define DUMP_BYTES_PER_ROW  16
    char buffer[500];
    char* bufptr = buffer;
    int i;
    INFO("dump size:%d, buf = %s", len, buf);
    if (!buf) {
        return;
    }
    for (i = 0; i < len; i++) {
        sprintf(bufptr, "%02X ", buf[i]);
        bufptr += strlen(bufptr);
        if (i % DUMP_BYTES_PER_ROW == (DUMP_BYTES_PER_ROW - 1)) {
            INFO("%s", buffer);
            bufptr = buffer;
        }
    }
    if (bufptr != buffer) {
        // print last line
        INFO("%s", buffer);
    }
    INFO("\n");

}

mm_status_t SubtitleSink::setClock(ClockSP clock)
{
    ENTER();
    mm_status_t ret = mClockWrapper->setClock(clock);
    return ret;
}

mm_status_t SubtitleSink::init()
{
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void SubtitleSink::uninit()
{
    ENTER();
    MMMsgThread::exit();
    EXIT();
}

const char * SubtitleSink::name() const
{
    ENTER();
    return mComponentName.c_str();
}

mm_status_t SubtitleSink::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeSubtitle) {
        mReader = component->getReader(kMediaTypeSubtitle);
        if (mReader) {
            EXIT_AND_RETURN(MM_ERROR_SUCCESS);
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}

mm_status_t SubtitleSink::prepare()
{
    ENTER();
    postMsg(ADFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::start()
{
    ENTER();
    postMsg(ADFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::resume()
{
    ENTER();
    postMsg(ADFF_MSG_resume, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::stop()
{
    ENTER();
    postMsg(ADFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::pause()
{
    ENTER();
    postMsg(ADFF_MSG_pause, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::seek(int msec, int seekSequence)
{
    ENTER();
    postMsg(ADFF_MSG_seek, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::reset()
{
    ENTER();
    mIsPaused = true;
    postMsg(ADFF_MSG_reset, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::flush()
{
    ENTER();
    postMsg(ADFF_MSG_flush, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t SubtitleSink::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void SubtitleSink::clearBuffers()
{
    while(!mSubtitleQueue.empty()) {
        mSubtitleQueue.pop();
    }
}

mm_status_t SubtitleSink::scheduleRenderSubtitle()
{
    ENTER();
    MMAutoLock locker(mLock);
    if (mIsPaused)
        return MM_ERROR_SUCCESS;


    if (mSubtitleQueue.empty()) {
        return MM_ERROR_SUCCESS;
    }
    MediaBufferSP buffer = mSubtitleQueue.front();
    if (!buffer) {
        //EOS frame, buffer is NULL
        postMsg(ADFF_MSG_renderSubtile, 0, NULL);
        return MM_ERROR_SUCCESS;
    }

    int64_t pts = buffer->pts();
    int64_t lateUs = mClockWrapper->getMediaLateUs(pts);

    int64_t delayUs = -1ll;
    if (lateUs > 100*1000ll){
        delayUs = lateUs - 100*1000ll;
    } else {
        delayUs = 0;
    }

    if (lateUs < -10*1000ll || lateUs > 500*1000ll) {
        DEBUG("pts %" PRId64 " ms, lateUs %" PRId64 " ms, delayUs %" PRId64 " ms", pts/1000ll, lateUs/1000ll, delayUs/1000ll);
    }

    postMsg(ADFF_MSG_renderSubtile, 0, NULL, delayUs);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


void SubtitleSink::onRenderSubtitle(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
     {
        MMAutoLock locker(mLock);
        do {
        if (mSubtitleQueue.empty()) {
            INFO("onRenderSubtitle empty ");
            break;
        }
        MediaBufferSP buffer = mSubtitleQueue.front();
        if (!buffer || buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
            INFO("onRenderSubtitle done EOS frame ");
            INFO("EOS frame");
            mIsPaused = true;
            mSubtitleQueue.pop();
            notify(kEventEOS, 0, 0, nilParam);
            EXIT();
        }
        int64_t pts = buffer->pts();
        int64_t lateUs = mClockWrapper->getMediaLateUs(pts);
        bool render = true;
        render = lateUs < 200*1000ll;
        if (render) {
            MMParamSP param(new MMParam());
            if ( !param ) {
                MMLOGE("no mem\n");
                notify(kEventError, MM_ERROR_NO_MEM, 0, nilParam);
                return;
            }
            MediaMetaSP meta = buffer->getMediaMeta();
            if (!meta) {
                break;
            }
            int stream_index = 0;
            meta->getInt32(MEDIA_ATTR_STREAM_INDEX, stream_index);
            uint8_t *sourceBuf = NULL;
            buffer->getBufferInfo((uintptr_t*)&sourceBuf, NULL, NULL, 1);
            param->writeCString("UTF-8");
            param->writeCString((const char*)sourceBuf);
            param->writeCString((const char*)sourceBuf);
            param->writeInt32(stream_index);
            param->writeInt32(pts/1000);
            param->writeInt32(buffer->duration());
            notify(kEventInfoSubtitleData, 0, 0, param);
            mSubtitleQueue.pop();
        }
        } while(0);
    }
    
    scheduleRenderSubtitle();
    EXIT();

}

void SubtitleSink::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);

    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();

}

void SubtitleSink::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        MMAutoLock locker(mLock);
        if (!mReadThread) {
            // create thread to read buffer
            mReadThread.reset (new ReadThread(this), MMThread::releaseHelper);
            mReadThread->create();
        }
        mIsPaused = false;
        mReadThread->signalContinue();
    }
    if (mClockWrapper) {
        mClockWrapper->resume();
    }
    // scheduleRenderSubtitle();

    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        MMAutoLock locker(mLock);
        mIsPaused = false;
        mReadThread->signalContinue();
        if (mClockWrapper) {
            mClockWrapper->resume();
        }
    }
    // scheduleRenderSubtitle();
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    mIsPaused = true;
    mMediaBuffer.reset();
    mLastDuration = 0;
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    mIsPaused = true;
    if (mClockWrapper) {
        mClockWrapper->pause();
    }
    mReadThread->signalContinue();
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    clearBuffers();
    if (mClockWrapper) {
        mClockWrapper->flush();
    }
    mMediaBuffer.reset();
    mLastDuration = 0;

    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}
void SubtitleSink::onSeek(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);

    notify(kEventSeekComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}
void SubtitleSink::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{

    ENTER();
    {
        mIsPaused = true;
        if (mReadThread) {
            mReadThread->signalExit();
            mReadThread.reset();
        }
    }
    MMAutoLock locker(mLock);
    clearBuffers();
    mReader.reset();
    mMonitorWrite.reset();
    mMediaBuffer.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onSetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //notify(EVENT_SETPARAMETERSCOMPLETE, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void SubtitleSink::onGetParameters(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    //notify(EVENT_GETPARAMETERSCOMPLETE, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::SubtitleSink *sinkComponent = new YUNOS_MM::SubtitleSink(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}


void releaseComponent(YUNOS_MM::Component *component)
{
    delete component;
}
}

