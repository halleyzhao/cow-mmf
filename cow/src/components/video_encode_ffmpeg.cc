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
#include <multimedia/av_ffmpeg_helper.h>
#include "video_encode_ffmpeg.h"

namespace YUNOS_MM
{
#ifdef _VIDEO_CODEC_FFMPEG
MM_LOG_DEFINE_MODULE_NAME("VEFF")

DataDump gEncodedDataDump("/tmp/veff_dump.h264");
static const char * COMPONENT_NAME = "VideoEncodeFFmpeg";
static const char * MMTHREAD_NAME = "VideoEncodeFFmpeg::EncodeThread";
static const float ASSUME_DEFAULT_FPS  = 25.0;

static const char* g_PresetStr[] =  {
    "ultrafast",
    "superfast",
    "veryfast",
    "faster",
    "fast",
    "medium",
    "slow",
    "slower",
    "veryslow",
    "placebo"
};
typedef enum {
    Preset_UltraFast,
    Preset_SuperFast,
    Preset_VeryFast,
    Preset_Faster,
    Preset_Fast,
    Preset_Medium,
    Preset_Slow,
    Preset_Slower,
    Preset_VerySlow,
    Preset_Placebo
} PresetType;

const uint32_t k_PreSetMax = sizeof(g_PresetStr)/sizeof(const char*);
const uint32_t k_CRFMin = 0;
const uint32_t k_CRFMax  = 51;
/*
const char* g_tune[] = {
    "film",
    "animation",
    "grain",
    "stillimage",
    "psnr",
    "ssim",
    "fastdecode",
    "zerolatency"
};
*/
const int TrafficControlLowBar = 1;
const int TrafficControlHighBar = 10;

#define DEFF_MAX_BITATE      1000000
#define DEFF_MAX_FPS         60
#define DEFF_MAX_WIDTH       1920
#define DEFF_MAX_HEIGHT      1920

#define ENTER() VERBOSE(">>>")
#define EXIT() do {VERBOSE(" <<<"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)", (_code)); return (_code);}while(0)

#define VEFF_MSG_prepare (msg_type)1
#define VEFF_MSG_start (msg_type)2
#define VEFF_MSG_resume (msg_type)3
#define VEFF_MSG_pause (msg_type)4
#define VEFF_MSG_stop (msg_type)5
#define VEFF_MSG_flush (msg_type)6
#define VEFF_MSG_reset (msg_type)7

BEGIN_MSG_LOOP(VideoEncodeFFmpeg)
    MSG_ITEM(VEFF_MSG_prepare, onPrepare)
    MSG_ITEM(VEFF_MSG_start, onStart)
    MSG_ITEM(VEFF_MSG_stop, onStop)
    MSG_ITEM(VEFF_MSG_flush, onFlush)
    MSG_ITEM(VEFF_MSG_reset, onReset)
END_MSG_LOOP();

const char* getPresetString(PresetType type)
{
    if (type <0 || type > k_PreSetMax -1)
        return g_PresetStr[Preset_Medium];

    return g_PresetStr[type];
}

// ////////////////////// EncodeThread
VideoEncodeFFmpeg::EncodeThread::EncodeThread(VideoEncodeFFmpeg* encoder)
    : MMThread(MMTHREAD_NAME),
      mEncoder(encoder),
      mContinue(true),
      mEos(eEosNormal)
{
    ENTER();
    EXIT();
}

VideoEncodeFFmpeg::EncodeThread::~EncodeThread()
{
    ENTER();
    EXIT();
}

void VideoEncodeFFmpeg::EncodeThread::signalExit()
{
    ENTER();
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mEncoder->mMonitorWrite.get());
    trafficControlWrite->unblockWait();
    MMAutoLock locker(mEncoder->mLock);
    mContinue = false;
    mEncoder->mCondition.signal();
    EXIT();
}

