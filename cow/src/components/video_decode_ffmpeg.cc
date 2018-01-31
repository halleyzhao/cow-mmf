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

#include "video_decode_ffmpeg.h"

namespace YUNOS_MM
{
#ifdef _VIDEO_CODEC_FFMPEG
MM_LOG_DEFINE_MODULE_NAME("VDFF")

static const char * COMPONENT_NAME = "VideoDecodeFFmpeg";
static const char * MMTHREAD_NAME = "VideoDecodeFFmpeg::DecodeThread";

const int TrafficControlLowBar = 5;
const int TrafficControlHighBar = 10;

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define VDFF_MSG_prepare (msg_type)1
#define VDFF_MSG_start (msg_type)2
#define VDFF_MSG_resume (msg_type)3
#define VDFF_MSG_pause (msg_type)4
#define VDFF_MSG_stop (msg_type)5
#define VDFF_MSG_flush (msg_type)6
#define VDFF_MSG_reset (msg_type)7

BEGIN_MSG_LOOP(VideoDecodeFFmpeg)
    MSG_ITEM(VDFF_MSG_prepare, onPrepare)
    MSG_ITEM(VDFF_MSG_start, onStart)
    MSG_ITEM(VDFF_MSG_resume, onResume)
    MSG_ITEM(VDFF_MSG_pause, onPause)
    MSG_ITEM(VDFF_MSG_stop, onStop)
    MSG_ITEM(VDFF_MSG_flush, onFlush)
    MSG_ITEM(VDFF_MSG_reset, onReset)
END_MSG_LOOP()

// ////////////////////// DecodeThread
VideoDecodeFFmpeg::DecodeThread::DecodeThread(VideoDecodeFFmpeg* decoder)
    : MMThread(MMTHREAD_NAME)
    , mDecoder(decoder)
    , mContinue(true)
    , mInputFrameCount(0)
    , mOutputFrameCount(0)
{
    ENTER();
    EXIT();
}

VideoDecodeFFmpeg::DecodeThread::~DecodeThread()
{
    ENTER();
    EXIT();
}

void VideoDecodeFFmpeg::DecodeThread::signalExit()
{
    ENTER();
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
    trafficControlWrite->unblockWait();
    MMAutoLock locker(mDecoder->mLock);
    mContinue = false;
    mDecoder->mCondition.signal();
    EXIT();
}

void VideoDecodeFFmpeg::DecodeThread::signalContinue()
{
    ENTER();
    mDecoder->mCondition.signal();
    EXIT();
}

// DecodeThread
void VideoDecodeFFmpeg::DecodeThread::main() {
    ENTER();

    while (1) {
        MediaBufferSP mediaBuffer;
        //Should hold heavy lock
        MMAutoLock locker(mDecoder->mLock);
        if (!mContinue) {
            break;
        }
        if (mDecoder->mIsPaused) {
            INFO("pause wait");
            mDecoder->mCondition.wait();
            INFO("pause wait wakeup");
            continue;
        }
        mDecoder->mReader->read(mediaBuffer);

        if (mediaBuffer) {
            int gotFrame = 0;
            uint8_t *pktData = NULL;
            int pktSize = 0;

            int64_t targetTime = -1ll;
            if (mediaBuffer->getMediaMeta()->getInt64(MEDIA_ATTR_TARGET_TIME, targetTime)) {
                mDecoder->mTargetTimeUs = targetTime;
                INFO("mTargetTimeUs %0.3f\n", mDecoder->mTargetTimeUs/1000000.0f);
            }

            bool inputEOS = mediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS);
            if (AVBufferHelper::convertToAVPacket(mediaBuffer, &mDecoder->mAVPacket)) {
                DEBUG("get one packet from source component :0x%x, size:%d\n",
                      mDecoder->mAVPacket->data,mDecoder->mAVPacket->size);
                pktData = mDecoder->mAVPacket->data;
                pktSize = mDecoder->mAVPacket->size;
            } else if (inputEOS)
                INFO("got EOS pkt");
            else
                WARNING("fail to retrieve AVPacket from MediaBuffer");

            // If flush flag is set, discard all the decoded frames and flush internel buffers in codecs.
            if (mDecoder->mNeedFlush) {
                DEBUG("need flush old buffer in codec\n");
                avcodec_flush_buffers(mDecoder->mAVCodecContext);
                mDecoder->mNeedFlush = false;
            }

            while (pktSize > 0 || inputEOS) {
                AVPacket * pkt = mDecoder->mAVPacket;
                AVPacket tempPkt;
                if (!pkt) {
                    ASSERT(inputEOS);
                    av_init_packet(&tempPkt);
                    tempPkt.size = 0;
                    pkt = &tempPkt;
                }
                int len = avcodec_decode_video2(mDecoder->mAVCodecContext, mDecoder->mAVFrame, &gotFrame, pkt);
                DEBUG("len:%d, gotFrame:%d\n",len,gotFrame);
                if (len > 0)
                    mInputFrameCount++;
                else if (!inputEOS) {
                    WARNING("Error while decoding.\n");
                    break;
                }


                if (gotFrame > 0) {
                    mOutputFrameCount++;
                    DEBUG("mInputFrameCount: %d, mOutputFrameCount: %d, mDecoder->mAVFrame->pkt_pts: %" PRId64, mInputFrameCount, mOutputFrameCount, mDecoder->mAVFrame->pkt_pts);
                    bool check = true;
#ifndef __MM_NATIVE_BUILD__
                    if (!mDecoder->mNativeWindow && !mDecoder->mSurfaceTexture) {
                        check = false;
                    }
#endif
                    if (check && mDecoder->mAVFrame->pkt_pts < mDecoder->mTargetTimeUs) {
                        DEBUG("ignore this frame: %" PRId64 " ms, mTargetTimeUs %" PRId64 " ms",
                              mDecoder->mAVFrame->pkt_pts/1000, mDecoder->mTargetTimeUs/1000);
                    } else {
                        mDecoder->mTargetTimeUs = -1ll;
                        // MediaBufferSP mediaOutputBuffer = AVBufferHelper::createMediaBuffer(mDecoder->mAVFrame, false, true);
                        MediaBufferSP mediaOutputBuffer = mDecoder->createMediaBufferFromAVFrame();
                        if (!mediaOutputBuffer) {
                            INFO("fail to create mediaOutputBuffer\n");
                            break;
                        }

#ifdef __DEBUG_DUMP_RAW_VIDEO__
                        int32_t scaleFactor[3] = {1, 2, 2}; // for YV12, W/H scale factor is same
                        AVFrame * frm = mDecoder->mAVFrame;
                        DEBUG("AVFrame width: %d, height: %d, linesize: %d, %d, %d", frm->width, frm->height, frm->data[0], frm->data[1], frm->data[2]);
                        for (int i=0; i<3; i++) {
                            for (int j = 0; j < frm->height/scaleFactor[i]; j++) {
                                mDecoder->mDumpRawVideo.dump(frm->data[i] + frm->linesize[i]*j, frm->width/scaleFactor[i]);
                            }
                        }
#endif
                        if (!mDecoder->mNotifyWH || mDecoder->mDstWidth != mDecoder->mAVFrame->width || mDecoder->mDstHeight != mDecoder->mAVFrame->height) {
                            mDecoder->mNotifyWH = true;
                            mDecoder->mDstWidth = mDecoder->mAVFrame->width;
                            mDecoder->mDstHeight = mDecoder->mAVFrame->height;
                            mDecoder->notify(kEventGotVideoFormat, mDecoder->mDstWidth, mDecoder->mDstHeight, nilParam);
                        }

                        mediaOutputBuffer->setMonitor(mDecoder->mMonitorWrite);

#ifndef __MM_NATIVE_BUILD__
                        MediaMetaSP meta = mediaOutputBuffer->getMediaMeta();
                        if (mDecoder->mSurfaceTexture)
                            meta->setPointer("surface-texture", mDecoder->mSurfaceTexture);
                        else if (mDecoder->mNativeWindow)
                            meta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mDecoder->mNativeWindow);
#endif

                        mm_status_t status = MM_ERROR_SUCCESS;
                        do {
                            status = mDecoder->mWriter->write(mediaOutputBuffer);
                            if (status == MM_ERROR_AGAIN) {
                                usleep(5000);
                                continue;
                            } else
                                break;
                        } while (1);

                        if (status != MM_ERROR_SUCCESS && status != MM_ERROR_ASYNC) {
                            ERROR("decoder fail to write Sink");
                            EXIT();
                        }
                        TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mDecoder->mMonitorWrite.get());
                        trafficControlWrite->waitOnFull();
                    }
                } else if (inputEOS) {
                    INFO("end of decoding, output buffers have been drained");
                    MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
                    mediaOutputBuffer->setFlag(MediaBuffer::MBFT_EOS);
                    mediaOutputBuffer->setSize(0);
                    mm_status_t ret = mDecoder->mWriter->write(mediaOutputBuffer);
                    if ((ret != MM_ERROR_SUCCESS) && (ret != MM_ERROR_EOS)) {
                        WARNING("decoder fail to write Sink");
                        continue;
                    }
                    mDecoder->mIsPaused = true;
                    break;
                }
                pktSize -= len;
                pktData += len;
            }
        } else {
            VERBOSE("read NULL buffer from demuxer\n");
            usleep(10*1000);
        }

    }

    INFO("video Output thread exited\n");
    EXIT();
}



