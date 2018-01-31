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

#include <multimedia/media_attr_str.h>
#include <multimedia/pipeline_basic.h>
#include "video_decode_plugin_imp.h"
#include "native_surface_help.h"

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>
#include "native_surface_help.h"
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static uint32_t ENC_WIDTH = 1280;
static uint32_t ENC_HEIGHT = 720;

namespace YUNOS_MM {

DEFINE_LOGTAG(VideoDecodePluginImp);

static int32_t TIME_BASE_NUM_DEF = 1;
static int32_t TIME_BASE_DEN_DEF = 1000000;

// #### decode pipeline
class DecodePipe : public PipelineBasic {
  public:
    explicit DecodePipe(MediaMetaSP meta);
    virtual ~DecodePipe();
    virtual mm_status_t reset();
    bool  OnEncodedFrame(MediaBufferSP buffer);

  protected:
    virtual mm_status_t prepareInternal();

  private:
    MediaMetaSP mMeta;
    Component::WriterSP mSourceWriter;
    MM_DISALLOW_COPY(DecodePipe)
    DECLARE_LOGTAG()
};
DEFINE_LOGTAG(DecodePipe);

DecodePipe::DecodePipe(MediaMetaSP meta)
    : mMeta(meta)
{
    FUNC_TRACK();
    mComponents.reserve(3);
}

DecodePipe::~DecodePipe()
{
    FUNC_TRACK();
}

mm_status_t DecodePipe::reset()
{
    FUNC_TRACK();
    mSourceWriter.reset();
    return PipelineBasic::reset();
}

mm_status_t DecodePipe::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;
    ComponentSP videoSource;
    ComponentSP videoCodec;
    ComponentSP videoSink;

    const char * mime;
    if (!mMeta->getString(MEDIA_ATTR_MIME, mime)) {
        MMLOGE("no mime provided\n");
        return MM_ERROR_FATAL_ERROR;
    }

    // create components
    MMLOGV("video mime: %s\n", mime);
    videoSource = createComponentHelper(NULL, "media/app-source");
    videoCodec = createComponentHelper(NULL, mime);
    videoSink = createComponentHelper(NULL, MEDIA_MIMETYPE_VIDEO_RENDER);

    if (!videoSource || !videoCodec || !videoSink) {
        ERROR("fail to create components: videoSource:%p, videocodec: %p, videoSink: %p", videoSource.get(), videoCodec.get(), videoSink.get());
        return false;
    }

    // set parameters
    videoSource->setParameter(mMeta);
    videoCodec->setParameter(mMeta);
    videoSink->setParameter(mMeta);

    // link components
    videoCodec->addSource(videoSource.get(), Component::kMediaTypeVideo);
    videoCodec->addSink(videoSink.get(), Component::kMediaTypeVideo);

    mSourceWriter = videoSource->getWriter(Component::kMediaTypeVideo);

    mComponents.push_back(ComponentInfo(videoSource, ComponentInfo::kComponentTypeSource));
    mComponents.push_back(ComponentInfo(videoCodec, ComponentInfo::kComponentTypeFilter));
    mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));

    return status;
}
bool DecodePipe::OnEncodedFrame(MediaBufferSP buffer)
{
    mm_status_t ret = mSourceWriter->write(buffer);
    if (ret != MM_ERROR_SUCCESS || !buffer) {
        MMLOGI("readsink ret: %d\n", ret);
        return false;
    }

    return true;
}

// #### VideoDecodePluginImp
/*static */bool VideoDecodePluginImp::releaseMediaBuffer(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    if (!mediaBuffer) {
        MMLOGE("invalid buf\n");
        return true;
    }

    uint8_t * buf;
    int32_t offsets;
    int32_t strides;
    int dimension = 1;
    if (!mediaBuffer->getBufferInfo((uintptr_t*)&buf, &offsets, &strides, dimension)
        || !buf) {
        MMLOGE("invalid buf\n");
        return true;
    }

    delete []buf;
    return true;
}

VideoDecodePluginImp::VideoDecodePluginImp()
    :  mSelfSurface(NULL)
{
    FUNC_TRACK();
}

VideoDecodePluginImp::~VideoDecodePluginImp()
{
    FUNC_TRACK();
    mPlayer.reset();
}

