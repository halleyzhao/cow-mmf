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
 #include <unistd.h>
#include "example_pipeline.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"
#include <libavutil/pixfmt.h>


using namespace YUNOS_MM;
MM_LOG_DEFINE_MODULE_NAME("ExamplePipeline")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)

static MediaMetaSP nilMeta;
static int32_t g_width = 176;
static int32_t g_height = 144;

ExamplePipelinePlayer::ExamplePipelinePlayer()
{
    mComponents.reserve(5);
    FUNC_TRACK();

}

ExamplePipelinePlayer::~ExamplePipelinePlayer()
{
    FUNC_TRACK();
}

mm_status_t ExamplePipelinePlayer::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    const char* videoMime = MEDIA_MIMETYPE_VIDEO_AVC;
    mm_status_t status = MM_ERROR_SUCCESS;
    ComponentSP videoSink;
    ComponentSP videoDecoder;
    setState(mState, kComponentStatePreparing);
    PlaySourceComponent* source = getSourceComponent();
    DEBUG("prepareInternal start");
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);

    status = source->prepare();
    ASSERT_RET(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC, status);
    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mComponents[mDemuxIndex].state, kComponentStatePrepared, false/*pipeline state*/);
    }
    if (status != MM_ERROR_SUCCESS)
        return status;

        // setup video components
        videoDecoder = createComponentHelper(NULL, videoMime);
        videoSink = createComponentHelper(NULL, MEDIA_MIMETYPE_VIDEO_RENDER);
        //ASSERT_RET(videoDecoder && videoSink, MM_ERROR_NO_COMPONENT);
        if (!videoDecoder) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video decoder\n");
            return status;
        }
        if (!videoSink) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video sink\n");
            return status;
        }
       status = videoDecoder->init();
       if (status != MM_ERROR_SUCCESS) {
        if (status != MM_ERROR_ASYNC) {
            ERROR("decoder init fail %d \n", status);
            return status;
        }
        else{
            DEBUG("decoder init ASYNC\n");
            usleep(10*1000);
        }
    }

        status = videoDecoder->addSource(source, Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        mMediaMeta->setInt32(YUNOS_MM::MEDIA_ATTR_WIDTH, g_width);
        mMediaMeta->setInt32(YUNOS_MM::MEDIA_ATTR_HEIGHT,g_height);
        mMediaMeta->setInt32(YUNOS_MM::MEDIA_ATTR_COLOR_FORMAT,(int)AV_PIX_FMT_YUV420P);
        DEBUG("videoDecoder  setParameter:");
        videoDecoder->setParameter(mMediaMeta);

        status = videoDecoder->addSink(videoSink.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        // add video filters if necessary
        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoDecoder));
            mVideoCodecIndex = mComponents.size() - 1;
            mComponents.push_back(ComponentInfo(videoSink));
            mVideoSinkIndex = mSinkClockIndex = mComponents.size() - 1;
            mConnectedStreamCount++;
        }

        mHasVideo = true;
#ifdef __MM_YUNOS_CNTRHAL_BUILD__
    {
        MMAutoLock locker(mLock);
        //protect mSurface
        if (mSurface && videoDecoder) {
            if (mIsSurfaceTexture)
                mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, mSurface);
            else
               mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mSurface);

            DEBUG("videoDecoder setParameter surface:");
            videoDecoder->setParameter(mMediaMeta);
        }
    }
#endif
    DEBUG("prepareInternal end");

    return status;
}



/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    ExamplePipelinePlayer *pipelinePlayer = new ExamplePipelinePlayer();
    if (pipelinePlayer == NULL) {
        return NULL;
    }
    if (pipelinePlayer->init() != MM_ERROR_SUCCESS) {
        delete pipelinePlayer;
        pipelinePlayer = NULL;
        return NULL;
    }
    return static_cast<YUNOS_MM::Pipeline*>(pipelinePlayer);
}

void releasePipeline(YUNOS_MM::Pipeline *pipeline)
{
    if (pipeline) {
        pipeline->uninit();
        delete pipeline;
        pipeline = NULL;
    }
}
}

