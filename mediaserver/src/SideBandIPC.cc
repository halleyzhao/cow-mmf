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

#include <multimedia/mm_debug.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <SideBandIPC.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(SideBandIPC);

BEGIN_MSG_LOOP(SideBandIPC)
    MSG_ITEM(SB_MSG_sendFd, onSendFd)
    MSG_ITEM(SB_MSG_init, onInit)
END_MSG_LOOP()

SideBandIPC::SideBandIPC(std::string name, bool isServer)
    : MMMsgThread(name.c_str()),
      mName(name),
      mState(UNINITIALIZED),
      mFd(-1),
      mListeningFd(-1),
      mIsServer(isServer),
      mCondition(mLock) {

    INFO("socket name:%s", name.c_str());
    MMMsgThread::run();
}

SideBandIPC::~SideBandIPC() {
    MMMsgThread::exit();

    if (mFd >= 0)
        close(mFd);

    if (mListeningFd >= 0)
        close(mListeningFd);

    unlink(mName.c_str());
}

bool SideBandIPC::init() {

    mLock.acquire();
    if (mState == CONNECTING) {
        mLock.release();

        INFO("SideBandIPC is connecting");
        int count = 0;
        do {
            usleep(10);
        } while(mState != CONNECTED || count++ < 200);

        return (mState == CONNECTED);
    }

    mLock.release();

    {
        MMAutoLock lock(mLock);
        if (mState == CONNECTED)
            return true;
    }

    if (mIsServer)
        return setupSideBandChannel();
    else
        return connectSideBandChannel();

}

bool SideBandIPC::setupSideBandChannel() {

    int server_sockfd = -1, client_sockfd = -1;
    int server_len, client_len;
    struct sockaddr_un server_address;
    struct sockaddr_un client_address;
    int ret;

    unlink(mName.c_str());

    server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sockfd < 0) {
        ERROR("fail to create socket");
        goto out;
    }

    mListeningFd = server_sockfd;

    server_address.sun_family = AF_UNIX;
    if (mName.size() > 0)
        strcpy(server_address.sun_path, mName.c_str());

    INFO("create server socket (fd: %d; name: %s) for listening", mListeningFd, server_address.sun_path);

    server_len = sizeof (server_address);
    ret = bind(server_sockfd, (struct sockaddr *)&server_address, server_len);
    if (ret) {
        ERROR("bind return error %d, %s", errno, strerror(errno));
        goto out;
    }

    ret = chmod(server_address.sun_path, 0777);
    if (ret < 0) {
        ERROR("failed to chmod file %s", mName.c_str());
        goto out;
    }

    ret = listen(server_sockfd, 5);
    if (ret) {
        ERROR("listen return error %d, %s", errno, strerror(errno));
        goto out;
    }

    {
        // make sure we have socket created and is lisntening
        MMAutoLock lock(mLock);
        mState = CONNECTING;
        mCondition.signal();
    }

    INFO("Server is waiting for client connect...\n");
    client_len = sizeof (client_address);
    client_sockfd = accept(server_sockfd, (struct sockaddr *)&server_address, (socklen_t *)&client_len);
    if (client_sockfd == -1) {
        ERROR("accept fail");
        goto out;
    }
    INFO("Server accept incoming connection\n");

    mFd = client_sockfd;

    {
        MMAutoLock lock(mLock);
        mState = CONNECTED;
        mCondition.signal();
    }

    return true;

out:
    if (server_sockfd >= 0) {
        close(server_sockfd);
        mListeningFd = -1;
    }

    if (client_sockfd >= 0) {
        close(client_sockfd);
        mFd = -1;
    }

    unlink(mName.c_str());

    return false;
}

