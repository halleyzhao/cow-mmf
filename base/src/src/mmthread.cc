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

#include <assert.h>
#include <string.h>
#include<sys/prctl.h>

#include "multimedia/mm_debug.h"
#include "multimedia/mmthread.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MMTHRD")

MMThread::MMThread(const char *threadName, bool joinable /*= true*/)
                        : //mThreadId(0),
                          mThreadCreated(false),
                          mJoinable(joinable),
                          mThreadName(threadName)
{
	memset(&mThreadId, 0, sizeof(mThreadId));
}

MMThread::~MMThread()
{
    MMLOGV("destroy thread name = %s\n", mThreadName.c_str());
}

int MMThread::create()
{
    MMLOGV(">>>\n");
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, mJoinable ? PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED);
    //pthread_attr_setstacksize(&attr,STACK_SIZE);

    int ret = pthread_create(&mThreadId, &attr, threadfunc, this);
    if ( ret ) {
        int tmp = errno; //avoid to overwrite
        MMLOGE("failed to start thread, errno %d, %s\n", tmp, strerror(tmp));
        pthread_attr_destroy(&attr);
        return -1;
    }

    pthread_attr_destroy(&attr);
    MMLOGV("created thread name = %s\n", mThreadName.c_str());
    mThreadCreated = true;
    MMLOGV("start thread success\n");
    return 0;
}

void MMThread::destroy()
{
    MMLOGV(">>>\n");
    if ( !mThreadCreated ) {
        MMLOGV("<<<not started\n");
        return;
    }

    mThreadCreated = false;
    if ( mJoinable ) {
        void * aaa = NULL;
        MMLOGI("joining thread name = %s\n", mThreadName.c_str());
        int ret = pthread_join(mThreadId, &aaa);
        MMLOGV("joined thread name = %s\n",mThreadName.c_str());
        MMLOGI("<<< ret %d\n", ret);
    }
}

/*static*/ void * MMThread::threadfunc(void * param)
{
    assert(param!=NULL);
    MMLOGV(">>>\n");
    MMThread * me = static_cast<MMThread*>(param);
    prctl(PR_SET_NAME,me->mThreadName.c_str()) ;
    me->main();
    MMLOGV("<<<\n");
    return NULL;
}

/* static */ void MMThread::releaseHelper(MMThread *thread)
{
    if (!thread)
        return;

    thread->destroy();
    delete thread;
}

}