void VideoEncodeFFmpeg::EncodeThread::signalContinue()
{
    ENTER();
    mEncoder->mCondition.signal();
    EXIT();
}

static bool releaseOutputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

static int scaleFrame( uint8_t *in, uint8_t *out,
                        int32_t srcWidth, int32_t srcHeight,
                        int32_t dstWidth, int32_t dstHeight,
                        AVPixelFormat srcFormat, AVPixelFormat dstFormat)
{
    struct	SwsContext		*p_sw_ctx = NULL;
    unsigned char *p_src[4],*p_dst[4];
    int	src_width[4],dst_width[4];
    AVPixelFormat video_type = AV_PIX_FMT_NV21;

    //check && init
    if ((srcFormat != dstFormat) &&
         (srcFormat != AV_PIX_FMT_YUV420P)) {
        ERROR("now,we only support yuv420 format\n");
        return -1;
    }

    memset(p_src,0,4*sizeof(int));
    memset(p_dst,0,4*sizeof(int));
    memset(src_width,0,4*sizeof(int));
    memset(dst_width,0,4*sizeof(int));

    //start to scale
    if(srcFormat == AV_PIX_FMT_NV21) {
        video_type = AV_PIX_FMT_NV21;
        p_src[0] = in;
        p_src[1] = p_src[0] + srcWidth * srcHeight;
        p_dst[0] = out;
        p_dst[1] = p_dst[0] + dstWidth * dstHeight;
        src_width[0] = srcWidth;
        src_width[1] = srcWidth;
        dst_width[0] = dstWidth;
        dst_width[1] = dstHeight;
    } else if (srcFormat == AV_PIX_FMT_YUV420P){
        video_type = AV_PIX_FMT_YUV420P;
        p_src[0] = in;
        p_src[1] = p_src[0] + srcWidth * srcHeight;
        p_src[2] = p_src[1] + ((srcWidth * srcHeight)>>2);
        p_dst[0] = out;
        p_dst[1] = p_dst[0] + dstWidth * dstHeight;
        p_dst[2] = p_dst[1] + ((dstWidth * dstHeight)>>2);
        src_width[0] = srcWidth;
        src_width[2] = src_width[1] = srcWidth >> 1;
        dst_width[0] = dstWidth;
        dst_width[2] = dst_width[1] = dstWidth >> 1;
    }

    p_sw_ctx = sws_getCachedContext(p_sw_ctx, srcWidth,srcHeight,video_type,
                                    dstWidth, dstHeight,video_type,
                                    SWS_BICUBIC,NULL,NULL,NULL);

    if(p_sw_ctx){
        sws_scale(p_sw_ctx,p_src,src_width,0,srcHeight,p_dst,dst_width);
        return dstWidth * dstHeight * 3 /2;
    } else {
        ERROR("scale frame is error\n");
    }

    return -1;
}

