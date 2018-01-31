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
#include <multimedia/media_buffer.h>
#include <multimedia/media_attr_str.h>
#include <cow_util.h>
#include "multimedia/mm_audio.h"
#include "rtp_muxer.h"
#include <WfdMediaMsgInfo.h>
#include <multimedia/av_ffmpeg_helper.h>


//#undef AUIDO_VIDEO_IN_ORDER_DTS
#define AUIDO_VIDEO_IN_ORDER_DTS
namespace YUNOS_MM
{

DEFINE_LOGTAG(RtpMuxerSink)
DEFINE_LOGTAG(RtpMuxerSink::RtpMuxerSinkBuffer)
DEFINE_LOGTAG(RtpMuxerSink::RtpMuxerSinkWriter)
DEFINE_LOGTAG(RtpMuxerSink::MuxThread)

//#define VERBOSE DEBUG
#define ENTER() VERBOSE(">>>\n")
#define FLEAVE() do {VERBOSE(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER2()
#define FLEAVE2() return;
#define FLEAVE_WITH_CODE2(_code)  return (_code);

static const char *MMTHREAD_NAME = "RtpMuxerThread";
#define LOCAL_BASE_PORT          6667
#define TrafficControlLowBar     1
#define TrafficControlHighBar    60
#define GET_TIMES_INFO           1
///////////////RtpMuxerSink::RtpMuxerSinkBuffer/////////////////////
RtpMuxerSink::RtpMuxerSinkBuffer::RtpMuxerSinkBuffer(RtpMuxerSink *sink, TypeEnum type)
{
    ENTER();
#if !TRANSMIT_LOCAL_AV
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "RtpMuxerSinkBuffer"));
#endif
    mSink = sink;
    mType = type;

    FLEAVE();
}

RtpMuxerSink::RtpMuxerSinkBuffer::~RtpMuxerSinkBuffer()
{
    ENTER();

#if !TRANSMIT_LOCAL_AV
    while(!mBuffer.empty()) {
       mBuffer.pop();
    }
#endif

    FLEAVE();
}

#if TRANSMIT_LOCAL_AV
/*static*/ bool  RtpMuxerSink::RtpMuxerSinkBuffer::releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    int size;

    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, &size, 1))) {
        VERBOSE("not found the buffer\n");
        return false;
    }
    //VERBOSE("release output buffer:%p,%d\n", buffer, size);
    if (buffer)
        delete [] buffer;
    return true;
}

MediaBufferSP RtpMuxerSink::RtpMuxerSinkBuffer::readOneFrameFromLocalFile()
{
    ENTER();
    int64_t headerBuffer[5];
    int *val;
    int size, offset;
    uint8_t *buf;
    MediaBufferSP mediaBuffer;
#define READ_LOCAL_DATA_HEADER(fp)  if ( feof(fp) ) {\
                                      ERROR("the local file has not been open\n"); \
                                      return MediaBufferSP((MediaBuffer*)NULL); \
                                    } \
                                    val = (int*)(&headerBuffer[0]); \
                                    fread( &val[1], 1, 24, fp); \
                                    size = val[6]&0x7FFFFFFF; \
                                    buf = new uint8_t[ size+128 ]; \
                                    fread(buf+offset, 1, size, fp); \
                                    VERBOSE("header:0x%x,ftell(fp):%d,size:%d,flag:0x%x\n", val[1], ftell(fp), size, val[6]);

    if (mType == VideoType) {
        offset = 64;
        READ_LOCAL_DATA_HEADER( mSink->mLocalVideoFp );
        if ( ftell( mSink->mLocalVideoFp ) == (24+size) ) {
           VERBOSE("read the codec data only\n");
           mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
           mediaBuffer->setFlag(MediaBuffer::MBFT_CodecData);
           mediaBuffer->addReleaseBufferFunc(releaseOutputBuffer);
           mediaBuffer->setBufferInfo((uintptr_t *)(&buf), &offset, &size, 1);
           mediaBuffer->setSize( offset + size);
           uint8_t *localBuf = buf + offset;
           MediaMetaSP meta = mediaBuffer->getMediaMeta();
           meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, localBuf, size);
           return mediaBuffer;
        }

        mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
        mediaBuffer->setDuration(40);
        if (val[6] & 0x80000000)
           mediaBuffer->setFlag(MediaBuffer::MBFT_KeyFrame);
    } else {
        offset = 0;
        READ_LOCAL_DATA_HEADER( mSink->mLocalAudioFp );

        mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawAudio);
        mediaBuffer->setDuration(46);
        mediaBuffer->setFlag(MediaBuffer::MBFT_KeyFrame);
    }
    mediaBuffer->setPts(headerBuffer[1]);
    mediaBuffer->setDts(headerBuffer[1]);
    mediaBuffer->addReleaseBufferFunc(releaseOutputBuffer);
    int tmp = size + offset;
    mediaBuffer->setBufferInfo((uintptr_t *)(&buf), &offset, &tmp, 1);
    mediaBuffer->setSize(tmp);

    if ((mType == VideoType) && (!mSink->mSavedStartTime) ) {
        mSink->mStartTime = av_gettime();
        mSink->mSavedStartTime = true;
        VERBOSE("the start time :%" PRId64 "\n", mSink->mStartTime);
    }
    int64_t nowTime = av_gettime();
    VERBOSE("nowTime:%" PRId64 ", startTime:%" PRId64 ", dts:%" PRId64 "\n", nowTime, mSink->mStartTime, headerBuffer[1]);
    if ( headerBuffer[1] > (nowTime - mSink->mStartTime)) {
        usleep( headerBuffer[1] - (nowTime - mSink->mStartTime));
    }
    VERBOSE("nowTime:%" PRId64 ", startTime:%" PRId64 ", dts:%" PRId64 "\n", av_gettime(), mSink->mStartTime, headerBuffer[1]);

    return mediaBuffer;
}
#endif

