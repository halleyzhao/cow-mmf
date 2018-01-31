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

#include <multimedia/mm_errors.h>
#include <multimedia/mmmsgthread.h>
#include <multimedia/mm_cpp_utils.h>
#include <string>

#ifndef __ipc_socket_fd_h
#define __ipc_socket_fd_h

namespace YUNOS_MM {

class SideBandIPC : public MMMsgThread{

public:
    SideBandIPC(std::string name, bool isServer);
    virtual ~SideBandIPC();

    mm_status_t sendFd(int fd);
    mm_status_t sendFdAsync(int fd);

    int recvFd();

    bool init();
    bool initAsync();

protected:
#define SB_MSG_sendFd (msg_type)1
#define SB_MSG_init (msg_type)2
    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onSendFd)
    DECLARE_MSG_HANDLER(onInit)

private:
    /* server */
    bool setupSideBandChannel();

    /* client */
    bool connectSideBandChannel();

private:
    std::string mName;

    enum Status {
        UNINITIALIZED,
        CONNECTING,
        CONNECTED,
        TEARDOWN
    };

    Status mState;

    int mFd;
    int mListeningFd;
    bool mIsServer;
    Lock mLock;
    Condition mCondition;

    static const char * MM_LOG_TAG;
    SideBandIPC(const SideBandIPC &);
    SideBandIPC & operator=(const SideBandIPC &);
};

};
#endif
