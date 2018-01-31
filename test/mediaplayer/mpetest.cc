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
#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

extern "C" {
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <inttypes.h>
#include <libavformat/avformat.h>
}

#include "multimedia/mediaplayer.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "mmwakelocker.h"
#include "multimedia/audio.h"
#include "multimedia/av_buffer_helper.h"

#include "native_surface_help.h"

MM_LOG_DEFINE_MODULE_NAME("MPETEST")


static const int WAIT_FOR_COMPLETE = 1;

static const char *g_video_file_path = "/usr/bin/ut/res/video/test.mp4";

using namespace YUNOS_MM;

MediaPlayer * s_g_player = NULL;

static int s_g_prepared = 0;
static int s_g_play_completed = 0;
static int s_g_error_occured = 0;
static int s_g_report_video_size =0;


static AVFormatContext * mAVFormatContext = NULL;
static AVInputFormat * mAVInputFormat = NULL;
static MediaMetaSP s_g_MetaData = MediaMeta::create();;
static MediaMetaSP s_g_audioMetaData = MediaMeta::create();;
static MediaMetaSP s_g_videoMetaData = MediaMeta::create();;
static int s_g_audioStreamId = -1;
static int s_g_videoStreamId = -1;
static const AVRational gTimeBase = {1,1000000};
AVRational s_g_audioTimeBase;
AVRational s_g_videoTimeBase;
static std::string s_g_audioMime;
static std::string s_g_videoMime;




// fixed point to double
#define CONV_FP(x) ((double) (x)) / (1 << 16)

// double to fixed point
#define CONV_DB(x) (int32_t) ((x) * (1 << 16))

static double av_display_rotation_get(const int32_t matrix[9])
{
    double rotation, scale[2];

    scale[0] = hypot(CONV_FP(matrix[0]), CONV_FP(matrix[3]));
    scale[1] = hypot(CONV_FP(matrix[1]), CONV_FP(matrix[4]));

    if (scale[0] == 0.0 || scale[1] == 0.0)
        return NAN;

    rotation = atan2(CONV_FP(matrix[1]) / scale[1],
                     CONV_FP(matrix[0]) / scale[0]) * 180 / M_PI;

    return rotation;
}

