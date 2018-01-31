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
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/pipeline_player_base.h"

using namespace YUNOS_MM;
MM_LOG_DEFINE_MODULE_NAME("TranscodePipeline")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
#define STREAM_DRIFT_MAX      100000 // 100ms

class TranscodePipeline : public PipelinePlayerBase {
  public:

    TranscodePipeline();
    ~TranscodePipeline();

  protected:
    virtual mm_status_t prepareInternal();

  private:
    MM_DISALLOW_COPY(TranscodePipeline)
}; // TranscodePipeline

TranscodePipeline::TranscodePipeline()
{
    mComponents.reserve(5);
    FUNC_TRACK();

}

TranscodePipeline::~TranscodePipeline()
{
    FUNC_TRACK();
}

mm_status_t TranscodePipeline::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    const char* videoMime = NULL;
    const char* audioMime = NULL;
    mm_status_t status = MM_ERROR_SUCCESS;
    mm_status_t ret = MM_ERROR_SUCCESS;
    ComponentSP avmuxer;
    ComponentSP videoFission;  // use MediaFission to drive video data from demuxer to muxer
    ComponentSP fileSink;

    MediaMetaSP mediaMetaFile = MediaMeta::create();
    mediaMetaFile->setString(MEDIA_ATTR_OUTPUT_FORMAT, "mp4");

    extern const char *g_out_video_file_path;
    mediaMetaFile->setString(MEDIA_ATTR_FILE_PATH, g_out_video_file_path);

    // MMAutoLock locker(mLock); NO big lock
    setState(mState, kComponentStatePreparing);
    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);

    status = source->prepare();
    ASSERT_RET(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC, status);
    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mComponents[mDemuxIndex].state, kComponentStatePrepared, false/*pipeline state*/);
    }
    if (status != MM_ERROR_SUCCESS)
        return status;

    status = updateTrackInfo();
    ASSERT_RET(status == MM_ERROR_SUCCESS, status);

    if (mStreamInfo[Component::kMediaTypeVideo].size()) {
        ASSERT(mSelectedTrack[Component::kMediaTypeVideo] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeVideo] < mStreamInfo[Component::kMediaTypeVideo].size());
        videoMime = mStreamInfo[Component::kMediaTypeVideo][mSelectedTrack[Component::kMediaTypeVideo]].mime.c_str();
    }
    if (mStreamInfo[Component::kMediaTypeAudio].size()) {
        ASSERT(mSelectedTrack[Component::kMediaTypeAudio] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeAudio] < mStreamInfo[Component::kMediaTypeAudio].size());
        audioMime = mStreamInfo[Component::kMediaTypeAudio][mSelectedTrack[Component::kMediaTypeAudio]].mime.c_str();
    }
    INFO("media content info, video: %s, audio: %s\n", videoMime, audioMime);

    if ((audioMime == NULL) && (videoMime == NULL)) {
        return MM_ERROR_OP_FAILED;
    }
    if (audioMime != NULL && !strcmp(audioMime, "unknown")) {
        notify(Component::kEventError, MM_ERROR_NO_AUDIODECODER, 0, nilParam);
        audioMime = NULL;
        ret = MM_ERROR_NO_AUDIODECODER;
    }
    if (videoMime != NULL && !strcmp(videoMime, "unknown")) {
        notify(Component::kEventError, MM_ERROR_NO_VIDEODECODER, 0, nilParam);
        videoMime = NULL;
        ret = MM_ERROR_NO_VIDEODECODER;
    }

    // mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);

    MediaMetaSP tmpMeta = MediaMeta::create();
    while (videoMime) {
        // setup video components
        ComponentSP videoDecoder, videoEncoder;
        videoDecoder = createComponentHelper("FFmpeg", videoMime);
        videoFission = createComponentHelper("MediaFission", NULL);
        videoEncoder = createComponentHelper("FFmpeg", videoMime, true);
        avmuxer = createComponentHelper("AVMuxer", NULL);
        //ASSERT_RET(videoFission && avmuxer, MM_ERROR_NO_COMPONENT);
        if (!videoDecoder ||!videoFission || !videoEncoder || !avmuxer) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video related components videoDecoder: %p, videoFission: %p, videoEncoder: %p, avmuxer: %p",
                videoDecoder.get(), videoFission.get(), videoEncoder.get(), avmuxer.get());
            notify(Component::kEventError, MM_ERROR_NO_VIDEODECODER, 0, nilParam);
            break;
        }

        // change component name to ease debug
        tmpMeta->setString("comp-name", "MF-Video");
        videoFission->setParameter(tmpMeta);

        status = videoDecoder->addSource(source, Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoDecoder->addSink(videoFission.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoEncoder->addSource(videoFission.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        status = videoEncoder->addSink(avmuxer.get(), Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        // add video filters if necessary
        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoDecoder, ComponentInfo::kComponentTypeFilter));
            mComponents.push_back(ComponentInfo(videoFission, ComponentInfo::kComponentTypeFilter));
            mVideoCodecIndex = mComponents.size() - 1;
            mComponents.push_back(ComponentInfo(videoEncoder, ComponentInfo::kComponentTypeFilter));
            mComponents.push_back(ComponentInfo(avmuxer, ComponentInfo::kComponentTypeFilter));
            // mVideoSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }


        //Media which only contains audio stream doesn't support variable rate
        // mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, true);
        mHasVideo = true;

        break;
    }

    // videoFission->setParameter(mMediaMeta);  // FIXME, is MediaMeta needed?

    while (audioMime) {
        // setup audio components
        ComponentSP audioDriver = createComponentHelper("MediaFission", NULL); // use MediaFission to drive audio data from demuxer to muxer
        // ASSERT_RET(audioDriver && avmuxer, MM_ERROR_NO_COMPONENT);
        if (!audioDriver) {
            ERROR("fail to create MediaFission to drive audio data");
            notify(Component::kEventError, MM_ERROR_NO_AUDIODECODER, 0, nilParam);
            break;
        }

        // change component name to ease debug
        tmpMeta->setString("comp-name", "MF-Audio");
        audioDriver->setParameter(tmpMeta);

        status = audioDriver->addSource(source, Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio driver (MediaFission)");
            break;
        }

        status = audioDriver->addSink(avmuxer.get(), Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio decoder\n");
            break;
        }

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioDriver, ComponentInfo::kComponentTypeFilter));
            // mAudioCodecIndex = mComponents.size() - 1;
            // mAudioSinkIndex = mSinkClockIndex = mComponents.size() - 1;  // FIXME, how to handle audio sink index?
        }
