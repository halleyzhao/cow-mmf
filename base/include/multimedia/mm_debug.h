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

#ifndef __mm_debug_H
#define __mm_debug_H

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <multimedia/mm_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_LOG_DEFINE_MODULE_NAME(_module_name) static const char *MM_LOG_TAG = _module_name;
#define MM_LOG_DEFINE_LEVEL(_level) static const MMLogLevelType LOG_LEVEL = _level;

typedef enum {
    MM_LOG_ERROR = 0,
    MM_LOG_WARN,
    MM_LOG_INFO,
    MM_LOG_DEBUG,
    MM_LOG_VERBOSE,
    MM_LOG_LEVEL_COUNT,
} MMLogLevelType;

// MM_LOG_LEVEL is set as build flag, one of the level defined above
#define MM_LOG_DEFAULT MM_LOG_LEVEL

/* USAGE:
 * - log to given file name
 *     setprop mm.log.file /data/log.txt
 *     export  MM_LOG_FILE=/data/log.txt
 * - log to stderr/stdout
 *     setprop mm.log.file "stderr"
 *     export  MM_LOG_FILE="stdout"
 * - default log output
 *     logcat if it is not __MM_NATIVE_BUILD__
 *     stderr for __MM_NATIVE_BUILD__
 * - reduce log output at runtime:
 *     setprop mm.log.level v/V | d/D | i/I | w/W | e/E
 *     export  MM_LOG_LEVEL=v/V | d/D | i/I | w/W | e/E
 *     debug/Debug/DEBUG is also fine
 * - compile time log level is set by: -DMM_LOG_LEVEL=MM_LOG_DEBUG
 */

/**
 * Get the current log level
 *
 * @see MMLogLevelType
 *
 * @return current log level
 */
MMLogLevelType mm_log_get_level(void);

/**
 * Set the log level
 *
 * @see MMLogLevelType
 *
 * @param level log level
 */
bool mm_log_set_level(MMLogLevelType level);

/**
 * Send the specified message to the log if the level is less than or equal
 * to the current log level.
 *
 * @param level The log level
 * @param tag  The tag of current module.
 * @param fmt The format string (printf-compatible) that specifies how
 *        subsequent arguments are converted to output.
 */
int mm_log(MMLogLevelType level, const char *tag, const char *fmt,...);

//some necessary information show on stdout
#define PRINTF printf

#define MM_LOG_(level, tag, format, ...) do {                                                       \
    const char* _log_filename = strrchr(__FILE__, '/');                                                  \
    _log_filename = (_log_filename ? (_log_filename + 1) : __FILE__);                                              \
    mm_log(level, tag, "[MM] (%s, %s, %d)" format,  _log_filename, __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while(0)

#if MM_LOG_LEVEL >= MM_LOG_ERROR
#define ERROR(format, ...)  MM_LOG_(MM_LOG_ERROR, MM_LOG_TAG, format, ##__VA_ARGS__)
#define MMLOGE(format, ...)  MM_LOG_(MM_LOG_ERROR, MM_LOG_TAG, format, ##__VA_ARGS__)
#else
#define ERROR(format, ...)
#define MMLOGE(format, ...)
#endif

#if MM_LOG_LEVEL >= MM_LOG_WARN
#define WARNING(format, ...)  MM_LOG_(MM_LOG_WARN, MM_LOG_TAG, format, ##__VA_ARGS__)
#define MMLOGW(format, ...)  MM_LOG_(MM_LOG_WARN, MM_LOG_TAG, format, ##__VA_ARGS__)
#else
#define WARNING(format, ...)
#define MMLOGW(format, ...)
#endif

#if MM_LOG_LEVEL >= MM_LOG_INFO
#define INFO(format, ...)  MM_LOG_(MM_LOG_INFO, MM_LOG_TAG, format, ##__VA_ARGS__)
#define MMLOGI(format, ...)  MM_LOG_(MM_LOG_INFO, MM_LOG_TAG, format, ##__VA_ARGS__)
#else
#define INFO(format, ...)
#define MMLOGI(format, ...)
#endif

#if MM_LOG_LEVEL >= MM_LOG_DEBUG
#define DEBUG(format, ...)  MM_LOG_(MM_LOG_DEBUG, MM_LOG_TAG, format, ##__VA_ARGS__)
#define MMLOGD(format, ...)  MM_LOG_(MM_LOG_DEBUG, MM_LOG_TAG, format, ##__VA_ARGS__)
#else
#define DEBUG(format, ...)
#define MMLOGD(format, ...)
#endif

#if MM_LOG_LEVEL >= MM_LOG_VERBOSE
#define VERBOSE(format, ...)  MM_LOG_(MM_LOG_VERBOSE, MM_LOG_TAG, format, ##__VA_ARGS__)
#define MMLOGV(format, ...)  MM_LOG_(MM_LOG_VERBOSE, MM_LOG_TAG, format, ##__VA_ARGS__)
#else
#define VERBOSE(format, ...)
#define MMLOGV(format, ...)
#endif

#ifndef DEBUG_FOURCC
#if MM_LOG_LEVEL >= MM_LOG_DEBUG
#define DEBUG_FOURCC(preStr, fourcc) do { \
    uint32_t i_fourcc = fourcc; \
    char *ptr = (char*)(&(i_fourcc)); \
    DEBUG("%s fourcc: 0x%x, %c%c%c%c\n", preStr, i_fourcc, *(ptr), *(ptr+1), *(ptr+2), *(ptr+3)); \
} while(0)
#endif
#endif


