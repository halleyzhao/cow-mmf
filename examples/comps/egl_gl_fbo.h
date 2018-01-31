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

#ifndef __egl_gl_fbo_h__
#define __egl_gl_fbo_h__
// #define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <NativeWindowBuffer.h>
#include <os-compatibility.h>
#include "multimedia/mm_cpp_utils.h"

namespace YUNOS_MM {
class EglWindowContext;
typedef MMSharedPtr <EglWindowContext> EglWindowContextSP;

class EglWindowContext {
    #define MAX_BUFFER_NUM 32
  public:
    EglWindowContext();
    // destruction doesn't run in waylang/egl thread, there will be crash if we delete WindowSurface here
    // now we destroy WindowSurface in egl thread, the unique one as player's listener thread (for the last loop upon MSG_STOPPED)
    ~EglWindowContext() { /* deinit(true); */ }

    bool init(/*void* nativeDisplay = NULL, void* nativeWindow = NULL*/);
    bool deinit();
    bool processBuffer(YNativeSurfaceBuffer* anb, uint32_t width, uint32_t height);

  private:
    int32_t mWidth;
    int32_t mHeight;
    EGLSurface mEglSurface;
    EGLContext mEglContext;
    GLuint mProgramId[2];
    GLint mVertexPointerHandle[2];
    GLint mVertexTextureHandle[2];
    GLint mSamplerHandle[2];

    typedef struct{
        YNativeSurfaceBuffer* anb;
        GLuint texId;
        EGLImageKHR img;
    }  BufferInfo;
    BufferInfo mBufferInfo[MAX_BUFFER_NUM ];
    GLuint mCurrTexId;
    GLuint mRgbxTexId[2]; // RGBX FBO for comparison test
    bool mUseSingleTexture;

    bool bindTexture(YNativeSurfaceBuffer* anb);
    bool updateFbo(GLenum target, GLuint FboTex, int mode);
    bool drawTestRect(GLuint tex, GLenum texTarget, int mode /* 0: center diamond, 1: left-half, 2: right-half*/);
}; // endof EglWindowContext
}// end of namespace YUNOS_MM
#endif // __egl_gl_fbo_h__