// /////////////////////////////////////
VideoDecodeFFmpeg::VideoDecodeFFmpeg(const char *mimeType, bool isEncoder)
                                           : MMMsgThread(COMPONENT_NAME),
                                             mComponentName(COMPONENT_NAME),
                                             mIsPaused(true),
                                             mNeedFlush(false),
                                             mAVCodecContext(NULL),
                                             mAVCodecContextByUs(false),
                                             mAVCodecContextLock(NULL),
                                             mAVCodec(NULL),
                                             mAVPacket(NULL),
                                             mAVFrame(NULL),
                                             mCodecID(0),
                                             mSrcFormat(AV_PIX_FMT_NONE),
                                             mSrcWidth(0),
                                             mSrcHeight(0),
                                             mNotifyWH(false),
#ifndef __EMULATOR__
                                             mDstFormat(AV_PIX_FMT_YUV420P),
#else
                                             mDstFormat(AV_PIX_FMT_RGBA),
#endif
                                             mDstWidth(0),
                                             mDstHeight(0),
                                             mCondition(mLock),
#ifdef __DEBUG_DUMP_RAW_VIDEO__
                                             mDumpRawVideo("/data/ffmpeg_decoder_dump.yuv"),
#endif
#ifndef __MM_NATIVE_BUILD__
                                             mSurfaceTexture(NULL),
                                             mNativeWindow(NULL),
