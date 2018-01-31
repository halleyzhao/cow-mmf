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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "egl_gl_fbo.h"
#include "multimedia/mm_debug.h"
#include "WaylandConnection.h"
#include <Surface.h>

MM_LOG_DEFINE_MODULE_NAME("EGL-FBO")
#if 0
#undef DEBUG
#undef INFO
#undef WARNING
#undef ERROR
#define DEBUG(format, ...)  printf("[D] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)  printf("[I] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define WARNING(format, ...)  printf("[W] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERROR(format, ...)  printf("[E] %s, line: %d:" format "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

#define CHECK_GL_ERROR() do {                   \
        int error = glGetError();               \
        if (error != GL_NO_ERROR)               \
            ERROR("glGetError() = %d", error);  \
    } while (0)

// shader source
#define SHADER(Src) #Src
    // draw yuv texture
    const char* vs0 =SHADER(
            attribute vec2 position;
            attribute vec2 texcoord;
            varying vec2 v_texcoord;
            void main() {
               gl_Position = vec4(position, 0.0, 1.0);
               v_texcoord = texcoord;
            }
        );

    const char* fs0 = "#extension GL_OES_EGL_image_external : require \n" SHADER(
            precision mediump float;
            varying vec2 v_texcoord;
            uniform samplerExternalOES sampler;
            void main() {
               gl_FragColor = texture2D(sampler, v_texcoord);
            }
        );
    // render to YUV FBO
    const char* vs1 = "#version 300 es \n" \
        SHADER(
            in vec2 position;
            in vec2 texcoord;
            out vec2 v_texcoord;
            void main() {
                 gl_Position = vec4(position, 0.0, 1.0);
                 v_texcoord = texcoord;
            }
        );
    const char* fs1 = "#version 300 es \n" \
            "#extension GL_EXT_YUV_target : require \n" \
        SHADER(
            precision mediump float;
            layout (yuv) out vec4 fragColor ;
            in vec2 v_texcoord;
            uniform sampler2D sampler;
            void main() {
                fragColor = texture(sampler, v_texcoord);
            }
        );
// gl_FragColor = texture2D(sampler, v_texcoord);
// layout (yuv) out vec4 color;
// yuvCscStandardEXT conv = itu_601;
#undef SHADER

// FIXME, FBO doesn't require native display/surface, but not reality yet (fail to eglCreateWindowSurface())
class WorkAroundEglInit {
  public:
  static void Init()  {
        mConnection = new yunos::wpc::WaylandConnection(NULL);
        mWlSurface = mConnection->compositor_create_surface();

        struct wl_egl_window_copy* tmp = (struct wl_egl_window_copy*)malloc(sizeof(struct wl_egl_window_copy));
        memset(tmp, 0, sizeof(struct wl_egl_window_copy));
        tmp->surface = mWlSurface;
        tmp->attached_width  = 0;
        tmp->attached_height = 0;
        tmp->nativewindow = 0;
        mEglWindow = (struct wl_egl_window*)tmp;
        ASSERT(mConnection && mWlSurface && mEglWindow);
        mInited = true;
    }
    static void Deinit() {
        usleep(50000);
        mConnection->destroy_surface(mWlSurface);
        mConnection->dec_ref();
        delete mConnection;
        free(mEglWindow);
        mConnection = NULL;
        mWlSurface = NULL;
    }
    static struct wl_display* getNativeDisplay() {
        if (!mInited)
            Init();
        return mConnection->get_wayland_display();
    }
    static struct wl_egl_window* getNativeSurface() { return mEglWindow; }

  private:
    // hack, copied from wayland, with additional padding
    struct wl_egl_window_copy {
        struct wl_surface *surface;

        int width;
        int height;
        int dx;
        int dy;

        int attached_width;
        int attached_height;

        void *nativewindow;
        void (*resize_callback)(struct wl_egl_window *, void *);
        void (*free_callback)(struct wl_egl_window *, void *);
        uint8_t padding[20];
    };
  public: // C++ should support 'private-static' variable declare/define at the same time. however fails
      static yunos::wpc::WaylandConnection* mConnection;
      static struct wl_surface* mWlSurface; // a wl_surface to work around eglCreateWindowSurface issue
      static struct wl_egl_window* mEglWindow;
      static bool mInited;
};
yunos::wpc::WaylandConnection* WorkAroundEglInit::mConnection = NULL;
struct wl_surface* WorkAroundEglInit::mWlSurface = NULL;
struct wl_egl_window* WorkAroundEglInit::mEglWindow = NULL;
bool WorkAroundEglInit::mInited = false;