#if 0 // how to handle clock?
        if (avmuxer) {
            ClockSP clock = avmuxer->provideClock();
            avmuxer->setClock(clock);
        }
#endif
        mHasAudio = true;
        break; //always break here
    }

    while(status == MM_ERROR_SUCCESS) {
        fileSink = createComponentHelper(NULL, "media/file-sink");
        if (!fileSink) {
            ERROR("fail to create file sink to write data");
            notify(Component::kEventError, MM_ERROR_COMPONENT_CONNECT_FAILED, 0, nilParam);
            break;
        }
        mComponents.push_back(ComponentInfo(fileSink, ComponentInfo::kComponentTypeSink));

        status = avmuxer->addSink(fileSink.get(), Component::kMediaTypeVideo);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("fail to link avmuxer to filesink");
            notify(Component::kEventError, MM_ERROR_COMPONENT_CONNECT_FAILED, 0, nilParam);
            break;
        }

        // setup components parameters
        MediaMetaSP mediaMetaMuxer = MediaMeta::create();
        mediaMetaMuxer->setInt32(MEDIA_ATTR_MUXER_STREAM_DRIFT_MAX, STREAM_DRIFT_MAX);
        status = avmuxer->setParameter(mediaMetaMuxer);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("fail to set parameter to avmuxer");
            break;
        }

        status = avmuxer->setParameter(mediaMetaFile);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("fail to set parameter to avmuxer");
            break;
        }

        status = fileSink->setParameter(mediaMetaFile);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("fail to set parameter to filesink");
            break;
        }
        // always break
        break;
    }

    if (MM_ERROR_SUCCESS != ret)
        status = ret;
    return status;
}

/////////////////////////////////////////////////////////////////////////////////////
Pipeline* createTranscodePipeline()
{
    TranscodePipeline *trPipeline = new TranscodePipeline();

    return trPipeline;
}

