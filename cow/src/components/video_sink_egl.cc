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
#include "video_sink_egl.h"
#include <multimedia/component.h>
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM
{
MM_LOG_DEFINE_MODULE_NAME("VideoSinkEGL")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

//////////////////////////////////////////////////////////////////////////////////
VideoSinkEGL::VideoSinkEGL()
{
    FUNC_TRACK();
}

VideoSinkEGL::~VideoSinkEGL()
{
    FUNC_TRACK();
}

mm_status_t VideoSinkEGL::initCanvas()
{
    FUNC_TRACK();

    mCanvas = new VSSCanvas();
    if (!mCanvas){
        ERROR("initCanvas new memory failed\n");
        return MM_ERROR_NO_MEM;
    }

#ifdef _ENABLE_EGL
    if (!mX11Display) {
        char *displayName = getenv("DISPLAY");
        mX11Display = (intptr_t)XOpenDisplay(displayName);
        if (!mX11Display) {
           ERROR("XOpenDisplay failed:%s!\n",displayName);
           return MM_ERROR_IVALID_OPERATION;
        }
    }
    mCanvas->X11Display = (Display*) mX11Display;

    // assumed window resolution
    mCanvas->X11Window = XCreateSimpleWindow(mCanvas->X11Display,
                                                                          DefaultRootWindow(mCanvas->X11Display),
                                                                          0, 0, mWidth, mHeight, 0, 0,
                                                                          WhitePixel(mCanvas->X11Display, 0));
    XMapWindow(mCanvas->X11Display, mCanvas->X11Window);
    mCanvas->EglContext = eglInit(mCanvas->X11Display, mCanvas->X11Window, 0, false);
    glGenTextures(1, &mCanvas->TextureId);
#endif

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkEGL::drawCanvas(MediaBufferSP buffer)
{
    FUNC_TRACK();
    uintptr_t bufs[3];
    int32_t offsets[3];
    int32_t strides[3];

    if (!(buffer->getBufferInfo(bufs, offsets, strides, 2))) {
        ERROR("fail to retrieve buffer info");
        return MM_ERROR_INVALID_PARAM;
    }

#ifdef _ENABLE_EGL
    GLenum target = GL_TEXTURE_2D;
    EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
    glBindTexture(target, mCanvas->TextureId);

    // FIXME, assumed RGBX texture for now
    DEBUG("drm buffer handle: %p", bufs[0]);
    eglImage = createEglImageFromHandle(mCanvas->EglContext->eglContext.display,
                                        mCanvas->EglContext->eglContext.context,
                                        bufs[0], mWidth, mHeight, strides[0]);

    if (eglImage != EGL_NO_IMAGE_KHR) {
        imageTargetTexture2D(target, eglImage);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        drawTextures(mCanvas->EglContext, target, &mCanvas->TextureId, 1);

        destroyImage(mCanvas->EglContext->eglContext.display, eglImage);
    } else {
        ERROR("fail to create EGLImage from drm buf handle");
    }
    eglSwapBuffers(mCanvas->EglContext->eglContext.display,
                   mCanvas->EglContext->eglContext.surface);
#endif

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkEGL::uninitCanvas()
{
    FUNC_TRACK();

    if (mCanvas) {

#ifdef _ENABLE_EGL
        if (mCanvas->EglContext) {
            eglRelease(mCanvas->EglContext);
            mCanvas->EglContext = NULL;
        }

        if (mCanvas->X11Display && mCanvas->X11Window) {
            XDestroyWindow(mCanvas->X11Display, mCanvas->X11Window);
            mCanvas->X11Window = 0;
        }
#endif
        delete mCanvas;
        mCanvas = NULL;
    }

    return MM_ERROR_SUCCESS;
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C"
{

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::VideoSinkEGL *sinkComponent = new YUNOS_MM::VideoSinkEGL();
    if (sinkComponent == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}

void releaseComponent(YUNOS_MM::Component *component)
{
    // FIXME, uninit()?
    delete component;
}

}

