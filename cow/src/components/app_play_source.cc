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
#include <math.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_play_source.h"
#include <unistd.h>
#include <multimedia/media_attr_str.h>
#include <multimedia/mm_types.h>

#ifndef MM_LOG_OUTPUT_V
#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>

namespace YUNOS_MM {

DEFINE_LOGTAG(APPPlaySource)
DEFINE_LOGTAG(APPPlaySource::APPPlaySourceWriter)
DEFINE_LOGTAG(APPPlaySource::APPPlaySourceReader)
DEFINE_LOGTAG(APPPlaySource::Stream)

const uint32_t kMaxCacheBufferCount = 10;

APPPlaySource::Stream::Stream() : mDropBuffer(true), mInputBufferCount(0), mOutputBufferCount(0), mDropBufferCount(0)
{
    MMLOGD("+\n");
    mDropBuffer = mm_check_env_str("mm.webrtc.input.drop", "MM_WEBRTC_INPUT_DROP", "1", mDropBuffer);
}

APPPlaySource::Stream::~Stream()
{
    MMLOGD("+\n");
    flush();
    MMLOGD("-\n");
}

void APPPlaySource::Stream::flush()
{
    MMLOGD("+\n");
    std::queue<MediaBufferSP> x;
    std::swap(mBuffers, x);
    MMLOGD("-\n");
}

APPPlaySource::APPPlaySourceReader::APPPlaySourceReader(APPPlaySource * component, MediaType mediaType)
                    : mComponent(component),
                    mMediaType(mediaType)
{
    MMLOGI("%d\n", mediaType);
}

APPPlaySource::APPPlaySourceReader::~APPPlaySourceReader()
{
    MMLOGI("%d\n", mMediaType);
}

mm_status_t APPPlaySource::APPPlaySourceReader::read(MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    return mComponent->read(buffer, mMediaType);
}

MediaMetaSP APPPlaySource::APPPlaySourceReader::getMetaData()
{
    MMLOGI("+\n");
    return mComponent->getMeta(mMediaType);
}

APPPlaySource::APPPlaySourceWriter::APPPlaySourceWriter(APPPlaySource * component) : mComponent(component)
{
    MMLOGI("+\n");
}

APPPlaySource::APPPlaySourceWriter::~APPPlaySourceWriter()
{
    MMLOGI("+\n");
}

mm_status_t APPPlaySource::APPPlaySourceWriter::write(const MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    return mComponent->write(buffer);
}

mm_status_t APPPlaySource::APPPlaySourceWriter::setMetaData(const MediaMetaSP & metaData)
{
    MMLOGI("+\n");
    return mComponent->setMeta(metaData);
}

APPPlaySource::APPPlaySource()
{
    MMLOGI("+\n");
}

APPPlaySource::~APPPlaySource()
{
    MMLOGI("+\n");
    reset();
}

const std::list<std::string> & APPPlaySource::supportedProtocols() const
{
    static std::list<std::string> protocols;
    return protocols;
}

Component::WriterSP APPPlaySource::getWriter(MediaType mediaType)
{
    MMLOGV("mediaType: %d\n", mediaType);
    MMAutoLock lock(mLock);
    return WriterSP(new APPPlaySourceWriter(this));
}

Component::ReaderSP APPPlaySource::getReader(MediaType mediaType)
{
    MMLOGI("mediaType: %d\n", mediaType);
    MMAutoLock lock(mLock);

    return ReaderSP(new APPPlaySourceReader(this, mediaType));
}

mm_status_t APPPlaySource::setMeta(const MediaMetaSP & metaData)
{
    MediaType mediatype = getMediaTypeByMeta(metaData);
    MMLOGI("%d\n", mediatype);
    Stream * stream = getStreamByMediaType(mediatype);
    if (!stream) {
        MMLOGE("invalid media type\n");
        return MM_ERROR_INVALID_PARAM;
    }

    MMAutoLock lock(mLock);
    stream->mMeta = metaData;
    return MM_ERROR_SUCCESS;
}

MediaMetaSP APPPlaySource::getMeta(MediaType mediatype)
{
    MMLOGI("%d\n", mediatype);
    Stream * stream = getStreamByMediaType(mediatype);
    if (!stream) {
        MMLOGE("invalid media type\n");
        return MediaMetaSP((MediaMeta*)NULL);
    }

    MMAutoLock lock(mLock);
    return MediaMetaSP(stream->mMeta);
}

mm_status_t APPPlaySource::reset()
{
    MMLOGI("+\n");
    flush();
    return MM_ERROR_SUCCESS;
}

mm_status_t APPPlaySource::flush()
{
    MMLOGI("+\n");
    MMAutoLock lock(mLock);
    for (int i = 0; i < kMediaTypeCount; ++i) {
        mStreams[i].flush();
    }
    MMLOGI("-\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t APPPlaySource::pushData(MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    return write(buffer);
}

mm_status_t APPPlaySource::write(const MediaBufferSP & buffer)
{
    MMLOGV("+\n");
    if (!buffer) {
        MMLOGE("invalid buffer\n");
        return MM_ERROR_INVALID_PARAM;
    }
    MediaMetaSP meta = buffer->getMediaMeta();
    if (!meta) {
        MMLOGE("buffer no meta\n");
        return MM_ERROR_INVALID_PARAM;
    }
    MediaType mediatype = getMediaTypeByMeta(meta);
    Stream * stream = getStreamByMediaType(mediatype);
    if (!stream) {
        MMLOGE("invalid media type\n");
        return MM_ERROR_INVALID_PARAM;
    }

    if (stream->mDropBuffer && stream->mBuffers.size() > kMaxCacheBufferCount) {
        // TODO, drop non-ref frame
        stream->mDropBufferCount++;
        MMLOGD("stream: %d, mInputBufferCount: %d, mOutputBufferCount: %d, mDropBufferCount: %d, cache size: %zu",
            mediatype, stream->mInputBufferCount, stream->mOutputBufferCount, stream->mDropBufferCount, stream->mBuffers.size());
        return MM_ERROR_SUCCESS;
    }
    MMAutoLock lock(mLock);
    stream->mBuffers.push(buffer);
    stream->mInputBufferCount++;
    MMLOGD("stream: %d, mInputBufferCount: %d, mOutputBufferCount: %d, mDropBufferCount: %d, cache size: %zu",
        mediatype, stream->mInputBufferCount, stream->mOutputBufferCount, stream->mDropBufferCount, stream->mBuffers.size());
    return MM_ERROR_SUCCESS;
}

mm_status_t APPPlaySource::read(MediaBufferSP & buffer, MediaType mediatype)
{
    MMLOGV("+\n");
    Stream * stream = getStreamByMediaType(mediatype);
    if (!stream) {
        MMLOGE("invalid media type\n");
        return MM_ERROR_INVALID_PARAM;
    }

    MMAutoLock lock(mLock);
    if (stream->mBuffers.empty()) {
        MMLOGV("empty\n");
        return MM_ERROR_AGAIN;
    }
    stream->mOutputBufferCount++;
    MMLOGD("stream: %d, mInputBufferCount: %d, mOutputBufferCount: %d, mDropBufferCount: %d, cache size: %zu",
        mediatype, stream->mInputBufferCount, stream->mOutputBufferCount, stream->mDropBufferCount, stream->mBuffers.size());
    buffer = stream->mBuffers.front();
    stream->mBuffers.pop();
    MMLOGV("-\n");
    return MM_ERROR_SUCCESS;
}

mm_status_t APPPlaySource::setParameter(const MediaMetaSP & meta)
{
    MMLOGV("+\n");
    MediaType mt = getMediaTypeByMeta(meta);
    switch (mt) {
        case kMediaTypeAudio:
            MMLOGI("got audio meta\n");
            mStreams[kMediaTypeAudio].mMeta = meta;
            return MM_ERROR_SUCCESS;
        case kMediaTypeVideo:
            MMLOGI("got video meta\n");
            mStreams[kMediaTypeVideo].mMeta = meta;
            return MM_ERROR_SUCCESS;
        default:
            MMLOGI("got media meta\n");
            mMeta = meta;
            return MM_ERROR_SUCCESS;
    }
}

APPPlaySource::Stream * APPPlaySource::getStreamByMediaType(MediaType mediaType)
{
    switch (mediaType) {
        case kMediaTypeAudio:
            return &mStreams[kMediaTypeAudio];
        case kMediaTypeVideo:
            return &mStreams[kMediaTypeVideo];
        default:
            return NULL;
    }
}

Component::MediaType APPPlaySource::getMediaTypeByMeta(const MediaMetaSP & meta)
{
    MediaMeta::iterator i;
    for (i = meta->begin(); i != meta->end(); i++) {
        const MediaMeta::MetaItem & item = *i;
        if (strcmp(MEDIA_ATTR_MIME, item.mName)) {
            continue;
        }

        if (item.mType != MediaMeta::MT_String) {
            MMLOGE("invalid type for mime, expect str\n");
            return kMediaTypeUnknown;
        }

        if (!strncmp(item.mValue.str, "audio", 5)) {
            MMLOGV("audio\n");
            return kMediaTypeAudio;
        }

        if (!strncmp(item.mValue.str, "video", 5)) {
            MMLOGV("video\n");
            return kMediaTypeVideo;
        }

        MMLOGE("unsupported mime: %s\n", item.mValue.str);
        return kMediaTypeUnknown;
    }
    return kMediaTypeCount;
}

mm_status_t APPPlaySource::getParameter(MediaMetaSP & meta)
{
    MMLOGV("+\n");
    return MM_ERROR_UNSUPPORTED;
}


extern "C" {

using namespace YUNOS_MM;

MM_LOG_DEFINE_MODULE_NAME("APPPlaySourceCreater");

Component * createComponent(const char* mimeType, bool isEncoder)
{
    MMLOGI("+\n");
    APPPlaySource * com = new APPPlaySource();
    if ( !com ) {
        MMLOGE("no mem\n");
        return NULL;
    }

    MMLOGI("ret: %p\n", com);
    return com;
}

void releaseComponent(Component * component)
{
    MMLOGI("%p\n", component);
    if ( component ) {
        APPPlaySource * com = DYNAMIC_CAST<APPPlaySource*>(component);
        MM_RELEASE(com);
    }
}
}

}

