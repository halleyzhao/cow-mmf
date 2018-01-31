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
#ifndef pipeline_lpa_h
#define pipeline_lpa_h
#include "multimedia/pipeline_player_base.h"


namespace YUNOS_MM {
class PipelineLPA;
typedef MMSharedPtr<PipelineLPA> PipelineLPASP;

class PipelineLPA : public PipelinePlayerBase {
  public:

    PipelineLPA();
    ~PipelineLPA();
    virtual mm_status_t seek(SeekEventParamSP param);

  protected:
    virtual mm_status_t prepareInternal();

  private:
    bool mIsLPASupport;
    MM_DISALLOW_COPY(PipelineLPA)
};

}

#endif

