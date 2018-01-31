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
#include "pipeline_player.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"
#include "multimedia/mm_cpp_utils.h"

#define COW_PLAY_DISABLE_AUDIO_STR "cowplayer.disable.audio"
#define COW_PLAY_DISABLE_AUDIO_ENV "COWPLAYER_DISABLE_AUDIO"
#define COW_PLAY_DISABLE_VIDEO_STR "cowplayer.disable.video"
#define COW_PLAY_DISABLE_VIDEO_ENV "COWPLAYER_DISABLE_VIDEO"
#define SUBTITLE_SINK "SubtitleSink"
#define SUBTITLE_SOURCE "SubtitleSource"


namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-PLP")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

static MediaMetaSP nilMeta;
PipelinePlayer::PipelinePlayer()
{
    mComponents.reserve(5);
    FUNC_TRACK();

}

PipelinePlayer::~PipelinePlayer()
{
    FUNC_TRACK();
    /*
    ListenerPipeline* listener = DYNAMIC_CAST<ListenerPipeline*>(mListenerReceive.get());
    listener->removeWatcher();
    */
}

mm_status_t PipelinePlayer::prepareInternal()//audio source is followed by video source
{
    FUNC_TRACK();
    const char* videoMime = NULL;
    const char* audioMime = NULL;
    mm_status_t status = MM_ERROR_SUCCESS;
    mm_status_t ret = MM_ERROR_SUCCESS;
    ComponentSP videoSink;
    ComponentSP videoDecoder, videoFilter;
    ComponentSP subtitleSink;
    ComponentSP subtitleSource;
    MMParamSP param;

    // MMAutoLock locker(mLock); NO big lock
    setState(mState, kComponentStatePreparing);
    PlaySourceComponent* source = getSourceComponent();
    PlaySourceComponent* videoSource = source;
    PlaySourceComponent* audioSource = source;
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

    bool externalResource = !strcmp(source->name(), "APPPlaySource");
    if (!externalResource) {
        if (mStreamInfo[Component::kMediaTypeVideo].size()) {
            ASSERT(mSelectedTrack[Component::kMediaTypeVideo] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeVideo] < mStreamInfo[Component::kMediaTypeVideo].size());
            videoMime = mStreamInfo[Component::kMediaTypeVideo][mSelectedTrack[Component::kMediaTypeVideo]].mime.c_str();
        }
        if (mStreamInfo[Component::kMediaTypeAudio].size()) {
            ASSERT(mSelectedTrack[Component::kMediaTypeAudio] >=0 && (uint32_t)mSelectedTrack[Component::kMediaTypeAudio] < mStreamInfo[Component::kMediaTypeAudio].size());
            audioMime = mStreamInfo[Component::kMediaTypeAudio][mSelectedTrack[Component::kMediaTypeAudio]].mime.c_str();
        }

        if ((audioMime == NULL) &&
            (videoMime == NULL)) {
            return MM_ERROR_OP_FAILED;
        }

        while (mUriType == kUriMPD && (videoMime == NULL || audioMime == NULL)) {
            DashSourceComponent* dashSource = getDashSourceComponent();
            if (!dashSource) {
                ERROR("is not dash");
                break;
            }
            param = dashSource->getMediaRepresentationInfo();
            ComponentSP secondSource;
            DashSourceComponent* secondDashSource = NULL;
            if (videoMime == NULL) {
                if (mm_check_env_str(COW_PLAY_DISABLE_VIDEO_STR, COW_PLAY_DISABLE_VIDEO_ENV, "1"))
                    break;
                INFO("try to find video from MPD");
                videoMime = getMimeFromMediaRepresentationInfo(Component::kMediaTypeVideo, param);
                if (videoMime)
                    secondSource = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_DASH_DEMUXER);
                secondDashSource = DYNAMIC_CAST<DashSourceComponent*>(secondSource.get());
                videoSource = secondDashSource;
                if (secondDashSource)
                    secondDashSource->selectMediaRepresentation(-1, Component::kMediaTypeVideo, -1, -1);
            } else if (audioMime == NULL) {
                if (mm_check_env_str(COW_PLAY_DISABLE_AUDIO_STR, COW_PLAY_DISABLE_AUDIO_ENV, "1"))
                    break;
                INFO("try to find audio type from MPD");
                audioMime = getMimeFromMediaRepresentationInfo(Component::kMediaTypeAudio, param);
                if (audioMime)
                    secondSource = createComponentHelper(NULL, MEDIA_MIMETYPE_MEDIA_DASH_DEMUXER);
                secondDashSource = DYNAMIC_CAST<DashSourceComponent*>(secondSource.get());
                audioSource = secondDashSource;
                if (secondDashSource)
                    secondDashSource->selectMediaRepresentation(-1, Component::kMediaTypeAudio, -1, -1);
            }

            if (!secondDashSource) {
                WARNING("fail to create component:%s\n", MEDIA_MIMETYPE_MEDIA_DASH_DEMUXER);
                break;
            }

            status = secondDashSource->setMPD(dashSource->getMPD());
            if (status != MM_ERROR_SUCCESS) {
                WARNING("fail to set MPD");
                break;
            }

            int32_t idx;
            {
                MMAutoLock locker(mLock);
                mComponents.push_back(ComponentInfo(secondSource, ComponentInfo::kComponentTypeSource));
                idx = mComponents.size() - 1;
                mDashSecondSourceIndex = idx;
            }

            status = secondSource->prepare();
            ASSERT_RET(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC, status);
            if (status == MM_ERROR_ASYNC) {
                status = waitUntilCondition(mComponents[idx].state, kComponentStatePrepared, false/*pipeline state*/);
            }
            if (status != MM_ERROR_SUCCESS)
                return status;

            INFO("source is prepared");
        }
    } else {
        videoMime = mVideoMime.empty() ? NULL : mVideoMime.c_str();
        audioMime = mAudioMime.empty() ? NULL : mAudioMime.c_str();
    }

    INFO("media content info, video: %s, audio: %s\n", videoMime, audioMime);

    if (mm_check_env_str(COW_PLAY_DISABLE_AUDIO_STR, COW_PLAY_DISABLE_AUDIO_ENV, "1")) {
        audioMime = NULL;
    }
    if (mm_check_env_str(COW_PLAY_DISABLE_VIDEO_STR, COW_PLAY_DISABLE_VIDEO_ENV, "1")) {
        videoMime = NULL;
    }
    if ((audioMime == NULL) &&
        (videoMime == NULL)) {
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

    mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);


    while (videoMime) {
        // setup video components
        videoDecoder = createComponentHelper(NULL, videoMime);
        if (mm_check_env_str("mm.player.gl.filter","MM_PLAYER_GL_FILTER", "1", false)) {
            INFO("test OpenGL video filter in cowplayer");
            videoFilter = createComponentHelper(NULL, "video/filter-gl");
        }
        videoSink = createComponentHelper(NULL, MEDIA_MIMETYPE_VIDEO_RENDER);
        //ASSERT_RET(videoDecoder && videoSink, MM_ERROR_NO_COMPONENT);
        if (!videoDecoder) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video decoder\n");
            notify(Component::kEventError, MM_ERROR_NO_VIDEODECODER, 0, nilParam);
            break;
        }

        if (!videoSink) {
            status = MM_ERROR_NO_COMPONENT;
            ERROR("fail to create video sink\n");
            break;
        }

        {
             // component parameters should be set before linking, to make sure these parameters impact peer components
            MMAutoLock locker(mLock);
            //protect mSurface
            if (mSurface) {
                if (mIsSurfaceTexture)
                    mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, mSurface);
                else
                    mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mSurface);
                videoSink->setParameter(mMediaMeta);
            }
            videoDecoder->setParameter(mMediaMeta);
        }

        status = videoDecoder->addSource(videoSource, Component::kMediaTypeVideo);
        ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (videoFilter) {
            status = videoDecoder->addSink(videoFilter.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            status = videoFilter->addSink(videoSink.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        } else {
            status = videoDecoder->addSink(videoSink.get(), Component::kMediaTypeVideo);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }

        {
            mClock = videoSink->provideClock();
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(videoDecoder, ComponentInfo::kComponentTypeFilter));
            mVideoCodecIndex = mComponents.size() - 1;
            if (videoFilter)
                mComponents.push_back(ComponentInfo(videoFilter, ComponentInfo::kComponentTypeFilter));
            mComponents.push_back(ComponentInfo(videoSink, ComponentInfo::kComponentTypeSink));
            mVideoSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }


        //Media which only contains audio stream doesn't support variable rate
        mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, true);
        mHasVideo = true;
