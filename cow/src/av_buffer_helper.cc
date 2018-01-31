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

#include <stdlib.h>
#include <multimedia/av_ffmpeg_helper.h>
#include "multimedia/av_buffer_helper.h"
#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"

MM_LOG_DEFINE_MODULE_NAME("Cow-AVBufferHelper");
// #define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__)
#define FUNC_TRACK()

namespace YUNOS_MM {
const char* AVPacketMetaName = "AVPacketPointer";
const char* AVFrameMetaName = "AVFramePointer";

static bool releaseMediaBufferFromAVPacket(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    AVPacket *avpkt = NULL;
    MediaMetaSP meta = mediaBuffer->getMediaMeta();

    if (meta && mediaBuffer->isFlagSet(MediaBuffer::MBFT_AVPacket)) {
        void *ptr = NULL;
        meta->getPointer(AVPacketMetaName, ptr);
        avpkt = (AVPacket*)ptr;
    }

    if (avpkt) {
        av_free_packet(avpkt);
        // be sure the AVPacket is from malloc()
        free(avpkt);
    } else
        WARNING("seems AVPacket leak");

    return true;
}

MediaBufferSP AVBufferHelper::createMediaBuffer(AVPacket *pkt, bool releasePkt)
{
    FUNC_TRACK();
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    MediaMetaSP meta = buffer->getMediaMeta();

    if (!buffer)
        return buffer;

    buffer->setFlag(MediaBuffer::MBFT_AVPacket);
    if (meta)
        meta->setPointer(AVPacketMetaName, pkt);
    if (pkt->flags & AV_PKT_FLAG_KEY)
        buffer->setFlag(MediaBuffer::MBFT_KeyFrame);

    // map AVPacket fields to MediaBuffer
    buffer->setBufferInfo((uintptr_t*)&pkt->data, NULL, NULL, 1);
    buffer->setSize(pkt->size);
    buffer->setDts(pkt->dts);
    buffer->setPts(pkt->pts);
    buffer->setDuration(pkt->duration);
    meta->setInt32(MEDIA_ATTR_STREAM_INDEX, pkt->stream_index);

    if (releasePkt)
        buffer->addReleaseBufferFunc(releaseMediaBufferFromAVPacket);

    return buffer;
}

static bool releaseMediaBufferFromAVFrame(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    AVFrame *avfrm = NULL;
    MediaMetaSP meta = mediaBuffer->getMediaMeta();

    if (meta && mediaBuffer->isFlagSet(MediaBuffer::MBFT_AVFrame)) {
        void *ptr = NULL;
        meta->getPointer(AVFrameMetaName, ptr);
        avfrm = (AVFrame*)ptr;
    }

    if (avfrm)
        av_frame_free(&avfrm);
    else
        WARNING("seems AVFrame leak");

    return true;
}

MediaBufferSP AVBufferHelper::createMediaBuffer(AVFrame *frame, bool isAudio, bool releaseFrame)
{
    FUNC_TRACK();
    MediaBufferSP buffer;
    if (isAudio)
        buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
    else
        buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);

    MediaMetaSP meta = buffer->getMediaMeta();
    MMASSERT(meta);
    if (meta)
        meta->setPointer(AVFrameMetaName, frame);
    buffer->setFlag(MediaBuffer::MBFT_AVFrame);

    // map AVFrame fields to MediaBuffer
     buffer->setBufferInfo((uintptr_t*)frame->data, NULL, (int32_t*)frame->linesize
        , MediaBufferMaxDataPlane < AV_NUM_DATA_POINTERS ? MediaBufferMaxDataPlane : AV_NUM_DATA_POINTERS);

    if (frame->key_frame)
        buffer->setFlag(MediaBuffer::MBFT_KeyFrame);
    buffer->setPts(frame->pkt_pts);
    // buffer->setDuration(frame->pkt_duration);

    if (isAudio) {
        meta->setInt32(MEDIA_ATTR_AUDIO_SAMPLES, frame->nb_samples);
        #if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53, 25, 0)
        meta->setInt32(MEDIA_ATTR_SAMPLE_RATE, frame->sample_rate);
        meta->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, (int64_t)frame->channel_layout);
        #endif
    } else {
        meta->setInt32(MEDIA_ATTR_WIDTH, frame->width);
        meta->setInt32(MEDIA_ATTR_HEIGHT, frame->height);
        meta->setInt32(MEDIA_ATTR_COLOR_FORMAT, frame->format);
        meta->setFraction(MEDIA_ATTR_SAMPLE_ASPECT_RATION, frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
   }

    if (releaseFrame)
        buffer->addReleaseBufferFunc(releaseMediaBufferFromAVFrame);

    // interlaced_frame, top_field_first
    // FIXME AVFrame->extended_data for audio
    return buffer;
}

