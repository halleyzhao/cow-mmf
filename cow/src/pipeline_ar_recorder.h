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
#ifndef pipeline_ar_recorder_h
#define pipeline_ar_recorder_h
#include "multimedia/recorder_common.h"
#include "pipeline_recorder_base.h"
#ifndef __DISABLE_AUDIO_STREAM__
#include "multimedia/mm_audio.h"
#endif

namespace YUNOS_MM {
class PipelineArRecorder;
typedef MMSharedPtr<PipelineArRecorder> PipelineVrRecorderSP;

class PipelineArRecorder : public PipelineRecorderBase {
  public:
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    PipelineArRecorder();
    virtual ~PipelineArRecorder();

  protected:

    virtual mm_status_t prepareInternal();
    virtual mm_status_t stopInternal();
    virtual mm_status_t resetInternal();

  private:

    bool mHasVideo;
    bool mHasAudio;

    MM_DISALLOW_COPY(PipelineArRecorder)

}; // PipelineArRecorder

} // YUNOS_MM

#endif // pipeline_ar_recorder_h