// ##################################################
static void printGLString() {
    typedef struct {
        const char* str;
        GLenum e;
    }GLString;
    GLString glStrings[] = {
        {"Version", GL_VERSION},
        {"Vendor", GL_VENDOR},
        { "Renderer", GL_RENDERER},
        { "Extensions", GL_EXTENSIONS}
    };
    uint32_t i =0;

    for (i=0; i<sizeof(glStrings)/sizeof (GLString); i++) {
        const char *v = (const char *) glGetString(glStrings[i].e);
        INFO("GL %s = %s", glStrings[i].str, v);
    }
}

// parameter to init EGL
static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
};

static EGLint config_attribs[] = {
       EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
       EGL_RED_SIZE,        1,
       EGL_GREEN_SIZE,      1,
       EGL_BLUE_SIZE,       1,
       EGL_ALPHA_SIZE,      1,
       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, // use gles v3
       EGL_NONE
};

// singleton/global context
static EGLDisplay eglDisplay = NULL;
static EGLConfig eglConfig = NULL; // choosed EGLConfig
static EGLConfig* configs;         // all possible EGLConfigs
#ifndef GL_GLEXT_PROTOTYPES
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
static PFNGLDRAWBUFFERSEXTPROC glDrawBuffersEXT;
#endif

// shader program for gles2
unsigned LoadShader(unsigned type, const std::string& source) {
    FUNC_TRACK();
    unsigned shader = glCreateShader(type);
    const char* src = source.data();
    if (shader) {
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);

        int compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char logInfo[4096];
            glGetShaderInfoLog(shader, sizeof(logInfo), NULL, logInfo);
            ERROR("compile shader logInfo = %s\n", logInfo);
            DEBUG("shader source:\n %s\n", src);
            ASSERT(0 && "compile fragment shader failed");
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

unsigned LoadProgram(const std::string& vertex_source, const std::string& fragment_source) {
    FUNC_TRACK();
    unsigned vertex_shader = LoadShader(GL_VERTEX_SHADER, vertex_source);
    unsigned fragment_shader = LoadShader(GL_FRAGMENT_SHADER, fragment_source);
    unsigned program = glCreateProgram();
    if (vertex_shader && fragment_shader && program) {
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);

        int linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            ASSERT(0 && "link shader program failed");
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vertex_shader)
        glDeleteShader(vertex_shader);
    if (fragment_shader)
        glDeleteShader(fragment_shader);

    return program;
}

// destroy egl global context
static void destroyEglGlobal() __attribute__((destructor(203)));
static void destroyEglGlobal() {
    if (!eglDisplay)
        return;

    printf("%s, about to call elgTerminate\n", __func__);
    eglTerminate(eglDisplay);
    free(configs);
    configs = NULL;
    eglDisplay= NULL;
    eglConfig = NULL;
    printf("%s, done\n", __func__);

    WorkAroundEglInit::Deinit();
}

