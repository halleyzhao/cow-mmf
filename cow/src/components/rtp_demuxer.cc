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
#include <multimedia/av_ffmpeg_helper.h>
#include "third_helper.h"

#include "rtp_demuxer.h"
#include "multimedia/component.h"

namespace YUNOS_MM
{

DEFINE_LOGTAG(RtpDemuxer)
DEFINE_LOGTAG(RtpDemuxer::RtpDemuxerBuffer)
DEFINE_LOGTAG(RtpDemuxer::RtpDemuxerReader)
DEFINE_LOGTAG(RtpDemuxer::ReaderThread)
#ifdef VERBOSE
#undef VERBOSE
#endif
#define VERBOSE WARNING
#define ENTER() VERBOSE(">>>\n")
#define FLEAVE() do {VERBOSE(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

static const char * COMPONENT_NAME = "RtpDemuxer";
static const char * MMTHREAD_NAME = "RtpDemuxer::ReaderThread";

#define TrafficControlLowBarAudio     1
#define TrafficControlHighBarAudio    40
#define TrafficControlLowBarVideo     1
#define TrafficControlHighBarVideo    20
#define RTP_READ_NUM                  10
#define OPEN_RTP_STREAM_TIME          "2000"
#define MAX_NDIR_FRAME_NUM            50

#define RDS_MSG_prepare               (msg_type)1
#define RDS_MSG_start                 (msg_type)2
#define RDS_MSG_stop                  (msg_type)3
#define RDS_MSG_reset                 (msg_type)4

BEGIN_MSG_LOOP(RtpDemuxer)
    MSG_ITEM(RDS_MSG_prepare, onPrepare)
    MSG_ITEM(RDS_MSG_start, onStart)
    MSG_ITEM(RDS_MSG_stop, onStop)
    MSG_ITEM(RDS_MSG_reset, onReset)
END_MSG_LOOP()

#define GET_TIMES_INFO           1

///////////////RtpDemuxer::RtpDemuxerBuffer/////////////////////
RtpDemuxer::RtpDemuxerBuffer::RtpDemuxerBuffer(RtpDemuxer *source, MediaType type):
      mType(-1)
    , mSource(NULL)
{
    ENTER();
    if (type == Component::kMediaTypeVideo)
       mMonitorWrite.reset(new TrafficControl(
                            TrafficControlLowBarVideo,
                            TrafficControlHighBarVideo,
                            "RtpDemuxerBuffer"));
    else if (type == Component::kMediaTypeAudio)
       mMonitorWrite.reset(new TrafficControl(
                            TrafficControlLowBarAudio,
                            TrafficControlHighBarAudio,
                            "RtpDemuxerBuffer"));
    else {
        ERROR("MediaType :%d is error\n", type);
        FLEAVE();
    }

    mSource = source;
    mType = type;

    FLEAVE();
}

RtpDemuxer::RtpDemuxerBuffer::~RtpDemuxerBuffer()
{
    ENTER();

    while(!mBuffer.empty()) {
       mBuffer.pop();
    }

    FLEAVE();
}

MediaBufferSP RtpDemuxer::RtpDemuxerBuffer::readBuffer()
{
    ENTER();

    MMAutoLock locker(mSource->mLock);

    if (!mBuffer.empty()) {
       MediaBufferSP buffer = mBuffer.front();
       mBuffer.pop();
       return buffer;
    }

    return MediaBufferSP((MediaBuffer*)NULL);
}

mm_status_t RtpDemuxer::RtpDemuxerBuffer::writeBuffer(MediaBufferSP buffer)
{
    ENTER();

    if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        VERBOSE("Notify kEventEOS:%d\n", mType);
        mSource->notify(kEventEOS, 0, 0, nilParam);
        FLEAVE_WITH_CODE(MM_ERROR_EOS);
    }

    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    trafficControlWrite->waitOnFull();

