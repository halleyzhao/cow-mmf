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


#include "third_helper.h"

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(AVLogger)


static MMLogLevelType convertLevel(int level) {
    switch (level) {
        case 0:
        case 8:
        case 16:
            return MM_LOG_ERROR;
        case 24:
            return MM_LOG_WARN;
        case 32:
            return MM_LOG_INFO;
        default:
            return MM_LOG_VERBOSE;
    }
}

/*static */void AVLogger::av_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    MMLogLevelType mmLevel = convertLevel(level);
    if (mmLevel > mm_log_get_level()) {
        return;
    }

    try {
        char * buf = new char[1024];
        if ( buf ) {
            int ret = vsnprintf(buf, 1023, fmt, vl);
            buf[ret] = '\0';
            MMLOGI("%s\n", buf);
            delete [] buf;
        }
    } catch (...) {
        MMLOGE("no mem\n");
    }
}

}