#endif

                                             mTargetTimeUs(-1ll)
{

    if (mm_check_env_str("video.decoder.output.format", "VIDEO_DECODER_OUTPUT_FORMAT", "rgb")) {
        mDstFormat = AV_PIX_FMT_RGBA;
    }

}

VideoDecodeFFmpeg::~VideoDecodeFFmpeg()
{
    ENTER();

    EXIT();
}

mm_status_t VideoDecodeFFmpeg::release()
{
    ENTER();
    if (mAVFrame) {
        av_frame_free(&mAVFrame);
        mAVFrame = NULL;
    }

    if (mAVCodecContextByUs && mAVCodecContext) {
        av_free(mAVCodecContext);
        mAVCodecContext = NULL;
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t VideoDecodeFFmpeg::init()
{
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoDecodeFFmpeg::uninit()
{
    ENTER();
    MMMsgThread::exit();

    EXIT();
}

const char * VideoDecodeFFmpeg::name() const
{
    ENTER();
    return mComponentName.c_str();
}

mm_status_t VideoDecodeFFmpeg::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (metaData) {
                av_register_all();
                //#0 get the parameters from the source
                mInputMetaData = metaData->copy();
                mm_status_t ret = parseInputMeta(mInputMetaData);
                if (ret != MM_ERROR_SUCCESS) {
                    ERROR("parseInputMeta from addSource is failed:%d\n",ret);
                    EXIT_AND_RETURN(ret);
                }
                //#1 set the output metadata
                mOutputMetaData = metaData->copy();
                // FIXME: assumed output color-format
                DEBUG("assumed ffmpeg decode output color-format is YV12");
                mOutputMetaData->setInt32(MEDIA_ATTR_COLOR_FORMAT, 'YV12');
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}

mm_status_t VideoDecodeFFmpeg::addSink(Component * component, MediaType mediaType)
{
    ENTER();

    //#0 check the call sequence
    DEBUG("output width: %d, height: %d format: %d\n", mDstWidth, mDstHeight, mDstFormat);
    if ((!mDstWidth)||(!mDstHeight)){
        ERROR("please call setParameter before add sink, width: %d, height: %d format: %d\n", mDstWidth, mDstHeight, mDstFormat);
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }
    if ((!mAVCodecContext)||(!mOutputMetaData)) {
        ERROR("please call addSource before add sink\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    //#1 set the write meta data
    // FIXME, we needn't setup output format here, but copied from input format already (in addSource)
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);
        if (mWriter && mOutputMetaData) {
            int ret = mOutputMetaData->setInt32(MEDIA_ATTR_WIDTH, mDstWidth);
            if (!ret) {
                ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_WIDTH);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            ret = mOutputMetaData->setInt32(MEDIA_ATTR_HEIGHT, mDstHeight);
            if (!ret) {
                ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_WIDTH);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            // FIXME, assumed ffmpeg video decode output color-format is YV12
            DEBUG("assumed ffmpeg video decode output color-format is YV12");
            ret = mOutputMetaData->setInt32(MEDIA_ATTR_COLOR_FORMAT, 'YV12');
            if (!ret) {
                ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_COLOR_FORMAT);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }

            mWriter->setMetaData(mOutputMetaData);
        }
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t VideoDecodeFFmpeg::prepare()
{
    ENTER();
    postMsg(VDFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::start()
{
    ENTER();
    postMsg(VDFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::resume()
{
    ENTER();
    postMsg(VDFF_MSG_resume, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::stop()
{
    ENTER();
    postMsg(VDFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::pause()
{
    ENTER();
    postMsg(VDFF_MSG_pause, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::reset()
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    postMsg(VDFF_MSG_reset, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::flush()
{
    ENTER();
    postMsg(VDFF_MSG_flush, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoDecodeFFmpeg::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    mm_status_t ret;

#ifndef __MM_NATIVE_BUILD__
    if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE, mNativeWindow)) {
        INFO("set parameter surface %p", mNativeWindow);
    }

    if (meta->getPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, mSurfaceTexture)) {
        INFO("set parameter surface texture %p", mSurfaceTexture);
    }
#endif

    // FIXME, these parameter needn't fail the process. and should NOT set from app. but retrieve from uplink component
    ret = meta->getInt32(MEDIA_ATTR_WIDTH, mDstWidth);
    ret = meta->getInt32(MEDIA_ATTR_HEIGHT, mDstHeight);
    int tmp;
    ret = meta->getInt32(MEDIA_ATTR_COLOR_FORMAT, tmp);
    if (!ret) {
        INFO("no output color format set, use default");
    } else
        mDstFormat = (AVPixelFormat)tmp;
    INFO("dstWidth:%d,dstHeight:%d,dstFormat:%d", mDstWidth,mDstHeight,(int)mDstFormat);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t VideoDecodeFFmpeg::parseInputMeta(MediaMetaSP & meta)
{
    ENTER();

    //CodecCtx
    void* ptr = NULL;

    if (meta->getInt32(MEDIA_ATTR_WIDTH, mDstWidth))
        INFO("get meta data: width %d\n", mDstWidth);

    if (meta->getInt32(MEDIA_ATTR_HEIGHT, mDstHeight))
        INFO("get meta data: height %d\n", mDstHeight);

    bool ret = meta->getInt32(MEDIA_ATTR_CODECID, mCodecID);
    if (!ret) {
        const char *mime = NULL;
        ret = meta->getString(MEDIA_ATTR_MIME, mime);
        if (!ret || !mime) {
            ERROR("fail to get getString data %s\n", MEDIA_ATTR_MIME);
            EXIT_AND_RETURN(MM_ERROR_NO_AUDIODECODER);
        }
        CowCodecID codecId= mime2CodecId(mime);
        mCodecID = CodecId2AVCodecId(codecId);
    }

    ret = meta->getPointer(MEDIA_ATTR_CODEC_CONTEXT, ptr);
    if (ret && ptr) {
        DEBUG("USING existed avcodec context");
        mAVCodecContext = static_cast<AVCodecContext*> (ptr);
        ret = meta->getPointer(MEDIA_ATTR_CODEC_CONTEXT_MUTEX, ptr);
        if (!ret) {
            // compressed data may not come from AVDemuxer, still go on
            ERROR("fail to get data pointer %s\n", MEDIA_ATTR_CODEC_CONTEXT_MUTEX);
            mAVCodecContextLock = NULL;
        } else
            mAVCodecContextLock = static_cast<Lock*> (ptr);
    } else {
        AVCodec *codec = NULL;
        codec = avcodec_find_decoder((AVCodecID)mCodecID);
        if (!codec) {
            ERROR("video decode not find ");
            EXIT_AND_RETURN(MM_ERROR_NO_VIDEODECODER);
        }

        mAVCodecContext = avcodec_alloc_context3(mAVCodec);

        mAVCodecContext->width = mDstWidth;
        mAVCodecContext->height = mDstHeight;
        mAVCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        mAVCodecContextByUs = true;
        INFO("alloc codec context %dx%d", mDstWidth, mDstHeight);
        uint8_t *extradata;
        int32_t size;
        if (ret = meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, extradata, size) && extradata) {
            INFO("copy extra data, size %d", size);
            // TODO free extradata
            mAVCodecContext->extradata = av_mallocz(size + 32);
            if (mAVCodecContext->extradata) {
                memcpy(mAVCodecContext->extradata, extradata, size);
                mAVCodecContext->extradata_size = size;
            } else
                WARNING("fail to alloc mem for extra data");
        }
    }

    INFO("the mAVCodecContext:0x%x\n",mAVCodecContext);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

#ifdef __USEING_SOFT_VIDEO_CODEC_FOR_MS__
/*static*/ int VideoDecodeFFmpeg::h264_set_extradata(AVCodecContext *avctx, int *dpbSize)
{
    int i;
    int ret = -1;

    H264ParamSets ps;
    const PPS *pps = NULL;
    const SPS *sps = NULL;
    int is_avc = 0;
    int nal_length_size = 0;

    memset(&ps, 0, sizeof(ps));

    ret = ff_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &is_avc, &nal_length_size, 0, avctx);
    if (ret < 0) {
        ff_h264_ps_uninit(&ps);
        return -1;
    }

    for (i = 0; i < MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const PPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const SPS*)ps.sps_list[pps->sps_id]->data;
            if (sps) {
                *dpbSize = sps->ref_frame_count;
                DEBUG("dpb size %d", *dpbSize);
            }

        }
    }


    ff_h264_ps_uninit(&ps);
    return 0;
}
#endif
void VideoDecodeFFmpeg::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);

    if (!mAVCodecContextLock) {
        WARNING("mAVCodecContextLock is NULL");
    }

    //#0 check
    if ((!mDstWidth)||(!mDstHeight)){
        ERROR("please call setParameter before prepare\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    if (!mAVCodecContext) {
        ERROR("please call addSource before prepare\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    mm_status_t ret;
    int32_t     format;
    ret = mOutputMetaData->getInt32(MEDIA_ATTR_COLOR_FORMAT, format);
    if (!ret ||(AVPixelFormat)format != 'YV12') {
        ERROR("please call addSink before prepare\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    //# open the CodecCtx && get srcWidth && srcHeight && srcformat

    //# malloc AVFrame
    mAVFrame = av_frame_alloc();
    if (!mAVFrame) {
        ERROR("av_frame_alloc is error\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    mSrcWidth = mAVCodecContext->width;
    mSrcHeight = mAVCodecContext->height;
    mSrcFormat = mAVCodecContext->pix_fmt;
    INFO("SrcWidth:%d,SrcHeight:%d,SrcFormat:%d\n",mSrcWidth,mSrcHeight,(int)mSrcFormat);

#ifdef __USEING_SOFT_VIDEO_CODEC_FOR_MS__
    int32_t memorySize = 0;
    int dpbSize = 4; // default count
    int inputBufferSize = 2*1024*1024/*input buffer size, refer to sprd hw decoder*/;
    if (mAVCodecContext) {
        if (mAVCodecContext->codec_id == AV_CODEC_ID_H264) {
            dpbSize = 16;
            if (h264_set_extradata(mAVCodecContext, &dpbSize)) {
                ERROR("parse extra data error");
            }
        } else if (mAVCodecContext->codec_id == AV_CODEC_ID_VP8) {
            dpbSize = 3; //vp8 refs count less than 3
        }

    }
    memorySize = mSrcHeight*mSrcWidth*3/2*(dpbSize+7);
    memorySize + inputBufferSize;
    DEBUG("memory size %d report", memorySize);

    notify(kEventInfo, kEventCostMemorySize, memorySize, nilParam);
#endif
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}



void VideoDecodeFFmpeg::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (mAVCodec == NULL) {
        int ret = -1;
        mAVCodec = avcodec_find_decoder((AVCodecID)mCodecID);
        if (mAVCodec == NULL) {
            ERROR("error no Codec found :%d\n", mCodecID);
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
        {
            if(mAVCodecContextLock)
                mAVCodecContextLock->acquire();
            ret = avcodec_open2(mAVCodecContext, mAVCodec, NULL);
            if(mAVCodecContextLock)
                mAVCodecContextLock->release();
        }
        if ( ret < 0) {
            ERROR("error avcodec_open failed:%d\n",ret);
            notify(kEventStartResult, MM_ERROR_OP_FAILED, 0, nilParam);
            EXIT();
        }
    }

    if (!mDecodeThread) {
        // create thread to decode buffer
        mDecodeThread.reset (new DecodeThread(this), MMThread::releaseHelper);
        mDecodeThread->create();
    }
    mIsPaused = false;
    mTargetTimeUs = -1ll;
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegVideoDecodeWrite"));
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mDecodeThread->signalContinue();
    EXIT();
}

void VideoDecodeFFmpeg::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegVideoDecodeWrite"));
    mIsPaused = false;
    mTargetTimeUs = -1ll;
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    mDecodeThread->signalContinue();
    EXIT();
}

void VideoDecodeFFmpeg::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    MMAutoLock locker(mLock);
    mNeedFlush = true;
    if (mAVCodecContext) {
        if(mAVCodecContextLock)
            mAVCodecContextLock->acquire();
        avcodec_close(mAVCodecContext);
        mAVCodec = NULL;
        if(mAVCodecContextLock)
            mAVCodecContextLock->release();
    }
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void VideoDecodeFFmpeg::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    if (trafficControlWrite)
        trafficControlWrite->unblockWait();
    MMAutoLock locker(mLock);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void VideoDecodeFFmpeg::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    mNeedFlush = true;
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void VideoDecodeFFmpeg::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mDecodeThread) {
            mDecodeThread->signalExit();
            mDecodeThread.reset();
        }
    }
    MMAutoLock locker(mLock);
    release();
    mReader.reset();
    mWriter.reset();
    mMonitorWrite.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

/*static*/ bool VideoDecodeFFmpeg::releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer[3] = { NULL, NULL, NULL };
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)buffer, NULL, NULL, 3))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    if (buffer[0])
        free(buffer[0]);
    return true;
}

/*static*/ bool VideoDecodeFFmpeg::releaseOutputAVBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer[3] = { NULL, NULL, NULL };
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)buffer, NULL, NULL, 3))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    if (buffer[0])
        av_free(buffer[0]);
    return true;
}

// we can't hold a reference to AVFrame to avoid data copy
// FIXME, add buffer pool or use buffer from downlink component (VideoSinkSurface)
MediaBufferSP VideoDecodeFFmpeg::createMediaBufferFromAVFrame()
{
    ENTER();
    MediaBufferSP mediaBuffer;

    if (mSrcFormat != AV_PIX_FMT_YUV420P && AV_PIX_FMT_YUVJ420P != AV_PIX_FMT_YUVJ420P) {
        ERROR("now, we only support yv420p format:%d\n", (int)mSrcFormat);
        return mediaBuffer;
    }

    mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
    MediaMetaSP outMeta = mediaBuffer->getMediaMeta();
    outMeta->setInt32(MEDIA_ATTR_WIDTH, mAVFrame->width);
    outMeta->setInt32(MEDIA_ATTR_HEIGHT, mAVFrame->height);
    outMeta->setInt32(MEDIA_ATTR_COLOR_FORMAT, mAVFrame->format);
    if (mDstFormat == AV_PIX_FMT_YUV420P)
        outMeta->setInt32(MEDIA_ATTR_COLOR_FOURCC, '420p');
    outMeta->setFraction(MEDIA_ATTR_SAMPLE_ASPECT_RATION, mAVFrame->sample_aspect_ratio.num, mAVFrame->sample_aspect_ratio.den);

    if (mAVFrame->key_frame)
        mediaBuffer->setFlag(MediaBuffer::MBFT_KeyFrame);
    mediaBuffer->setPts(mAVFrame->pkt_dts);
    uintptr_t buf[3];
    int size;
    uint8_t *data;

    switch ((int)mDstFormat) {
        case (int)AV_PIX_FMT_YUV420P: {
            size = mDstWidth * mDstHeight * 3 /2;
            data = (uint8_t*) malloc(size);;
            if (!data) {
                ERROR("new data is failed\n");
                mediaBuffer.reset();
                return mediaBuffer;
            }
            uint8_t *ptr = data;
            int32_t scaleFactor[3] = {1, 2, 2}; // for YV12, W/H scale factor is same
            for (int i=0; i<3; i++) {
                for(int j = 0; j < mSrcHeight/scaleFactor[i]; j++) {
                    memcpy(ptr, mAVFrame->data[i]+ j * mAVFrame->linesize[i], mSrcWidth/scaleFactor[i]);
                    ptr += mSrcWidth/scaleFactor[i];
                }
            }
            buf[0] = (uintptr_t)data;
            buf[1] = (uintptr_t)(data+mSrcWidth*mSrcHeight);
            buf[2] = (uintptr_t)(data+mSrcWidth*mSrcHeight*5/4);
            int32_t offset[3] = {0, 0, 0};
            int32_t stride[3] = {mSrcWidth, mSrcWidth/2, mSrcWidth/2};
            mediaBuffer->setBufferInfo(buf, offset, stride, 3);
            mediaBuffer->setSize(size);
            mediaBuffer->addReleaseBufferFunc(releaseOutputBuffer);
            break;
        }
        case (int)AV_PIX_FMT_RGBA: {

            struct SwsContext * sws = sws_getContext(mSrcWidth,
                                mSrcHeight,
                                AV_PIX_FMT_YUV420P,
                                mSrcWidth,
                                mSrcHeight,
                                AV_PIX_FMT_RGBA,
                                SWS_FAST_BILINEAR,
                                NULL,
                                NULL,
                                NULL);
            if (!sws) {
                ERROR("new sws is failed\n");
                mediaBuffer.reset();
                return mediaBuffer;
            }

            AVFrame * frameOut = av_frame_alloc();
            size = avpicture_get_size(AV_PIX_FMT_RGBA, mSrcWidth, mSrcHeight);
            data = (uint8_t*)av_malloc(size*sizeof(uint8_t));
            avpicture_fill((AVPicture*)frameOut, data, AV_PIX_FMT_RGBA, mSrcWidth, mSrcHeight);

            int ret = sws_scale(sws,
                (const uint8_t* const*)mAVFrame->data,
                mAVFrame->linesize,
                0,
                mSrcHeight,
                frameOut->data,
                frameOut->linesize);

            VERBOSE("scal ret: %d, w: %u, h: %u\n", ret, mSrcWidth, mDstHeight);
#if 0
            static FILE *fp = NULL;
            if (fp == NULL) {
                char name[128];
                sprintf(name, "/data/%dx%d_dec.rgb888", mSrcWidth, mSrcHeight);
                fp = fopen(name, "wb");
            }
            fwrite(frameOut->data[0], frameOut->linesize[0], mSrcHeight, fp);
#endif
            buf[0] = (uintptr_t)frameOut->data[0];

            int32_t offsets[1] = {0};
            int32_t strides[1] = {frameOut->linesize[0]};

            mediaBuffer->setBufferInfo(buf, offsets, strides, 1);
            mediaBuffer->setSize(size);
            mediaBuffer->addReleaseBufferFunc(releaseOutputAVBuffer);

            av_frame_free(&frameOut);
            sws_freeContext(sws);
            break;
        }
        default:
            ERROR("dst format %d is not supported\n", (int)mDstFormat);
            mediaBuffer.reset();
            return mediaBuffer;
    }

    return mediaBuffer;
}

AVCodecID VideoDecodeFFmpeg::CodecId2AVCodecId(CowCodecID id)
{
#undef item
#define item(_avid, _cowid) \
        case _cowid:\
            MMLOGI("%s -> %s\n", #_avid, #_cowid);\
            return _avid

        switch ( id ) {
            item(AV_CODEC_ID_NONE, kCodecIDNONE);
            item(AV_CODEC_ID_H264, kCodecIDH264);
            item(AV_CODEC_ID_HEVC, kCodecIDHEVC);
            default:
                MMLOGV("%d -> AV_CODEC_ID_NONE\n", id);
                return AV_CODEC_ID_NONE;
        }
}

#endif
}
