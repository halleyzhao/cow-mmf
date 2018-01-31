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

#include "MMSession.h"
#include "MMDpmsProxy.h"

#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

MM_LOG_DEFINE_MODULE_NAME("MMSession");

Lock MMSession::sStatusLock;

MMSession::MMSession(pid_t pid, uid_t uid, String name)
    : mPid(pid),
      mUid(uid),
      mStartTimeUs(0),
      mEndTimeUs(0),
      mNumDecodeFrames(0),
      mNumDropFrames(0),
      mNumRenderFrames(0),
      mRecordFrames(0),
      mState(INIT) {
    mSessionName = name;
}

MMSession::~MMSession() {

}

pid_t MMSession::getPid() const {
    return mPid;
}

uid_t MMSession::getUid() const {
    return mUid;
}

void MMSession::resumeSession() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    mStartTimeUs = t.tv_sec * 1000000LL + t.tv_nsec / 1000LL;

    mNumDecodeFrames = 0;
    mNumDropFrames = 0;
    mNumRenderFrames = 0;
    mRecordFrames = 0;

    mBuffering_100 = -1;
}

void MMSession::pauseSession() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    mEndTimeUs = t.tv_sec * 1000000LL + t.tv_nsec / 1000LL;
}

void onMediaInstanceDeath(String &mediaName, MMSession::MediaUsage type, uid_t uid);
void onMediaUsageUpdate(pid_t pid, MMSession::MediaUsage type, bool turnoff);

void MMSession::onSessionDeath(MediaUsage type) {
    INFO("destroy session");
    onMediaInstanceDeath(mSessionName, type, mUid);
}

void MMSession::setSessionState(SessionState state) {
    MMAutoLock lock(sStatusLock);
    mState = state;
}

/* static */
const char* MMSession::stateToString(SessionState state) {

    if (state == INIT)
        return "init";
    else if (state == CONNECTED)
        return "connected";
    else if (state == PREPARING)
        return "preparing";
    else if (state == PREPARED)
        return "prepared";
    else if (state == STARTING)
        return "starting";
    else if (state == PLAYING)
        return "playing";
    else if (state == PAUSING)
        return "pausing";
    else if (state == PAUSED)
        return "paused";
    else if (state == STOPPING)
        return "stopping";
    else if (state == STOPPED)
        return "stopped";
    else if (state == RESETTING)
        return "resetting";
    else if (state == RESET)
        return "reset";
    else if (state == INVALID)
        return "invalid";

    return NULL;
}

/*static*/
const char* MMSession::UsageToString(MediaUsage type) {
    switch(type) {
        case MU_Player:
            return "MediaPlayer";
            break;
        case MU_MediaCodec:
            return "MediaCodec";
            break;
        case MU_Recorder:
            return "MediaRecorder";
            break;
        case MU_WFDSource:
            return "WFDSource";
            break;
        case MU_WFDSink:
            return "WFDSink";
            break;
        default:
            ERROR("undefined media usage %d", type);
    }

    return "unknown media usage";
}

void MMSession::onSessionStateUpdate(bool turnOff) {
    onMediaUsageUpdate(mPid, mType, turnOff);
}

/* static */
void MMSession::sysUpdateMediaUsage(pid_t pid, MediaUsage type, bool turnOff) {
    MMDpmsProxy::updateServiceSwitch(pid, type, turnOff);
}

} // end of namespace YUNOS_MM
