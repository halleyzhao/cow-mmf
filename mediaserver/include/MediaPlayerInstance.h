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

#include <MediaServiceLooper.h>

#ifndef __media_player_instance_h
#define __media_player_instance_h

namespace YUNOS_MM {

using namespace yunos;

class MediaPlayerInstance : public MediaServiceLooper {

public:
    MediaPlayerInstance(const String &service,
                        const String &path,
                        const String &iface,
                        pid_t pid,
                        uid_t uid,
                        int playType);

    virtual ~MediaPlayerInstance();

protected:
    static const char * MM_LOG_TAG;

private:
    pid_t mPid;
    uid_t mUid;
    int mPlayType;

    virtual SharedPtr<DAdaptor> createMediaAdaptor();
    virtual SharedPtr<DProxy> createMediaProxy() { return NULL; }

private:
    MediaPlayerInstance(const MediaPlayerInstance &);
    MediaPlayerInstance & operator=(const MediaPlayerInstance &);
};

} // end of YUNOS_MM
#endif
