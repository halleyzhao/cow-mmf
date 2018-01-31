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

#include <MediaPlayerInstance.h>
#include <MediaPlayerAdaptor.h>
#include <dbus/DProxy.h>

#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

// libbase name space
using namespace yunos;

DEFINE_LOGTAG(MediaPlayerInstance)

MediaPlayerInstance::MediaPlayerInstance(const String &service,
                                         const String &path,
                                         const String &iface,
                                         pid_t pid,
                                         uid_t uid,
                                         int playType)
    : MediaServiceLooper(service, path, iface),
      mPid(pid),
      mUid(uid),
      mPlayType(playType) {
    INFO("create media player instance");
}

MediaPlayerInstance::~MediaPlayerInstance() {

}

SharedPtr<DAdaptor> MediaPlayerInstance::createMediaAdaptor() {

    MediaPlayerAdaptor *p = new MediaPlayerAdaptor(mService, mObjectPath, mInterface, mPid, mUid, mPlayType);

    p->setCallBackLooper(mLooper);

    mSession = p;

    // MediaPlayerAdaptor is hold MediaServiceLooper->mDAdaptor
    return p;
}

} // end of namespace YUNOS_MM
