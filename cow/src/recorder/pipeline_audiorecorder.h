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
#ifndef pipeline_audiorecorder_h
#define pipeline_audiorecorder_h
#include "multimedia/recorder_common.h"
#ifndef __DISABLE_AUDIO_STREAM__
#include "multimedia/mm_audio.h"
#endif
#include "pipeline_recorder_base.h"


namespace YUNOS_MM {
class PipelineAudioRecorder;
typedef MMSharedPtr<PipelineAudioRecorder> PipelineAudioRecorderSP;

class PipelineAudioRecorder : public PipelineRecorderBase {
  public:
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    PipelineAudioRecorder();
    virtual ~PipelineAudioRecorder();

  protected:

    virtual mm_status_t prepareInternal();
    virtual mm_status_t stopInternal();
    virtual mm_status_t resetInternal();

  private:

    MM_DISALLOW_COPY(PipelineAudioRecorder)

}; // PipelineAudioRecorder

} // YUNOS_MM

#endif // pipeline_audiorecorder_h

