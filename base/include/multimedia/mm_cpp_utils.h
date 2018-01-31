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


#ifndef __mm_cpp_utils_H
#define __mm_cpp_utils_H

#include <pthread.h>
#include <sys/time.h>
#include <stdint.h>
#include <string>

#include <tr1/memory>

namespace YUNOS_MM {
    #define MMSharedPtr std::tr1::shared_ptr
    #define MMWeakPtr std::tr1::weak_ptr
    #define DynamicPointerCast std::tr1::dynamic_pointer_cast
    #define StaticPointerCast std::tr1::static_pointer_cast
    #define EnableSharedFromThis std::tr1::enable_shared_from_this
    #define DYNAMIC_CAST dynamic_cast

#define MM_RELEASE(_p) do { if (_p) {delete (_p); (_p) = NULL; } }while(0)
#define MM_RELEASE_ARRAY(_p) do { if (_p) { delete [](_p); (_p) = NULL; } }while(0)

#define MM_DISALLOW_COPY(_class) \
private: \
    _class(const _class &); \
    _class & operator=(const _class &);

class Lock
{
  public:
    enum {
        PRIVATE = 0,
        SHARED = 1
    };

    Lock(int type = PRIVATE);
    ~Lock();
    void acquire();
    void release();
    void tryLock();
    friend class Condition;

  private:
    pthread_mutex_t m_lock;
    MM_DISALLOW_COPY(Lock);
};

class MMAutoLock
{
  public:
    explicit MMAutoLock(Lock& lock);
    ~MMAutoLock();
    void lock();
    void unlock();
    Lock& m_lock;
  private:
    MM_DISALLOW_COPY(MMAutoLock);
};

class Condition
{
  public:
    enum {
        PRIVATE = 0,
        SHARED = 1
    };

    explicit Condition(Lock& lock, int type = PRIVATE);
    ~Condition();
    int wait();
    int timedWait(int64_t delayUs);
    void signal();
    void broadcast();

  private:
    Lock& m_lock;
    pthread_cond_t m_cond;
    MM_DISALLOW_COPY(Condition);
};

class MMAutoCount {
  public:
    explicit MMAutoCount(Lock & lock, int32_t & count);
    ~MMAutoCount();
  private:
    Lock & mLock;
    int32_t & mCount;
};

#define DECLARE_LOGTAG() private:\
    static const char * MM_LOG_TAG;

#define DEFINE_LOGTAG(_class) const char * _class::MM_LOG_TAG = #_class;
#define DEFINE_LOGTAG1(_class, suffix) const char * _class::MM_LOG_TAG = #_class#suffix;

std::string mm_get_env_str(const char *property_str, const char* env_str);
bool mm_set_env_str(const char *property_str, const char *env_str, const char *value);
bool mm_check_env_str(const char *property_str, const char* env_str, const char* expect_str_prefix="1", bool default_value=false);

#define MM_BOOL_EXPR(_expr) ({int _r = (_expr) ? 1 : 0; _r;})

#define MM_LIKELY(_expr) __builtin_expect(MM_BOOL_EXPR(_expr), 1)
#define MM_UNLIKELY(_expr) __builtin_expect(MM_BOOL_EXPR(_expr), 0)

const char * getCharSetByCountry(const char * country);
void mm_print_backtrace();
int64_t getTimeUs();
}

#endif