#ifdef __PLATFORM_TV__
        mExternalSubtitleEnabled = true;
#endif
        if (mExternalSubtitleEnabled || source->getSelectedTrack(Component::kMediaTypeSubtitle) > 0) {
            subtitleSink = createComponentHelper(SUBTITLE_SINK, MEDIA_MIMETYPE_SUBTITLE_SINK);
            if (!subtitleSink) {
                ERROR("fail to create subtitle sink\n");
                break;
            }
            if (mSubtitleUri.empty() && source->getSelectedTrack(Component::kMediaTypeSubtitle) > 0) {
                status = subtitleSink->addSource(source, Component::kMediaTypeSubtitle);
            } else {
                subtitleSource = createComponentHelper(SUBTITLE_SOURCE, MEDIA_MIMETYPE_SUBTITLE_SOURCE);
                if (!subtitleSource) {
                    ERROR("fail to create subtitle sink\n");
                    break;
                }
                PlaySourceComponent *subtitleSrc = DYNAMIC_CAST<PlaySourceComponent*>(subtitleSource.get());
                subtitleSrc->setUri(mSubtitleUri.c_str());
                status = subtitleSink->addSource(subtitleSrc, Component::kMediaTypeSubtitle);
                {
                    MMAutoLock locker(mLock);
                    mComponents.push_back(ComponentInfo(subtitleSource, ComponentInfo::kComponentTypeSource));
                    mSubtitleSourceIndex = mComponents.size() - 1;
                }
            }
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
            {
                MMAutoLock locker(mLock);
                mComponents.push_back(ComponentInfo(subtitleSink, ComponentInfo::kComponentTypeSink));
                mSubtitleSinkIndex = mComponents.size() - 1;
            }
            mHasSubTitle = true;
        }
        break;
    }

    while (audioMime) {
        // setup audio components
        ComponentSP audioDecoder = createComponentHelper(NULL, audioMime);
        ComponentSP audioSink = createComponentHelper(NULL, MEDIA_MIMETYPE_AUDIO_RENDER);
        // ASSERT_RET(audioDecoder && audioSink, MM_ERROR_NO_COMPONENT);
        if (!audioDecoder) {
            ERROR("fail to create audio decoder\n");
            notify(Component::kEventError, MM_ERROR_NO_AUDIODECODER, 0, nilParam);
            break;
        }

        if (!audioSink) {
            ERROR("fail to create audio sink\n");
            break;
        }

        status = audioDecoder->addSource(audioSource, Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio decoder\n");
            break;
        }

        status = audioDecoder->addSink(audioSink.get(), Component::kMediaTypeAudio);
        // ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        if (status != MM_ERROR_SUCCESS) {
            ERROR("connect failed from demuxer to audio decoder\n");
            break;
        }

        {
            MMAutoLock locker(mLock);
            mComponents.push_back(ComponentInfo(audioDecoder, ComponentInfo::kComponentTypeFilter));
            mAudioCodecIndex = mComponents.size() - 1;
            mComponents.push_back(ComponentInfo(audioSink, ComponentInfo::kComponentTypeSink));
            mAudioSinkIndex = mSinkClockIndex = mComponents.size() - 1;
        }
        if (videoSink) {
            mClock = audioSink->provideClock();
            videoSink->setClock(mClock);
        }
        PlaySinkComponent *sink = DYNAMIC_CAST<PlaySinkComponent*>(audioSink.get());
        if(sink){
#ifdef __MM_YUNOS_LINUX_BSP_BUILD__
            sink->setAudioConnectionId(mAudioConnectionId.c_str());
#else
            sink->setAudioStreamType(mAudioStreamType);
#endif
        }

        else{
            ERROR("DYNAMIC_CAST fail\n");
        }

        mHasAudio = true;
        break; //always break here
    }
    if (subtitleSink)
        subtitleSink->setClock(mClock);
    if (subtitleSource)
        subtitleSource->setClock(mClock);

    if (MM_ERROR_SUCCESS != ret)
        status = ret;
    return status;
}