// init the global env for all eglContext
static bool ensureEglGlobal()
{
    FUNC_TRACK();
    EGLint major, minor;
    EGLBoolean ret = EGL_FALSE;
    EGLint count = -1, n = 0, i = 0, size = 0;

    WorkAroundEglInit::Init();

    if (eglDisplay && eglConfig)
        return true;

    ASSERT(!eglDisplay && !eglConfig);

#ifndef GL_GLEXT_PROTOTYPES
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    ASSERT(eglCreateImageKHR != NULL);
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    ASSERT(eglDestroyImageKHR != NULL);
    glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    ASSERT(glEGLImageTargetTexture2DOES != NULL);
    glDrawBuffersEXT = (PFNGLDRAWBUFFERSEXTPROC)eglGetProcAddress("glDrawBuffersEXT");
    ASSERT(glDrawBuffersEXT != NULL);
#endif

#ifdef YUNOS_ENABLE_UNIFIED_SURFACE
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#elif PLUGIN_HAL  // FIXME, use EGL_DEFAULT_DISPLAY
    eglDisplay = eglGetDisplay(WorkAroundEglInit::getNativeDisplay());
#else
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

    ASSERT(eglDisplay != NULL);
    ret = eglInitialize(eglDisplay, &major, &minor);
    ASSERT(ret == EGL_TRUE);

    ret = eglBindAPI(EGL_OPENGL_ES_API);
    ASSERT(ret == EGL_TRUE);

    if (!eglGetConfigs(eglDisplay, NULL, 0, &count) || count < 1)
        ASSERT(0);

    configs = (EGLConfig*)malloc(sizeof(EGLConfig) * count);
    ASSERT(configs);
    memset(configs, 0, sizeof(EGLConfig) * count);

    ret = eglChooseConfig(eglDisplay, config_attribs, configs, count, &n);
    ASSERT(ret && n >= 1);

    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(eglDisplay, configs[i], EGL_BUFFER_SIZE, &size);
        if (32 == size) {
            eglConfig = configs[i];
            break;
        }
    }

    DEBUG("ensureEglGlobal done");
    return true;
}
namespace YUNOS_MM {
// ################################################################
// EGL context per window
EglWindowContext::EglWindowContext()
    : mWidth(0), mHeight(0)
    , mEglSurface(EGL_NO_SURFACE), mEglContext(EGL_NO_CONTEXT)
    , mCurrTexId(0), mUseSingleTexture(true)
{
    FUNC_TRACK();

    int i=0;
    for (i=0; i<MAX_BUFFER_NUM; i++) {
        mBufferInfo[i].anb = NULL;
        mBufferInfo[i].texId = 0;
        mBufferInfo[i].img = EGL_NO_IMAGE_KHR;
    }

    for (i=0; i<2; i++) {
        mProgramId[i] = 0;
        mVertexPointerHandle[i] = -1;
        mVertexTextureHandle[i] = -1;
        mSamplerHandle[i] = -1;
    }

    mRgbxTexId[0] = 0;
    mRgbxTexId[1] = 0;

    mUseSingleTexture = YUNOS_MM::mm_check_env_str("mm.test.use.single.texture", "MM_TEST_USE_SINGLE_TEXTURE", "1", true);
}

/* usually, FBO doesn't require native-display to init EGL, doesn't require native-window to create EGLSurface.
 * however, it isn't the fact on our platform yet.
 */
bool EglWindowContext::init(/*void* nativeDisplay, void* nativeWindow*/)
{
    FUNC_TRACK();
    EGLBoolean ret;

    if (!ensureEglGlobal())
        return false;

    // shader source code
    static const char* vs[2] = {vs0, vs1};
    const char* fs[2] = {fs0, fs1};

    // init EGL/GLES context
    mEglContext = eglCreateContext(eglDisplay, eglConfig, NULL, context_attribs);
    void *w = NULL;
    // a NULL native surface to create EGLSurface should be ok (especially for FBO), however it fails. let's work around it
    w = WorkAroundEglInit::getNativeSurface();
    mEglSurface = eglCreateWindowSurface(eglDisplay, eglConfig,
            (EGLNativeWindowType)w, NULL);
    ASSERT(mEglContext != EGL_NO_CONTEXT && mEglSurface != EGL_NO_SURFACE);

    ret = eglMakeCurrent(eglDisplay, mEglSurface, mEglSurface, mEglContext);
    ASSERT(ret == EGL_TRUE);

    printGLString();

    // compile and link shader program
    uint32_t i, j;
    for (i=0; i<2; i++) {
        ASSERT(mProgramId[i] == 0 && mVertexPointerHandle[i] == -1 && mVertexTextureHandle[i] == -1 && mSamplerHandle[i] == -1);
        mProgramId[i] = LoadProgram(vs[i], fs[i]);
        ASSERT(mProgramId);
        glUseProgram(mProgramId[i]);
        mVertexPointerHandle[i] = glGetAttribLocation(mProgramId[i], "position");
        mVertexTextureHandle[i] = glGetAttribLocation(mProgramId[i], "texcoord");
        mSamplerHandle[i] = glGetUniformLocation(mProgramId[i], "sampler");
        // ASSERT(GLenum(GL_NO_ERROR) == glGetError());
        ASSERT(mVertexPointerHandle[i] != -1);
        // ASSERT(mVertexPointerHandle[i] != -1 && mVertexTextureHandle[i] != -1 && mSamplerHandle[i] != -1);
        DEBUG("index: %d, mVertexPointerHandle: %d, mVertexTextureHandle: %d, mSamplerHandle: %d",
            i, mVertexPointerHandle[i], mVertexTextureHandle[i], mSamplerHandle[i]);
    }

    // create temp RGBX texture test. [0] uses as src to draw rect, [1] uses as fbo;
    // use same resolution as WindowSurface, so we needn't change glViewport() for FBO
    uint32_t texW = 256, texH = 256; // I tried to use video size to create these texture. but it seems late to init EGL when the first video frame comes (mediacodec stucks)
    char* texData = (char*) malloc (texW * texH * 4);
    glGenTextures(2, mRgbxTexId);

    for (i=0; i<2; i++) {
        glBindTexture(GL_TEXTURE_2D, mRgbxTexId[i]);

        // memset(texData, i*0xf0, texW * texH * 4);
        uint32_t *pixel = (uint32_t*)texData;
        uint32_t pixelValue = 0;
        if (i == 0)
            pixelValue = 0x00f000ff;
        else
            pixelValue = 0x0000f0ff;
        for (j=0; j<texW; j++ ){
            *pixel++ = pixelValue;
        }
        for (j=1; j<texH; j++) {
            memcpy(texData+j*texW*4, texData, texW*4);
        }

        glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, texW, texH, 0,GL_RGBA, GL_UNSIGNED_BYTE, texData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        CHECK_GL_ERROR();
    }
    DEBUG("mRgbxTexId[0]: %d, mRgbxTexId[1]: %d", mRgbxTexId[0], mRgbxTexId[1]);
    free(texData);

    DEBUG("texW: %d, texH: %d", texW, texH);
    glViewport(0, 0, mWidth, mHeight);
    if (mUseSingleTexture)
        glGenTextures(1, &mCurrTexId);
    DEBUG("mCurrTexId: %d", mCurrTexId);
    return true;
}
bool EglWindowContext::deinit() {
    FUNC_TRACK();
    if (mUseSingleTexture && mCurrTexId)
        glDeleteTextures(1, &mCurrTexId);
    glDeleteTextures(2, mRgbxTexId);

    int i = 0;
    for (i = 0; i < MAX_BUFFER_NUM; i++) {
        if (mBufferInfo[i].anb == NULL)
            break;
        if (mBufferInfo[i].texId)
            glDeleteTextures(1, &mBufferInfo[i].texId);
        eglDestroyImageKHR(eglDisplay, mBufferInfo[i].img);

        mBufferInfo[i].anb = NULL;
        mBufferInfo[i].texId = 0;
        mBufferInfo[i].img = EGL_NO_IMAGE_KHR;
    }
    // Free GL resource.
    for (i=0; i<2; i++) {
        if (mProgramId[i]) {
            glDeleteProgram(mProgramId[i]);
            mProgramId[i] = 0;
            mVertexPointerHandle[i] = -1;
            mVertexTextureHandle[i] = -1;
            mSamplerHandle[i] = -1;
        }
    }

    ASSERT(eglDisplay != EGL_NO_DISPLAY && mEglContext != EGL_NO_CONTEXT && mEglSurface != EGL_NO_SURFACE);
    eglDestroyContext(eglDisplay, mEglContext);
    eglDestroySurface(eglDisplay, mEglSurface);
    eglMakeCurrent(eglDisplay, NULL, NULL, NULL);

    mEglContext = EGL_NO_CONTEXT;
    mEglSurface = EGL_NO_SURFACE;

    // workAroundEglDeinit();
    return true;
}

bool EglWindowContext::bindTexture(YNativeSurfaceBuffer* anb)
{
    FUNC_TRACK();
    int i;
    bool needBindTexture = false;

    if (!anb)
        return false;

    for (i = 0; i < MAX_BUFFER_NUM; i++) {
        if (mBufferInfo[i].anb == anb)
            break;

        if (mBufferInfo[i].anb == NULL) {
            EGLClientBuffer clientBuffer = (EGLClientBuffer)anb;
        #ifdef PLUGIN_HAL
            EGLenum target = 0x3140;
        #else
            EGLenum target = EGL_NATIVE_BUFFER_YUNOS;
        #endif
            mBufferInfo[i].img = eglCreateImageKHR(eglDisplay, EGL_NO_CONTEXT, target, clientBuffer, 0);
            ASSERT(mBufferInfo[i].img != EGL_NO_IMAGE_KHR);

            // Note: increase ref count; since eglDestroyImageKHR will decrease one refcount for mali driver
            VERBOSE("(%s, %d): eglCreateImageKHR\n", __func__, __LINE__);

            if (!mUseSingleTexture)
                glGenTextures(1, &mBufferInfo[i].texId);

            mBufferInfo[i].anb = anb;
            needBindTexture = true;
            break;
        }
    }

    if (i >= MAX_BUFFER_NUM) {
        ERROR("run out of buffer, mBufferInfo is full\n");
        return false;
    }

    if (!mUseSingleTexture)
        mCurrTexId = mBufferInfo[i].texId;
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, mCurrTexId);
#ifdef __PHONE_BOARD_MTK__
    needBindTexture = true;
#endif
    if (needBindTexture || mUseSingleTexture)
        glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, mBufferInfo[i].img);

    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // INFO("bind texture[%d]: %d for anb %p", i, mCurrTexId, anb);
    glViewport(0, 0, mWidth, mHeight);
    return true;
}

