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
#include "multimedia/mm_cpp_utils.h"
#if defined(OS_YUNOS)
#include <properties.h>
#endif
#include "multimedia/mm_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <cxxabi.h>
#include <execinfo.h>

namespace YUNOS_MM {

std::string mm_get_env_str(const char *property_str, const char* env_str)
{
    // do NOT use mm_debug, since log init requires mm_get_env_str
    const char *str = NULL;

    // FIXME, add property support for host only
#if !defined(__MM_NATIVE_BUILD__)
    // no multi-thread support for static buf
    int len = 0;
    static char  buf[PROP_VALUE_MAX];
    len = property_get(property_str, buf , "");
    if (len > 0){
        str= buf;
    } else
#endif
    {
        if (env_str)
            str= getenv(env_str);
    }

    fprintf(stderr, "property_str: %s, env_str: %s, result: %s\n", PRINTABLE_STR(property_str), PRINTABLE_STR(env_str), PRINTABLE_STR(str));

    if (!str)
        str = "";

    return str;
}

bool mm_check_env_str(const char *property_str, const char* env_str, const char* expect_str_prefix, bool default_value)
{
    std::string result = mm_get_env_str(property_str, env_str);

    if (result.empty())
        return default_value;

    if (!expect_str_prefix)
        return true;

    if (strncasecmp(result.c_str(), expect_str_prefix, strlen(expect_str_prefix)))
        return false;

    return true;
}

bool mm_set_env_str(const char *property_str, const char *env_str, const char *value)
{
    int ret = 0, ret1 = 0;

    if (property_str)
        ret = property_set(property_str, value);

    if (env_str)
        ret1 = setenv(env_str, value, 1);

    return (!ret && !ret1);
}

// mm log can be used from here
MM_LOG_DEFINE_MODULE_NAME("UTIL");

const char * getCharSetByCountry(const char * country)
{
    if (!strncmp(country, "ja", 2)) {
        return "shift-jis";
    }

    if (!strncmp(country, "ko", 2)) {
        return "EUC-KR";
    }

    if (!strncmp(country, "zh", 2)) {
        if (!strcmp(country, "zh-CN")) {
            return "gbk";
        }

        return "Big5";
    }

    if (!strncmp(country, "ru", 2)) {
        return "cp1251";
    }

    if (!strncmp(country, "es", 2)
        || !strncmp(country, "pt", 2)
        || !strncmp(country, "fr", 2)
        || !strncmp(country, "de", 2)
        || !strncmp(country, "tr", 2)
        || !strncmp(country, "it", 2)
        || !strncmp(country, "in", 2)
        || !strncmp(country, "ms", 2)
        || !strncmp(country, "vi", 2)
        || !strncmp(country, "ar", 2)
        || !strncmp(country, "nl", 2))
    {
        return "ISO-8859-1";
    }

    return "";
}

static std::string demangle(const char* const symbol)
{
    const std::unique_ptr< char, decltype(&std::free) > demangled(abi::__cxa_demangle(symbol, 0, 0, 0), &std::free);
    if(demangled) {
        return demangled.get();
    }
    else {
        return symbol;
    }
}

void mm_print_backtrace()
{
  void* addresses[256];
  // std::extent< decltype(addresses) >::value is similar to sizeof(addresses)/sizeof(addresses[0])
  const int n = ::backtrace(addresses, std::extent< decltype(addresses) >::value);
  const std::unique_ptr< char*, decltype(&std::free) > symbols(::backtrace_symbols(addresses, n), &std::free);
  for(int i = 0; i < n; ++i) {
    // we parse the symbols retrieved from backtrace_symbols() to
    // extract the "real" symbols that represent the mangled names.
    char* const symbol = symbols.get()[ i ];
    char* end = symbol;
    while(*end) {
      ++end;
    }
    // scanning is done backwards, since the module name might contain both '+' or '(' characters.
    while(end != symbol && *end != '+') {
      --end;
    }
    char* begin = end;
    while(begin != symbol && *begin != '(') {
      --begin;
    }

    std::string lineStr;
    if(begin != symbol) {
      lineStr = std::string(symbol, ++begin - symbol);
      *end++ = '\0'; // not sure the intention
      lineStr += demangle(begin) + "+" + end;
    } else {
        lineStr = symbol;
    }
    DEBUG("[%02d]: %s", i, lineStr.c_str());
  }
}

int64_t getTimeUs()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000LL + t.tv_nsec / 1000LL;
}

// class Lock
Lock::Lock(int type)
{
    if (type == SHARED) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&m_lock, &attr);
        pthread_mutexattr_destroy(&attr);
    } else {
        pthread_mutex_init(&m_lock, NULL);
    }
}

Lock::~Lock()
{
    pthread_mutex_destroy(&m_lock);
}

void Lock::acquire()
{
    pthread_mutex_lock(&m_lock);
}

void Lock::release()
{
    pthread_mutex_unlock(&m_lock);
}

void Lock::tryLock()
{
    pthread_mutex_trylock(&m_lock);
}

// class MMAutoLock
MMAutoLock::MMAutoLock(Lock& lock) : m_lock(lock)
{
    m_lock.acquire();
}
MMAutoLock::~MMAutoLock()
{
    m_lock.release();
}

void MMAutoLock::lock() {
    m_lock.acquire();
}
void MMAutoLock::unlock() {
    m_lock.release();
}

// class Condition
Condition::Condition(Lock& lock, int type):m_lock(lock)
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    if (type == SHARED) {
        pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    }
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&m_cond, &attr);
    pthread_condattr_destroy(&attr);
}

Condition::~Condition()
{
    pthread_cond_destroy(&m_cond);
}

int Condition::wait()
{
    return pthread_cond_wait(&m_cond, &m_lock.m_lock);
}

int Condition::timedWait(int64_t delayUs)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += delayUs / 1000000;
    ts.tv_nsec +=  (delayUs % 1000000) * 1000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    return pthread_cond_timedwait(&m_cond, &m_lock.m_lock, &ts);
}

void Condition::signal()
{
    pthread_cond_signal(&m_cond);
}

void Condition::broadcast()
{
    pthread_cond_broadcast(&m_cond);
}

// class MMAutoCount
MMAutoCount::MMAutoCount(Lock & lock, int32_t & count)
    : mLock(lock)
    , mCount(count)
{
    MMAutoLock l(mLock);
    mCount++;
}
MMAutoCount::~MMAutoCount()
{
    MMAutoLock l(mLock);
    mCount--;
}

}