// encode Buffer
void VideoEncodeFFmpeg::EncodeThread::main()
{
    ENTER();
    int     frameIdx = 0;
    int64_t lastPts = 0, duration;

    if (mEncoder->mFrameFps < 0.00001)
        mEncoder->mFrameFps = ASSUME_DEFAULT_FPS; // never happen
    duration = (int64_t)((1000*1000)/(mEncoder->mFrameFps));

    while(1) {
        MediaBufferSP mediaInputBuffer;
        {
            MMAutoLock locker(mEncoder->mLock);
            if (!mContinue) {
                break;
            }
            if (mEncoder->mIsPaused) {
                INFO("pause wait");
                mEncoder->mCondition.wait();
                INFO("pause wait wakeup");
            }
            mEncoder->mReader->read(mediaInputBuffer);
        }
        if (mediaInputBuffer) {
            // get the data buffer from MediaBufferSP
            uint8_t *yuvBuffer = NULL;
            int32_t offset = 0, length = 0, avcSize = 0;
            mediaInputBuffer->getBufferInfo((uintptr_t *)&yuvBuffer, &offset, &length, 1);
            length = mediaInputBuffer->size();
            INFO("read from source filter: buffer:%p,%d\n", yuvBuffer, length);

            //create the media buffer for output
            if (mediaInputBuffer->isFlagSet(MediaBuffer::MBFT_EOS) || !yuvBuffer || !length) {
                DEBUG("mEos: %d", mEos);
                mEos = eEOSInput;
            }

            //scale one frame
            uint8_t *scalePtr = NULL,*encodePtr = NULL;
            DEBUG("in: %dx%d, 0x%x, out: %dx%d, 0x%x",
                mEncoder->mInputWidth, mEncoder->mInputHeight, mEncoder->mInputFormat,
                mEncoder->mEncodeWidth, mEncoder->mEncodeHeight, mEncoder->mEncodeFormat);
            if (yuvBuffer &&
                (mEncoder->mInputWidth != mEncoder->mEncodeWidth ||
                mEncoder->mInputHeight != mEncoder->mEncodeHeight ||
                mEncoder->mInputFormat != mEncoder->mEncodeFormat)) {
                int ret = scaleFrame( yuvBuffer,
                            mEncoder->mScaledBuffer,
                            mEncoder->mInputWidth, mEncoder->mInputHeight,
                            mEncoder->mEncodeWidth, mEncoder->mEncodeHeight,
                            mEncoder->mInputFormat, mEncoder->mEncodeFormat );
                if (ret < 0){
                    ERROR("scaleFrame is error\n");
                    EXIT();
                }
                scalePtr = mEncoder->mScaledBuffer;
            } else
                scalePtr = yuvBuffer;

            //encode one frame
            int64_t pts = mediaInputBuffer->pts();
            int64_t dts = mediaInputBuffer->pts();  // both pts and dts use inputbuffer pts
            if (mEos == eEOSInput) {
                pts = dts = lastPts + duration;
            }

            do { // upon eEOSInput, encodeFrame will be  called many times with input/scalePtr is NULL until there is no more output
                mEncoder->encodeFrame(scalePtr, pts, dts, &encodePtr, &avcSize);
                INFO("encodeFrame length:%d\n", avcSize);
                if (pts <0) {
                    pts = lastPts + duration;
                    dts = pts;
                }

                //send one frame
                MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
                if (avcSize > 0) {
                    lastPts = pts;
                    mediaOutputBuffer->setPts(pts);
                    mediaOutputBuffer->setDts(dts);
                    mediaOutputBuffer->setDuration(duration);
                    mediaOutputBuffer->addReleaseBufferFunc(releaseOutputBuffer);
                    mediaOutputBuffer->setBufferInfo((uintptr_t *)&encodePtr, NULL, &avcSize, 1);
                    mediaOutputBuffer->setMonitor(mEncoder->mMonitorWrite);
                    mediaOutputBuffer->setSize(avcSize);
                    mediaOutputBuffer->setMediaMeta(mEncoder->mOutputMetaData);

                    INFO("video info: buffer:%p, size:%d, pts:%" PRId64 ", dts: %" PRId64 " duration:%" PRId64 "",
                        encodePtr, avcSize, pts, dts, duration);

                    mm_status_t status = MM_ERROR_SUCCESS;
                    while (1) {
                        mm_status_t status = mEncoder->mWriter->write(mediaOutputBuffer) ;
                        if (status != MM_ERROR_AGAIN)
                            break;
                        usleep(5000);
                    }

                    if (status != MM_ERROR_SUCCESS) {
                        ERROR("decoder fail to write Sink");
                        EXIT();
                    }

                    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mEncoder->mMonitorWrite.get());
                    trafficControlWrite->waitOnFull();
                }else if (mEos == eEOSInput) {
                    DEBUG();
                    mediaOutputBuffer->setFlag(MediaBuffer::MBFT_EOS);
                    mediaOutputBuffer->setSize(0);
                    mm_status_t status = MM_ERROR_SUCCESS;
                    while (1) {
                        mm_status_t status = mEncoder->mWriter->write(mediaOutputBuffer) ;
                        if (status != MM_ERROR_AGAIN)
                            break;
                        usleep(5000);
                    }

                    if (status != MM_ERROR_SUCCESS) {
                        ERROR("decoder fail to write Sink");
                        EXIT();
                    }
                    DEBUG();
                    mEos = eEOSOutput;
                }

                frameIdx++;
                scalePtr = NULL;
            }while (mEos == eEOSInput);
        }else {
            INFO("read NULL buffer from source plugin\n");
            if (mEos == eEOSOutput)
                break;
            usleep(10*1000);
        }
    }

    INFO("Encode thread exited");
}

