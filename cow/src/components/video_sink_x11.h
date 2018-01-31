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

#ifndef video_sink_egl_h
#define video_sink_egl_h

#include "multimedia/mmmsgthread.h"
#include "video_sink.h"

#ifdef _ENABLE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xvlib.h>
#include <X11/keysym.h>

#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include <libswscale/swscale.h>

#ifdef __cplusplus
}
#endif

#define XVFORMAT_MAX_NUM    32
namespace YUNOS_MM
{

class VideoSinkX11 : public VideoSink
{

public:
    VideoSinkX11();

protected:
    virtual ~VideoSinkX11();

private:
    typedef struct tagVSSCanvas
    {
#ifdef _ENABLE_X11
        Display * X11Display;
        Window X11Window;
        XGCValues  xGcv;
        GC gc;
        XvAdaptorInfo *info;
        int adaptor;
        char xvName[XVFORMAT_MAX_NUM][5];
        int supportFormatNum;
        int currentFormatIdx;
#endif
    }VSSCanvas;
    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t uninitCanvas();

private:
    VSSCanvas *mCanvas;
};//VideoSinkX11

}// end of namespace YUNOS_MM
#endif//video_sink_surface_h
