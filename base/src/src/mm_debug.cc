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
#include <string.h>
#include <stdio.h>
#if defined(OS_YUNOS)
#include <properties.h>
#include <log/Log.h>
#endif
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"

#include <unistd.h>
#include <sys/syscall.h>
#define GETTID()    syscall(__NR_gettid)
#define GETPID()    syscall(__NR_getpid)

typedef MMSharedPtr<FILE> FilePtr;
static FilePtr mm_log_file;
static MMLogLevelType mm_current_log_level = MM_LOG_VERBOSE;
static bool loginit = false;
static YUNOS_MM::Lock lock;

#define MM_LOG_LEVEL_STR_KEY        "mm.log.level"
#define MM_LOG_LEVEL_STR_ENV        "MM_LOG_LEVEL"
#define MM_LOG_FILE_STR_KEY        "mm.log.file"
#define MM_LOG_FILE_STR_ENV        "MM_LOG_FILE"

static MMLogLevelType proToLevel(const char *buf)
{
    if(buf == NULL){
        return MM_LOG_DEFAULT;
    }

    switch (buf[0]) {
        case 'v':
        case 'V':
            return MM_LOG_VERBOSE;
        case 'd':
        case 'D':
            return MM_LOG_DEBUG;
        case 'i':
        case 'I':
            return MM_LOG_INFO;
        case 'w':
        case 'W':
            return MM_LOG_WARN;
        case 'e':
        case 'E':
            return MM_LOG_ERROR;
        default:
            return MM_LOG_DEFAULT;
    }
}

MMLogLevelType mm_log_get_level(void){
    return mm_current_log_level;
}

static void mm_log_close_log_file(FILE *file)
{
    if (file && file != stderr && file != stdout)
        fclose(file);
}

// FIXME, more init work now, update the name
bool mm_log_set_level(MMLogLevelType level)
{
    std::string env_str;

    // prob log set configuration
    if (level < MM_LOG_ERROR ||level  >MM_LOG_VERBOSE){
        return false;
    }

    // FIXME, why mm_log is not inside namespace YUNOS_MM?
    env_str = YUNOS_MM::mm_get_env_str(MM_LOG_LEVEL_STR_KEY, MM_LOG_LEVEL_STR_ENV);
    if (env_str.c_str())
        mm_current_log_level = proToLevel(env_str.c_str());
    else
        mm_current_log_level = level;

    // prob log file configuration
    env_str = YUNOS_MM::mm_get_env_str(MM_LOG_FILE_STR_KEY, MM_LOG_FILE_STR_ENV);

    if (env_str.size()) {
        if (!strcmp(env_str.c_str(), "stderr"))
            mm_log_file.reset(stderr, mm_log_close_log_file);
        else if (!strcmp(env_str.c_str(), "stdout"))
            mm_log_file.reset(stdout, mm_log_close_log_file);
        else {
            char logFileName[256];
            long int _pid = (long int)GETPID();
            long int _tid = (long int)GETTID();

            memset(logFileName, 0, sizeof(logFileName));
            sprintf(logFileName, "%s/%ld.log", env_str.c_str(), _pid);
            mm_log_file.reset(fopen(logFileName, "a+"), mm_log_close_log_file);
            if (mm_log_file.get()) {
                fprintf(mm_log_file.get(), "open_mm_log_file: (%ld: %ld)", _pid, _tid);
            }
        }
    }

#if defined(__MM_NATIVE_BUILD__)
    if (!mm_log_file)
        mm_log_file.reset(stderr, mm_log_close_log_file);
#endif

    if (mm_log_file) {
        if (mm_log_file.get() == stderr)
            fprintf(stderr, "mm log to stderr\n");
        else if (mm_log_file.get() == stdout)
            fprintf(stderr, "mm log to stdout\n");
        else
            fprintf(stderr, "mm log to %s\n", env_str.c_str());
    } else {
#ifndef __MM_NATIVE_BUILD__
        fprintf(stderr, "mm log to logcat\n");
#else
        fprintf(stderr, "fixme, mm log to unknown\n");
#endif
    }

    loginit = true;

    return loginit;
}

#if defined(OS_YUNOS)
static int logcatPriority[MM_LOG_LEVEL_COUNT] = {
    kLogPriorityError,
    kLogPriorityWarn,
    kLogPriorityInfo,
    kLogPriorityDebug,
    kLogPriorityVerbose,
};
#endif
static char log_level_char[MM_LOG_LEVEL_COUNT] = {'E', 'W', 'I', 'D', 'V'};

