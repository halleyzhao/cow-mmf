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
    bool  OnFrame(MediaBufferSP buffer);

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
bool DecodePipe::OnFrame(MediaBufferSP buffer)
{
    FUNC_TRACK();
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

    void* ptr = NULL;
    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    if (meta && meta->getPointer("webrtc-videoframe", ptr)) {
        if (ptr) {
            woogeen::base::VideoFrame::ReleaseVieoFrame((woogeen::base::VideoFrame*)ptr);
        }
    }
    return true;
}

VideoDecodePluginImp::VideoDecodePluginImp(void * surface, int surfaceType)
    :  mSurface(surface)
    , mSelfSurface(NULL)
    , mGotKeyFrame(false)
{
    FUNC_TRACK();
}

VideoDecodePluginImp::~VideoDecodePluginImp()
{
    FUNC_TRACK();
    mPlayer.reset();
}

bool VideoDecodePluginImp::InitDecodeContext(woogeen::base::MediaCodec::VideoCodec video_codec)
{
    FUNC_TRACK();
    mm_status_t st = MM_ERROR_SUCCESS;
    MMLOGI("video_codec: %d", video_codec);

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
    if (!mSurface) {
        mSelfSurface = createSimpleSurface(ENC_WIDTH, ENC_HEIGHT);
        mSurface = mSelfSurface;
    }
    meta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mSurface);
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

bool VideoDecodePluginImp::OnFrame(woogeen::base::VideoFrame* frame)
{
    FUNC_TRACK();
    bool ret = false;
    MediaBufferSP buf = createMediaBuffer(frame);
    if (!buf || !mPipeline) {
        return false;
    }
    DecodePipe *pipe = DYNAMIC_CAST<DecodePipe*>(mPipeline.get());
    if (pipe)
        ret = pipe->OnFrame(buf);

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

bool isKeyFrame(uint8_t* data, size_t data_size)
{
    if (data_size<4)
        return false;

    uint32_t pos=2; // start from the 3rd byte since there is pos++ at the begining of loop
    while (pos<data_size-1 && pos<200) { // assume SPS exists no later than 200 bytes
        pos++;
        if (*(data+pos-3) || *(data+pos-2) || *(data+pos-1) != 1)  // search start code
            continue;
        if (*(data+pos) == 0x67)
            return true;
         // SPS may not be the first NALU, continue
    }

    return false;
}

MediaBufferSP VideoDecodePluginImp::createMediaBuffer(woogeen::base::VideoFrame* frame)
{
    FUNC_TRACK();
    MediaBufferSP buffer;

    uint8_t* data = NULL;
    size_t data_size = 0;
    int64_t time_stamp = -1;
    frame->GetFrameInfo(data, data_size, time_stamp);

    if (!mGotKeyFrame) {
        if (!isKeyFrame(data, data_size))
            return buffer;
        INFO("got key frame");
        mGotKeyFrame = true;
     }

    buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer) {
        MMLOGE("failed to create buffer\n");
        return buffer;
    }

    buffer->setBufferInfo((uintptr_t*)&data, NULL, NULL, 1);
    buffer->setSize(data_size);
    buffer->setDts(time_stamp);
    buffer->setPts(time_stamp);
    buffer->setDuration(0);
    DEBUG("data: %p, data_size: %p, time_stamp: %" PRId64, data, data_size, time_stamp);

    MediaMetaSP meta = mBufferMeta->copy();
    meta->setPointer("webrtc-videoframe", frame);
    buffer->setMediaMeta(meta);

    buffer->addReleaseBufferFunc(releaseMediaBuffer);

    return buffer;
}


}