MediaBufferSP RtpMuxerSink::RtpMuxerSinkBuffer::getBuffer()
{
    ENTER2();

    MMAutoLock locker(mSink->mLock);

#if !TRANSMIT_LOCAL_AV
    if (!mBuffer.empty())
        return mBuffer.front();
    return MediaBufferSP((MediaBuffer*)NULL);
#else
    VERBOSE("start to read media buffer,mType:%d\n", mType);
    if (mType == VideoType) {
        mSink->mVideoTmpBuffer = readOneFrameFromLocalFile();
        return mSink->mVideoTmpBuffer;
    } else {
        mSink->mAudioTmpBuffer = readOneFrameFromLocalFile();
        return mSink->mAudioTmpBuffer;
    }
#endif
}

mm_status_t RtpMuxerSink::RtpMuxerSinkBuffer::popBuffer()
{
    ENTER2();

    MMAutoLock locker(mSink->mLock);

#if !TRANSMIT_LOCAL_AV
    if(!mBuffer.empty()) {
        mBuffer.pop();
    }
#else
    if ((mType == VideoType) && (mSink->mVideoTmpBuffer.get()) )
        mSink->mVideoTmpBuffer.reset();
    if ((mType == AudioType) && (mSink->mAudioTmpBuffer.get()) )
        mSink->mAudioTmpBuffer.reset();
#endif

    FLEAVE_WITH_CODE2(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::RtpMuxerSinkBuffer::writeBuffer(MediaBufferSP buffer)
{
    ENTER2();

    if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        VERBOSE("Notify kEventEOS:%d\n", mType);
        mSink->notify(kEventEOS, 0, 0, nilParam);
        FLEAVE_WITH_CODE2(MM_ERROR_EOS);
    }
#if !TRANSMIT_LOCAL_AV
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    trafficControlWrite->waitOnFull();

    {
        MMAutoLock locker(mSink->mLock);

        buffer->setMonitor(mMonitorWrite);
        mBuffer.push(buffer);
    }
#endif
    FLEAVE_WITH_CODE2(MM_ERROR_SUCCESS);
}

int RtpMuxerSink::RtpMuxerSinkBuffer::size()
{
#if !TRANSMIT_LOCAL_AV
    return mBuffer.size();
#else
    return 1;
#endif
}

/////////////RtpMuxerSink::RtpMuxerSinkWriter////////////////////
mm_status_t RtpMuxerSink::RtpMuxerSinkWriter::write(const MediaBufferSP &buffer)
{
    ENTER2();

    //VERBOSE("RtpMuxerSinkWriter::write:%d\n", mType);

    if (mType == VideoType && mSink && mSink->mVideoBuffer)
        mSink->mVideoBuffer->writeBuffer(buffer);
    else if (mType == AudioType && mSink && mSink->mAudioBuffer)
        mSink->mAudioBuffer->writeBuffer(buffer);
    else {
        ERROR("type is failed\n");
        FLEAVE_WITH_CODE2(MM_ERROR_IVALID_OPERATION);
    }
#if !TRANSMIT_LOCAL_AV
    {
       MMAutoLock locker(mSink->mLock);
       mSink->mCondition.signal();
    }
#endif
    FLEAVE_WITH_CODE2(MM_ERROR_SUCCESS);
}

#define ADDSTREAM_GET_META_INT32(_attr, _val) do {\
    VERBOSE("trying get meta: %s\n", _attr);\
    if ( info.meta->getInt32(_attr, _val) ) {\
        VERBOSE("got meta: %s -> %d\n", _attr, _val);\
    } else {\
        VERBOSE("meta %s not provided\n", _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_INT64(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    if ( info.meta->getInt64(_attr, _val) ) {\
        VERBOSE("got meta: %s -> %d\n", _attr, _val);\
    } else {\
        VERBOSE("meta %s not provided\n", _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_INT32_TO_FRACTION(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    int32_t _i32;\
    if ( info.meta->getInt32(_attr, _i32) ) {\
        VERBOSE("meta: %s -> %d\n", _attr, _i32);\
        _val.num = _i32;\
        _val.den = 1;\
    } else {\
        VERBOSE("media meta %s not provided\n", _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_INT32_TO_UINT32(_attr, _val) do {\
    MMLOGV("trying get meta: %s\n", _attr);\
    int32_t _i32;\
    if ( info.meta->getInt32(_attr, _i32) ) {\
        VERBOSE("got meta: %s -> %d\n", _attr, _i32);\
        _val = _i32;\
    } else {\
        VERBOSE("meta %s not provided\n", _attr);\
    }\
}while(0)

#define ADDSTREAM_GET_META_BUF(_attr, _buf, _buf_size) do {\
    MMLOGV("trying getting meta: %s\n", _attr);\
    uint8_t* _data;\
    int32_t _size;\
    if ( info.meta->getByteBuffer(_attr, _data, _size) ) {\
        VERBOSE("got meta: %s -> size: %d\n", _attr, _size);\
        _buf = (uint8_t*)av_malloc(_size);\
        if ( !_buf ) {\
            ERROR("no mem\n");\
            _buf_size = 0;\
            break;\
        }\
        memcpy(_buf, _data, _size);\
        _buf_size = _size;\
    } else {\
        VERBOSE("%s not provided\n", _attr);\
    }\
}while(0)

static AVSampleFormat convertAudioFormat(snd_format_t sampleFormat)
{
#undef item
#define item(_av, _audio) \
        case _audio:      \
            return _av

        switch ( sampleFormat ) {
            item(AV_SAMPLE_FMT_U8, SND_FORMAT_PCM_8_BIT);
            item(AV_SAMPLE_FMT_S16, SND_FORMAT_PCM_16_BIT);
            item(AV_SAMPLE_FMT_S32, SND_FORMAT_PCM_32_BIT);
            //item(AV_SAMPLE_FMT_FLT, AUDIO_SAMPLE_FLOAT32LE);
            default:
              //MMLOGV("%d -> AUDIO_SAMPLE_INVALID\n", sampleFormat);
              return AV_SAMPLE_FMT_NONE;
            }
}

#if TRANSMIT_LOCAL_AV
static void resetVideoMetaData(const MediaMetaSP & metaData)
{
    metaData->setInt32(MEDIA_ATTR_CODECID, kCodecIDH264);
    metaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
    metaData->setInt32(MEDIA_ATTR_WIDTH, 768);
    metaData->setInt32(MEDIA_ATTR_HEIGHT, 320);
    metaData->setInt32(MEDIA_ATTR_AVG_FRAMERATE,25);
    metaData->setInt32(MEDIA_ATTR_BIT_RATE, 467744);
}

static void resetAudioMetaData(const MediaMetaSP & metaData)
{
    metaData->setInt32(MEDIA_ATTR_CODECID, kCodecIDAAC);
    metaData->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
    metaData->setInt32(MEDIA_ATTR_SAMPLE_RATE, 44100);
    metaData->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, 1);
    metaData->setInt32(MEDIA_ATTR_CHANNEL_COUNT, 2);
    //metaData->setInt32(MEDIA_ATTR_BIT_RATE, 32128);
    metaData->setInt32(MEDIA_ATTR_BIT_RATE, 2086);
}
#endif
mm_status_t RtpMuxerSink::RtpMuxerSinkWriter::setMetaData(const MediaMetaSP & metaData)
{
    ENTER();

    VERBOSE("setMetaData type:%d, meta:%p\n", mType, metaData.get());

    if (mType == VideoType) {
#if TRANSMIT_LOCAL_AV
        VERBOSE("reset the video meta data\n");
        resetVideoMetaData(metaData);
#endif
        mSink->mVideoMetaData = metaData;
        if (mSink->mVideoBuffer.get())
            mSink->mVideoBuffer.reset();
        RtpMuxerSink::RtpMuxerSinkBuffer *videoBuffer = NULL;
        videoBuffer = new RtpMuxerSinkBuffer( mSink, VideoType );
        if (!videoBuffer) {
           ERROR("new video RtpMuxerSinkBuffer failed\n");
           FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
        }
        mSink->mVideoBuffer.reset(videoBuffer);
        mSink->mHasVideoTrack = true;
    }
    else if (mType == AudioType) {
#if TRANSMIT_LOCAL_AV
        VERBOSE("reset the audio meta data\n");
        resetAudioMetaData(metaData);
#endif
        mSink->mAudioMetaData = metaData;
        if (mSink->mAudioBuffer.get())
            mSink->mAudioBuffer.reset();
        RtpMuxerSink::RtpMuxerSinkBuffer *audioBuffer = NULL;
        audioBuffer = new RtpMuxerSinkBuffer( mSink, AudioType );
        if (!audioBuffer) {
           ERROR("new audio RtpMuxerSinkBuffer failed\n");
           FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
        }
        mSink->mAudioBuffer.reset(audioBuffer);
        mSink->mHasAudioTrack = true;
    }
    else {
        ERROR("no find av type:d\n", mType);
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

/////////////RtpMuxerSink::MuxThread////////////////////////////////
RtpMuxerSink::MuxThread::MuxThread(RtpMuxerSink *sink)
                                      : MMThread(MMTHREAD_NAME),
                                        mSink(sink),
                                        mContinue(true)
{
    ENTER();
    FLEAVE();
}

RtpMuxerSink::MuxThread::~MuxThread()
{
    ENTER();
    FLEAVE();
}

void RtpMuxerSink::MuxThread::signalExit()
{
    ENTER();

    {
        MMAutoLock locker(mSink->mLock);
        mContinue = false;
        mSink->mCondition.signal();
    }

    destroy();
    FLEAVE();
}

void RtpMuxerSink::MuxThread::signalContinue()
{
    ENTER();
    mSink->mCondition.signal();
    FLEAVE();
}

static int64_t convertTime(int64_t ts, AVRational timebase0, AVRational timebase1)
{
    AVPacket pkt;

    av_init_packet(&pkt);
    pkt.dts = ts;
    av_packet_rescale_ts(&pkt, timebase0,timebase1);

    return pkt.dts;
}

static void fakeAACExtraData(uint8_t **ppData, int *size)
{
    uint8_t *data = (uint8_t*)av_malloc(5);
    data[0] = 0x12;
    data[1] = 0x10;
    data[2] = 0x56;
    data[3] = 0xE5;
    data[4] = 0x00;

    *ppData = data;
    *size = 5;
    return;
}

static int64_t miniChangeTS(int64_t wroteDts, int64_t *dts, int64_t *pts)
{

#ifdef AUIDO_VIDEO_IN_ORDER_DTS
    if ( (*dts) == wroteDts ) {
       (*dts) ++;
       (*pts) ++;
    }
#else
    if ( (*dts) <= wroteDts ) {
       (*dts) = wroteDts + 1;
       (*pts) = wroteDts + 1;
    }
#endif

    wroteDts = *dts;

    return wroteDts;
}

void RtpMuxerSink::MuxThread::main()
{
    ENTER();
    AVRational timeBaseQ = {1, AV_TIME_BASE};
#define NEVER_PTS_DTS  -65536
    int64_t audioDts = NEVER_PTS_DTS, videoDts = NEVER_PTS_DTS;
    MediaBufferSP audioBuffer, videoBuffer;
    bool openedFile = false, isVDoneData = false, isADoneData = true;
    uint8_t *avcHeader = NULL, *vExtraData = NULL;
    int avcHeaderSize = 0, vExtraDataSize = 0;
    mm_status_t ret = MM_ERROR_SUCCESS;
    bool hasVideoSample = false;
    bool hasAudioSample = false;

    while(1) {
        //#0 check the end command
        {
            MMAutoLock locker(mSink->mLock);
            if (!mContinue) {
                break;
            }

            bool needPaused = false;
            if ( mSink->mIsPaused )
                needPaused = true;
            //only audio or only video or audio video exist at the same time
            //for example:
            //VideoTrack: true, AudioTrack:true, VideoBuffer->size > 0, AudioBuffer->size() = 0, not wait
            //VideoTrack: true, AudioTrack:true, VideoBuffer->size = 0, AudioBuffer->size() = 0,  wait
            else if ( (mSink->mHasVideoTrack + mSink->mHasAudioTrack) >
                      ((!!mSink->mVideoBuffer->size()) + (!!mSink->mVideoBuffer->size())) )
                         needPaused = true;

            if (needPaused) {
                VERBOSE("pause wait");
                mSink->mCondition.wait();
                VERBOSE("pause wait wakeup");
            }
        }

        //#1 get a video buffer
        if ( mSink->mHasVideoTrack ) { //has video stream
            if (!hasVideoSample) {
                videoBuffer = mSink->mVideoBuffer->getBuffer();
                if ( videoBuffer.get() ) {
                    videoDts = convertTime(videoBuffer->dts(), mSink->mVTimeBase, timeBaseQ);
                    hasVideoSample = true;
                }
            }
            if (hasVideoSample) {
                if ((!isVDoneData)&&(videoBuffer->isFlagSet(MediaBuffer::MBFT_CodecData))) {
                    INFO("codec data only\n");
                    if (mSink->setExtraData(videoBuffer,
                                            &avcHeader, &avcHeaderSize,
                                            &vExtraData, &vExtraDataSize)
                                   != MM_ERROR_SUCCESS) {
                         ERROR("not found the extra data\n");
                         break;
                    }
                   isVDoneData = true;
                   mSink->mVideoBuffer->popBuffer();
                   videoDts = NEVER_PTS_DTS;
                   hasVideoSample = false;
                } //if ((!isVDoneData)&&(videoBuffer->isFlagSet
                //VERBOSE("get the video buffer size:%d, dts:%" PRId64 "\n",mSink->mVideoBuffer->size(), videoDts);
            }//if (videoBuffer.get())
        }//if ( mSink->mHasVideoTrack )

        //#2 get the audio buffer
        if ( mSink->mHasAudioTrack ) { //has audio stream
            if (!hasAudioSample) { //audio buffer is null
                audioBuffer = mSink->mAudioBuffer->getBuffer();
                if (audioBuffer.get()) {
                   audioDts = convertTime(audioBuffer->dts(), mSink->mATimeBase, timeBaseQ);
                   hasAudioSample = true;
                }
            }
            if (hasAudioSample) {
                if ((!isADoneData)&&(audioBuffer->isFlagSet(MediaBuffer::MBFT_CodecData))) {
                       VERBOSE("codec data only\n");
                       isADoneData = true;
                       mSink->mAudioBuffer->popBuffer();
                       audioDts = NEVER_PTS_DTS;
                       hasAudioSample = false;
                }
                //VERBOSE("get the audio buffer size:%d, dts:%" PRId64 "\n", mSink->mAudioBuffer->size(), audioDts);
            }//if (audioBuffer.get())
        }//if ( mSink->mHasAuido )

        //#3 create the rtp file
        if ((!openedFile) && isADoneData && isVDoneData) {
            if (mSink->openWebFile(mSink->mLocalPort,
                                   vExtraData, vExtraDataSize,
                                   NULL, 0) != MM_ERROR_SUCCESS) {
                VERBOSE("open the web file is failed\n");
                break;
            }
            VERBOSE("open file is ok\n");
            openedFile = true;
        }

        //#4 audio exist && video exist
#ifdef AUIDO_VIDEO_IN_ORDER_DTS
        if ( mSink->mHasAudioTrack && hasAudioSample &&
             mSink->mHasVideoTrack && hasVideoSample) {
#else
        if (hasVideoSample || hasAudioSample) {
#endif
            //check which packet to send
            if ((!hasVideoSample) || (hasAudioSample && audioDts <= videoDts)) {
                VERBOSE("start to write audio data into rtp\n");
                ret = mSink->writeWebFile(audioBuffer, AudioType,
                                          NULL, 0);
                mSink->mAudioBuffer->popBuffer();
                audioDts = NEVER_PTS_DTS;
                hasAudioSample = false;
            } else { // videoPts > audioPts
                VERBOSE("start to write video data into rtp\n");
                ret = mSink->writeWebFile(videoBuffer, VideoType,
                                          avcHeader, avcHeaderSize);
                mSink->mVideoBuffer->popBuffer();
                videoDts = NEVER_PTS_DTS;
                hasVideoSample = false;
           }
        }

        //#5 only write video data
        if (mSink->mHasVideoTrack && hasVideoSample &&
            !mSink->mHasAudioTrack) {
                VERBOSE("start to only write video data into rtp\n");
                ret = mSink->writeWebFile(videoBuffer, VideoType,
                                          avcHeader, avcHeaderSize);
                mSink->mVideoBuffer->popBuffer();
                videoDts = NEVER_PTS_DTS;
                hasVideoSample = false;
        }//only write video data

        //#6 only write audio data
        if (mSink->mHasAudioTrack && hasAudioSample &&
            !mSink->mHasVideoTrack) {
               VERBOSE("start to only write audio data into rtp\n");
               ret = mSink->writeWebFile(audioBuffer, AudioType,
                                         NULL, 0);
               mSink->mAudioBuffer->popBuffer();
               audioDts = NEVER_PTS_DTS;
               hasAudioSample = false;
        }

        if (ret != MM_ERROR_SUCCESS) {
            ERROR("writeWebFile error:%d\n", ret);
            break;
        }
    }

    if ( hasVideoSample )
      mSink->mVideoBuffer->popBuffer();
    if ( hasAudioSample )
      mSink->mAudioBuffer->popBuffer();

    if (openedFile)
        mSink->closeWebFile();

    if ( avcHeader ) {
        delete [] avcHeader;
        avcHeaderSize = 0;
    }
    if ( vExtraData ) {
        delete [] vExtraData;
        vExtraDataSize = 0;
    }

    VERBOSE("Output thread exited\n");
    FLEAVE();
}

//////////////////////RtpMuxerSink////////////////////////////////////
RtpMuxerSink::RtpMuxerSink(const char *mimeType, bool isEncoder)
                           :mCondition(mLock),
                            mIsPaused(true),
                            mHasVideoTrack(false),
                            mHasAudioTrack(false),
                            mLocalPort(LOCAL_BASE_PORT),
                            mFormatCtx(NULL),
                            mWroteDts(-1)
{
    ENTER();
    mVTimeBase = { 0 };
    mATimeBase = { 0 };

    av_register_all();
    avformat_network_init();
#if DUMP_H264_DATA
    mH264Fp = fopen("/tmp/rtp_h264.h264", "wb");
    if ( !mH264Fp ) {
        VERBOSE("CAUTION: OPEN DUMP H264 FILE ERROR\n");
    }
#endif
#if DUMP_AAC_DATA
    mAacFp = fopen("/tmp/rtp_aac.aac", "wb");
    if ( !mAacFp ) {
        VERBOSE("CAUTION: OPEN DUMP AAC FILE ERROR\n");
    }
#endif
#if TRANSMIT_LOCAL_AV
    mLocalVideoFp = fopen("/tmp/only_video.bin", "rb");
    if ( !mLocalVideoFp ) {
       VERBOSE("CAUTION: OPEN ONLY VIDEO FILE ERROR\n");
    }
    mLocalAudioFp = fopen("/tmp/only_audio.bin", "rb");
    if ( !mLocalAudioFp ) {
       VERBOSE("CAUTION: OPEN ONLY AUDIO FILE ERROR\n");
    }
    mSavedStartTime = false;
#endif
    FLEAVE();
}

RtpMuxerSink::~RtpMuxerSink()
{
    ENTER();

#if DUMP_H264_DATA
    if ( mH264Fp )
        fclose(mH264Fp);
    mH264Fp = NULL;
#endif


#if DUMP_AAC_DATA
    if ( mAacFp )
        fclose(mAacFp);
    mAacFp = NULL;
#endif

#if TRANSMIT_LOCAL_AV
    if (mLocalVideoFp)
        fclose(mLocalVideoFp);
    mLocalVideoFp = NULL;

    if (mLocalAudioFp)
        fclose(mLocalAudioFp);
    mLocalAudioFp = NULL;
#endif

    FLEAVE();
}

mm_status_t RtpMuxerSink::prepare()
{
    ENTER();

    MMAutoLock locker(mLock);

    //#0 check the video audio exist
    if ((!mHasVideoTrack) && (!mHasAudioTrack)) {
        ERROR("now find neither video nor audio exists! please add writer\n");
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }
    if ( mFormatCtx ) {
        ERROR("now find mFormatCtx is not null\n");
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }

    //#1 check the video parameters
    if ( mHasVideoTrack ) {
        if (!mVideoBuffer.get()) {
            ERROR("now find videoBuffer is null, check setmetadata\n");
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
        if (!mVideoMetaData.get()) {
            ERROR("now find mVideoMetaData is null, check setmetadata\n");
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
    }

    //#2 check the audio parameters
    if ( mHasAudioTrack ) {
        if (!mAudioBuffer.get())  {
            ERROR("now find audioBuffer is null, check setmetadata\n");
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
        if (!mAudioMetaData.get()) {
            ERROR("now find mAudioMetaData is null, check setmetadata\n");
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
    }

    //#3 check RemoteIP && RemotePort
    if ( mRemoteURL.empty() ) {
        ERROR("now find remote url:%s is error,please setParameter\n",
              mRemoteURL.c_str());
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }

    mm_status_t ret = internalPrepare();
    FLEAVE_WITH_CODE(ret);
}

mm_status_t RtpMuxerSink::internalPrepare()
{
    ENTER();

    //#0 try to find one correct local port
    #define MAX_LOOP_COUNT   100
#if 0
    int i;
    for (i = 0; i < MAX_LOOP_COUNT; i++) {
        if ( !openWebFile(mLocalPort+i, NULL, 0, NULL, 0) ) {
            VERBOSE("now find the correct local port:%d\n", mLocalPort+i);
            mLocalPort += i;
            closeWebFile();
            mFormatCtx = NULL;
            notify(kEventInfoExt, WFD_MSG_INFO_RTP_CREATE, mLocalPort, nilParam);
            break;
        } else {
            usleep(10);
        }
    }

    if (i == MAX_LOOP_COUNT) {
        VERBOSE("now NO find the correct local port,EXIT\n");
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }
#endif
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::start()
{
    ENTER();

    MMAutoLock locker(mLock);

    //#0 create the mux thread into pause state
    if (!mMuxThread) {
        mMuxThread.reset (new MuxThread(this), MMThread::releaseHelper);
        mMuxThread->create();
    }
    mIsPaused = false;

    mMuxThread->signalContinue();

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::pause()
{
    ENTER();

    MMAutoLock locker(mLock);

    mIsPaused = true;
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::stop()
{
    ENTER();

    mm_status_t ret = internalStop();
    FLEAVE_WITH_CODE(ret);
}

mm_status_t RtpMuxerSink::internalStop()
{
    ENTER();

    mIsPaused = true;
    if (mMuxThread) {
        mMuxThread->signalExit();
    }

    {
        MMAutoLock locker(mLock);
        if ( mVideoBuffer.get()) {
            VERBOSE("now the video buffer size:%d\n", mVideoBuffer->size());
            TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mVideoBuffer->mMonitorWrite.get());
            trafficControlWrite->unblockWait();
        }

        if ( mAudioBuffer.get()) {
            VERBOSE("now the audio buffer size:%d\n", mAudioBuffer->size());
            TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mAudioBuffer->mMonitorWrite.get());
            trafficControlWrite->unblockWait();
        }
     }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::reset()
{
    ENTER();

    internalStop();

    {
        MMAutoLock locker(mLock);
        if ( mVideoBuffer.get() )
            mVideoBuffer.reset();

        if ( mAudioBuffer.get() )
            mAudioBuffer.reset();

        mRemoteURL.clear();
        mHasVideoTrack = mHasAudioTrack = false;
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

static int getRemoteURL(const char *buf, std::string &remoteURL)
{
    const char  *p;
    char pool[32];

    p = strstr(buf, "/video ");
    if ( p == NULL) {
       return MM_ERROR_INVALID_PARAM;
    }

    memcpy(pool, buf, p-buf);
    pool[ p-buf ] = '\0';
    remoteURL = pool;

    return MM_ERROR_SUCCESS;
}

mm_status_t RtpMuxerSink::setParameter(const MediaMetaSP & meta)
{
    ENTER();

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_FILE_PATH) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                VERBOSE("invalid type for %s\n", item.mName);
                continue;
            }
            //mUrl = item.mValue.str;
            if ( getRemoteURL(item.mValue.str, mRemoteURL) != MM_ERROR_SUCCESS ) {
                VERBOSE("getRemoteURL \n");
            }
            continue;
        }
    }

    MMLOGW("file path:%s\n", mRemoteURL.c_str());

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

Component::WriterSP RtpMuxerSink::getWriter(MediaType mediaType)
{
    ENTER();

    if ((int)mediaType == Component::kMediaTypeVideo)
        return Component::WriterSP(new RtpMuxerSink::RtpMuxerSinkWriter(this, VideoType));
    else if ((int)mediaType == Component::kMediaTypeAudio)
        return Component::WriterSP(new RtpMuxerSink::RtpMuxerSinkWriter(this, AudioType));
    else {
        ERROR("not supported mediatype: %d\n", mediaType);
    }

    return Component::WriterSP((Component::Writer*)NULL);
}

mm_status_t RtpMuxerSink::setStreamParameters(AVStream *stream, StreamInfo info)
{
    ENTER();
    AVCodecContext *c = stream->codec;

    VERBOSE("setStreamParameters:%d, meta:%p\n", info.type, info.meta.get());
    if ( !info.meta.get()) {
        ERROR("meta not provided:%d\n", info.type);
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }

    int32_t codecid = 0;
    if ( !info.meta->getInt32(MEDIA_ATTR_CODECID, codecid) ){
        ERROR("no codec id provided\n");
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    } else if ((info.type == VideoType) && (codecid != kCodecIDH264)) {
        ERROR("no codec id provided:%d\n", codecid);
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    } else if ((info.type == AudioType) && (codecid != kCodecIDAAC)) {
        ERROR("no codec id provided:%d\n", codecid);
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }

    stream->id = mFormatCtx->nb_streams - 1;
    switch ( info.type ) {
        case VideoType:
            info.meta->getFraction(MEDIA_ATTR_TIMEBASE, mVTimeBase.num, mVTimeBase.den);
            c->codec_type =  AVMEDIA_TYPE_VIDEO;
            c->time_base = mVTimeBase;
            c->flags |= (CODEC_FLAG_GLOBAL_HEADER | CODEC_FLAG_BITEXACT);
            break;
        case AudioType:
            info.meta->getFraction(MEDIA_ATTR_TIMEBASE, mATimeBase.num, mATimeBase.den);
            c->codec_type =  AVMEDIA_TYPE_AUDIO;
            c->time_base = mATimeBase;
            break;
        default:
            break;
    }
    c->codec_id = (AVCodecID)codecid;
    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_WIDTH, c->width);
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_HEIGHT, c->height);
    ADDSTREAM_GET_META_INT32_TO_FRACTION(MEDIA_ATTR_AVG_FRAMERATE, stream->avg_frame_rate);
    int32_t i32 = 0;
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_SAMPLE_RATE, c->sample_rate);
    i32 = 0;
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_SAMPLE_FORMAT, i32);
    c->sample_fmt = convertAudioFormat((snd_format_t)i32);
    int64_t i64 = 0;
    ADDSTREAM_GET_META_INT64(MEDIA_ATTR_CHANNEL_LAYOUT, i64);
    c->channel_layout = (uint64_t)i64;
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_CHANNEL_COUNT, c->channels);
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_BLOCK_ALIGN, c->block_align);

    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_CODECPROFILE, c->profile);

    int32_t tmp;
    ADDSTREAM_GET_META_INT32(MEDIA_ATTR_BIT_RATE, tmp);
    c->bit_rate = tmp;

    if ( (!info.size)&&(info.type==AudioType) )
        fakeAACExtraData( &c->extradata, &c->extradata_size);
    else if (info.size) {
#if 1||(!TRANSMIT_LOCAL_AV)
        c->extradata = (uint8_t*)av_malloc( info.size );
        if ( !c->extradata ) {
            ERROR("av_malloc failed:%d\n", info.size );
            c->extradata_size = 0;
            FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
        }

        INFO("got codec data, size: %d\n", info.size );
        memcpy(c->extradata, info.data, info.size);
        c->extradata_size = info.size;
#else
        c->extradata = (uint8_t*)av_malloc(7);
        c->extradata[0] = 0x13;
        c->extradata[1] = 0x90;
        c->extradata[2] = 0x56;
        c->extradata[3] = 0xE5;
        c->extradata[4] = 0xA5;
        c->extradata[5] = 0x48;
        c->extradata[6] = 0x0;
        c->extradata_size = 7;
#endif
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::setExtraData(const MediaBufferSP & buffer,
                                           uint8_t **ppAVCHeader, int *avcHeaderSize,
                                           uint8_t **ppVExtraData, int *VExtraDataSize)
{
    MediaMetaSP meta = buffer->getMediaMeta();
    uint8_t * data;
    int32_t size;
    if ( !meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size) ) {
        ERROR("no codec data\n");
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }
    VERBOSE("setExtraData data:%p,size:%d\n", data, size);

    *ppAVCHeader = new uint8_t [size+16];
    if (!(*ppAVCHeader)) {
        ERROR("new space for avcheader is failed\n");
        FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
    }
    memcpy(*ppAVCHeader, data, size);
    *avcHeaderSize = size;

    MediaBufferSP mediaBuffer = MakeAVCCodecExtradata(data, size);
    if (!mediaBuffer) {
        ERROR("make avcc extradata failed\n");
        if(*ppAVCHeader) delete []*ppAVCHeader;
        *avcHeaderSize = 0;
        FLEAVE_WITH_CODE(MM_ERROR_MALFORMED);
    }

    uint8_t *buffers;
    int32_t offsets;
    int32_t strides;
    if ( !mediaBuffer->getBufferInfo((uintptr_t*)&buffers, &offsets, &strides, 1) ) {
        ERROR("failed to get bufferinfo\n");
        if(*ppAVCHeader) delete []*ppAVCHeader;
        *avcHeaderSize = 0;
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }

    data = buffers;
    size = strides;

   *ppVExtraData = new uint8_t [size+16];
   if (!(*ppVExtraData)) {
       ERROR("new space for ppVExtraData is failed\n");
       if(*ppAVCHeader) delete []*ppAVCHeader;
        *avcHeaderSize = 0;
       FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
   }
   memcpy(*ppVExtraData, data, size);
   *VExtraDataSize = size;

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::openWebFile(int localPort,
                                           uint8_t *videoData, int vDataSize,
                                           uint8_t *audioData, int aDataSize)
{
    ENTER();

    //#0 create the web file header
    AVOutputFormat * outputFormat = av_guess_format("rtp_mpegts", NULL, NULL);
    if ( !outputFormat ) {
        ERROR("rtp_mpegts format not supported.\n");
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }

    mFormatCtx = avformat_alloc_context();
    if ( !mFormatCtx ) {
        ERROR("failed to create avcontext\n");
        FLEAVE_WITH_CODE(MM_ERROR_INVALID_PARAM);
    }
    mFormatCtx->oformat = outputFormat;

    //#1 add the stream file
    StreamInfo  streamInfo[2];
    int streamNum = 0;
    if ( mHasVideoTrack ) {
        streamInfo[streamNum].type = VideoType;
        streamInfo[streamNum].meta = mVideoMetaData;
        streamInfo[streamNum].data = videoData;
        streamInfo[streamNum++].size = vDataSize;
    }
    if ( mHasAudioTrack ) {
        streamInfo[streamNum].type = AudioType;
        streamInfo[streamNum].meta = mAudioMetaData;
        uint8_t * data = NULL;
        int32_t size = 0;
        if ( !mAudioMetaData->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size) ) {
           streamInfo[streamNum].data = audioData;
           streamInfo[streamNum++].size = aDataSize;
        } else {
           streamInfo[streamNum].data = data;
           streamInfo[streamNum++].size = size;
        }

        VERBOSE("get the byte buffer:%p,%d\n", data, size);
    }

    for (int i = 0; i < streamNum; i++) {
        mm_status_t ret;
        AVStream *outStream = avformat_new_stream(mFormatCtx, NULL);
        if (!outStream) {
            ERROR( "Failed allocating output stream\n");
            FLEAVE_WITH_CODE(ret);
        }

        ret = setStreamParameters(outStream, streamInfo[i]);
        if (ret != MM_ERROR_SUCCESS) {
            ERROR("openWebFile set codec parameters failed\n");
            FLEAVE_WITH_CODE(ret);
        }
    }

    //#2 dump file & open file
    int retVal;
    av_dump_format(mFormatCtx, 0, mRemoteURL.c_str(), 1);
    if (!(outputFormat->flags & AVFMT_NOFILE)) {
        char url[1024];
        sprintf(url, "%s?localport=%d\n", mRemoteURL.c_str(), mLocalPort);
        VERBOSE("the total path:%s\n", url);
        retVal = avio_open(&mFormatCtx->pb, url, AVIO_FLAG_WRITE);
        if (retVal < 0) {
            ERROR("avio_pen failed:%d\n", retVal);
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
        VERBOSE("now open the :%s file\n", url);
    }
    retVal = avformat_write_header(mFormatCtx, NULL);
    if (retVal < 0) {
        ERROR( "Error occurred when opening output URL:ret:%d\n", retVal);
        if (!(outputFormat->flags & AVFMT_NOFILE))
            avio_close(mFormatCtx->pb);
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }
    mWroteDts = -1;

    VERBOSE("open file is ok\n");

    for (int i = 0; i < streamNum; i++) {
        VERBOSE("video stream_idx:%d, CodecID:%d, width:%d, height:%d, format:%d, gop:%d, bitrate:%d\n",
                mFormatCtx->streams[i]->id,
                mFormatCtx->streams[i]->codec->codec_id,
                mFormatCtx->streams[i]->codec->width,
                mFormatCtx->streams[i]->codec->height,
                mFormatCtx->streams[i]->codec->pix_fmt,
                mFormatCtx->streams[i]->codec->gop_size,
                mFormatCtx->streams[i]->codec->bit_rate);
         VERBOSE("audio stream_idx:%d, CodecID:%d, format:%d, sample_rate:%d, channles:%d, bit_rate:%d\n",
                mFormatCtx->streams[i]->id,
                mFormatCtx->streams[i]->codec->codec_id,
                mFormatCtx->streams[i]->codec->sample_fmt,
                mFormatCtx->streams[i]->codec->sample_rate,
                mFormatCtx->streams[i]->codec->channels,
                mFormatCtx->streams[i]->codec->bit_rate);
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::writeWebFile(MediaBufferSP buffer, TypeEnum type,
                                           uint8_t *avcHeader, int avcSize)
{
    ENTER();
    AVPacket pkt;
    AVRational streamTB;
    uint8_t *data = NULL;
    int32_t offset = 0, dataSize = 0, curStreamIdx;
#if GET_TIMES_INFO
    int64_t time1 = av_gettime();
#endif
    av_init_packet(&pkt);

    if ( type==VideoType ) {
        curStreamIdx = 0;
        streamTB = mVTimeBase;
    } else {
        streamTB = mATimeBase;
        if ( mHasVideoTrack )
            curStreamIdx = 1;
        else
            curStreamIdx = 0;
    }

    buffer->getBufferInfo((uintptr_t *)&data, &offset, &dataSize, 1);
    if ( !data ) {
        ERROR("data is null\n");
        return MM_ERROR_OP_FAILED;
    }
    dataSize = buffer->size();
    VERBOSE("curStreamIdx:%d, offset:%d, dataSize:%d\n", curStreamIdx, offset, dataSize);
    pkt.stream_index = curStreamIdx;
    pkt.data = data + offset;
    pkt.size = dataSize - offset;
    if ( type==VideoType ) {
       if ( buffer->isFlagSet(MediaBuffer::MBFT_KeyFrame) ) {
           pkt.flags |= AV_PKT_FLAG_KEY;
           memcpy(data+offset-avcSize, avcHeader, avcSize);
           pkt.data = data + offset - avcSize;
           pkt.size = dataSize - offset + avcSize;
           VERBOSE("copy video IDR frame length:%d\n",pkt.size);
       }
       else
           pkt.flags &= ~AV_PKT_FLAG_KEY;
    } else
       pkt.flags |= AV_PKT_FLAG_KEY;

#if DUMP_H264_DATA
    if ( mH264Fp && ( type == VideoType ) ) {
        fwrite(pkt.data, 1, pkt.size, mH264Fp);
        fflush(mH264Fp);
    }
#endif
#if DUMP_AAC_DATA
    if ( mAacFp &&  (type == AudioType) ) {
        fwrite(pkt.data, 1, pkt.size, mAacFp);
        fflush(mAacFp);
    }
#endif
    pkt.pts = convertTime(buffer->pts(), streamTB,
                          mFormatCtx->streams[curStreamIdx]->time_base);
    pkt.dts = convertTime(buffer->dts(), streamTB,
                          mFormatCtx->streams[curStreamIdx]->time_base);
    mWroteDts = miniChangeTS(mWroteDts, &pkt.dts, &pkt.pts);
    pkt.duration = convertTime(buffer->duration(), streamTB,
                               mFormatCtx->streams[curStreamIdx]->time_base);
    pkt.pos = -1;

    INFO("write buffer id:%d,size:%d,pts:%" PRId64 ",dts:%" PRId64 ",wroteDts:%" PRId64 "\n",
             pkt.stream_index, pkt.size, pkt.pts, pkt.dts, mWroteDts);

    int ret = av_interleaved_write_frame(mFormatCtx,&pkt);
    if (ret != 0) {
        ERROR("write file faild:%d\n", ret);
        pkt.size = -1;
        av_free_packet(&pkt);
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }

    av_free_packet(&pkt);
#if GET_TIMES_INFO
    int64_t time2 = av_gettime();
    INFO("write the buffer time:%" PRId64 "\n", time2-time1);
#endif
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpMuxerSink::closeWebFile()
{
    ENTER();

    if (mFormatCtx) {
        av_write_trailer(mFormatCtx);
        if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE))
            avio_close(mFormatCtx->pb);

        avformat_free_context(mFormatCtx);
        mFormatCtx->oformat = NULL;
    }

    mWroteDts = -1;
    mFormatCtx = NULL;

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::RtpMuxerSink *sinkComponent = new YUNOS_MM::RtpMuxerSink(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}

void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}

}
