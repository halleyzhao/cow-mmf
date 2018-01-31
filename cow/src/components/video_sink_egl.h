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

#ifdef _ENABLE_EGL
#include <X11/Xlib.h>
#include "egl_util.h"
#include "gles2_help.h"
#endif

namespace YUNOS_MM
{

class VideoSinkEGL : public VideoSink
{

public:
    VideoSinkEGL();

protected:
    virtual ~VideoSinkEGL();

private:
    typedef struct tagVSSCanvas
    {
#ifdef _ENABLE_EGL
        Display   *X11Display;
        Window    X11Window;
        EGLContextType *EglContext;
        GLuint TextureId;
#endif
    }VSSCanvas;
    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t uninitCanvas();

private:
    VSSCanvas *mCanvas;
};//VideoSinkEGL

}// end of namespace YUNOS_MM
#endif//video_sink_surface_h