bool EglWindowContext::drawTestRect(GLuint tex, GLenum texTarget, int mode /* 0: center diamond, 1: left-half, 2: right-half , 3: full-window */)
{
    FUNC_TRACK();
    static const GLfloat vtx[] = {
        // center diamond
        0.0f, -0.75f,
        -0.75f, 0.0f,
        0.0f, 0.75f,
        0.75f, 0.0f,
        // half-left
        -1.0f, 1.0f,
        -1.0f, -1.0f,
        0.0f, -1.0f,
        0.0f, 1.0f,
        // right-half
        -0.0f, 1.0f,
        -0.0f, -1.0f,
        1.0f, -1.0f,
        1.0f, 1.0f,
        // full window
        -1.0f, 1.0f,
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f, 1.0, };
    static const GLfloat texcoords[4][2] = {
        {  0.0f,  0.0f },
        {  0.0f,  1.0f },
        {  1.0f,  1.0f },
        {  1.0f,  0.0f },
    };

    int programIdx = 1;
    if (texTarget == GL_TEXTURE_EXTERNAL_OES)
        programIdx = 0;

    glBindTexture(texTarget, tex);
    glUseProgram(mProgramId[programIdx]);
    glEnableVertexAttribArray(mVertexPointerHandle[programIdx]);
    glVertexAttribPointer(mVertexPointerHandle[programIdx], 2, GL_FLOAT, GL_FALSE, 0, vtx+mode*8);
    CHECK_GL_ERROR();
    if (mVertexTextureHandle[programIdx] != -1) {
        glEnableVertexAttribArray(mVertexTextureHandle[programIdx]);
        glVertexAttribPointer(mVertexTextureHandle[programIdx], 2, GL_FLOAT, GL_FALSE, 0, texcoords);
    }
    if (mSamplerHandle[programIdx] != -1)
        glUniform1i(mSamplerHandle[programIdx], 0);
    CHECK_GL_ERROR();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    CHECK_GL_ERROR();

    return true;
}