const char* PipelinePlayer::getMimeFromMediaRepresentationInfo(Component::MediaType type, MMParamSP param) {
    if (!param)
        return NULL;

    int dynamic = param->readInt32();

    if (dynamic) {
        param->readInt32();
        param->readInt32();
    }

    param->readInt32(); // period count
    param->readInt32(); // period start
    param->readInt32(); // period duration
    // check mime type only from peroid 0

    int streamCount = param->readInt32();
    bool found = false;
    INFO("zengli find %d stream", streamCount);

    for (int i = 0; i < streamCount; i++) {
        int t = param->readInt32();

        INFO("need media type %d, get type %d", type, t);
        if (t == type)
            found = true;

        int adaptationSet = param->readInt32();
        for (int j = 0; j < adaptationSet; j++) {
            param->readCString();
            param->readInt32();
            int representation = param->readInt32();
            for (int k = 0; k < representation; k++) {
                if (found)
                    return param->readCString();
                else
                    param->readCString();
                param->readInt32();
                int codec = param->readInt32();
                for (int l = 0; l < codec; l++)
                    param->readCString();
            }
        }
    }
    return NULL;
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Pipeline* createPipeline()
{
    YUNOS_MM::PipelinePlayer *pipelinePlayer = new YUNOS_MM::PipelinePlayer();
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

