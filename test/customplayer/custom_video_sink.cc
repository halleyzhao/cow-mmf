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
#include "custom_video_sink.h"
#include <multimedia/component.h>
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM
{
MM_LOG_DEFINE_MODULE_NAME("VideoSinkCustom")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

//////////////////////////////////////////////////////////////////////////////////
VideoSinkCustom::VideoSinkCustom()
{
    FUNC_TRACK();
}

VideoSinkCustom::~VideoSinkCustom()
{
    FUNC_TRACK();
}

mm_status_t VideoSinkCustom::initCanvas()
{
    FUNC_TRACK();

    // init customize video rendering context
    mCanvas = new VSSCanvas();
    if (!mCanvas){
        ERROR("initCanvas new memory failed\n");
        return MM_ERROR_NO_MEM;
    }

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkCustom::drawCanvas(MediaBufferSP buffer)
{
    FUNC_TRACK();
    uintptr_t bufs[3];
    int32_t offsets[3];
    int32_t strides[3];

    if (!(buffer->getBufferInfo(bufs, offsets, strides, 2))) {
        ERROR("fail to retrieve buffer info");
        return MM_ERROR_INVALID_PARAM;
    }

    // customized video rendering here
    DEBUG("customized video rendering");

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkCustom::uninitCanvas()
{
    FUNC_TRACK();

    if (mCanvas) {
        delete mCanvas;
        mCanvas = NULL;
    }

    return MM_ERROR_SUCCESS;
}

void releaseCustomVideoSink(VideoSinkCustom *component)
{
    if (!component) {
        ERROR("invalid component to release");
        return;
    }
    component->uninit();
    delete component;
}

} // YUNOS_MM