/*static */snd_format_t convertAudioFormat(AVSampleFormat avFormat)
{
#undef item
#define item(_av, _audio) \
    case _av:\
        MMLOGI("%s -> %s\n", #_av, #_audio);\
        return _audio

    switch ( avFormat ) {
        item(AV_SAMPLE_FMT_U8, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLT, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBL, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_U8P, SND_FORMAT_PCM_8_BIT);
        item(AV_SAMPLE_FMT_S16P, SND_FORMAT_PCM_16_BIT);
        item(AV_SAMPLE_FMT_S32P, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_FLTP, SND_FORMAT_PCM_32_BIT);
        item(AV_SAMPLE_FMT_DBLP, SND_FORMAT_PCM_32_BIT);
        default:
            MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", avFormat);
            return SND_FORMAT_INVALID;
    }
}

static mm_status_t selectTrack(bool isVideo, int streamId)
{
    AVStream * stream = mAVFormatContext->streams[streamId];
    if (isVideo)
        memcpy(&s_g_videoTimeBase, &stream->time_base, sizeof(AVRational));
    else
        memcpy(&s_g_audioTimeBase, &stream->time_base, sizeof(AVRational));

    AVCodecContext * codecContext = mAVFormatContext->streams[streamId]->codec;

    MediaMetaSP theMeta;
    if (isVideo) {
        theMeta = s_g_videoMetaData;
        memcpy(&s_g_videoTimeBase, &mAVFormatContext->streams[streamId]->time_base, sizeof(s_g_videoTimeBase));

        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            theMeta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        AVRational * avr = &mAVFormatContext->streams[streamId]->avg_frame_rate;
        if ( avr->num > 0 && avr->den > 0 ) {
            MMLOGD("has avg fps: %d\n", avr->num / avr->den);
            theMeta->setInt32(MEDIA_ATTR_AVG_FRAMERATE, avr->num / avr->den);
        }
        theMeta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);
        theMeta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        theMeta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        theMeta->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        theMeta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        theMeta->setInt32(MEDIA_ATTR_WIDTH, codecContext->width);
        theMeta->setInt32(MEDIA_ATTR_HEIGHT, codecContext->height);

        if (stream->nb_side_data) {
            for (int i = 0; i < stream->nb_side_data; i++) {
                AVPacketSideData sd = stream->side_data[i];
                if (sd.type == AV_PKT_DATA_DISPLAYMATRIX) {
                    double degree = av_display_rotation_get((int32_t *)sd.data);
                    MMLOGD("degree %0.2f\n", degree);

                    int32_t degreeInt = (int32_t)degree;
                    if (degreeInt < 0)
                        degreeInt += 360;
                    theMeta->setInt32(MEDIA_ATTR_ROTATION, (int32_t)degreeInt);
                }
                else {
                    MMLOGD("ignore type %d\n", sd.type);
                    continue;
                }
            }
        }

    } else {
        theMeta = s_g_audioMetaData;
        s_g_MetaData->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            theMeta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        theMeta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);
        theMeta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        theMeta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        theMeta->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        theMeta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        theMeta->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, convertAudioFormat(codecContext->sample_fmt));
        theMeta->setInt32(MEDIA_ATTR_SAMPLE_RATE, codecContext->sample_rate);
        theMeta->setInt32(MEDIA_ATTR_CHANNEL_COUNT, codecContext->channels);
        theMeta->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, codecContext->channel_layout);
        theMeta->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        theMeta->setInt32(MEDIA_ATTR_BLOCK_ALIGN, codecContext->block_align);

        if (mAVInputFormat == av_find_input_format("aac") ||
           mAVInputFormat == av_find_input_format("mpegts") ||
           mAVInputFormat == av_find_input_format("hls,applehttp")) {
           theMeta->setInt32(MEDIA_ATTR_IS_ADTS, 1);
        }
    }

    theMeta->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);

    theMeta->setPointer(MEDIA_ATTR_CODEC_CONTEXT, codecContext);
    //s_g_audioMetaData->setPointer(MEDIA_ATTR_CODEC_CONTEXT_MUTEX, &mAVLock);
    theMeta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
    theMeta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
    theMeta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
    theMeta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);

    if (isVideo) {
        s_g_videoMime = codecId2Mime((CowCodecID)codecContext->codec_id);
        MMLOGI("video mime: %s\n", s_g_videoMime.c_str());
    } else {
        s_g_audioMime = codecId2Mime((CowCodecID)codecContext->codec_id);
        MMLOGI("audio mime: %s\n", s_g_audioMime.c_str());
    }

    return MM_ERROR_SUCCESS;
}

static mm_status_t createContext()
{
    mAVFormatContext = avformat_alloc_context();
    if ( !mAVFormatContext ) {
        MMLOGE("failed to create avcontext\n");
        return MM_ERROR_INVALID_PARAM;
    }

    AVDictionary *options = NULL;
    int ret = avformat_open_input(&mAVFormatContext, g_video_file_path, NULL, &options);
    if (options) {
        av_dict_free(&options);
    }
    if ( ret < 0 ) {
        MMLOGE("failed to open input: %d\n", ret);
        return MM_ERROR_INVALID_PARAM;
    }


    mAVFormatContext->flags |= AVFMT_FLAG_GENPTS;
    MMLOGV("finding stream info\n");
    ret = avformat_find_stream_info(mAVFormatContext, NULL);
    if ( ret < 0 ) {
        MMLOGE("failed to find stream info: %d\n", ret);
        return ret;
    }

    mAVInputFormat = mAVFormatContext->iformat;
    MMLOGV("name: %s, long_name: %s, flags: %d, mime: %s\n",
        mAVInputFormat->name, mAVInputFormat->long_name, mAVInputFormat->flags, mAVInputFormat->mime_type);

    for (unsigned int i = 0; i < mAVFormatContext->nb_streams; ++i) {
        AVStream * s = mAVFormatContext->streams[i];
        AVMediaType type = s->codec->codec_type;
        MMLOGV("steramid: %d, codec_type: %d, start_time: %" PRId64 ", duration: %" PRId64 ", nb_frames: %" PRId64 "\n",
            i,
            s->codec->codec_type,
            s->start_time == (int64_t)AV_NOPTS_VALUE ? -1 : s->start_time,
            s->duration,
            s->nb_frames);
        switch ( type ) {
            case AVMEDIA_TYPE_VIDEO:
                if (s_g_videoStreamId < 0) {
                    s_g_videoStreamId = i;
                    selectTrack(true, i);
                }
                MMLOGI("found video stream(%d)\n", i);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (s_g_audioStreamId < 0) {
                    s_g_audioStreamId = i;
                    selectTrack(false, i);
                }
                MMLOGI("found audio stream(%d)\n", i);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                break;
            default:
                MMLOGI("not supported mediatype: %d\n", type);
                break;
        }

    }


    return MM_ERROR_SUCCESS;
}


