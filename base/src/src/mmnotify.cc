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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "multimedia/mm_debug.h"
#include "multimedia/mmnotify.h"
#include "multimedia/mmmsgthread.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MMNOITFY")

void MMNotify::setWhat(uint32_t what)
{
    MMLOGV(">>>\n");
    mWhat = what;
    MMLOGV("<<<\n");
}

bool MMNotify::registerThreadHandler(MMMsgThread* thread)
{
    MMLOGV(">>>\n");
    mMMMsgthread = thread;
    MMLOGV("<<<\n");
    return mMMMsgthread != NULL;
}

MMNotify::MMNotify()
    :mWhat(0)
    ,mMMMsgthread(NULL)
{
    MMLOGV(">>>\n");
    MMLOGV("<<<\n");
}

MMNotify::MMNotify(const MMNotify &notify)
    : mWhat(notify.mWhat),
      mMMMsgthread(notify.mMMMsgthread) {

    MMLOGV(">>>\n");
    MMLOGV("<<<\n");
}

MMNotify::~MMNotify()
{
    MMLOGV(">>>\n");
    MMLOGV("<<<\n");
}

bool MMNotify::postAsync(int64_t delayUs)
{
    MMLOGV(">>>\n");
    int ret = -1;

    if (mMMMsgthread)
        ret = mMMMsgthread->postMsg(mWhat, 0, this, delayUs);

    return (ret == 0);
}

} // namespace of YUNOS_MM
