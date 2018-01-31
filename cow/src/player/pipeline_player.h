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
#ifndef pipeline_player_h
#define pipeline_player_h
#include "multimedia/pipeline_player_base.h"


namespace YUNOS_MM {
class PipelinePlayer;
typedef MMSharedPtr<PipelinePlayer> PipelinePlayerSP;

class PipelinePlayer : public PipelinePlayerBase {
  public:

    PipelinePlayer();
    ~PipelinePlayer();

  protected:
    virtual mm_status_t prepareInternal();
    const char* getMimeFromMediaRepresentationInfo(Component::MediaType type, MMParamSP param);

  private:
    MM_DISALLOW_COPY(PipelinePlayer)
}; // PipelinePlayer

} // YUNOS_MM

#endif // pipeline_player_h