static void releaseContext()
{
    if ( mAVFormatContext ) {
        avformat_close_input(&mAVFormatContext);
        mAVFormatContext = NULL;
    }
}

static MediaBufferSP createBuf(AVPacket * packet, bool audio)
{
    MediaBufferSP buf = AVBufferHelper::createMediaBuffer(packet, true);
    if ( !buf ) {
        MMLOGE("failed to createMediaBuffer\n");
        return MediaBufferSP((MediaBuffer*)NULL);;
    }

    MediaMetaSP meta = buf->getMediaMeta();
    meta->setString(MEDIA_ATTR_MIME, audio ? s_g_audioMime.c_str() : s_g_videoMime.c_str());

    return buf;
}

#define FREE_AVPACKET(_pkt) do {\
    if ( _pkt ) {\
        av_free_packet(_pkt);\
        free(_pkt);\
        (_pkt) = NULL;\
    }\
}while(0)

static mm_status_t writeEos(bool audio)
{
    MMLOGI("+\n");
    AVPacket * packet = (AVPacket*)malloc(sizeof(AVPacket));
    if (!packet) {
        MMLOGE("no mem\n");
        return MM_ERROR_NO_MEM;
    }
    av_init_packet(packet);
    MediaBufferSP buf = createBuf(packet, audio);
    if ( !buf ) {
        MMLOGE("failed to createMediaBuffer\n");
        FREE_AVPACKET(packet);
        return MM_ERROR_NO_MEM;
    }
    MMLOGI("write eos to media: %d\n", audio);
    buf->setFlag(MediaBuffer::MBFT_EOS);
    buf->setSize(0);
    s_g_player->pushData(buf);
    return MM_ERROR_SUCCESS;
}

static mm_status_t readFrame()
{
    MMLOGV("+\n");


    AVPacket * packet = NULL;
    do {
        packet = (AVPacket*)malloc(sizeof(AVPacket));
        if ( !packet ) {
            MMLOGE("no mem\n");
            return MM_ERROR_NO_MEM;
        }
        av_init_packet(packet);

        int ret = av_read_frame(mAVFormatContext, packet); //0: ok, <0: error/end
        if ( ret < 0 ) {
            free(packet);
            packet = NULL;

            char errorBuf[256] = {0};
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            MMLOGW("read_frame failed: %d (%s)\n", ret, errorBuf);
            if ( ret == AVERROR_EOF ) {
                MMLOGI("eof\n");
                writeEos(true);
                writeEos(false);

                return MM_ERROR_NO_MORE;
            }

            return MM_ERROR_IO;
        }

        bool isAudio = packet->stream_index == s_g_audioStreamId;
        bool isVideo = packet->stream_index == s_g_videoStreamId;

        if (!isAudio && !isVideo) {
            MMLOGV("stream index %d ignore\n", packet->stream_index);
            FREE_AVPACKET(packet);
            usleep(0);
            continue;
        }

        //If packet timestamp is -1, no need to checkHighWater
        if (packet->pts != (int64_t)AV_NOPTS_VALUE){
            av_packet_rescale_ts(packet, isAudio ? s_g_audioTimeBase : s_g_videoTimeBase, gTimeBase);
        }


        MediaBufferSP buf = createBuf(packet, isAudio);
        if ( !buf ) {
            MMLOGE("failed to createMediaBuffer\n");
            FREE_AVPACKET(packet);
            return MM_ERROR_NO_MEM;
        }

        s_g_player->pushData(buf);

    } while(0);

    return MM_ERROR_SUCCESS;
}



