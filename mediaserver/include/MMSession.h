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

#include <string/String.h>

#include <sys/types.h>
#include <unistd.h>

#include <multimedia/mm_cpp_utils.h>

#ifndef __media_session_h
#define __media_session_h

namespace YUNOS_MM {

using namespace yunos;

#define NOTIFY_STATUS(code)                   \
    if (param1) {                             \
        ERROR("state is invalid");            \
        setSessionState(INVALID);             \
    } else                                    \
        setSessionState((SessionState)code);  \

class MMSession {

public:
    enum SessionState {
        INIT,
        CONNECTED,
        PREPARING,
        PREPARED,
        STARTING,
        PLAYING,
        PAUSING,
        PAUSED,
        STOPPING,
        STOPPED,
        RESETTING,
        RESET,
        INVALID
    };

    enum MediaUsage {
        MU_Player,
        MU_MediaCodec,
        MU_Recorder,
        MU_WFDSource,
        MU_WFDSink
    };

    static void sysUpdateMediaUsage(pid_t pid, MediaUsage type, bool turnOff);
    static const char* UsageToString(MediaUsage type);

    MMSession(pid_t pid, uid_t uid, String name);

    virtual ~MMSession();

    pid_t getPid() const;
    uid_t getUid() const;
    MediaUsage getType() const { return mType; }

    virtual const char* debugInfoMsg() { return NULL; }

    static Lock sStatusLock;

    SessionState getSessionState() { return mState; }

    bool isActive() {
        if (mState == STARTING || mState == PLAYING)
            return true;

        return false;
    }

protected:
    pid_t mPid;
    uid_t mUid;
    MediaUsage mType;

    // perf stastictc
    uint64_t mStartTimeUs;
    uint64_t mEndTimeUs;
    uint32_t mNumDecodeFrames;
    uint32_t mNumDropFrames;
    uint32_t mNumRenderFrames;
    uint32_t mRecordFrames;

    // meta data
    String mUri;
    int64_t mDurationMs;
    int64_t mPositionMs;
    int mBuffering_100;

    // update perf data
    virtual void update() {}
    void setSessionState(SessionState state);

    void resumeSession();

    void pauseSession();

    void onSessionDeath(MediaUsage type);

    void onSessionStateUpdate(bool turnOff);

    static const char* stateToString(SessionState state);

    String mDebugInfoMsg;
    String mSessionName;

private:
    SessionState mState;
};


} // end of YUNOS_MM
#endif
