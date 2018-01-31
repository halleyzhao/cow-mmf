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
#include "video_sink_surface.h"
#include <multimedia/component.h>
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

#include "media_surface_utils.h"
#include "media_surface_texture.h"

namespace YUNOS_MM
{

MM_LOG_DEFINE_MODULE_NAME("VideoSinkSurface")
//#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

class MediaSurfaceTexureListener : public YunOSMediaCodec::SurfaceTextureListener {
public:
    MediaSurfaceTexureListener(VideoSinkSurface *owner) { mOwner = owner; }
    virtual ~MediaSurfaceTexureListener() {}

    virtual void onMessage(int msg, int param1, int param2) {
        if (mOwner) {
            mOwner->notify(Component::kEventUpdateTextureImage, param1, param2, nilParam);
         }
    }

private:

    VideoSinkSurface* mOwner;
};

//////////////////////////////////////////////////////////////////////////////////
VideoSinkSurface::VideoSinkSurface()
    : mCanvas(NULL),
      mSurfaceWrapper(NULL),
      mSurfaceTexture(NULL),
      mNativeWindow(NULL),
      mBQProducer(NULL)
{
    FUNC_TRACK();
}

VideoSinkSurface::~VideoSinkSurface()
{
    FUNC_TRACK();
}

///////////////////////////////////////// RawVideo render
void VideoSinkSurface::WindowSurfaceRender(YunOSMediaCodec::SurfaceWrapper* winSurface, MediaBufferSP buffer)
{
    FUNC_TRACK();
    int32_t width = 0, height = 0, format = 0;
    uint8_t *buf[3] = {NULL, NULL, NULL};
    int32_t offset[3] = {0, 0, 0};
    int32_t stride[3] = {0, 0, 0};
    int32_t isRgb = 0;
    MediaMetaSP meta = buffer->getMediaMeta();

    if (!meta) {
        ERROR("no meta data");
        return;
    }

    if (buffer->isFlagSet(MediaBuffer::MBFT_EOS) || !(buffer->size())) {
        INFO("abort, EOS or buffer size is zero");
        return;
    }

    meta->getInt32(MEDIA_ATTR_WIDTH, width);
    meta->getInt32(MEDIA_ATTR_HEIGHT, height);
    meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, format);

    if (winSurface == NULL || width <= 0 || height <=0/* || format != AV_PIX_FMT_YUV420P*/)
        return;

    if (!meta->getInt32(MEDIA_ATTR_COLOR_FOURCC, isRgb))
        isRgb = 1;
    else
        isRgb = 0;

    DEBUG("width: %d, height: %d, format: %x, rgb: %d", width, height, format, isRgb);

    if (!(buffer->getBufferInfo((uintptr_t *)buf, offset, stride, 3))) {
        WARNING("fail to retrieve data from mediabuffer");
    }
    for (int i=0; i<3; i++) {
        DEBUG("i: %d, buf: %p, offset: %d, stride: %d", i, buf[i], offset[i], stride[i]);
    }

    uint32_t flags;

    if (winSurface && !mCanvas->isSurfaceCfg) {
        int ret = 0;

        ret = winSurface->set_buffers_dimensions(width, height, flags);

        uint32_t halFormat = FMT_PREF(YV12);

        if (isRgb)
            halFormat = FMT_PREF(RGBA_8888);

        ret |= winSurface->set_buffers_format(halFormat, flags);

        ret |= winSurface->set_usage(ALLOC_USAGE_PREF(HW_TEXTURE) |
              //ALLOC_USAGE_PREF(EXTERNAL_DISP) |
              ALLOC_USAGE_PREF(SW_READ_OFTEN) |
              ALLOC_USAGE_PREF(SW_WRITE_OFTEN),
              flags);

        int bufCnt = 5;
        int minUndequeue = 2;
        ret |= winSurface->set_buffer_count(bufCnt, flags);

        MMNativeBuffer *anb[16] = {NULL,};

        for (int i = 0; i < bufCnt; i++)
            winSurface->dequeue_buffer_and_wait(&anb[i], flags);

        for (int i = 0; i < minUndequeue; i++)
            winSurface->cancel_buffer(anb[i], -1, flags);

        for (int i = minUndequeue; i < bufCnt; i++) {
            fillBufferBlank(winSurface, anb[i]);
            winSurface->queueBuffer(anb[i], -1, flags);
            winSurface->finishSwap(flags);
        }

        INFO("config native window, return %d", ret);
        mCanvas->isSurfaceCfg = true;
    }

