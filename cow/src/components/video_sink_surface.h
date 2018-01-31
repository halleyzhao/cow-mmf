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

#ifndef video_sink_surface_h
#define video_sink_surface_h

#include "multimedia/mmmsgthread.h"
#include "video_sink.h"

#include "mm_surface_compat.h"

#if defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
#define WindowSinkSurface WindowSurface
#endif

namespace YunOSMediaCodec {
    class SurfaceWrapper;
    class MediaSurfaceTexture;
}

namespace YUNOS_MM
{

class MediaSurfaceTexureListener;
typedef MMSharedPtr < MediaSurfaceTexureListener > MediaSurfaceTexureListenerSP;

class MyProducerListener : public yunos::libgui::IProducerObserver
{
public:
     // Callback when a buffer is released by the server.
    virtual void onBufferReleased(YNativeSurfaceBuffer *buffer);
};

class VideoSinkSurface : public VideoSink
{

public:
    VideoSinkSurface();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);

protected:
    virtual ~VideoSinkSurface();

private:
    typedef struct tagVSSCanvas
    {
        bool isSurfaceCfg;
    }VSSCanvas;
    virtual mm_status_t initCanvas();
    virtual mm_status_t drawCanvas(MediaBufferSP buffer);
    virtual mm_status_t uninitCanvas();

    void WindowSurfaceRender(YunOSMediaCodec::SurfaceWrapper*, MediaBufferSP buffer);
    void fillBufferBlank(YunOSMediaCodec::SurfaceWrapper*,  MMNativeBuffer *anb);

friend class MediaSurfaceTexureListener;

private:
    mm_status_t drawCanvas_raw(MediaBufferSP buffer);
    mm_status_t drawCanvas_graphicsBufferHandle(MediaBufferSP buffer);

    VSSCanvas *mCanvas;
    YunOSMediaCodec::SurfaceWrapper* mSurfaceWrapper;
    YunOSMediaCodec::MediaSurfaceTexture *mSurfaceTexture;
    WindowSinkSurface *mNativeWindow;
    MMBQProducer* mBQProducer;
    MMProducerListenerPtr mProducerListener;

    MediaSurfaceTexureListenerSP mSurfaceTextureListener;
};//VideoSinkSurface

}// end of namespace YUNOS_MM
#endif//video_sink_surface_h