bool EglWindowContext::updateFbo(GLenum target, GLuint FboTex, int mode)
{
    FUNC_TRACK();
    GLuint mFramebuffer = 0;

    DEBUG("FboTex: %d", FboTex);
    glBindTexture(target, FboTex);
    CHECK_GL_ERROR();

    glGenFramebuffers(1, &(mFramebuffer));
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, FboTex, 0);
    CHECK_GL_ERROR();

    GLenum pDrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffersEXT(1, pDrawBuffers);

    GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    CHECK_GL_ERROR();
    if( status != GL_FRAMEBUFFER_COMPLETE ) {
        ERROR("error creating FBO: %d", status );
    }

    // draw something to FBO
    drawTestRect(mRgbxTexId[0], GL_TEXTURE_2D, mode);
    CHECK_GL_ERROR();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool EglWindowContext::processBuffer(YNativeSurfaceBuffer* anb, uint32_t width, uint32_t height)
{
    FUNC_TRACK();
    mWidth = width;
    mHeight = height;
    if (! bindTexture(anb))
        return false;

    static uint32_t drawCount = 0;
    drawCount++;
     // 0: does nothing
     // 1: use texture as FBO and does glClear().
     // 2: use texture as FBO and draw a rectangle on it. it is ok on RGBX FBO but fails on YUV FBO
    int drawMode = drawCount/60 % 4;
     INFO("rendering mode #####################  %d  anb: %p######################", drawMode, anb);

    // updateFbo(GL_TEXTURE_2D, mRgbxTexId[1], true);
    updateFbo(GL_TEXTURE_EXTERNAL_OES, mCurrTexId, drawMode);

    return true;
}
} // end of namespace YUNOS_MM