    int fenceFd = -1;
    void* pointer = NULL;
    MMNativeBuffer* anb = NULL;
    /*
    Rect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    */

    /*
    anw->dequeueBuffer(anw,
            &anb, &fenceFd);
    */
    winSurface->dequeue_buffer_and_wait(&anb, flags);
    if (!anb) {
        ERROR("dequeue buffer but get NULL");
        return;
    }
    /*
    winSurface->mapBuffer(anb,
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN,
            rect, &pointer);
    */
    winSurface->mapBuffer(anb, 0, 0, width, height, &pointer, flags);

    if (!pointer) {
        ERROR("fail to map native window buffer");
        return;
    }

    int srcW = width, srcH = height;
    int dstW = anb->stride, dstH = anb->height;

    int32_t scaleFactor[3] = {1, 2, 2}; // for YV12, W/H scale factor is same
    uint8_t *dstY = (uint8_t*)pointer;
    uint8_t *dstV = dstY + dstW * dstH;
#ifdef __USEING_UV_STRIDE16__
    int uvStride = ((dstW/scaleFactor[2] + 15) & (~15));
#else
    int uvStride = dstW/scaleFactor[2];
#endif
    //uint8_t *dstU = dstY + dstW * dstH +  dstW * dstH / 4;
    uint8_t *dstU = dstY + dstW * dstH +  uvStride * dstH / 2;

    if (isRgb) {
        srcW *= 4;
        dstW *= 4;
    }

    for(int j = 0; j < srcH/scaleFactor[0]; j++) {
        memcpy(dstY, buf[0]+ j * stride[0], srcW/scaleFactor[0]);
        dstY += dstW/scaleFactor[0];
    }

    if (isRgb)
        goto out;

    for(int j = 0; j < srcH/scaleFactor[1]; j++) {
        memcpy(dstU, buf[1]+ j * stride[1], srcW/scaleFactor[1]);
        dstU += uvStride;
    }

    for(int j = 0; j < srcH/scaleFactor[2]; j++) {
        memcpy(dstV, buf[2]+ j * stride[2], srcW/scaleFactor[2]);
        dstV += uvStride;
    }

out:
#if 0
            static FILE *fp = NULL;
            if (fp == NULL) {
                char name[128];
                sprintf(name, "/data/%dx%d.rgb888", width, height);
                fp = fopen(name, "wb");
            }
            fwrite(pointer, srcW, srcH, fp);
#endif

    winSurface->unmapBuffer(anb, flags);
    winSurface->queueBuffer(anb, fenceFd, flags);
    winSurface->finishSwap(flags);
}

mm_status_t VideoSinkSurface::initCanvas()
{
    FUNC_TRACK();

    mCanvas = new VSSCanvas();
    if (!mCanvas){
        ERROR("initCanvas new memory failed\n");
        return MM_ERROR_NO_MEM;
    }

    mCanvas->isSurfaceCfg = false;
    return MM_ERROR_SUCCESS;
}
mm_status_t VideoSinkSurface::drawCanvas_raw(MediaBufferSP buffer)
{
    FUNC_TRACK();
    MediaMetaSP meta = buffer->getMediaMeta();

    void *surface = NULL;
    bool isTexture = false;

    if (!meta) {
        ERROR("no meta data");
        return MM_ERROR_INVALID_PARAM;
    }

    if (meta) {
        if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE, (void *&)surface))
            isTexture = false;
        else if (meta->getPointer("surface-texture", (void *&)surface) && surface)
            isTexture = true;

        if (surface && !mSurfaceWrapper) {
            if (isTexture) {
                mSurfaceTexture = (YunOSMediaCodec::MediaSurfaceTexture *)surface;
                mSurfaceTextureListener.reset(new MediaSurfaceTexureListener(this));
                mSurfaceTexture->setListener(mSurfaceTextureListener.get());
                mSurfaceWrapper = new YunOSMediaCodec::SurfaceTextureWrapper(mSurfaceTexture);
            } else {
                mNativeWindow = (WindowSurface*) surface;
                mSurfaceWrapper = new YunOSMediaCodec::WindowSurfaceWrapper(mNativeWindow);
            }
        }

        if (surface && surface != mNativeWindow && surface != mSurfaceTexture) {
            ERROR("surface is not consistent");
            return MM_ERROR_INVALID_PARAM;
        }
    }

    WindowSurfaceRender(mSurfaceWrapper, buffer);

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkSurface::uninitCanvas()
{
    FUNC_TRACK();
    if (mCanvas) {
        delete mCanvas;
        mCanvas = NULL;
    }

    if (mSurfaceTexture) {
        mSurfaceTexture->setListener(NULL);
    }

    if (mSurfaceWrapper)
        delete mSurfaceWrapper;

    mSurfaceWrapper = NULL;

    if (mBQProducer)
        mBQProducer->disconnect();
    mBQProducer = NULL;

    return MM_ERROR_SUCCESS;
}

