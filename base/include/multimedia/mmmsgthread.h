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

#ifndef __mmmsgthread_H
#define __mmmsgthread_H

#include <semaphore.h>
#include <list>
#include <map>
#include <assert.h>
#include <sys/time.h>

#include <multimedia/mmthread.h>
#include <multimedia/mm_refbase.h>
#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {

class MMMsgThread : public MMThread {
public:
    MMMsgThread(const char *threadName);
    // FIXME, move it to private and uses releaseHelper() instead
    virtual ~MMMsgThread();
    static void releaseHelper(MMMsgThread *thread);

    int run();
    void exit();

public:
    typedef uint32_t msg_type;
    typedef unsigned int param1_type;
    typedef void * param2_type;
    typedef MMRefBaseSP param3_type;
    #define PARAM3_NULL MMRefBaseSP((MMRefBase*)NULL)

    friend class MMNotify;

protected:
    class Message {
    public:
        Message(msg_type what, param1_type param1, param2_type param2, uint32_t rspId, int64_t whenUs)
                : mWhat(what), mParam1(param1), mParam2(param2), mParam3(PARAM3_NULL), mResponseId(rspId), mWhenUs(whenUs)
        {
        }
        Message(msg_type what, param1_type param1, param2_type param2, param3_type param3, uint32_t rspId, int64_t whenUs)
                : mWhat(what), mParam1(param1), mParam2(param2), mParam3(param3), mResponseId(rspId), mWhenUs(whenUs)
        {
        }

        virtual ~Message() {}

    public:

    public:
        msg_type what() const { return mWhat; }
        param1_type param1() const { return mParam1; }
        param2_type param2() const { return mParam2; }
        param3_type param3() const { return mParam3; }
        int64_t when() const { return mWhenUs; }

        uint32_t respId() const { return mResponseId; }

    protected:
        msg_type mWhat;
        param1_type mParam1;
        param2_type mParam2;
        param3_type mParam3;

        uint32_t mResponseId;
        int64_t mWhenUs;
    };


    class ResponseMessage {
    public:
        ResponseMessage(param1_type param1, param2_type param2, param3_type param3 = PARAM3_NULL)
                    : mParam1(param1), mParam2(param2), mParam3(param3)
        {
        }
        ~ResponseMessage(){}

    public:
        param1_type param1() const { return mParam1; }
        param2_type param2() const { return mParam2; }
        param3_type param3() const { return mParam3; }

    private:
        param1_type mParam1;
        param2_type mParam2;
        param3_type mParam3;
    };

    typedef MMSharedPtr<ResponseMessage> ResponseMessageSP;

protected:
    int postMsg(msg_type what,
                param1_type param1,
                param2_type param2,
                int64_t timeoutUs = 0,
                param3_type param3 = PARAM3_NULL);
    int sendMsg(msg_type what,
                param1_type param1,
                param2_type param2,
                param1_type * resp_param1,
                param2_type * resp_param2);
    int sendMsg(msg_type what,
                param1_type param1,
                param2_type param2,
                param3_type param3,
                param1_type * resp_param1,
                param2_type * resp_param2,
                param3_type * resp_param3);
    int postReponse(uint32_t respId, param1_type param1, param2_type param2, param3_type param3 = PARAM3_NULL);

private:
    int doPost(msg_type what,
                param1_type param1,
                param2_type param2,
                param3_type param3,
                uint32_t rspId,
                int64_t timeoutUs = 0);

protected:
    static int64_t getTimeUs();


// message handler propotype:
//     void onMessage1(param1_type, param2_type, uint32_t)
//     param1_type: the first param
//     param2_type: the second param
//     uint32_t: the response id, 0 means the sender not need response

// helper macros
#define ASSERT_NEED_RSP(_rspId) assert(_rspId!=0)
#define ASSERT_NOTNEED_RSP(_rspId) assert(_rspId==0)

#define DECLARE_MSG_LOOP() \
public:\
    virtual void main();

#define DECLARE_MSG_HANDLER(_handler) \
protected:\
    virtual void _handler(param1_type param1, param2_type param2, uint32_t rspId);

#define DECLARE_MSG_HANDLER2(_handler) \
protected:\
    virtual void _handler(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId);

#define DECLARE_MSG_LOOP_PURE_VIRTUAL() \
public:\
    virtual void main() = 0;

#define DECLARE_MSG_HANDLER_PURE_VIRTUAL(_handler) \
protected:\
    virtual void _handler(param1_type param1, param2_type param2, uint32_t rspId) = 0;

#define DECLARE_MSG_HANDLER2_PURE_VIRTUAL(_handler) \
protected:\
    virtual void _handler(param1_type param1, param2_type param2, param3_type param3, uint32_t rspId) = 0;

#define BEGIN_MSG_LOOP(_theclass) void _theclass::main() {      \
     while ( 1 ) {                                              \
        const Message * msg = NULL;                             \
        {                                                       \
            MMAutoLock locker(mMsgThrdLock);                      \
            if (MM_UNLIKELY(!mContinue && mMsgQ.empty())) {     \
                /* break the loop, exit the thread */           \
                break;                                          \
            }                                                   \
                                                                \
            /* wait until a msg to examine */                   \
            if (mMsgQ.empty()) {                                \
                mMsgCond.wait();                                \
                continue;                                       \
            }                                                   \
                                                                \
            msg = mMsgQ.front();                                \
            int64_t nowUs = getTimeUs();                        \
            if (mContinue && msg->when() > nowUs) {             \
                int64_t delayUs = msg->when() - nowUs;          \
                mMsgCond.timedWait(delayUs);                    \
                /* may wakeup by new coming msg, check again */ \
                continue;                                       \
            }                                                   \
                                                                \
            /* it's time to process current msg */              \
            mMsgQ.pop_front();                                  \
        }                                                       \
                                                                \
        /* process current msg, unlocked state */               \
        switch ( msg->what() ) {

#define MSG_ITEM(_msg, _hdr) \
            case _msg:\
                _hdr(msg->param1(), msg->param2(), msg->respId());\
                break;\

#define MSG_ITEM2(_msg, _hdr) \
            case _msg:\
                _hdr(msg->param1(), msg->param2(), msg->param3(), msg->respId());\
                break;\

#define END_MSG_LOOP()\
            default:\
                MMLOGE("unknown msg: %u", msg->what());\
                break;\
        }\
                                                                \
        delete msg;                                             \
    } /* end of while(1) */                                     \
                                                                \
    MMLOGI("MMMsgThread exit, thread is over\n");               \
}


protected:
    bool mContinue;

    Lock mMsgThrdLock;
    Condition mMsgCond;

    typedef std::list<const Message*> msg_list_t;
    msg_list_t mMsgQ;

    Condition mReplyCond;
    typedef std::map<uint32_t, ResponseMessageSP> rsp_map_t;
    typedef std::pair<uint32_t, ResponseMessageSP> rsp_pair_t;
    rsp_map_t mRespMap;
    uint32_t mCurRespId;

#undef PARAM3_NULL
};

}
#endif // __mmmsgthread_H