// /////////////////////////////////////
#define VEFF_PROCESS_CREATED              0
#define VEFF_PROCESS_ADDSOURCE            1
#define VEFF_PROCESS_ADDSINK              2
#define VEFF_PROCESS_PREPARE              4

VideoEncodeFFmpeg::VideoEncodeFFmpeg(const char *mimeType, bool isEncoder)
                                           : MMMsgThread(COMPONENT_NAME),
                                             mComponentName(COMPONENT_NAME),
                                             mIsPaused(true),
                                             mCodecID(AV_CODEC_ID_MPEG4),
                                             mAVCodecContext(NULL),
                                             mAVCodec(NULL),
                                             mAVPacket(NULL),
                                             mAVFrame(NULL),
                                             mScaledBuffer(NULL),
                                             mInputFormat(AV_PIX_FMT_YUV420P),
                                             mInputWidth(0),
                                             mInputHeight(0),
                                             mEncodeFormat(AV_PIX_FMT_YUV420P),
                                             mEncodeWidth(0),
                                             mEncodeHeight(0),
                                             mFrameFps(ASSUME_DEFAULT_FPS),
                                             mBitRate(0),
                                             mFlags(VEFF_PROCESS_CREATED),
                                             mCRF(40),
                                             mPreset(Preset_Slow),
                                             mCondition(mLock),
                                             mInputBufferCount(0),
                                             mOutputBufferCount(0)
{
    ENTER();
    if (mm_check_env_str("mm.venc.type", "MM_VENC_TYPE", "h264", false)) {
        mCodecID = AV_CODEC_ID_H264;
    }
    DEBUG("mCodecID: %d, AV_CODEC_ID_H264: %d, AV_CODEC_ID_MPEG4: %d", mCodecID, AV_CODEC_ID_H264, AV_CODEC_ID_MPEG4);
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegVideoEncodeWrite"));
    mOutputMetaData = MediaMeta::create();
    EXIT();
}

VideoEncodeFFmpeg::~VideoEncodeFFmpeg()
{
    ENTER();

    EXIT();
}