bool SideBandIPC::connectSideBandChannel() {

    struct sockaddr_un address;
    int sockfd;
    int len;
    int result;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        ERROR("fail to create socket");
        return false;
    }

    mFd = sockfd;
    INFO("create socket %d", mFd);
    address.sun_family = AF_UNIX;
    if (mName.size() > 0)
        strcpy (address.sun_path, mName.c_str());
    len = sizeof (address);

    {
        MMAutoLock lock(mLock);
        mState = CONNECTING;
        mCondition.signal();
    }

    result = connect (sockfd, (struct sockaddr *)&address, len);
    if (result == -1) {
        ERROR("connect return error %d, %s", errno, strerror(errno));
        close(mFd);
        return false;
    }

    {
        MMAutoLock lock(mLock);
        mState = CONNECTED;
    }

    return true;
}

void SideBandIPC::onSendFd(param1_type param1, param2_type param2, uint32_t rspId) {
    if (rspId != 0) {
        ERROR("not async sendFd");
        return;
    }

    sendFd((int)param1);
}

void SideBandIPC::onInit(param1_type param1, param2_type param2, uint32_t rspId) {
    if (rspId != 0) {
        ERROR("not async init");
        return;
    }

    init();
}

mm_status_t SideBandIPC::sendFdAsync(int fd) {
    {
        MMAutoLock lock(mLock);
        if(!created())
            MMMsgThread::run();
    }

    int ret = postMsg(SB_MSG_sendFd, fd, NULL);

    {
        MMAutoLock lock(mLock);
        if (mState != CONNECTED) {
            INFO("not connected, waiting 1000ms");
            mCondition.timedWait(1000*1000*1);
        }
    }

    return ret ? MM_ERROR_OP_FAILED : MM_ERROR_ASYNC;
}

bool SideBandIPC::initAsync() {
    {
        MMAutoLock lock(mLock);
        if(!created())
            MMMsgThread::run();
    }

    int ret = postMsg(SB_MSG_init, 0, NULL);

    {
        MMAutoLock lock(mLock);
        mCondition.timedWait(1000*1000*2);
    }

    return ret ?  false : true;
}

mm_status_t SideBandIPC::sendFd(int fd) {

    if (!init())
        return MM_ERROR_NOT_INITED;

    char buf = '1';
    struct msghdr   hdr;
    struct iovec    vec[1];

    union {
        struct cmsghdr    cm;
        char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;

    mm_status_t status = MM_ERROR_SUCCESS;

    hdr.msg_control = control_un.control;
    hdr.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&hdr);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = fd;

    vec[0].iov_base = &buf;
    vec[0].iov_len = 1;

    hdr.msg_name = NULL;
    hdr.msg_namelen = 0;
    hdr.msg_iov = vec;
    hdr.msg_iovlen = 1;
    hdr.msg_flags = 0;

    if(sendmsg(mFd, &hdr, 0) < 0) {
        ERROR("sendmsg fail");
        status = MM_ERROR_OP_FAILED;
    }

    INFO("sendFd return status %d", status);
    return status;
}

int SideBandIPC::recvFd() {

    if (!init())
        return MM_ERROR_NOT_INITED;

    char c;
    struct iovec vec[1];
    ssize_t n;
    struct msghdr msg;

    union {
        struct cmsghdr    cm;
        char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    vec[0].iov_base = &c;
    vec[0].iov_len = 1;
    msg.msg_iov = vec;
    msg.msg_iovlen = 1;
    msg.msg_flags = 0;

    if ( (n = recvmsg(mFd, &msg, 0)) <= 0) {
        ERROR("recvmsg return %" PRId64 "", n);
        return -1;
    }

    int fd = -1;
    if ( (cmptr = CMSG_FIRSTHDR(&msg)) != NULL &&
            cmptr->cmsg_len == CMSG_LEN(sizeof(int))) {
        if (cmptr->cmsg_level != SOL_SOCKET) {
            ERROR("control level != SOL_SOCKET");
            return -1;
        }
        if (cmptr->cmsg_type != SCM_RIGHTS) {
            ERROR("control type != SCM_RIGHTS");
            return -1;
        }
        fd = *((int *) CMSG_DATA(cmptr));
    } else {
        ERROR("invalid fd received\n");
        fd = -1;
    }

    return fd;
}

};