bool VideoDecodePluginImp::InitDecodeContext(woogeen::base::MediaCodec::VideoCodec video_codec, void * surface, int  surfaceType)
{
    FUNC_TRACK();
    mm_status_t st = MM_ERROR_SUCCESS;
    MMLOGI("video_codec: %d, surface: %p, is texture; %d\n", video_codec, surface, surfaceType);

    if (mPlayer)
        return true;

    mCodecType = video_codec;
    // config preparation
     MediaMetaSP meta = MediaMeta::create();
    // FIXME, decide codec type from video_codec
    meta->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_VIDEO_AVC);
    meta->setInt32(MEDIA_ATTR_CODECID, kCodecIDH264);

    mBufferMeta = MediaMeta::create();
    if (!mBufferMeta || !mBufferMeta->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_VIDEO_AVC)) {
        MMLOGE("failed to create buffer meta\n");
        return false;
    }

    // create surface
    if (!surface) {
        mSelfSurface = createSimpleSurface(ENC_WIDTH, ENC_HEIGHT);
        surface = mSelfSurface;
    }
    meta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, surface);
    meta->setInt32("video-force-render", 1);
    meta->setFraction(MEDIA_ATTR_TIMEBASE, TIME_BASE_NUM_DEF, TIME_BASE_DEN_DEF);
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    meta->setInt32(MEDIA_ATTR_WIDTH, ENC_WIDTH);
    meta->setInt32(MEDIA_ATTR_HEIGHT, ENC_HEIGHT);
#endif
    // create player
    mPlayer = CowAppBasic::create(new CowAppBasic());
    if (!mPlayer)
        return false;

    // create my pipeline
    mPipeline = Pipeline::create(new DecodePipe(meta));
    if (!mPipeline)
        return false;

    // prepare & start
    st = mPlayer->setPipeline(mPipeline);
    MMASSERT(st == MM_ERROR_SUCCESS);
    st = mPlayer->prepare();
    MMASSERT(st == MM_ERROR_SUCCESS);
    st = mPlayer->start();
    MMASSERT(st == MM_ERROR_SUCCESS || st == MM_ERROR_ASYNC);

    return true;
}

bool VideoDecodePluginImp::OnEncodedFrame(std::unique_ptr<woogeen::base::VideoEncodedFrame> & frame)
{
    FUNC_TRACK();
    bool ret = false;
    MediaBufferSP buf = createMediaBuffer(frame);
    if (!buf || !mPipeline) {
        return false;
    }
    DecodePipe *pipe = DYNAMIC_CAST<DecodePipe*>(mPipeline.get());
    if (pipe)
        ret = pipe->OnEncodedFrame(buf);

    return ret;
}

bool VideoDecodePluginImp::Release()
{
    FUNC_TRACK();
    mPipeline.reset();
    mPlayer.reset();

    if (mSelfSurface) {
        destroySimpleSurface((WindowSurface*)mSelfSurface);
        mSelfSurface = NULL;
    }

    return true;
}

woogeen::base::VideoDecoderInterface* VideoDecodePluginImp::Copy()
{
    FUNC_TRACK();
    VideoDecodePlugin* decoder = NULL;
    decoder = new VideoDecodePluginImp();
    if (!decoder)
        return NULL;
    // decoder->InitDecodeContext(mCodecType, NULL);
    return decoder;
}

MediaBufferSP VideoDecodePluginImp::createMediaBuffer(std::unique_ptr<woogeen::base::VideoEncodedFrame> & frame)
{
    FUNC_TRACK();
    MediaBufferSP buffer;

     uint8_t * realBuf = new uint8_t[frame->length];
    if (!realBuf)
        return buffer;

    buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer) {
        MMLOGE("failed to create buffer\n");
        delete []realBuf;
        return buffer;
    }

    memcpy(realBuf, frame->buffer, frame->length);
    buffer->setBufferInfo((uintptr_t*)&realBuf, NULL, NULL, 1);
    buffer->setSize(frame->length);
    buffer->setDts(frame->time_stamp);
    buffer->setPts(frame->time_stamp);
    buffer->setDuration(0);

    buffer->addReleaseBufferFunc(releaseMediaBuffer);

    buffer->setMediaMeta(mBufferMeta);

    return buffer;
}


}