int mm_log(MMLogLevelType level, const char *tag, const char *fmt, ...)
{
    int ret = 0;
    #define MM_LOG_BUF_SIZE 1024

    if(level > mm_current_log_level || level < MM_LOG_ERROR)
        return -1;

    YUNOS_MM::MMAutoLock locker(lock);
    if(!loginit){
        if (!mm_log_set_level(MM_LOG_DEFAULT))
            return -1;
    }

    va_list ap;
    char buf[MM_LOG_BUF_SIZE];
    va_start(ap, fmt);
    vsnprintf(buf, MM_LOG_BUF_SIZE, fmt, ap);
    va_end(ap);

    FILE *fp = mm_log_file.get();
    if (fp) {
        timeval t;
        gettimeofday(&t, NULL);
        int64_t nowMs = t.tv_sec * 1000LL + t.tv_usec/1000LL;
        int32_t timeH = int32_t(nowMs/3600000 + 8); // hard code it, assume GMT 8 time zone
        nowMs = nowMs - nowMs/3600000*3600000;
        int32_t timeM = int32_t(nowMs/60000);
        nowMs = nowMs - nowMs/60000*60000;
        int32_t timeS = int32_t(nowMs/1000);
        nowMs = nowMs - nowMs/1000*1000;
        int32_t timeMs = nowMs%1000;

        fprintf(fp, "%.2d:%.2d:%.2d.%.3d  %ld %ld [%c] %s: %s\n",
            timeH%24, timeM, timeS, timeMs,
            (long int)GETPID(), (long int)GETTID(),
            log_level_char[level], tag, buf);
    }
    else {
#if defined(OS_YUNOS)
        int prio = kLogPriorityDefault;
        if (level>=MM_LOG_ERROR && level <=MM_LOG_VERBOSE)
            prio = logcatPriority[level];

        ret = yunosLogPrint(kLogIdMain, prio, tag, "%s", buf);
#else
        fprintf(stderr, "internal bug, log file isn't inited\n");
#endif
    }

    return ret;
 }

#ifdef __cplusplus
    int FuncTracker::sIndent = 2;
#endif

void hexDump(const void* ptr, uint32_t size, uint32_t bytesPerLine)
{
    const uint8_t *data = (uint8_t*)ptr;
    mm_log(MM_LOG_DEBUG, "hexDump", " data=%p, size=%d, bytesPerLine=%d\n", data, size, bytesPerLine);
    // assert(data && size && bytesPerLine);

    if (!data || !size)
        return;

    #define MAX_DUMP_SIZE   256
    if (size > MAX_DUMP_SIZE) {
        mm_log(MM_LOG_WARN, "hexDump", " data size is huge, truncate to %d\n", MAX_DUMP_SIZE);
        size = MAX_DUMP_SIZE;
    }

    char oneLineData[bytesPerLine*4+1];
    uint32_t offset = 0, lineStart = 0, i= 0;
    while (offset < size) {
        sprintf(&oneLineData[i*4], "%02x, ", *(data+offset));
        offset++;
        i++;
        if (offset == size || (i % bytesPerLine == 0)) {
            oneLineData[4*i-1] = '\0';
            mm_log(MM_LOG_DEBUG, "hexDump", "%04x: %s", lineStart, oneLineData);
            lineStart += bytesPerLine;
            i = 0;
        }
    }
}

void hexDumpHeadTailer(const void* data, uint32_t size, uint32_t dumpSize, uint32_t bytesPerLine)
{
    mm_log(MM_LOG_DEBUG, "hexDumpHeadTailer", " data=%p, size=%d, dumpSize: %d, bytesPerLine=%d\n", data, size, dumpSize, bytesPerLine);
    if (!data || !size || !dumpSize || !bytesPerLine)
        return;
    if(dumpSize > size)
        dumpSize = size;

    hexDump(data, dumpSize, bytesPerLine);

    if (size > dumpSize) {
        hexDump((uint8_t*)data + size - dumpSize, dumpSize, bytesPerLine);
    }
}

// ///////////////// TidyLog /////////////////////////////////////
#define TIDY_LOG_COUNT     0
#define TIDY_LOG_FROM       0
#define TIDY_LOG_TO         10
#define TIDY_LOG_INTERVAL   50
TidyLog::TidyLog()
    :mCount(TIDY_LOG_COUNT), mFrom(TIDY_LOG_FROM), mTo(TIDY_LOG_TO), mInterval(TIDY_LOG_INTERVAL)
{
}

void TidyLog::incCount()
{
    mCount++;
}

void TidyLog::printLog(const char* file, const char* func, uint32_t line)
{
    bool doit = false;
    const char* _log_filename = strrchr(file, '/');
    _log_filename = (_log_filename ? (_log_filename + 1) : file);

    if (mCount>mFrom && mCount <mTo)
        doit = true;

    if (!(mCount % mInterval))
        doit = true;

    if (doit)
        mm_log(MM_LOG_DEBUG, "TidyLog", "(%s, %s, %d): count=%d", _log_filename, func, line, mCount);

}

void TidyLog::updateLogFrequency(uint32_t from, uint32_t to, uint32_t interval)
{
    mFrom = from;
    mTo = to;
    mInterval = interval;
}
