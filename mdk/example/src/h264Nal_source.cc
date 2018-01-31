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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "h264Nal_source.h"
 #ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include "multimedia/mm_debug.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/component.h"


MM_LOG_DEFINE_MODULE_NAME("H264NalSource")

#define AT_MSG_start (msg_type)1
static const char * MMMSGTHREAD_NAME = "H264NalSource";

const uint32_t StartCodeSize = 3;
static const int MaxNaluSize = 1024*1024; // assume max nalu size is 1M
static const int CacheBufferSize = 4 * MaxNaluSize;


BEGIN_MSG_LOOP(H264NalSource)
    MSG_ITEM(AT_MSG_start, onStart)
END_MSG_LOOP()


H264NalSource::H264NalSource()
    : MMMsgThread(MMMSGTHREAD_NAME)
    , mUri(NULL)
    , mState(STATE_NONE)
    , mFp(NULL)
    , mLength(0)
    , mLastReadOffset(0)
    , mAvailableData(0)
    , mBuffer(NULL)
    , mReadToEOS(false)
    , mParseToEOS(false) {

    mMetaData = MediaMeta::create();
}

H264NalSource::~H264NalSource() {
    avformat_close_input(&mInFmtCtx);
    MMMsgThread::exit();
    if(mFp)
        fclose(mFp);

    if(mBuffer)
        free(mBuffer);
}

mm_status_t H264NalSource::setUri(const char * uri,
                            const std::map<std::string, std::string> * headers/* = NULL*/){
    MMLOGI("uri: %s\n", uri);
    MMAutoLock lock(mLock);
    mUri = (char *)uri;
    return MM_ERROR_SUCCESS;
}

mm_status_t H264NalSource::setUri(int fd, int64_t offset, int64_t length){
    MMAutoLock lock(mLock);
    mFp = fdopen(fd, "r");
    mLastReadOffset = offset;
    mLength = length;

    int pos = fseek( mFp, offset,SEEK_SET);
    MMLOGI("fd %d, offset %" PRId64 " length %" PRId64 ", pos %" PRId64 "\n",
        fd, offset, length, pos);
    return MM_ERROR_SUCCESS;
}

bool H264NalSource::ensureBufferData(){
    int readCount = 0;
    if (mReadToEOS)
        return true;

    // available data is enough for parsing
    if (mLastReadOffset + MaxNaluSize < mAvailableData)
        return true;

    // move unused data to the begining of mBuffer
    if (mAvailableData + MaxNaluSize >= CacheBufferSize) {
        memcpy(mBuffer, mBuffer+mLastReadOffset, mAvailableData-mLastReadOffset);
        mAvailableData = mAvailableData-mLastReadOffset;
        mLastReadOffset = 0;
    }

    readCount = fread(mBuffer + mAvailableData, 1, MaxNaluSize, mFp);
    if (readCount < MaxNaluSize)
        mReadToEOS = true;

    mAvailableData += readCount;
    return true;
}

int32_t scanForStartCode(const uint8_t * data,
                 uint32_t offset, uint32_t size){
    uint32_t i;
    const uint8_t *buf;

    if (offset + 3 > size)
        return -1;

    for (i = 0; i < size - offset - 3 + 1; i++) {
        buf = data + offset + i;
        if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
            return i;
    }

    return -1;
}

#if 0
static void dumpData(const void* ptr, uint32_t size, uint32_t bytesPerLine){
    const uint8_t *data = (uint8_t*)ptr;
    mm_log(MM_LOG_DEBUG, "hexDump", " data=%p, size=%zu, bytesPerLine=%d\n", data, size, bytesPerLine);

    if (!data)
        return;

    char oneLineData[bytesPerLine*4+1];
    uint32_t offset = 0, lineStart = 0, i= 0;
    while (offset < size) {
        sprintf(&oneLineData[i*4], "%02x, ", *(data+offset));
        offset++;
        i++;
        if (offset == size || (i % bytesPerLine == 0)) {
            oneLineData[4*i-1] = '\0';
            mm_log(MM_LOG_DEBUG, "hexDump", "%04x: %s", lineStart, oneLineData);
            lineStart += bytesPerLine;
            i = 0;
        }
    }
}
#endif

