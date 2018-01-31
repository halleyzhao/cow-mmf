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

#ifndef __mm_dpms_proxy_h
#define __mm_dpms_proxy_h

#include "MMSession.h"

namespace YUNOS_MM {

class MMDpmsProxy {
public:
    static void updateServiceSwitch(pid_t pid, MMSession::MediaUsage type, bool turnOff);
};

} // end of YUNOS_MM
#endif