bool AVBufferHelper::convertToAVPacket(MediaBufferSP mediaBuffer, AVPacket **pkt)
{
    FUNC_TRACK();
    AVPacket *avpkt = NULL;
    MediaMetaSP meta = mediaBuffer->getMediaMeta();

    *pkt = NULL;
    if (meta && mediaBuffer->isFlagSet(MediaBuffer::MBFT_AVPacket)) {
        void *ptr = NULL;
        meta->getPointer(AVPacketMetaName, ptr);
        if (ptr) {
            *pkt = (AVPacket*)ptr;
            return true;
        } else
            return false;
    }

    // map the data
    avpkt = (AVPacket *)malloc(sizeof(AVPacket));
    if (!avpkt) {
        ERROR("fail to malloc AVPacket\n");
        return false;
    }
    av_init_packet(avpkt);

    if (!mediaBuffer->getBufferInfo((uintptr_t*)&avpkt->data, NULL, NULL, 1)) {
        av_free_packet(avpkt);
        free(avpkt);
        return false;
    }
    avpkt->size = mediaBuffer->size();
    avpkt->dts = mediaBuffer->dts();
    avpkt->pts = mediaBuffer->pts();
    avpkt->duration = mediaBuffer->duration();
    meta->getInt32(MEDIA_ATTR_STREAM_INDEX, avpkt->stream_index);
    meta->setPointer(AVPacketMetaName, avpkt);
    *pkt = avpkt;

    mediaBuffer->addReleaseBufferFunc(releaseMediaBufferFromAVPacket);
    return true;
}

//NOTE: Do not free buffer in this method.
void AVBufferHelper::AVBufferDefaultFree(void *opaque, uint8_t *data)
{
    //av_free(data);
}


bool AVBufferHelper::convertToAVFrame(MediaBufferSP mediaBuffer, AVFrame **frame)
{
    FUNC_TRACK();
    AVFrame *avfrm = NULL;
    MediaMetaSP meta = mediaBuffer->getMediaMeta();

    *frame = NULL;
    if (meta && mediaBuffer->isFlagSet(MediaBuffer::MBFT_AVFrame)) {
        void *ptr = NULL;
        meta->getPointer(AVFrameMetaName, ptr);
        if (ptr) {
            *frame = (AVFrame*)ptr;
            return true;
        } else
            return false;
    }

    // map the data
    avfrm = av_frame_alloc();
    if (!avfrm) {
        ERROR("fail to alloc AVFrame\n");
        return false;
    }

    if (!mediaBuffer->getBufferInfo((uintptr_t*)avfrm->data, NULL, (int32_t*)avfrm->linesize
        , MediaBufferMaxDataPlane < AV_NUM_DATA_POINTERS ? MediaBufferMaxDataPlane : AV_NUM_DATA_POINTERS)) {
        ERROR("convert to avframe failed\n");
        av_frame_free(&avfrm);
        return false;
    }

    //assume sample fmt is int16. To get more info to refer get_audio_buffer
    //planar = 0, planes = 1 for bit16 format
    avfrm->format = AV_SAMPLE_FMT_S16;
    avfrm->extended_data = avfrm->data;
    avfrm->buf[0] = av_buffer_create(avfrm->data[0], avfrm->linesize[0], AVBufferDefaultFree, NULL, 0);
    if (!avfrm->buf[0]) {
        ERROR("av_buffer_create failed, no mem\n");
        av_frame_free(&avfrm);
        return false;
    }

    //assume planes = 0
    avfrm->extended_data[0] = avfrm->data[0] = avfrm->buf[0]->data;

    avfrm->key_frame = mediaBuffer->isFlagSet(MediaBuffer::MBFT_KeyFrame);
    avfrm->pts = mediaBuffer->pts();
    if (meta) {
        // interlaced_frame, top_field_first
        meta->getInt32(MEDIA_ATTR_WIDTH, avfrm->width);
        meta->getInt32(MEDIA_ATTR_HEIGHT, avfrm->height);

        int32_t channel_count = 0;
        meta->getInt32(MEDIA_ATTR_CHANNEL_COUNT, channel_count);


        if (!meta->getInt32(MEDIA_ATTR_AUDIO_SAMPLES, avfrm->nb_samples) && channel_count > 0) {
            avfrm->nb_samples = avfrm->linesize[0]/channel_count/2;
        }

        if (meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, avfrm->format)) {
            if (avfrm->format != AV_SAMPLE_FMT_S16) {
                ERROR("unsupport fmt %d\n", avfrm->format);
                av_buffer_unref( &avfrm->buf[0]);
                av_frame_free(&avfrm);
                return false;
            }
        }
        meta->getFraction(MEDIA_ATTR_SAMPLE_ASPECT_RATION, avfrm->sample_aspect_ratio.num, avfrm->sample_aspect_ratio.den);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53, 25, 0)
        meta->getInt32(MEDIA_ATTR_SAMPLE_RATE, avfrm->sample_rate);


        int64_t temp = 0;
        if (!meta->getInt64(MEDIA_ATTR_CHANNEL_LAYOUT, temp) && channel_count > 0) {
            temp = (int64_t)av_get_default_channel_layout(channel_count);
            avfrm->channel_layout = (uint64_t)temp;
        }
#endif
    }

    *frame = avfrm;
    meta->setPointer(AVFrameMetaName, avfrm);
    mediaBuffer->setFlag(MediaBuffer::MBFT_AVFrame);
    mediaBuffer->addReleaseBufferFunc(releaseMediaBufferFromAVFrame);
    return true;
}

} // end of namespace YUNOS_MM

