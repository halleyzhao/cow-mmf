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

#include "MMDpmsProxy.h"
#include <multimedia/mm_debug.h>

#include <DPMSProxy.h>
using namespace yunos;

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MMDpmsProxy");

void MMDpmsProxy::updateServiceSwitch(pid_t pid, MMSession::MediaUsage type, bool turnOff) {

    SharedPtr<DPMSProxy> proxy = DPMSProxy::getInstance();
    if (!proxy) {
        ERROR("cannot get dpms proxy");
        return;
    }

    int pageType = -1;
    String name;

    if (type == MMSession::MU_Player) {
        pageType = DPMSProxy::SERVICE_MEDIA_PLAY;
        name = "MediaPlayer";
    } else if (type == MMSession::MU_Recorder) {
        pageType = DPMSProxy::SERVICE_MEDIA_RECORD;
        name = "MediaRecorder";
    } else {
        INFO("no need to track media usage %d", type);
        return;
    }

    INFO(">>> updateServiceSwitch: %s of process[pid %d] is turn %s",
         name.c_str(), pid, turnOff ? "off" : "on");

    proxy->updateServiceSwitch(!turnOff, pageType, pid);
    INFO("<<<");

    return;
}

}
