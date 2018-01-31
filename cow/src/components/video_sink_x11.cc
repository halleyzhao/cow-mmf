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
#include "video_sink_x11.h"
#include <multimedia/component.h>
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_attr_str.h"
#include <multimedia/mm_debug.h>

namespace YUNOS_MM
{

MM_LOG_DEFINE_MODULE_NAME("VideoSinkX11")
// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define FUNC_TRACK()

//////////////////////////////////////////////////////////////////////////////////
VideoSinkX11::VideoSinkX11()
{
    FUNC_TRACK();
}

VideoSinkX11::~VideoSinkX11()
{
    FUNC_TRACK();
}

mm_status_t VideoSinkX11::initCanvas()
{
    FUNC_TRACK();

    mCanvas = new VSSCanvas();
    if (!mCanvas){
        ERROR("initCanvas new memory failed\n");
        return MM_ERROR_NO_MEM;
    }

#ifdef _ENABLE_X11
    uint32_t numAdaptors;
    int i;

    //#0 open the display windows
    mCanvas->X11Display = XOpenDisplay( getenv( "DISPLAY" ) );
    if(mCanvas->X11Display == NULL) {
        ERROR("XOpenDisplay is failed\n");
        return MM_ERROR_IVALID_OPERATION;
    }
    INFO("XOpenDisplay is ok:%p\n", mCanvas->X11Display);

    //#1 query the format
    XvQueryAdaptors(mCanvas->X11Display,
                             DefaultRootWindow(mCanvas->X11Display),
                             &numAdaptors, &mCanvas->info);
    INFO("XvQueryAdaptors numAdaptors:%d\n",numAdaptors);

    mCanvas->supportFormatNum = 0;
    for( i=0; i < (int32_t)numAdaptors; i++) {
        int numFormats = 0;
        XvImageFormatValues *formats = XvListImageFormats(mCanvas->X11Display,
                                                          mCanvas->info[i].base_id,
                                                          &numFormats);
        INFO("i:%d,XvListImageFormats:%d\n",i,numFormats);
        for(int j = 0; j < numFormats; j++) {
            if (mCanvas->supportFormatNum >= XVFORMAT_MAX_NUM) {
                   ERROR("the current support format is too many\n");
                   uninitCanvas();
                   return MM_ERROR_IVALID_OPERATION;
            }

            mCanvas->xvName[mCanvas->supportFormatNum][4] = 0;
            memcpy(mCanvas->xvName[mCanvas->supportFormatNum],
                    &formats[j].id, 4);
            INFO("xv_name:idx:%d,%s,0x%x\n", mCanvas->supportFormatNum,
                                             mCanvas->xvName[mCanvas->supportFormatNum],
                                             formats[j].id);

            if (!strcmp(mCanvas->xvName[mCanvas->supportFormatNum],"I420")) {
                   INFO("find the correct format:%d,%s\n",
                                mCanvas->xvName[mCanvas->supportFormatNum],
                                mCanvas->xvName[mCanvas->supportFormatNum]);
                   mCanvas->currentFormatIdx = mCanvas->supportFormatNum;
                   mCanvas->adaptor = i;
            }
            mCanvas->supportFormatNum ++;
        }

        XFree(formats);
    }

    //#2 create the windows
    mCanvas->X11Window = XCreateSimpleWindow(mCanvas->X11Display,
                                             DefaultRootWindow(mCanvas->X11Display),
                                             0, 0, mWidth, mHeight, 0,
                                             WhitePixel(mCanvas->X11Display,
                                                        DefaultScreen(mCanvas->X11Display)),
                                            0x010203);
    INFO("window:%d\n", mCanvas->X11Window);

    XSelectInput(mCanvas->X11Display, mCanvas->X11Window, StructureNotifyMask|KeyPressMask);
    XMapWindow(mCanvas->X11Display, mCanvas->X11Window);
    mCanvas->gc = XCreateGC(mCanvas->X11Display, mCanvas->X11Window,0, &mCanvas->xGcv);
    printf("gc:%p\n",mCanvas->gc);

#endif

    INFO("call initWindow is ok, the current idx:%d,adaptor:%d\n",
               mCanvas->currentFormatIdx, mCanvas->adaptor);
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkX11::drawCanvas(MediaBufferSP buffer)
{
    FUNC_TRACK();
    MediaMetaSP meta = buffer->getMediaMeta();
    int32_t width = 0, height = 0, format = 0;
    XvImage *image=NULL;

    uintptr_t bufs[3];
    int32_t offsets[3];
    int32_t strides[3];

    if (!(buffer->getBufferInfo(bufs, offsets, strides, 2))) {
        ERROR("fail to retrieve buffer info");
        return MM_ERROR_INVALID_PARAM;
    }

    if (meta) {
        meta->getInt32(MEDIA_ATTR_WIDTH, width);
        meta->getInt32(MEDIA_ATTR_HEIGHT, height);
        meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, format);

        //INFO("w:%d,w1:%d,h:%d,h1:%d,f:%d\n",mWidth,width,mHeight,height,(int)format);

        assert((width==mWidth) && (height == mHeight) && (format == AV_PIX_FMT_YUV420P));
    }

#ifdef _ENABLE_X11
    //#3 create the image
    image=XvCreateImage(mCanvas->X11Display,
                        mCanvas->info[mCanvas->adaptor].base_id,
                        *((int*)mCanvas->xvName[mCanvas->currentFormatIdx]),
                        (char *)(bufs[0]), width, height);
    if (!image) {
        ERROR("create the image is failed\n");
        return MM_ERROR_IVALID_OPERATION;
    }

    XvPutImage(mCanvas->X11Display,
               mCanvas->info[mCanvas->adaptor].base_id,
               mCanvas->X11Window,
               mCanvas->gc,
               image, 0, 0, width, height,
               0, 0, width, height);
    XFree(image);
#endif

    return MM_ERROR_SUCCESS;
}

mm_status_t VideoSinkX11::uninitCanvas()
{
    FUNC_TRACK();

    if (mCanvas) {
#ifdef _ENABLE_X11
    if (mCanvas->X11Display && mCanvas->gc)
        XFreeGC(mCanvas->X11Display, mCanvas->gc);

    if (mCanvas->X11Display)
        XCloseDisplay (mCanvas->X11Display);

    mCanvas->X11Display = 0;
    mCanvas->X11Window = 0;
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
    YUNOS_MM::VideoSinkX11 *sinkComponent = new YUNOS_MM::VideoSinkX11();
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

