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

extern "C" {
#include <libavformat/avformat.h>
}

#include <FFMpegInitor.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("FFMpegInitor")

static int ffmpegLockCB(void **mutex, enum AVLockOp op)
{
    if (NULL == mutex) {
        MMLOGE("invalid param\n");
        return -1;
    }

    try {
        switch (op) {
        case AV_LOCK_CREATE:
        {
            *mutex = NULL;
            Lock * m = new Lock();
            *mutex = static_cast<void*>(m);
            break;
        }
        case AV_LOCK_OBTAIN:
        {
            Lock * m =  static_cast<Lock*>(*mutex);
            m->acquire();
            break;
        }
        case AV_LOCK_RELEASE:
        {
            Lock * m = static_cast<Lock*>(*mutex);
            m->release();
            break;
        }
        case AV_LOCK_DESTROY:
        {
            Lock * m = static_cast<Lock*>(*mutex);
            delete m;
            *mutex = NULL;
            break;
        }
        default:
            break;
        }
    } catch (...) {
        return -1;
    }
    return 0;
}

struct AVLogger {
    static void av_log_callback(void *ptr, int level, const char *fmt, va_list vl)
    {
        MMLogLevelType mmLevel = convertLevel(level);
        if (mmLevel > mm_log_get_level()) {
            return;
        }

        char * buf = new char[1024];
        if ( buf ) {
            int ret = vsnprintf(buf, 1023, fmt, vl);
            buf[ret] = '\0';
            MMLOGI("%s\n", buf);
            delete [] buf;
        }
    }

private:
    static MMLogLevelType convertLevel(int level) {
        switch (level) {
            case AV_LOG_PANIC:
            case AV_LOG_FATAL:
            case AV_LOG_ERROR:
                return MM_LOG_ERROR;
            case AV_LOG_WARNING:
                return MM_LOG_WARN;
            case AV_LOG_INFO:
                return MM_LOG_INFO;
            case AV_LOG_VERBOSE:
            case AV_LOG_DEBUG:
            case AV_LOG_TRACE:
            default:
                return MM_LOG_VERBOSE;
        }
    }

    DECLARE_LOGTAG()
};

DEFINE_LOGTAG(AVLogger)

DEFINE_LOGTAG(FFMpegInitor)

void FFMpegInitor::init()
{
    MMLOGI("+\n");
    class AVInitializer {
    public:
        AVInitializer() {
            MMLOGI("use log level converter\n");
            av_log_set_callback(AVLogger::av_log_callback);
            av_log_set_level(AV_LOG_ERROR);
            avcodec_register_all();
            av_register_all();
            avformat_network_init();
            av_lockmgr_register(&ffmpegLockCB);
        }
        ~AVInitializer() {
            av_log_set_callback(NULL);
            av_lockmgr_register(NULL);
            avformat_network_deinit();
        }
    };
    static AVInitializer sAVInit;
    MMLOGI("-\n");
}

}