MediaBufferSP H264NalSource::createMediaBuffer(VideoDecodeBuffer *buf){
    MediaBufferSP buffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
    if (!buffer)
        return buffer;

    MediaMetaSP meta = buffer->getMediaMeta();

    if(!mParseToEOS){
        buffer->setBufferInfo((uintptr_t*)&buf->data, NULL, NULL, 1);
        buffer->setSize(buf->size);
        //buffer->setPts(buf->timeStamp);
       //dumpData(buf->data, buf->size, 16);
    }
    else{
        MMLOGI("write eos to media");
        buffer->setFlag(MediaBuffer::MBFT_EOS);
        buffer->setSize(0);
    }

    return buffer;
}

bool H264NalSource::getOneNaluInput(VideoDecodeBuffer &inputBuffer){
    int32_t offset = -1;

    if(mParseToEOS)
        return false;

    // parsing data for one NAL unit
    ensureBufferData();
    MMLOGD("mLastReadOffset=0x%x, mAvailableData=0x%x\n", mLastReadOffset, mAvailableData);
    offset = scanForStartCode(mBuffer, mLastReadOffset+StartCodeSize, mAvailableData);

    if (offset == -1) {
        assert(mReadToEOS);
        offset = mAvailableData;
        mParseToEOS = true;
        MMLOGI("example source parse to EOS");
        notify(kEventEOS, 0, 0, MMParamSP((MMParam*)NULL));
    }

    inputBuffer.data = mBuffer + mLastReadOffset;
    inputBuffer.size = offset;
    // inputBuffer.flag = ;
    // inputBuffer.timeStamp = ; // ignore timestamp
    if (!mParseToEOS)
        inputBuffer.size += 3; // one inputBuffer is start and end with start code

    MMLOGI("offset=%d, NALU data=%p, size=%d\n", offset, inputBuffer.data, inputBuffer.size);
    mLastReadOffset += offset + StartCodeSize;
    return true;
}

mm_status_t H264NalSource::prepare(){
    MMAutoLock lock(mLock);
    mState = STATE_PREPARING;
    int32_t offset = -1;
    int ret = MMMsgThread::run();
    if (ret)
        return -1;

    if(mUri){
       mFp = fopen(mUri, "r");
        if (!mFp) {
            MMLOGE("fail to open input file: %s", mUri);
            return -1;
        }
    }

    if ( !mUri && !mFp) {
        MMLOGE("uri not set\n");
        return -1;
    }

    //FixME:Ffmpeg related operations should be done by decoder
    av_register_all();
    if (avformat_open_input(&mInFmtCtx, mUri, NULL, NULL) < 0) {
        MMLOGE("fail to open input url: %s by avformat\n", mUri);
        return -1;
    }

    if (!mInFmtCtx) {
        MMLOGE("fail to open input format context\n");
        return -1;
    }

    if (avformat_find_stream_info(mInFmtCtx,NULL) < 0) {
        MMLOGE("av_find_stream_info error\n");
        if (mInFmtCtx) {
            avformat_close_input(&mInFmtCtx);
        }
        return -1;
    }

    av_dump_format(mInFmtCtx,0,mUri,0);

    unsigned int j;
    // Find the first video stream

    for( j = 0; j < mInFmtCtx->nb_streams; j++) {
        if (mInFmtCtx->streams[j]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            mVideoStream = j;
            break;
        }
    }

    MMLOGV("videoStream = %d\n",mVideoStream);

    if (mVideoStream == -1) {
        MMLOGW("input file has no video stream\n");
        return 0; // Didn't find a video stream
    }
    MMLOGV("video stream num: %d\n",mVideoStream);
    mInCodecCtx = mInFmtCtx->streams[mVideoStream]->codec;

    //FixMe:If the follow-up ffmpeg decoder does not receive the codec context
    //then the ffmpeg decoder should create it by itself.
    mMetaData->setPointer(MEDIA_ATTR_CODEC_CONTEXT, mInCodecCtx);

    mMetaData->setInt32(MEDIA_ATTR_CODECID, mInCodecCtx->codec_id);
    mMetaData->setInt32(MEDIA_ATTR_CODECTAG, mInCodecCtx->codec_tag);
    mMetaData->setInt32(MEDIA_ATTR_CODECPROFILE, mInCodecCtx->profile);
    mMetaData->setInt32(MEDIA_ATTR_BIT_RATE, mInCodecCtx->bit_rate);
    mMetaData->setInt32(MEDIA_ATTR_WIDTH, mInCodecCtx->width);
    mMetaData->setInt32(MEDIA_ATTR_HEIGHT, mInCodecCtx->height);

    MMLOGV("codecContext: codec_type: %d, codec_id: 0x%x, codec_tag: 0x%x, profile: %d, width: %d, height: %d, extradata: %p, extradata_size: %d, channels: %d, sample_rate: %d, channel_layout: %" PRId64 ", bit_rate: %d\n",
                    mInCodecCtx->codec_type,
                    mInCodecCtx->codec_id,
                    mInCodecCtx->codec_tag,
                    mInCodecCtx->profile,
                    mInCodecCtx->width,
                    mInCodecCtx->height,
                    mInCodecCtx->extradata,
                    mInCodecCtx->extradata_size,
                    mInCodecCtx->channels,
                    mInCodecCtx->sample_rate,
                    mInCodecCtx->channel_layout,
                    mInCodecCtx->bit_rate);

    mBuffer = static_cast<uint8_t*>(malloc(CacheBufferSize));

    // locates to the first start code
    ensureBufferData();
    offset = scanForStartCode(mBuffer, mLastReadOffset, mAvailableData);
    if(offset == -1)
        return -1;

    mLastReadOffset = offset;
    mState = STATE_PREPARED;
   notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, MMParamSP((MMParam*)NULL));
    return 0;
}