class MyListener : public MediaPlayer::Listener {
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj)
    {
        MMLOGD("msg: %d, param1: %d, param2: %d, obj: %p", msg, param1, param2, obj);
        switch ( msg ) {
            case MSG_PREPARED:
                s_g_prepared = 1;
                break;
            case MSG_PLAYBACK_COMPLETE:
                s_g_play_completed = 1;
                break;
            case MSG_ERROR:
                s_g_error_occured = 1;
                break;
            case MSG_SET_VIDEO_SIZE:
                s_g_report_video_size = 1;
                break;
            default:
                break;
        }
    }
};

class MediaplayerTest : public testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

// wait until prepare is done
void waitUntilPrepareDone()
{
    int sleepCount = 0;
    while(!s_g_prepared && sleepCount++<100)
        usleep(50000);
}


uint32_t g_surface_width = 1280;
uint32_t g_surface_height = 720;

TEST_F(MediaplayerTest, mediaplayerplayvideo) {
    mm_status_t status;
    int ret = 0;
    MediaPlayer::VolumeInfo vol;
    std::map<std::string,std::string> header;

    MMLOGI("Hello mediaplayer video\n");
    AutoWakeLock awl;

    s_g_prepared = 0;
    s_g_play_completed = 0;
    s_g_error_occured = 0;
    s_g_report_video_size = 0;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    WindowSurface *ws = createSimpleSurface(800, 600);
    if (!ws) {
        MMLOGE("init surface fail\n");
        exit(-1);
    }
#endif

    if (createContext() != MM_ERROR_SUCCESS) {
        MMLOGE("failed to create context\n");
        exit(-1);
    }

    MyListener * listener = new MyListener();

    s_g_player = MediaPlayer::create(MediaPlayer::PlayerType_COW);
    if ( !s_g_player ) {
        MMLOGE("no mem\n");
        ret  = -1;
        goto error;
    }

#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    s_g_player->setVideoDisplay(ws);
#endif
    s_g_player->setListener(listener);

    MMLOGI("settting datasource\n");

    status = s_g_player->setDataSource("user://example");
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setdatasource failed: %d\n", status);
        ret = -1;
        goto error;
    }

    s_g_player->setParameter(s_g_MetaData);
    s_g_player->setParameter(s_g_audioMetaData);
    s_g_player->setParameter(s_g_videoMetaData);

    MMLOGI("prepare\n");
    status = s_g_player->prepareAsync();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("prepare failed: %d\n", status);
        ret = -1;
        goto error;
    }
    // wait until prepare is done
    waitUntilPrepareDone();
    if(!s_g_prepared){
        MMLOGE("prepareAsync failed after wait 5 seconds\n");
        ret = -1;
        goto error;
    }

    MMLOGI("setVolume\n");
    vol.left = vol.right = 1.0;
    status = s_g_player->setVolume(vol);
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("setVolume failed: %d\n", status);
    }

    MMLOGI("start\n");
    status = s_g_player->start();
    if(status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC){
        MMLOGE("start failed: %d\n", status);
        ret = -1;
        goto error;
    }

    MMLOGI("call start over\n");

    MMLOGI("waitting for complete\n");
    while ( readFrame() == MM_ERROR_SUCCESS ) {
        usleep(1000);
    }

    while (!s_g_play_completed && !s_g_error_occured) {
        MMLOGV("waitting play over\n");
        usleep(50000);
    }

    MMLOGI("stopping\n");
    s_g_player->stop();

    MMLOGI("resetting\n");
    s_g_player->reset();

error:
    MMLOGI("destroying\n");
    MediaPlayer::destroy(s_g_player);
    delete listener;
#ifndef __MM_YUNOS_LINUX_BSP_BUILD__
    destroySimpleSurface(ws);
#endif
    EXPECT_EQ(ret, 0);
    releaseContext();
    MMLOGI("exit\n");
}


int main(int argc, char* const argv[]) {
    class AVInitializer {
    public:
        AVInitializer() {
            av_register_all();
            avformat_network_init();
        }
        ~AVInitializer() {
            avformat_network_deinit();
        }
    };
    static AVInitializer sAVInit;

    int ret;
    try {
        ::testing::InitGoogleTest(&argc, (char **)argv);
        ret = RUN_ALL_TESTS();
    } catch (...) {
        ERROR("InitGoogleTest failed!");
        return -1;
    }
    return ret;
}
