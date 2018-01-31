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

#ifndef custom_video_sink_h
#define custom_video_sink_h

#include "multimedia/mmmsgthread.h"
#include "multimedia/video_sink.h"

namespace YUNOS_MM
{

class VideoSinkCustom : public VideoSink
{

public:
    VideoSinkCustom();
    virtual ~VideoSinkCustom();

private:
    typedef struct tagVSSCanvas
    {
        int32_t dummy;
    }VSSCanvas;
    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t uninitCanvas();

private:
    VSSCanvas *mCanvas;
};//VideoSinkCustom

void releaseCustomVideoSink(VideoSinkCustom *component);
}// end of namespace YUNOS_MM
#endif//custom_video_sink_h