mm_status_t VideoEncodeFFmpeg::release()
{
    ENTER();
    if (mAVFrame) {
        av_free(mAVFrame);
        mAVFrame = NULL;
    }
    if (mScaledBuffer) {
        delete []mScaledBuffer;
        mScaledBuffer = NULL;
    }

    mFlags = VEFF_PROCESS_CREATED;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t VideoEncodeFFmpeg::init()
{
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoEncodeFFmpeg::uninit()
{
    ENTER();
    MMMsgThread::exit();

    EXIT();
}

const char * VideoEncodeFFmpeg::name() const
{
    ENTER();
    return mComponentName.c_str();
}

mm_status_t VideoEncodeFFmpeg::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (metaData) {
                //#0 get the parameters from the source
                mInputMetaData = metaData->copy();
                mm_status_t ret = parseMetaFormat(mInputMetaData, true);
                if (ret != MM_ERROR_SUCCESS) {
                    ERROR("parseMetaFormat from addSource is failed:%d\n",ret);
                    EXIT_AND_RETURN(ret);
                }
                //#1 set the output metadata
                // mOutputMetaData = metaData->copy(); // it is incorrect to copy codec-data (which is from avdemuxer/decoder)
                mFlags |= VEFF_PROCESS_ADDSOURCE;
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}

mm_status_t VideoEncodeFFmpeg::addSink(Component * component, MediaType mediaType)
{
    ENTER();

    //#0 check the call sequence
    if (!(mFlags & VEFF_PROCESS_ADDSOURCE)) {
        ERROR("please call addSource before add sink\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    //#1 set the write meta data
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);
        if (mWriter && mOutputMetaData) {
            int ret = mOutputMetaData->setInt32(MEDIA_ATTR_WIDTH, mEncodeWidth);
            if (!ret) {
                ERROR("fail to set int32_t data %s", MEDIA_ATTR_WIDTH);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            ret = mOutputMetaData->setInt32(MEDIA_ATTR_HEIGHT, mEncodeHeight);
            if (!ret) {
                ERROR("fail to set int32_t data %s", MEDIA_ATTR_HEIGHT);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            ret = mOutputMetaData->setInt32(MEDIA_ATTR_COLOR_FORMAT, mEncodeFormat);
            if (!ret) {
                ERROR("fail to set int32_t data %s", MEDIA_ATTR_COLOR_FORMAT);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            ret = mOutputMetaData->setInt32(MEDIA_ATTR_CODECID, mCodecID);
            if (!ret) {
                ERROR("fail to set int32_t data %s", MEDIA_ATTR_CODECID);
                EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
            }
            DEBUG("mCodecID: %d", mCodecID);
            mWriter->setMetaData(mOutputMetaData);
        }
    }
    mFlags |= VEFF_PROCESS_ADDSINK;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t VideoEncodeFFmpeg::prepare()
{
    ENTER();

    //#0 check
    if (!(mFlags & VEFF_PROCESS_ADDSOURCE)) {
        ERROR("please call addSource before prepare\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }
    if (!(mFlags & VEFF_PROCESS_ADDSINK)) {
        ERROR("please call addSink before prepare\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    postMsg(VEFF_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoEncodeFFmpeg::start()
{
    ENTER();
    if (!(mFlags & VEFF_PROCESS_ADDSOURCE)) {
        ERROR("please call addSource before start\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }
    if (!(mFlags & VEFF_PROCESS_ADDSINK)) {
        ERROR("please call addSink before start\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }
    if (!(mFlags & VEFF_PROCESS_PREPARE)) {
        ERROR("please call prepare before start\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    postMsg(VEFF_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoEncodeFFmpeg::stop()
{
    ENTER();
    postMsg(VEFF_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoEncodeFFmpeg::reset()
{
    ENTER();
    postMsg(VEFF_MSG_reset, 0, NULL);
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    trafficControlWrite->unblockWait();
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t VideoEncodeFFmpeg::flush()
{
    ENTER();
    postMsg(VEFF_MSG_flush, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

#define DEFF_ONE_ELEMENT(name,a,b,c)                     \
         ret = meta->getInt32(name,a);                   \
         if ((!ret)||(a < b)||(a > c)) {                 \
            ERROR("read %s is error:%d\n",name,a);       \
            EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);     \
         }
mm_status_t VideoEncodeFFmpeg::setParameter(const MediaMetaSP & meta)
{
    ENTER();
    mm_status_t ret;
    ret = parseMetaFormat(meta, false);
    EXIT_AND_RETURN(ret);
}

/* parseMetaFormat is called by setParameter() and addSource(),
 * w/o parameters by setParameter(), width/height will use mInputMetaData as default
 */
mm_status_t VideoEncodeFFmpeg::parseMetaFormat(const MediaMetaSP & meta, bool isInput)
{
    ENTER();

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;

        if ( !strcmp(item.mName, MEDIA_ATTR_WIDTH) ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            if (isInput) {
                mInputWidth = item.mValue.ii;
                if (!mEncodeWidth)
                    mEncodeWidth = mInputWidth;
            } else
                mEncodeWidth = item.mValue.ii;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        } else  if ( !strcmp(item.mName, MEDIA_ATTR_HEIGHT) ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            if (isInput) {
                mInputHeight = item.mValue.ii;
                if (!mEncodeHeight)
                        mEncodeHeight = mInputHeight;
            } else
                mEncodeHeight = item.mValue.ii;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        } else  if ( !strcmp(item.mName, MEDIA_ATTR_COLOR_FORMAT) ) {
                if ( item.mType != MediaMeta::MT_Int32) {
                    MMLOGW("invalid type for %s\n", item.mName);
                    continue;
                }
                if (item.mValue.ii == 'YV12') {
                    if (isInput) {
                        mInputFormat = AV_PIX_FMT_YUV420P;
                        if (mEncodeFormat == AV_PIX_FMT_NONE)
                            mEncodeFormat = mInputFormat;
                    } else
                        mEncodeFormat = AV_PIX_FMT_YUV420P;
                }
                MMLOGI("key: %s, value: 0x%x", item.mName, item.mValue.ii);
        } else  if ( !strcmp(item.mName, MEDIA_ATTR_BIT_RATE) ) {
                if ( item.mType != MediaMeta::MT_Int32) {
                    MMLOGW("invalid type for %s\n", item.mName);
                    continue;
                }
                mBitRate = item.mValue.ii;
                MMLOGI("key: %s, value: %d", item.mName, item.mValue.ii);
        } else  if ( !strcmp(item.mName, MEDIA_ATTR_FRAME_RATE) ) {
                if ( item.mType != MediaMeta::MT_Float) {
                    MMLOGW("invalid type for %s\n", item.mName);
                    continue;
                }
                mFrameFps = item.mValue.f;
                MMLOGI("key: %s, value: %.2f\n", item.mName, item.mValue.f);
        } else  if ( !strcmp(item.mName, "video_encode_preset") ) {
            if ( item.mType != MediaMeta::MT_String) {
                MMLOGW("invalid type for %s", item.mName);
                continue;
            }
            uint32_t i=0;
            for (i=0; i<k_PreSetMax; i++) {
                if (!strcmp(item.mValue.str, g_PresetStr[i])) {
                    mPreset = i;
                    break;
                }
            }
            MMLOGI("key: %s, value: %s", item.mName, item.mValue.str);
        } else  if ( !strcmp(item.mName, "video_encode_crf") ) {
            if ( item.mType != MediaMeta::MT_Int32) {
                MMLOGW("invalid type for %s", item.mName);
                continue;
            }
            if (item.mValue.ui >=k_CRFMin && item.mValue.ui <= k_CRFMax)
                mCRF = item.mValue.ui;
            MMLOGI("key: %s, value: %d", item.mName, item.mValue.ui);
        } else {
            WARNING("unknown parameter %s", item.mName);
        }
    }

    if (!mBitRate && mEncodeWidth && mEncodeHeight && mFrameFps >0.00001) {
        mBitRate = mEncodeWidth * mEncodeHeight * mFrameFps /2000;
        INFO("calculate default bitrate to %d", mBitRate);
    }

    DEBUG("in: %dx%d, 0x%x, out: %dx%d, 0x%x, bitrate: %d, fps: %.2f",
        mInputWidth, mInputHeight, mInputFormat, mEncodeWidth, mEncodeHeight, mEncodeFormat, mBitRate, mFrameFps);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoEncodeFFmpeg::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);

    av_register_all();
    //#0 open encoder
    if (openEncoder() != MM_ERROR_SUCCESS) {
        ERROR("openEncoder is error\n");
        notify(kEventPrepareResult, MM_ERROR_IVALID_OPERATION,
               0, nilParam);
        EXIT();
    }

    //#1 malloc some space
    mAVFrame = av_frame_alloc();
    if (!mAVFrame) {
        ERROR("av_frame_alloc is error\n");
        closeEncoder();
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    mScaledBuffer = new uint8_t [mEncodeWidth * mEncodeHeight * 3 / 2];
    if (!mScaledBuffer) {
        ERROR("malloc space for mScaledBuffer is error\n");
        closeEncoder();
        av_free(mAVFrame);
        mAVFrame = NULL;
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    mFlags |= VEFF_PROCESS_PREPARE;

    EXIT();
}

void VideoEncodeFFmpeg::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (!mEncodeThread) {
        // create thread to decode buffer
        mEncodeThread.reset (new EncodeThread(this), MMThread::releaseHelper);
        mEncodeThread->create();
    }
    mIsPaused = false;
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mEncodeThread->signalContinue();
    EXIT();
}
/*
void VideoEncodeFFmpeg::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    AutoLock locker(mLock);
    mIsPaused = false;
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    mEncodeThread->signalContinue();
    EXIT();
}
*/
void VideoEncodeFFmpeg::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mEncodeThread) {
            mEncodeThread->signalExit();
            mEncodeThread.reset();
        }
    }
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}
/*
void VideoEncodeFFmpeg::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mIsPaused = true;
    TrafficControl * trafficControlWrite = static_cast<TrafficControl*>(mMonitorWrite.get());
    trafficControlWrite->unblockWait();
    AutoLock locker(mLock);
    mMonitorWrite.reset(new TrafficControl(TrafficControlLowBar, TrafficControlHighBar, "ffmpegVideoEncodeWrite"));
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}
*/
void VideoEncodeFFmpeg::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void VideoEncodeFFmpeg::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mEncodeThread) {
            mEncodeThread->signalExit();
            mEncodeThread.reset();
        }
    }

    MMAutoLock locker(mLock);
    release();
    mReader.reset();
    mWriter.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

mm_status_t VideoEncodeFFmpeg::configEncoder()
{
    ENTER();
    mAVCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    mAVCodecContext->codec_id = mCodecID;
    mAVCodecContext->pix_fmt = mEncodeFormat;

    mAVCodecContext->width = mEncodeWidth;
    mAVCodecContext->height = mEncodeHeight;
    mAVCodecContext->time_base.den = (int)mFrameFps;
    mAVCodecContext->time_base.num = 1;

    if (mCodecID == AV_CODEC_ID_MPEG4)
        mAVCodecContext->bit_rate = mBitRate;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t	VideoEncodeFFmpeg::openEncoder()
{
    ENTER();
    //#0 open the codec && codecContext
    mAVCodec = avcodec_find_encoder(mCodecID);
    if (mAVCodec == NULL) {
        ERROR("not found avc encoder in the ffmpeg\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    mAVCodecContext = avcodec_alloc_context3(mAVCodec);
    if (!mAVCodecContext) {
        ERROR("malloc context space is error\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }
    avcodec_get_context_defaults3(mAVCodecContext, mAVCodec);

    //#1 set the encoder parameters
    if (configEncoder() != MM_ERROR_SUCCESS) {
        ERROR("configure the encoder parameters error\n");
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    // #2 config complexity and quality
    // preset
    AVDictionary *dictParam = 0;
    const char* presetStr = getPresetString(PresetType(mPreset));
    std::string presetString = mm_get_env_str("mm.venc.preset", "MM_VENC_PRESET");
    if (!presetString.empty()) {
        presetStr = presetString.c_str();
    }
    DEBUG("x264 preset: %s", presetStr);
    av_dict_set(&dictParam, "preset", presetStr, 0);

    // crf
    int64_t crf = mCRF;
    std::string crfString = mm_get_env_str("mm.venc.crf", "MM_VENC_CRF");
    if (!crfString.empty()) {
        crf = atoi(crfString.c_str());
        // usually crf is between 18(good quality) and 28 (bad quality). but the reality on pad is [30, 40]
        crf = crf > k_CRFMax ? k_CRFMax : crf;
        crf = crf < k_CRFMin ? k_CRFMin : crf;
    }
    DEBUG("crf: %d", crf);
    av_opt_set_int(mAVCodecContext,"crf", crf, AV_OPT_SEARCH_CHILDREN);

    //#3 open the encoder parameters
    if(avcodec_open2(mAVCodecContext, mAVCodec, &dictParam)<0) {
        ERROR("unable to open avc encoder in the ffmpeg\n");
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t VideoEncodeFFmpeg::encodeFrame(uint8_t *data, int64_t & pts, int64_t & dts,
                                        unsigned char **out, int *outSize)
{
    ENTER();
    AVPacket pkt;
    AVFrame *frm = NULL;
    int gotFrame;
    uint8_t *buffer;
    int ret = -1;

    *out = NULL;
    *outSize = 0;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (data) {
        mAVFrame->format = mEncodeFormat;
        mAVFrame->width  = mEncodeWidth;
        mAVFrame->height = mEncodeHeight;

        mAVFrame->data[0] = data;
        mAVFrame->data[1] = data + mEncodeWidth * mEncodeHeight;
        mAVFrame->data[2] = mAVFrame->data[1] + (mEncodeWidth * mEncodeHeight)/4;
        mAVFrame->data[3] = NULL;
        mAVFrame->linesize[0] = mEncodeWidth;
        mAVFrame->linesize[1] = mEncodeWidth/2;
        mAVFrame->linesize[2] = mEncodeWidth/2;
        mAVFrame->linesize[3] = 0;
        mAVFrame->pts = pts;
        frm = mAVFrame;
        mInputBufferCount++;
    }

    ret = avcodec_encode_video2(mAVCodecContext, &pkt, frm, &gotFrame);
    INFO("ret: %d, gotFrame: %d, pts:%" PRId64 ", size:%d\n", ret, gotFrame, pkt.pts,pkt.size);
    if (!ret && gotFrame && pkt.size > 0) {
        mOutputBufferCount++;

        buffer = new uint8_t [pkt.size];
        if (!buffer) {
            ERROR("encoderFrame malloc buffer is error\n");
            EXIT_AND_RETURN(MM_ERROR_NO_MEM);
        }
        memcpy(buffer,pkt.data,pkt.size);

        *out = buffer;
        *outSize = pkt.size;
        pts = pkt.pts;
        dts = pkt.dts;
        // update/extract SPS/PPS for codec data
        if (pkt.flags & AV_PKT_FLAG_KEY) {
            // hard code 64 for codec data
            mOutputMetaData->setByteBuffer(MEDIA_ATTR_CODEC_DATA, pkt.data, 64);
        }
        gEncodedDataDump.dump(pkt.data, pkt.size);
        DEBUG("encoded_bytestream_dump: mOutputBufferCount: %d", mOutputBufferCount);
        hexDump(pkt.data, pkt.size, 16);
    }

    av_free_packet(&pkt);
    DEBUG("mInputBufferCount: %d, mOutputBufferCount: %d", mInputBufferCount, mOutputBufferCount);
    if (mOutputBufferCount && (mOutputBufferCount % 20 == 0 || mOutputBufferCount < 5))
        fprintf(stderr, "ffmpeg encoder: mInputBufferCount: %d, mOutputBufferCount: %d\n", mInputBufferCount, mOutputBufferCount);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void VideoEncodeFFmpeg::closeEncoder()
{
    ENTER();
    if(mAVCodecContext) {
        avcodec_close(mAVCodecContext);
        av_freep(&mAVCodecContext);
        mAVCodecContext = NULL;
    }

    EXIT();
}
#endif
}
