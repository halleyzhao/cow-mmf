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
#ifndef pipeline_player_test_h
#define pipeline_player_test_h
#include "multimedia/pipeline_player_base.h"


namespace YUNOS_MM {

class PipelinePlayerTest : public PipelinePlayerBase {
  public:
    PipelinePlayerTest() {}
    virtual ~PipelinePlayerTest() {}
    virtual mm_status_t prepareInternal();

  private:
    MM_DISALLOW_COPY(PipelinePlayerTest)
}; // PipelinePlayerTest

} // YUNOS_MM

#endif // pipeline_player_test_h