mm_status_t  H264NalSource::stop(){
    MMAutoLock lock(mLock);
    mState = STATE_STOPPED;
    return 0;
 }

 mm_status_t H264NalSource::start() {
     MMAutoLock lock(mLock);
     mState = STATE_STARTED;
     return postMsg(AT_MSG_start, 0, NULL);
}

void H264NalSource::onStart(param1_type param1, param2_type param2, uint32_t rspId){
    MediaBufferSP buffer;
    if(mState == STATE_STOPPED) return;
    if (mState != STATE_STARTED  || mAvailableSourceBuffers.size() >= 10)
        goto start_end;

    VideoDecodeBuffer inputBuffer;
    if (!getOneNaluInput(inputBuffer)){
        goto start_end;
    }
    buffer = createMediaBuffer(& inputBuffer);

    mAvailableSourceBuffers.push_back(buffer);
    MMLOGI("mAvailableSourceBuffers.size() %d", mAvailableSourceBuffers.size());
    usleep(1000ll);
start_end:
    start();

    return;

}

Component::ReaderSP H264NalSource::getReader(MediaType mediaType) {
    ReaderSP rsp;
    MMAutoLock lock(mLock);
    if (mediaType != kMediaTypeVideo) {
        MMLOGE("mediaType(%d) is not supported", mediaType);
        return rsp;
    }

    Reader *reader = new H264NalSourceReader(this);
    rsp.reset(reader);

    return rsp;
}

MediaMetaSP H264NalSource::H264NalSourceReader::getMetaData() {
    return mSource->mMetaData;
}

mm_status_t H264NalSource::H264NalSourceReader::read(MediaBufferSP & buffer) {
    MMAutoLock lock(mSource->mLock);
    MMLOGV("H264NalSourceReader, read, SIZE %d", mSource->mAvailableSourceBuffers.size());

    if ( !mSource->mAvailableSourceBuffers.empty() ) {
        buffer = mSource->mAvailableSourceBuffers.front();
        mSource->mAvailableSourceBuffers.pop_front();

        return MM_ERROR_SUCCESS;
    }

    return MM_ERROR_NO_MORE;//remeber when return NULL buffer , status is MM_ERROR_NO_MORE
}

const std::list<std::string> & H264NalSource::supportedProtocols() const{
     static std::list<std::string> protocols;
     std::string str("file");
     protocols.push_back(str);
     return protocols;
}

mm_status_t H264NalSource::getDuration(int64_t & durationMs){
    MMLOGI("Not supported!");
    return MM_ERROR_UNSUPPORTED;
}

MediaMetaSP H264NalSource::getMetaData(){
    return mMetaData;
}

extern "C" {

using namespace YUNOS_MM;

Component * createComponent(const char* mimeType, bool isEncoder){
    H264NalSource * h264NalSource = new H264NalSource();
    if ( !h264NalSource ) {
        MMLOGE("no mem\n");
        return NULL;
    }

    MMLOGI("ret: %p\n", h264NalSource);
    return h264NalSource;
}

void releaseComponent(Component * component){
    MMLOGI("%p\n", component);
    if ( component ) {
        H264NalSource * h264NalSource = DYNAMIC_CAST<H264NalSource*>(component);
        MMASSERT(h264NalSource != NULL);
        MM_RELEASE(h264NalSource);
    }
}
}