    {
        MMAutoLock locker(mSource->mLock);

        buffer->setMonitor(mMonitorWrite);
        mBuffer.push(buffer);
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

int RtpDemuxer::RtpDemuxerBuffer::size()
{
    return mBuffer.size();
}

///////////////RtpDemuxer::RtpDemuxerReader////////////////////
mm_status_t RtpDemuxer::RtpDemuxerReader::read(MediaBufferSP & buffer)
{
    ENTER();

    VERBOSE("RtpDemuxerReader::read:%d\n", mType);

    if (mType == Component::kMediaTypeVideo && mSource && mSource->mVideoBuffer)
        buffer = mSource->mVideoBuffer->readBuffer();
    else if (mType == Component::kMediaTypeAudio && mSource && mSource->mAudioBuffer)
        buffer = mSource->mAudioBuffer->readBuffer();
    else {
        ERROR("type:%d is error\n", mType);
        FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
    }

    {
       MMAutoLock locker(mSource->mLock);
       mSource->mCondition.signal();
    }

    if (!buffer.get()) {
        FLEAVE_WITH_CODE(MM_ERROR_AGAIN);
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

MediaMetaSP RtpDemuxer::RtpDemuxerReader::getMetaData()
{
    ENTER();

    VERBOSE("now, read meta data:%d\n", mType);
    MediaMetaSP meta = MediaMeta::create();
    if (!meta.get()) {
        ERROR("meta create unsuccessful\n");
        return ( MediaMetaSP((MediaMeta*)NULL) );
    }

    mm_status_t ret = mSource->getStreamParameter(meta, mType);
    if (ret != MM_ERROR_SUCCESS)
       return ( MediaMetaSP((MediaMeta*)NULL) );

    return meta;
}

/////////////////RtpDemuxer::ReaderThread////////////////////
RtpDemuxer::ReaderThread::ReaderThread(RtpDemuxer *source)
                                          : MMThread(MMTHREAD_NAME),
                                            mSource(source),
                                            mContinue(true),
                                            mVideoPktNum(0),
                                            mCorruptedVideoPktNum(0),
                                            mSkipCorruptedPtk(false)
{
    ENTER();
    FLEAVE();
}

RtpDemuxer::ReaderThread::~ReaderThread()
{
    ENTER();
    FLEAVE();
}

void RtpDemuxer::ReaderThread::signalExit()
{
    ENTER();

    {
        MMAutoLock locker(mSource->mLock);
        mContinue = false;
        mSource->mCondition.signal();
    }
    usleep(1000*1000);
    destroy();
    FLEAVE();
}

void RtpDemuxer::ReaderThread::signalContinue()
{
    ENTER();
    mSource->mCondition.signal();
    FLEAVE();
}

void RtpDemuxer::ReaderThread::main()
{
    ENTER();
    AVRational timeBaseQ = {1, AV_TIME_BASE};
    bool skipPacket = true;
    int readNDIRFrameCnt = MAX_NDIR_FRAME_NUM;

    mVideoPktNum = 0;
    mCorruptedVideoPktNum = 0;
    mSkipCorruptedPtk = mm_check_env_str("rtp.demuxer.skip.corrupt", NULL, "1", false);

    while(1) {
        //#0 check the end command
        {
            MMAutoLock locker(mSource->mLock);
            if (!mContinue) {
                break;
            }

            if (mSource->mIsPaused) {
               INFO("pause wait");
               mSource->mCondition.wait();
               INFO("pause wait wakeup");
            }
        }

        //#1 read the packet
        AVPacket *packet = NULL;
        int ret,t;

        struct timespec in_time, out_time;
        clock_gettime(CLOCK_MONOTONIC, &in_time);
        ret = mSource->readRtpStream(&packet);
        clock_gettime(CLOCK_MONOTONIC, &out_time);
        t = (out_time.tv_sec*1000000+out_time.tv_nsec/1000LL - in_time.tv_sec*1000000 -in_time.tv_nsec/1000LL);
        //check the eof, maybe rtsp stream has not eof
        if (ret == AVERROR_EOF) {
            av_free_packet (packet);
            av_free(packet);
            packet = NULL;
            //create one eof mediabuffer for video buffer
            MediaBufferSP videoBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
            if ( !videoBuffer.get() ) {
               VERBOSE("failed to createMediaBuffer\n");
               break;
            }
            videoBuffer->setFlag(MediaBuffer::MBFT_EOS);
            videoBuffer->setSize(0);
            mSource->mVideoBuffer->writeBuffer(videoBuffer);
            //create one eof mediabuffer for audio buffer
            MediaBufferSP audioBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
            if ( !audioBuffer.get() ) {
               VERBOSE("failed to createMediaBuffer\n");
               break;
            }
            audioBuffer->setFlag(MediaBuffer::MBFT_EOS);
            audioBuffer->setSize(0);
            mSource->mAudioBuffer->writeBuffer(audioBuffer);
        }else if (ret < 0) {
            ERROR("get one frame with ret %d", ret);
            usleep(1000*16);
            continue;
        }else {
            VERBOSE("avpacket %p:ret:%d,size:%d,index:%d,pts:%" PRId64 ",dts:%" PRId64 ",time:%d, flags:%d\n",
                    packet, ret, packet->size, packet->stream_index, packet->pts, packet->dts, t, packet->flags);
            mSource->checkRequestIDR(packet, readNDIRFrameCnt);
            //find the first video key frame
            if (skipPacket) {
                if ((packet->stream_index == mSource->mVideoIndex) &&
                     (packet->flags & AV_PKT_FLAG_KEY)) {
                    VERBOSE("find the rtsp video key frame\n");
                    skipPacket = false;
                }
            }
            //skip some packet
            if (skipPacket) {
                VERBOSE("skip current packet\n");
                av_free_packet (packet);
                av_free(packet);
                continue;
            }
            if (packet->stream_index == mSource->mVideoIndex) { //video
                mVideoPktNum++;
                if (packet->flags & AV_PKT_FLAG_CORRUPT) {
                    mCorruptedVideoPktNum++;
                    INFO("received %d video pkt, corrupt num %d", mVideoPktNum, mCorruptedVideoPktNum);
                    if (mSkipCorruptedPtk) {
                        INFO("skip corrupted video pkt");
                        av_free_packet (packet);
                        av_free(packet);
                        continue;
                    }
                }
                if (packet->pts == int64_t(AV_NOPTS_VALUE)) {
                    packet->pts = packet->dts = mSource->mVideoPts + 50;
                    mSource->mVideoPts += 50;
                } else {
                    av_packet_rescale_ts(packet,
                                         mSource->mFormatCtx->streams[mSource->mVideoIndex]->codec->time_base,
                                         timeBaseQ);
                    mSource->mVideoPts = packet->pts;
               }
               MediaBufferSP videoBuffer = AVBufferHelper::createMediaBuffer(packet, true);
               if ( !videoBuffer.get() ) {
                   ERROR("failed to createMediaBuffer\n");
                   av_free_packet (packet);
                   av_free(packet);
                   break;
               }
               if (packet->flags & AV_PKT_FLAG_KEY)
                   videoBuffer->isFlagSet(MediaBuffer::MBFT_KeyFrame);
               mSource->mVideoBuffer->writeBuffer(videoBuffer);
               VERBOSE("now write video packet into video buffer: pts:%" PRId64 ", dts:%" PRId64 ", level:%d\n", packet->pts, packet->dts, mSource->mVideoBuffer->size());
            } else if (packet->stream_index == mSource->mAudioIndex) { //audio
               if (packet->pts == int64_t(AV_NOPTS_VALUE)) {
                   packet->pts = mSource->mAudioPts + 21;
                   mSource->mAudioPts += 21;
               } else {
                   av_packet_rescale_ts(packet,
                                        mSource->mFormatCtx->streams[mSource->mAudioIndex]->codec->time_base,
                                        timeBaseQ);
                   mSource->mAudioPts = packet->pts;
               }
               MediaBufferSP audioBuffer = AVBufferHelper::createMediaBuffer(packet, true);
               if ( !audioBuffer.get() ) {
                   ERROR("failed to createMediaBuffer\n");
                   av_free_packet (packet);
                   av_free(packet);
                   break;
               }
               if (packet->flags & AV_PKT_FLAG_KEY)
                   audioBuffer->isFlagSet(MediaBuffer::MBFT_KeyFrame);
               mSource->mAudioBuffer->writeBuffer(audioBuffer);
               VERBOSE("now write audio packet into audio buffer, pts%" PRId64 ", dts:%" PRId64 ", level:%d\n", packet->pts, packet->dts, mSource->mAudioBuffer->size());
            } else { //no audio && no video
               VERBOSE("this packet is video nor audio:%d,%d,%d\n",
                        packet->stream_index, mSource->mVideoIndex, mSource->mAudioIndex);
               av_free_packet (packet);
               av_free(packet);
               break;
            }
        }// ret != eof
        usleep(5*1000);
    }//    while(1) {

    VERBOSE("Output thread exited\n");
    FLEAVE();
}

/////////////////////////RtpDemuxer////////////////////////
RtpDemuxer::RtpDemuxer()
           :MMMsgThread(COMPONENT_NAME),
            mCondition(mLock),
            mIsPaused(true),
            mExitFlag(false),
            mHasVideoTrack(false),
            mHasAudioTrack(false),
            mVideoIndex(-1),
            mAudioIndex(-1),
            mFormatCtx(NULL),
            mWidth(-1),
            mHeight(-1),
            mVideoPts(-50),
            mAudioPts(-21)
{
    ENTER();
    mVTimeBase = {0};
    mATimeBase = {0};

    class AVInitializer {
    public:
        AVInitializer() {
            MMLOGI("use log level converter\n");
            av_log_set_callback(AVLogger::av_log_callback);
            av_log_set_level(AV_LOG_ERROR);
            avcodec_register_all();
            av_register_all();
            avformat_network_init();
        }
        ~AVInitializer() {
            av_log_set_callback(NULL);
            avformat_network_deinit();
        }
    };
    static AVInitializer sAVInit;

    FLEAVE();
}

RtpDemuxer::~RtpDemuxer()
{
    ENTER();

    FLEAVE();
}

mm_status_t RtpDemuxer::init()
{
    ENTER();

    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret) {
        ERROR("rtp demuxer init error:%d\n", ret);
        FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
    }

    FLEAVE_WITH_CODE( MM_ERROR_SUCCESS);
}

void RtpDemuxer::uninit()
{
    ENTER();

    MMMsgThread::exit();

    FLEAVE();
}

mm_status_t RtpDemuxer::setUri(const char * uri, const std::map<std::string,
                                 std::string> * headers/* = NULL*/)
{
    ENTER();

    MMAutoLock locker(mLock);

    VERBOSE("uri: %s\n", uri);

    mRtpURL = uri;

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpDemuxer::prepare()
{
    ENTER();

    postMsg(RDS_MSG_prepare, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void RtpDemuxer::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);
    //#0 check url
    if (mRtpURL.empty()) {
        ERROR("please call setUrl to set the mRtpURL\n");
        notify(kEventPrepareResult, MM_ERROR_INVALID_URI, 0, nilParam);
        FLEAVE();
    }

    //#1 initiate the libav
    //av_register_all();
    //avformat_network_init();

    mm_status_t ret = openRtpStream();

    notify(kEventPrepareResult, ret, 0, nilParam);
    FLEAVE();
}

mm_status_t RtpDemuxer::start()
{
    ENTER();

    postMsg(RDS_MSG_start, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void RtpDemuxer::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);

    //#0 create the mux thread into pause state
    if (!mReaderThread) {
        mReaderThread.reset (new ReaderThread(this), MMThread::releaseHelper);
        mReaderThread->create();
    }
    mIsPaused = false;

    mReaderThread->signalContinue();

    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE();
}

mm_status_t RtpDemuxer::stop()
{
    ENTER();

    postMsg(RDS_MSG_stop, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

mm_status_t RtpDemuxer::internalStop()
{
    ENTER();

    mExitFlag = true;
    mIsPaused = true;
    if (mReaderThread) {
        mReaderThread->signalExit();
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

        closeRtpStream();
     }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

void  RtpDemuxer::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mm_status_t ret = internalStop();
    notify(kEventStopped, ret, 0, nilParam);
    FLEAVE();
}

mm_status_t RtpDemuxer::reset()
{
    ENTER();

    postMsg(RDS_MSG_reset, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void RtpDemuxer::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mm_status_t ret = internalStop();

    {
        MMAutoLock locker(mLock);
        if ( mVideoBuffer.get() )
            mVideoBuffer.reset();

        if ( mAudioBuffer.get() )
            mAudioBuffer.reset();

        mHasVideoTrack = mHasAudioTrack = false;
    }

    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);

    FLEAVE();
}

mm_status_t RtpDemuxer::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    mm_status_t ret;

    ret = meta->getInt32(MEDIA_ATTR_WIDTH, mWidth);
    if (!ret) {
        ERROR("read mWidth is error\n");
        FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
    }

    ret = meta->getInt32(MEDIA_ATTR_HEIGHT, mHeight);
    if (!ret) {
        ERROR("read mHeight is error\n");
        FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
    }

    INFO("get the width:%d, height:%d\n", mWidth, mHeight);

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t RtpDemuxer::getParameter(MediaMetaSP & meta) const
{
    ENTER();

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

Component::ReaderSP RtpDemuxer::getReader(MediaType mediaType)
{
    ENTER();

    if ((int)mediaType == Component::kMediaTypeVideo)
        return Component::ReaderSP(new RtpDemuxer::RtpDemuxerReader(this, Component::kMediaTypeVideo));
    else if ((int)mediaType == Component::kMediaTypeAudio)
        return Component::ReaderSP(new RtpDemuxer::RtpDemuxerReader(this, Component::kMediaTypeAudio));
    else {
        ERROR("not supported mediatype: %d\n", mediaType);
    }

    return Component::ReaderSP((Component::Reader*)NULL);
}

const std::list<std::string> & RtpDemuxer::supportedProtocols() const
{
    static std::list<std::string> protocols;

    std::string str("rtp");
    protocols.push_back(str);
    return protocols;
}

MMParamSP RtpDemuxer::getTrackInfo()
{
    MMAutoLock locker(mLock);

    MMParamSP param(new MMParam());
    if ( !param ) {
        ERROR("no memory for MMParam\n");
        return nilParam;
    }

    // stream count
    int streamCount = 0;
    if (mHasVideoTrack)
        streamCount ++;
    if (mHasAudioTrack)
        streamCount ++;
    param->writeInt32(streamCount);
    VERBOSE("stream count: %d\n", streamCount);

    // every stream
    for ( int i = 0; i < mFormatCtx->nb_streams; ++i ) {
        int width = 0, height = 0;
        if (i == mVideoIndex)
            param->writeInt32(kMediaTypeVideo); //stream_idx
        else if (i == mAudioIndex)
            param->writeInt32(kMediaTypeAudio);
        param->writeInt32(1); //number of stream_idx
        param->writeInt32(0); //[0..number -1]
        AVStream * stream = mFormatCtx->streams[i];
        // codec
        CowCodecID codecId;
        const char * codecName;
        AVCodecContext *ctx = stream->codec;
        if (ctx) {
            VERBOSE("has ctx\n");
            codecId = (CowCodecID)ctx->codec_id;
            codecName = avcodec_descriptor_get(ctx->codec_id)->name;
            VERBOSE("codecId: %d, codecName: %s\n", codecId, codecName);
        } else {
            codecId = kCodecIDNONE;
            codecName = "";
        }
        param->writeInt32(codecId);
        param->writeCString(codecName);
        param->writeCString(codecId2Mime((CowCodecID)codecId));

        // title
        AVDictionaryEntry * tagTitle = av_dict_get(stream->metadata, "title", NULL, 0);
        VERBOSE("getting title:%s\n", tagTitle);
#define WRITE_TAG(_tag) do {\
        if (_tag)\
        param->writeCString(_tag->value);\
        else\
        param->writeCString("");\
        }while(0)
       WRITE_TAG(tagTitle);

        // lang
        VERBOSE("getting lang\n");
        AVDictionaryEntry * tagLang = av_dict_get(stream->metadata, "language", NULL, 0);
        if (!tagLang)
            tagLang = av_dict_get(stream->metadata, "lang", NULL, 0);
        WRITE_TAG(tagLang);
        if (i == mVideoIndex) {
            width = ctx->width;
            height = ctx->height;
            param->writeInt32(width);
            param->writeInt32(height);
        }
        VERBOSE("id: %d(%d), title: %s, lang: %s, codecId: %d, codecName: %s, width:%d, height:%d\n",
            i, mFormatCtx->nb_streams, tagTitle ? tagTitle->value : "",
            tagLang ? tagLang->value : "",
            codecId, codecName, width, height);
    }

    return param;
}

/*static*/ int RtpDemuxer::exitFunc(void *handle)
{
    //ENTER();

    //VERBOSE("now check the exit func:%p\n", handle);

    RtpDemuxer * demuxer = (RtpDemuxer *)handle;
    if (demuxer) {
        //VERBOSE("now check the exit command:%p\n", demuxer->mExitFlag);
        if (!demuxer->mExitFlag)
            return 0;
        else {
            VERBOSE("now check the exit command:%p\n", demuxer->mExitFlag);
            return 1;
        }
    }

    return 0;
}

mm_status_t RtpDemuxer::openRtpStream()
{
    ENTER();

    //#1 open the rtp url stream
    mFormatCtx = avformat_alloc_context();
    if( !mFormatCtx ){
        ERROR("malloc memory for formatcontext:faild\n");
        FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
    }
    mFormatCtx->interrupt_callback.callback = exitFunc;
    mFormatCtx->interrupt_callback.opaque = this;
    int i, j, ret = -1;
    bool openRtp = false;
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "timeout", OPEN_RTP_STREAM_TIME, 0);
    for(i = 0; i < RTP_READ_NUM; i++) {
        struct timeval in_time,out_time;
        gettimeofday(&in_time, NULL);
        ret = avformat_open_input( &mFormatCtx, mRtpURL.c_str(), NULL, &opts);
        gettimeofday(&out_time, NULL);
        int t = (out_time.tv_sec*1000000+out_time.tv_usec - in_time.tv_sec*1000000 -in_time.tv_usec);
        DEBUG("avformat_open_input i:%d, ret:%d, url:%s,time:%d\n",i, ret, mRtpURL.c_str(), t);
        if (ret) {
            VERBOSE("read the rtp stream faild,ret:%d\n", ret);
            continue;
        }

        DEBUG("av input format name %s", mFormatCtx->iformat->name);
        if (avformat_find_stream_info(mFormatCtx, NULL) < 0) {
            ERROR("fail to find out stream info\n");
            avformat_close_input(&mFormatCtx);
            continue;
        } else if (!mFormatCtx->nb_streams) {
            ERROR("not find the correct AV: nb_streams:%d\n", mFormatCtx->nb_streams);
            avformat_close_input(&mFormatCtx);
            continue;
        }

        VERBOSE("avformat_find_stream_info nb_streams:%d\n", mFormatCtx->nb_streams);
        av_dump_format(mFormatCtx, 0, mRtpURL.c_str(), 0);

        for (j=0; j < mFormatCtx->nb_streams; j++) {
            if (mFormatCtx->streams[j]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
                mHasVideoTrack = true;
                mVideoIndex = j;

            } else if (mFormatCtx->streams[j]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
                mHasAudioTrack= true;
                mAudioIndex = j;
            }
        }
        if (mHasVideoTrack || mHasAudioTrack) {
            openRtp = true;
            break;
        }
    }//for (i = 0; ...)

    if (!openRtp) {
        if (mFormatCtx)
            avformat_free_context(mFormatCtx);
        mFormatCtx = NULL;
        FLEAVE_WITH_CODE( MM_ERROR_IVALID_OPERATION );
    }

    if (mHasVideoTrack) {

        if (mVideoBuffer.get())
            mVideoBuffer.reset();
            RtpDemuxerBuffer *videoBuffer = NULL;
            videoBuffer = new RtpDemuxerBuffer( this, kMediaTypeVideo );
            if (!videoBuffer) {
                ERROR("new video RtpDemuxerReader failed\n");
                FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
            }
        mVideoBuffer.reset(videoBuffer);

        INFO("video info:codecid:%d, width:%d(%d), height:%d(%d), time(%d,%d), extradata_size:%d\n",
             mFormatCtx->streams[mVideoIndex]->codec->codec_id,
             mFormatCtx->streams[mVideoIndex]->codec->width, mWidth,
             mFormatCtx->streams[mVideoIndex]->codec->height, mHeight,
             mFormatCtx->streams[mVideoIndex]->codec->time_base.num,
             mFormatCtx->streams[mVideoIndex]->codec->time_base.den,
             mFormatCtx->streams[mVideoIndex]->codec->extradata_size);
        for( j = 0; j < mFormatCtx->streams[mVideoIndex]->codec->extradata_size; j++)
            INFO("0x%x,", mFormatCtx->streams[mVideoIndex]->codec->extradata[j]);

        if ( mWidth > 0) {
            if (mFormatCtx->streams[mVideoIndex]->codec->width <= 0)
                mFormatCtx->streams[mVideoIndex]->codec->width = mWidth;
            else {
                if (mFormatCtx->streams[mVideoIndex]->codec->width != mWidth) {
                    ERROR("width in the stream:%d != mWidth:%d\n",
                           mFormatCtx->streams[mVideoIndex]->codec->width,
                           mWidth);
                    FLEAVE_WITH_CODE( MM_ERROR_IVALID_OPERATION );
                }
            }
        }

        if ( mHeight > 0) {
            if (mFormatCtx->streams[mVideoIndex]->codec->height <= 0)
                mFormatCtx->streams[mVideoIndex]->codec->height = mHeight;
            else {
                if (mFormatCtx->streams[mVideoIndex]->codec->height != mHeight) {
                    ERROR("height in the stream:%d != mHeight:%d\n",
                           mFormatCtx->streams[mVideoIndex]->codec->height,
                           mHeight);
                    FLEAVE_WITH_CODE( MM_ERROR_IVALID_OPERATION );
                }
            }
        }
        mFormatCtx->streams[mVideoIndex]->codec->time_base.num = 1,
        mFormatCtx->streams[mVideoIndex]->codec->time_base.den = 90000;
    }
    //FIXME: sometimes don't get the key parameters

    if(mHasAudioTrack) {
        if (mAudioBuffer.get())
            mAudioBuffer.reset();
        RtpDemuxerBuffer *audioBuffer = NULL;
        audioBuffer = new RtpDemuxerBuffer( this, kMediaTypeAudio );
        if (!audioBuffer) {
            ERROR("new video RtpDemuxerReader failed\n");
            FLEAVE_WITH_CODE(MM_ERROR_NO_MEM);
        }
        mAudioBuffer.reset(audioBuffer);
        INFO("audio info:codecid:%d, sample_fmt:%d, sample_rate:%d, channels:%d,time:(%d,%d), extradata_size:%d\n",
             mFormatCtx->streams[mAudioIndex]->codec->codec_id,
             mFormatCtx->streams[mAudioIndex]->codec->sample_fmt,
             mFormatCtx->streams[mAudioIndex]->codec->sample_rate,
             mFormatCtx->streams[mAudioIndex]->codec->channels,
             mFormatCtx->streams[mAudioIndex]->codec->time_base.num,
             mFormatCtx->streams[mAudioIndex]->codec->time_base.den,
             mFormatCtx->streams[mAudioIndex]->codec->extradata_size);
        for( j = 0; j < mFormatCtx->streams[mAudioIndex]->codec->extradata_size; j++)
            INFO("0x%x,", mFormatCtx->streams[mAudioIndex]->codec->extradata[j]);

        if (!mFormatCtx->streams[mAudioIndex]->codec->extradata_size) {
            mFormatCtx->streams[mAudioIndex]->codec->extradata_size = 5;
            uint8_t * data = (uint8_t*)av_malloc(5);
            data[0] = 0x12;
            data[1] = 0x10;
            data[2] = 0x56;
            data[3] = 0xE5;
            data[4] = 0x00;
            mFormatCtx->streams[mAudioIndex]->codec->extradata = data;
            INFO("write the extra data into audio\n");
        }

        mFormatCtx->streams[mAudioIndex]->codec->time_base.num = 1;
        mFormatCtx->streams[mAudioIndex]->codec->time_base.den = 90000;
    }
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

int RtpDemuxer::readRtpStream(AVPacket** pkt)
{
    ENTER();
    AVPacket * packet = NULL;
    int ret;

    packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    if (!packet) {
        ERROR("malloc memory for packet failed\n");
        return -1;
    }

    av_init_packet(packet);
    ret = av_read_frame(mFormatCtx, packet); //0: ok, <0: error/end
    VERBOSE("read one rtp frame ret:%d,idx:%d,size:%d,pts:%" PRId64 ",key:%d\n",
                ret, packet->stream_index, packet->size, packet->pts, packet->flags);
    //check the result
    if ((ret < 0) && (ret != AVERROR_EOF)) {
       av_free_packet (packet);
       av_free( packet );
       ERROR("read one packet failed:%d\n", ret);
    } else {
        *pkt = packet;
    }
    return ret;
}

void RtpDemuxer::checkRequestIDR(AVPacket *packet, int& readNDIRFrameCnt)
{
    ENTER();

    if (packet->stream_index == mVideoIndex) {
        if (packet->flags & AV_PKT_FLAG_KEY)
           readNDIRFrameCnt = 0;
        else
           readNDIRFrameCnt ++;
    }

    if (readNDIRFrameCnt >= MAX_NDIR_FRAME_NUM) {
        notify(kEventRequestIDR, 0, 0, nilParam);
        readNDIRFrameCnt = 0;
        return;
    }

    return;
}

void RtpDemuxer::closeRtpStream()
{
    ENTER();

    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = NULL;
    }

    FLEAVE();
}

/*static */snd_format_t RtpDemuxer::convertAudioFormat(AVSampleFormat avFormat)
{
#undef item
#define item(_av, _audio) \
    case _av:\
        VERBOSE("%s -> %s\n", #_av, #_audio);\
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

mm_status_t RtpDemuxer::getStreamParameter(MediaMetaSP & meta, MediaType type)
{
    ENTER();
    mm_status_t idx;

    //#0 check video && audio
    if ((type != kMediaTypeVideo) &&
         (type != kMediaTypeAudio)) {
        ERROR("type is error:%d\n", type);
        FLEAVE_WITH_CODE( MM_ERROR_INVALID_PARAM);
    }

    if (type == kMediaTypeVideo)
        idx = mVideoIndex;
    else
        idx = mAudioIndex;

    AVCodecContext * codecContext = mFormatCtx->streams[idx]->codec;
    VERBOSE("codecContext: codec_type: %d, codec_id: 0x%x, codec_tag: 0x%x, stream_codec_tag: 0x%x, profile: %d, width: %d, height: %d, extradata: %p, extradata_size: %d, channels: %d, sample_rate: %d, channel_layout: %" PRId64 ", bit_rate: %d, block_align: %d, avg_frame_rate: (%d, %d)\n",
                    codecContext->codec_type,
                    codecContext->codec_id,
                    codecContext->codec_tag,
                    codecContext->stream_codec_tag,
                    codecContext->profile,
                    codecContext->width,
                    codecContext->height,
                    codecContext->extradata,
                    codecContext->extradata_size,
                    codecContext->channels,
                    codecContext->sample_rate,
                    codecContext->channel_layout,
                    codecContext->bit_rate,
                    codecContext->block_align,
                    mFormatCtx->streams[idx]->avg_frame_rate.num,
                    mFormatCtx->streams[idx]->avg_frame_rate.den);

    if ( type == kMediaTypeVideo ) {
        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        AVRational * avr = &mFormatCtx->streams[idx]->avg_frame_rate;
        if ( avr->num > 0 && avr->den > 0 ) {
            VERBOSE("has avg fps: %d\n", avr->num / avr->den);
            meta->setInt32(MEDIA_ATTR_AVG_FRAMERATE, avr->num / avr->den);
        }
        meta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);
        meta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        meta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        meta->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        meta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        meta->setInt32(MEDIA_ATTR_WIDTH, codecContext->width);
        meta->setInt32(MEDIA_ATTR_HEIGHT, codecContext->height);
        meta->setInt32(MEDIA_ATTR_CODEC_DISABLE_HW_RENDER, 1);
        if (mFormatCtx->iformat && mFormatCtx->iformat->name)
            meta->setString(MEDIA_ATTR_CONTAINER, mFormatCtx->iformat->name);

    } else if ( type == kMediaTypeAudio ) {
        meta->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        if ( codecContext->extradata && codecContext->extradata_size > 0 ) {
            meta->setByteBuffer(MEDIA_ATTR_CODEC_DATA, codecContext->extradata, codecContext->extradata_size);
        }
        meta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);
        meta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
        meta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
        meta->setInt32(MEDIA_ATTR_STREAMCODECTAG, codecContext->stream_codec_tag);
        meta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
        meta->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, convertAudioFormat(codecContext->sample_fmt));
        meta->setInt32(MEDIA_ATTR_SAMPLE_RATE, codecContext->sample_rate);
        meta->setInt32(MEDIA_ATTR_CHANNEL_COUNT, codecContext->channels);
        meta->setInt64(MEDIA_ATTR_CHANNEL_LAYOUT, codecContext->channel_layout);
        meta->setInt32(MEDIA_ATTR_BIT_RATE, codecContext->bit_rate);
        meta->setInt32(MEDIA_ATTR_BLOCK_ALIGN, codecContext->block_align);
        meta->setInt32(MEDIA_ATTR_IS_ADTS, 1);
    }

    meta->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
    meta->setPointer(MEDIA_ATTR_CODEC_CONTEXT, codecContext);
    meta->setInt32(MEDIA_ATTR_CODECPROFILE, codecContext->profile);
    meta->setInt32(MEDIA_ATTR_CODECTAG, codecContext->codec_tag);
    meta->setString(MEDIA_ATTR_MIME, codecId2Mime((CowCodecID)codecContext->codec_id));
    meta->setInt32(MEDIA_ATTR_CODECID, codecContext->codec_id);

    FLEAVE_WITH_CODE( MM_ERROR_SUCCESS );
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::RtpDemuxer *sourceComponent = new YUNOS_MM::RtpDemuxer();
    if (sourceComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sourceComponent);
}

void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}

}

