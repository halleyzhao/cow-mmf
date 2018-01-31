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
#ifndef pipeline_basic_h
#define pipeline_basic_h
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_meta.h"
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/codec.h"
#include "multimedia/pipeline.h"
#include "multimedia/elapsedtimer.h"
#include "multimedia/mm_debug.h"


namespace YUNOS_MM {
class PipelineBasic : public Pipeline {
  public:
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t resume();
    virtual mm_status_t reset();
    virtual mm_status_t flush() { return MM_ERROR_SUCCESS; };
    virtual mm_status_t getState(ComponentStateType& state);
    virtual mm_status_t getCurrentPosition(int64_t& positionMs) const;
    virtual mm_status_t getDuration(int64_t& msec);
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    virtual mm_status_t  unblock();
    virtual ~PipelineBasic();

  protected:
     //prepareInternal should implemented by each derived class
     virtual mm_status_t prepareInternal() = 0;

  protected:
    void resetMemberVariables();
    ComponentStateType mState;
    int64_t mDurationMs;//Note: in ms
    uint32_t mWidth;
    uint32_t mHeight;
    int32_t mConnectedStreamCount; // how many streams are flowing in pipeline
    int32_t mEOSStreamCount;     // how many EOS received
    MediaMetaSP mMediaMeta; //saved param, so upper call methods in arbitrary order
    PipelineBasic();

    MM_DISALLOW_COPY(PipelineBasic)

    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER2(onComponentMessage)
    virtual void onCowMessage(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId){};  // empty it
}; // PipelineBasic

} // YUNOS_MM


#endif // pipeline_basic_h

