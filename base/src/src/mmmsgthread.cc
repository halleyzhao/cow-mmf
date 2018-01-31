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

#include "multimedia/mm_debug.h"
#include "multimedia/mmmsgthread.h"

namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("MMMSGTHRD")

MMMsgThread::MMMsgThread(const char *threadName)
                    : MMThread(threadName, true)
                    ,mContinue(false)
                    , mMsgCond(mMsgThrdLock)
                    , mReplyCond(mMsgThrdLock)
                    , mCurRespId(0)
{
    MMLOGV(">>>\n");
    //create();
    MMLOGV("<<<\n");
}

/*virtual*/ MMMsgThread::~MMMsgThread()
{
    MMLOGV(">>>\n");
    if (!mRespMap.empty())
        MMLOGW(">>>, size %zu, %zu\n", mMsgQ.size(), mRespMap.size());

    assert(mMsgQ.size() == 0);
    //assert(mRespMap.size() == 0);
    MMLOGV("<<<\n");
}


int MMMsgThread::run(){
    MMLOGV(">>>\n");
    mContinue = true;
    int ret = create();
    MMLOGV("<<<\n");
    return ret;
}

void MMMsgThread::exit(){
    MMLOGV(">>>\n");
    {
        MMAutoLock locker(mMsgThrdLock);
        if (!mContinue) {
            return;
        }
        MMLOGD("quiting thread, remain msg count: %zu\n", mMsgQ.size());
        for (msg_list_t::iterator it = mMsgQ.begin(); it != mMsgQ.end(); ++it) {
            MMLOGW("remain msg, param1 %d param2 %p what %d, reponseId %d\n",
                (*it)->param1(), (*it)->param2(), (*it)->what(), (*it)->respId());
        }
        mContinue = false;
        mMsgCond.broadcast();
        mReplyCond.broadcast();
    }

    destroy();
    MMLOGV("<<<\n");
}


int MMMsgThread::doPost(msg_type what,
            param1_type param1,
            param2_type param2,
            param3_type param3,
            uint32_t rspId,
            int64_t timeoutUs)
{
    MMLOGV(">>>what: %u\n", what);
    MMAutoLock locker(mMsgThrdLock);
    if ( !mContinue ) {
        MMLOGD("exiting, what %u\n", what);
        return 1;
    }
    if (timeoutUs < 0) {
        timeoutUs = 0;
    }
    int64_t whenUs = getTimeUs() + timeoutUs;

    Message * msg = new Message(what, param1, param2, param3, rspId, whenUs);
    if ( !msg ) {
        MMLOGE("no mem\n");
        return -1;
    }

    std::list<const Message*>::iterator it = mMsgQ.begin();

    while (it != mMsgQ.end() && (*it)->when() <= whenUs) {
        ++it;
    }

    //mMsgQ.push_back(msg);
    if (it == mMsgQ.begin()) {
        mMsgCond.signal();
    }
    mMsgQ.insert(it, msg);
    MMLOGV("<<<\n");
    return 0;
}

int MMMsgThread::postMsg(msg_type what,
                            param1_type param1,
                            param2_type param2,
                            int64_t timeoutUs,
                            param3_type param3/* = PARAM3_NULL*/)
{
   return doPost(what, param1, param2, param3, 0, timeoutUs);
}

int MMMsgThread::sendMsg(msg_type what,
            param1_type param1,
            param2_type param2,
            param1_type * resp_param1,
            param2_type * resp_param2)
{
    return sendMsg(what, param1, param2, MMRefBaseSP((MMRefBase*)NULL), resp_param1, resp_param2, NULL);
}


int MMMsgThread::sendMsg(msg_type what,
                param1_type param1,
                param2_type param2,
                param3_type param3,
                param1_type * resp_param1,
                param2_type * resp_param2,
                param3_type * resp_param3)
{
    MMLOGV("sendMsg: what %u >>>\n", what);
    uint32_t rspId;
    {
        MMAutoLock locker(mMsgThrdLock);
        rspId = ++mCurRespId;
        if ( rspId == 0 ) {
            ++rspId;
            ++mCurRespId;
        }
    }

    int ret = doPost(what, param1, param2, param3, rspId);

    MMLOGV("getting response %u what %u\n", rspId, what);
    while ( ret == 0 ) {
        MMAutoLock locker(mMsgThrdLock);
        if (!mContinue) {
            MMLOGW("task thread exiting, rspId %u,%u msg return without got response\n", rspId, what);
            ret = 1;
            break;
        }

        rsp_map_t::iterator i = mRespMap.find(rspId);
        if ( i == mRespMap.end() ) {
            mReplyCond.wait();
            continue;
        }
        /*
        i = mRespMap.find(rspId);
        if ( i == mRespMap.end() ) {
            MMLOGV("response %u not found, try again\n", rspId);
            pthread_mutex_unlock(&mMutex);
            continue;
        }
        */
        ResponseMessageSP rsp = i->second;
        if ( resp_param1 )
            *resp_param1 = rsp->param1();
        else {
            MMLOGV("response param1 not needed\n");
        }
        if ( resp_param2 )
            *resp_param2 = rsp->param2();
        else {
            if ( rsp->param2() ) {
                MMLOGW("response param2 provided but not received\n");
            } else {
                MMLOGW("response param2 not provided\n");
            }
        }
        if ( resp_param3 ) {
            *resp_param3 = rsp->param3();
        }
        mRespMap.erase(i);
        break;
    }

    MMLOGV("sendMsg: what %u rspId %d<<<\n", what, rspId);
    return ret;
}

int MMMsgThread::postReponse(uint32_t respId,
                                param1_type param1,
                                param2_type param2,
                                param3_type param3/* = PARAM3_NULL*/)
{
    MMLOGV("postReponse: respId %u >>>\n", respId);
    ResponseMessageSP msg(new ResponseMessage(param1, param2, param3));
    if ( !msg ) {
        MMLOGE("no mem\n");
        return -1;
    }
    MMAutoLock locker(mMsgThrdLock);
    if (!mContinue) {
        MMLOGE("task thread exiting, %u respId postReponse\n", respId);
        return 1;
    }
    mRespMap.insert(rsp_pair_t(respId, msg));
    mReplyCond.broadcast();
    MMLOGV("postReponse: respId %u <<<\n", respId);
    return 0;
}

/*static*/ int64_t MMMsgThread::getTimeUs()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000LL + t.tv_nsec / 1000LL;
}

/* static */ void MMMsgThread::releaseHelper(MMMsgThread *thread)
{
    if (!thread)
        return;

    thread->exit();
    delete thread;
}

}