#ifndef ASSERT
#define ASSERT(expr) do {                       \
        if (!(expr)) {                          \
            ERROR();                            \
            assert(0 && (expr));                \
        }                                       \
    } while(0)
#endif
#ifndef ASSERT_EQ
#define ASSERT_EQ(expr1, expr2) do {            \
        if (expr1 != expr2) {                   \
            ERROR();                            \
            assert(0 && (expr1 == expr2));      \
        }                                       \
    } while(0)
#endif
#ifndef ASSERT_ERROR
#define ASSERT_ERROR(expr, msg) do {            \
        if (!(expr)) {                          \
            ERROR(msg);                         \
            assert(0 && (expr));                \
        }                                       \
    } while(0)
#endif
#ifndef ASSERT_RET
#define ASSERT_RET(expr, ret) do {              \
        if (!(expr)) {                          \
            ERROR();                            \
            assert(0 && (expr));                \
            return ret;                         \
        }                                       \
    } while(0)
#endif

#ifndef MMASSERT
#define MMASSERT(expr) ASSERT(expr)
#endif

#ifdef __cplusplus
}
#endif

#define PRINTABLE_STR(str) (str ? str : "null")

#ifdef __cplusplus
#include <string>
class FuncTracker {
  public:
    static int sIndent;
    FuncTracker(const char* tag, const char* func, int32_t line)
        : mFunc(func)
        , mLine(line)
    {
        const char* separator= strrchr(tag, '/');
        mTag = separator ? (separator + 1) : tag;

        int i=0;
        for (i=0; i<sIndent; i++)
            indents += "  ";
        mm_log(MM_LOG_DEBUG, indents.c_str(), ">>>> [D][MM] enter (%s, %s, line %d)", mTag, mFunc, mLine);
        sIndent++;
    }
    ~FuncTracker() {
        mm_log(MM_LOG_DEBUG, indents.c_str(), "<<<< [D][MM] leave (%s, %s)", mTag, mFunc);
        sIndent--;
    }

  private:
    const char* mTag, *mFunc;
    int32_t mLine;
    std::string indents;
};

class DataDump {
  public:
    explicit DataDump(const char *filename) : mFp(NULL) {
        mFilename = filename;
    }
    virtual ~DataDump() {
        if (mFp) {
            fclose(mFp);
            mm_log(MM_LOG_INFO, "DataDump", "[I][MM] close %s for dump data\n", mFilename.c_str());
        }
    }
    bool dump(const void* data, uint32_t size) {
        if(!mFp) {
            mFp = fopen(mFilename.c_str(), "w+");
            if (!mFp) {
                mm_log(MM_LOG_ERROR, "DataDump", "[E][MM] fail to open %s to dump data\n", mFilename.c_str());
                return false;
            }
        }
        // issue empty data to close the file
        if (!data || !size) {
            fclose(mFp);
            mFp = NULL;
            mm_log(MM_LOG_INFO, "DataDump", "[I][MM] close %s for dump data\n", mFilename.c_str());
            return false;
        }

        if (fwrite(data, 1, size, mFp) != size) {
            return false;
        }
        // mm_log(MM_LOG_DEBUG, "DataDump", "[D][MM] dump data: %p with size: %zu to %s\n", data, size, mFilename.c_str());

        return true;
    }

  private:
    std::string mFilename;
    FILE *mFp;
};

class TidyLog {
  public:
    TidyLog();
    TidyLog(uint32_t from, uint32_t to, uint32_t interval)
          : mFrom(from), mTo(to), mInterval(interval) { }
    ~TidyLog() {}
    void incCount();
    void printLog(const char* file, const char* func, uint32_t line);
    void updateLogFrequency(uint32_t from, uint32_t to, uint32_t interval);

  private:
    uint32_t mCount;
    uint32_t mFrom;
    uint32_t mTo;
    uint32_t mInterval;
};

#define DEFINE_STATIC_TIDYLOG() static TidyLog _tidyLog;    _tidyLog.incCount();
#define PRINT_TIDY_LOG()        _tidyLog.printLog(__FILE__, __func__, __LINE__)

#else
#define FuncTracker(...) printf("enter (%s, %s)\n", __FILE__, __FUNCTION__)
#endif

void hexDump(const void* data, uint32_t size, uint32_t bytesPerLine);
void hexDumpHeadTailer(const void* data, uint32_t size, uint32_t dumpSize, uint32_t bytesPerLine);

#endif // __mm_debug_H
