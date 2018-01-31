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

#include <multimedia/mediarecorder.h>
#include "cowrecorder_wrapper.h"
#include <multimedia/mm_debug.h>
#include <multimedia/mm_cpp_utils.h>
#include <dlfcn.h>
#ifdef __USING_RECORDER_SERVICE__
#include "proxyrecorder_wrapper.h"
#endif

namespace YUNOS_MM {

DEFINE_LOGTAG(MediaRecorder)
/*static*/ MediaRecorder * MediaRecorder::create(RecorderType type/* = RecorderType_DEFAULT*/, const void * userDefinedData/* = NULL*/)
{
    INFO("+type: %d\n", type);

#ifdef __USING_RECORDER_SERVICE__
    const char* key = "host.media.recorder.type";
    std::string envStr = mm_get_env_str(key, NULL);
    const char* value = envStr.c_str();

    if (type == RecorderType_COWAudio) {
        INFO("recorder type: %s", value);
        if (strncasecmp(value, "local", 6))
            type = RecorderType_PROXY;
    }
#endif

    INFO("+type after check property: %d\n", type);

    switch ( type ) {
        case RecorderType_DEFAULT:
        case RecorderType_COW:
        case RecorderType_COWAudio:
        case RecorderType_RTSP:
            return new CowRecorderWrapper(type);
        case RecorderType_BINDER_PROXY:
            return createBinderProxy();
#ifdef __USING_RECORDER_SERVICE__
        case RecorderType_PROXY:
            return new ProxyRecorderWrapper();
#endif
        default:
            MMLOGE("invalid type: %d\n", type);
            return NULL;
    }
}

/*static*/ MediaRecorder * MediaRecorder::createBinderProxy() {
    INFO("create binder proxy of media recorder");
    const char* libName = "libmediaservice.so";
    //void* libHandle;
    typedef MediaRecorder* (*createFunc)();
    createFunc create = NULL;
    void *libHandle = NULL;

    libHandle = dlopen(libName, RTLD_NOW);
    if (libHandle == NULL) {
        ERROR("unable to dlopen %s, error: %s", libName, dlerror());
        return NULL;
    }

    create = (createFunc)dlsym(libHandle, "createRecorderBinderProxy");
    if (create == NULL) {
        dlclose(libHandle);
        ERROR("unable to dlsym %s, error: %s", libName, dlerror());
        return NULL;
    }

    MediaRecorder *recorder = create();
    if (recorder)
        recorder->mLibHandle = libHandle;
    else
        dlclose(libHandle);

    return recorder;
}

/*static*/ void MediaRecorder::destroy(MediaRecorder * recorder)
{
    INFO("+\n");
    MM_RELEASE(recorder);
}

MediaRecorder::MediaRecorder() : mListener(NULL), mLibHandle(NULL)
{
}

MediaRecorder::~MediaRecorder()
{
    if (mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
}
mm_status_t MediaRecorder::setListener(Listener * listener)
{
    INFO("+\n");
    mListener = listener;
    return MM_ERROR_SUCCESS;
}

}