void VideoSinkSurface::fillBufferBlank(YunOSMediaCodec::SurfaceWrapper* winSurface, MMNativeBuffer *anb) {
    void* pointer = NULL;
    uint32_t flags;

    if (!winSurface || !anb)
        return;

    winSurface->mapBuffer(anb, 0, 0, anb->width, anb->height, &pointer, flags);

    if (!pointer) {
        ERROR("fail to map native window buffer");
        return;
    }

    uint8_t *dstY = (uint8_t*)pointer;
    int dstW = anb->stride, dstH = anb->height;
    uint8_t *dstV = dstY + dstW * dstH;

    memset(dstY, 0, anb->stride * anb->height);
    memset(dstV, 128, anb->stride * anb->height / 2);

    winSurface->unmapBuffer(anb, flags);
}

// ////////////////////// GraphicsBufferHandle typed buffer render

// BQ producer listener
void MyProducerListener::onBufferReleased(YNativeSurfaceBuffer *buffer) {
    DEBUG("release buffer: %p", buffer);
    nativeBufferDecRef(buffer);
}

mm_status_t VideoSinkSurface::drawCanvas_graphicsBufferHandle(MediaBufferSP buffer)
{
    FUNC_TRACK();
    uintptr_t buffers[3] = {0};
    int32_t offsets[3] = {0};
    int32_t strides[3] = {0};
    DEBUG();
    if (!buffer || buffer->isFlagSet(MediaBuffer::MBFT_EOS) || !mBQProducer)
        return MM_ERROR_SUCCESS;

    buffer->getBufferInfo((uintptr_t *)buffers, offsets, strides, 3);
    MMBufferHandleT target_t = *(MMBufferHandleT*)buffers[0];
    ASSERT(target_t);

#ifdef __MM_YUNOS_YUNHAL_BUILD__
    int format = FMT_PREF(NV12);
#else
    int format = FMT_PREF(YV12);
#endif
    NativeWindowBuffer* nwb = new NativeWindowBuffer(mWidth, mHeight, format,
        ALLOC_USAGE_PREF(HW_TEXTURE) | ALLOC_USAGE_PREF(HW_RENDER), /*x_stride*/ mWidth, target_t, false);
    nwb->bufInit();
    nativeBufferIncRef(nwb);
    mBQProducer->attachAndPostBuffer((MMNativeBuffer*)nwb);
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkSurface::drawCanvas(MediaBufferSP buffer)
{
    mm_status_t ret = MM_ERROR_SUCCESS;
    MediaBuffer::MediaBufferType type = buffer->type();
    DEBUG();
    switch (type) {
    case MediaBuffer::MBT_RawVideo:
        ret = drawCanvas_raw(buffer);
        break;
    case MediaBuffer::MBT_GraphicBufferHandle:
        ret = drawCanvas_graphicsBufferHandle(buffer);
        break;
    default:
        break;
    }

    return ret;
}

mm_status_t VideoSinkSurface::setParameter(const MediaMetaSP & meta) {
    bool ret = true;
    void* ptr = NULL;

    ret = meta->getPointer(MEDIA_ATTR_VIDEO_BQ_PRODUCER, ptr);

    if (ret) {
        mBQProducer = (MMBQProducer*)ptr;
        mProducerListener.reset(new MyProducerListener());
        mBQProducer->connect(mProducerListener);
    }
    return VideoSink::setParameter(meta);
}


} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C"
{

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder)
{
    YUNOS_MM::VideoSinkSurface *sinkComponent = new YUNOS_MM::VideoSinkSurface();
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

