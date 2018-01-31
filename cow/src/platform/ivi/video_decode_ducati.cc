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

#include "video_decode_ducati.h"
#include <unistd.h>
// #include <sys/mman.h>
#include <stdlib.h>

#include "multimedia/mm_debug.h"
#include "multimedia/media_attr_str.h"

#include <wayland-client.h>
#include <wayland-drm-client-protocol.h>
// #include <wayland-scaler-client-protocol.h>

#include <ti/sdo/codecs/h264vdec/ih264vdec.h>
#include <ti/sdo/codecs/mpeg4vdec/impeg4vdec.h>
#include <ti/sdo/codecs/vc1vdec/ivc1vdec.h>
#include <ti/sdo/codecs/jpegvdec/ijpegvdec.h>
#include <ti/sdo/codecs/mpeg2vdec/impeg2vdec.h>
#include "cow_util.h"

MM_LOG_DEFINE_MODULE_NAME("VDDUCATI");
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

#define ALIGN2(x, n)   (((x) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))
#define MIN(a, b)        (((a) < (b)) ? (a) : (b))
#define PADX_H264   32
#define PADY_H264   24
#ifndef PAGE_SHIFT
#  define PAGE_SHIFT 12
#endif
#define FOURCC(ch0, ch1, ch2, ch3) \
        ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
         ((uint32_t)(uint8_t)(ch2) << 16)  | ((uint32_t)(uint8_t)(ch3) << 24))

namespace YUNOS_MM {
static const char * COMPONENT_NAME = "VideoDecodeDucati";
static const char * MMSGTHREAD_NAME = "VDDUCATI";
static const char * MMTHREAD_NAME = "VDDUCATI-Worker";
static const int32_t kExtraOutputFrameCount= 3;

// FIXME, if pipeline always call flush before seek. then decoder does nothing in seek.
#define VDDUCATI_MSG_prepare (msg_type)1
#define VDDUCATI_MSG_start (msg_type)2
#define VDDUCATI_MSG_pause (msg_type)4
#define VDDUCATI_MSG_resume (msg_type)5
#define VDDUCATI_MSG_stop (msg_type)6
#define VDDUCATI_MSG_flush (msg_type)7
#define VDDUCATI_MSG_reset (msg_type)8
#define VDDUCATI_MSG_handleResolutionChange (msg_type)11
#define VDDUCATI_MSG_recycleOutBuffer (msg_type)12

BEGIN_MSG_LOOP(VideoDecodeDucati)
    MSG_ITEM(VDDUCATI_MSG_prepare, onPrepare)
    MSG_ITEM(VDDUCATI_MSG_start, onStart)
    MSG_ITEM(VDDUCATI_MSG_pause, onPause)
    MSG_ITEM(VDDUCATI_MSG_resume, onResume)
    MSG_ITEM(VDDUCATI_MSG_stop, onStop)
    MSG_ITEM(VDDUCATI_MSG_flush, onFlush)
    MSG_ITEM(VDDUCATI_MSG_reset, onReset)
    MSG_ITEM(VDDUCATI_MSG_handleResolutionChange, onHandleResolutionChange)
    MSG_ITEM(VDDUCATI_MSG_recycleOutBuffer, onRecycleOutBuffer)
END_MSG_LOOP()

////////////////////////////////////
class dummy {}; // work around source-insight parse issue

// ////////////////////// WorkerThread
struct _outputBufferType{
    // information of drm bo
    uint32_t fourcc, width, height;
    // int nbo; always uses one bo
    struct omap_bo *bo[4];
    int32_t pitches[4];
    int32_t offsets[4];
    bool multiBo;   /* True when Y and U/V are in separate buffers/drm-bo. */
    int fd[4];      /* dmabuf */
    bool usedByCodec; // true: since send to codec, until freeBufID[]
    bool usedByClient; // true: since outputID[], until releaseOutMediaBuffer()
    uint32_t age;
    uint32_t drmName; // let decoder pass drm name to video sink
    int64_t pts;
};

class VideoDecodeDucati::WorkerThread : public MMThread {
  public:
    WorkerThread(VideoDecodeDucati * decoder);
    ~WorkerThread()
    {
        FUNC_TRACK();
        sem_destroy(&mSem);
    }

    void signalExit()
    {
        FUNC_TRACK();
        MMAutoLock locker(mDecoder->mLock);
        mContinue = false;
        sem_post(&mSem);
    }

    void signalFlush()
    {
        FUNC_TRACK();
        MMAutoLock locker(mDecoder->mLock);
        mFlush = true;
        sem_post(&mSem);
    }

    bool isFlushDone()
    {
        FUNC_TRACK();
        return !mFlush;
    }

    // signalStop = signalFlush + signalExit

    void signalContinue()
    {
        FUNC_TRACK();
        sem_post(&mSem);
    }

    // ducati
    bool initCodec();
    void deinitCodec();
    void freeOutputBuffers();
    int allocateOutputBuffers();
    OutputBufferType* getOneOutputBuffer();
    bool prepareCodecOutBuffer();
    static bool releaseOutMediaBuffer(MediaBuffer* mediaBuffer);
    MediaBufferSP createOutputMediaBuffer(OutputBufferType *outBuf);
    void dbgOutputBufferStatus();
    void updateOutputBufferAge();
    int32_t processBuffer();
    bool flushCodec(); //when mFlush is false, drain the output frame from DCE codec

  protected:
    virtual void main();

  public:
    // debug use only
    uint32_t mOutCount2; // recycled output buffer

  private:
    Lock mLock;
    VideoDecodeDucati * mDecoder;
    bool mInited;
    sem_t mSem;         // cork/uncork on pause/resume
    // FIXME, change to something like mExit
    bool mContinue;     // terminate the thread
    bool mFlush;     // flush codec
    enum EosStateType {
        kNoneEOS,
        kInputEOS,
        kOutputEOS,
    };
    EosStateType mEosState;


    // ducati related parameters
    bool multiBo;
    // int32_t mOmapFd;
    Engine_Handle mEngine;
    struct omap_device*  mDevice;
    VIDDEC3_Handle mCodec;
    VIDDEC3_Params *mParams;
    VIDDEC3_DynamicParams *mDynParams;
    VIDDEC3_Status *mStatus;
    XDM2_BufDesc *mInBufs;
    XDM2_BufDesc *mOutBufs;
    VIDDEC3_InArgs *mInArgs;
    VIDDEC3_OutArgs *mOutArgs;
    IH264VDEC_Params *mH264Params;
    IH264VDEC_DynamicParams *mH264DynParams;
    IH264VDEC_Status *mH264Status;
    bool mOutBufsInUse; // another output buffer isn't necessary since the previous frame hans't been completed yet

    // input info
    int32_t mPaddedWidth;
    int32_t mPaddedHeight;
    struct omap_bo* mInputBo;
    uint8_t* mInputBoBuffer;
    int32_t  mInputBufferSize;

    int32_t mNumBuffers; // dpb size, plus kExtraOutputFrameCount
    std::vector<OutputBufferType*> mOutputBuffers;
    OutputBufferType *mCurrOutBuf;
    int64_t mCurrentPts; // current input data pts
    size_t *mTempOutBufFds;  // used for lock/unlock in group

    // debug use only
    uint32_t mInCount;
    uint32_t mOutCount;
};

VideoDecodeDucati::WorkerThread::WorkerThread(VideoDecodeDucati * decoder)
    // general info
    : MMThread(MMTHREAD_NAME)
    , mOutCount2(0)
    , mDecoder(decoder)
    , mInited(false)
    , mContinue(true)
    , mFlush(false)
    , mEosState(kNoneEOS)
    // ducati related
    , multiBo(false)
    // , mOmapFd(0)
    , mEngine(0)
    , mDevice(NULL)
    , mCodec(0)
    , mParams(NULL)
    , mDynParams(NULL)
    , mStatus(NULL)
    , mInBufs(NULL)
    , mOutBufs(NULL)
    , mInArgs(NULL)
    , mOutArgs(NULL)
    , mH264Params(NULL)
    , mH264DynParams(NULL)
    , mH264Status(NULL)
    , mOutBufsInUse(false)
    // input info
    , mPaddedWidth(0)
    , mPaddedHeight(0)
    , mInputBo(NULL)
    , mInputBoBuffer(NULL)
    , mInputBufferSize(0)
    // output info
    , mNumBuffers(0)
    , mCurrOutBuf(NULL)
    , mCurrentPts(0)
    , mTempOutBufFds(NULL)
    // debug info
    , mInCount(0)
    , mOutCount(0)
{
    FUNC_TRACK();
    sem_init(&mSem, 0, 0);
}

/////////////////////////////////////// ducati mem helper func
static struct omap_bo *
alloc_bo(struct omap_device* device, uint32_t bpp, uint32_t width, uint32_t height,
        uint32_t *bo_handle, uint32_t *pitch)
{
    FUNC_TRACK();
    struct omap_bo *bo;
    uint32_t bo_flags = OMAP_BO_SCANOUT |OMAP_BO_WC;

    // the width/height has been padded already
    bo = omap_bo_new(device, width * height * bpp / 8, bo_flags);

    if (bo) {
        *bo_handle = omap_bo_handle(bo);
        *pitch = width * bpp / 8;
        if (bo_flags & OMAP_BO_TILED)
            *pitch = ALIGN2(*pitch, PAGE_SHIFT);
    }

    return bo;
}

static OutputBufferType*
alloc_buffer(struct omap_device* device, uint32_t fourcc, uint32_t w, uint32_t h)
{
    FUNC_TRACK();
    uint32_t bo_handles[4] = {0};

        OutputBufferType   *outBuf = (OutputBufferType*) calloc(sizeof(OutputBufferType), 1);
    if (!outBuf) {
        ERROR("allocation failed");
        return NULL;
    }

    outBuf->fourcc = fourcc;
    outBuf->width = w;
    outBuf->height = h;
    outBuf->multiBo = false;

    if (!fourcc)
        fourcc = FOURCC('N','V','1','2');

    switch (fourcc) {
    case FOURCC('N','V','1','2'):
            outBuf->bo[0] = alloc_bo(device, 8, outBuf->width, outBuf->height * 3 / 2,
                bo_handles, (uint32_t*)(outBuf->pitches));
            outBuf->offsets[0] = 0;
            outBuf->offsets[1] = outBuf->pitches[0] * outBuf->height;
            outBuf->pitches[1] = outBuf->pitches[0];
            omap_bo_get_name(outBuf->bo[0], &outBuf->drmName);
            outBuf->fd[0] = omap_bo_dmabuf(outBuf->bo[0]);
            break;
    default:
            ERROR("invalid format: 0x%08x", fourcc);
            break;
    }

    return outBuf;
}

int VideoDecodeDucati::WorkerThread::allocateOutputBuffers()
{
    FUNC_TRACK();
    int i;
    mOutputBuffers.resize(mNumBuffers);
    for (i=0; i<mNumBuffers; i++) {
        mOutputBuffers[i] = NULL;
    }

    for (i=0; i<mNumBuffers; i++) {
        OutputBufferType * outBuf = alloc_buffer(mDevice, FOURCC('N','V','1','2'), mPaddedWidth, mPaddedHeight);
        if( outBuf == NULL ) {
            return -ENOMEM;
        }

        if( outBuf ) {
            outBuf->usedByCodec = false;
            outBuf->usedByClient = false;
            outBuf->age = 100; // the value doesn't matter much, but greater than 0
            omap_bo_get_name(outBuf->bo[0], &outBuf->drmName);
        }
        mOutputBuffers[i] = outBuf;
    }

    return 0;
}

OutputBufferType *VideoDecodeDucati::WorkerThread::getOneOutputBuffer()
{
    FUNC_TRACK();
    uint32_t i = 0;
    OutputBufferType   *outBuf = NULL;

    // dbgOutputBufferStatus();
    for (i = 0; i < mOutputBuffers.size(); i++) {
        if (mOutputBuffers[i]->usedByCodec || mOutputBuffers[i]->usedByClient)
            continue;
        if (!outBuf) {
            outBuf = mOutputBuffers[i];
            continue;
        }
        if (outBuf->age < mOutputBuffers[i]->age)
            outBuf = mOutputBuffers[i];
    }

    /* barrier.. if we are using GPU blitting, we need to make sure
     * that the GPU is finished:
     */
     if (outBuf) {
        omap_bo_cpu_prep(outBuf->bo[0], OMAP_GEM_WRITE);
        omap_bo_cpu_fini(outBuf->bo[0], OMAP_GEM_WRITE);
    }
     if (!outBuf)
        DEBUG("fail to get available for output");

    return outBuf;
}

void VideoDecodeDucati::WorkerThread::freeOutputBuffers()
{
    FUNC_TRACK();
    uint32_t i = 0;
    OutputBufferType* outBuf = NULL;

    for (i=0; i<mOutputBuffers.size(); i++) {
        outBuf = mOutputBuffers[i];
        if (!outBuf)
            continue;

        close(outBuf->fd[0]);
        omap_bo_del(outBuf->bo[0]);
        if (multiBo) {
            close(outBuf->fd[1]);
            omap_bo_del(outBuf->bo[1]);
        }

        free(outBuf);
        mOutputBuffers[i] = NULL;
    }

    mOutputBuffers.clear();
}

#define VERSION_SIZE 128
bool VideoDecodeDucati::WorkerThread::initCodec()
{
    FUNC_TRACK();
    Engine_Error     ec;
    XDAS_Int32       err;

    DEBUG("original video resolution: %dx%d", mDecoder->mWidth, mDecoder->mHeight);
    // round up to marco block size
    mDecoder->mWidth = ALIGN2(mDecoder->mWidth, 4);
    mDecoder->mHeight = ALIGN2(mDecoder->mHeight, 4);
    DEBUG("aligned video resolution: %dx%d", mDecoder->mWidth, mDecoder->mHeight);
    // xxx, other codec type support
    mPaddedWidth  = ALIGN2(mDecoder->mWidth + (2 * PADX_H264), 7);
    mPaddedHeight = mDecoder->mHeight + 4 * PADY_H264;
    DEBUG("padded video resolution for video driver: %dx%d", mPaddedWidth, mPaddedHeight);
    // max output buffer count (dpb size)
    mNumBuffers   = (MIN(16, 32768 / ((mDecoder->mWidth / 16) * (mDecoder->mHeight / 16)))) + kExtraOutputFrameCount; // use more buffers, better for video rendering. 3;
    DEBUG("mNumBuffers: %d", mNumBuffers);

    mDevice = (struct omap_device*)dce_init();
    if(!mDevice) {
        ERROR("fail to init dce");
        return false;
    }

    // allocate actual output video buffer
    err = allocateOutputBuffers();
    if( err ) {
        ERROR("DCE: buffer allocation failed %d", err);
        freeOutputBuffers();
        return false;
    }
    if (!multiBo) {
        mTempOutBufFds = (size_t*)malloc(sizeof(int)*mNumBuffers);
    }
    else{
        mTempOutBufFds = (size_t*)malloc(sizeof(int)*(mNumBuffers*2));
    }

    // allocate input buffer
    mInputBo = omap_bo_new(mDevice, mDecoder->mWidth*mDecoder->mHeight, OMAP_BO_WC);
    mInputBoBuffer = (uint8_t*)omap_bo_map(mInputBo);

    mEngine = Engine_open((char*)"ivahd_vidsvr", NULL, &ec);
    if (!mEngine) {
        ERROR("fail to open codec engine");
        return false;
    }

    // xxx other codec support; viddec3test uses IVIDDEC3_Params
    mParams = (VIDDEC3_Params*)dce_alloc(sizeof(IH264VDEC_Params));
    if( !mParams ) {
        ERROR("DEC Parameter memory allocation failed");
        return false;
    }
    mParams->size = sizeof(IH264VDEC_Params);
    mParams->maxBitRate      = 10000000;
    mParams->displayDelay    = IVIDDEC3_DISPLAY_DELAY_AUTO;
    mParams->numOutputDataUnits  = 0;
    mParams->maxWidth            = mDecoder->mWidth;

    // common codec parameters
    mParams->maxHeight           = mDecoder->mHeight;
    mParams->maxFrameRate        = 30000;
    mParams->dataEndianness      = XDM_BYTE;
    mParams->forceChromaFormat   = XDM_YUV_420SP;
    mParams->operatingMode       = IVIDEO_DECODE_ONLY;
    //mParams->displayDelay        = IVIDDEC3_DECODE_ORDER;
    mParams->displayBufsMode     = IVIDDEC3_DISPLAYBUFS_EMBEDDED;
#if 1
    mParams->inputDataMode       = IVIDEO_ENTIREFRAME;
#else
    // still fail to create codec when I try to use IH264VDEC_NALUNIT_MODE
    mParams->inputDataMode       = IH264VDEC_NALUNIT_MODE; // IVIDEO_SLICEMODE;
    mDecoder->mForceByteStream = false;
#endif
    mParams->metadataType[0]     = IVIDEO_METADATAPLANE_NONE;
    mParams->metadataType[1]     = IVIDEO_METADATAPLANE_NONE;
    mParams->metadataType[2]     = IVIDEO_METADATAPLANE_NONE;
    mParams->outputDataMode      = IVIDEO_ENTIREFRAME;
    mParams->numInputDataUnits   = 0;
    mParams->errorInfoMode       = IVIDEO_ERRORINFO_OFF;

    // xxx other codec support; viddec3test doesn't setup h264 specific parameters
    mH264Params = (IH264VDEC_Params *) mParams;
    mH264Params->dpbSizeInFrames = IH264VDEC_DPB_NUMFRAMES_AUTO;
    mH264Params->pConstantMemory = 0;
    mH264Params->presetLevelIdc = IH264VDEC_LEVEL41;
    mH264Params->errConcealmentMode = IH264VDEC_APPLY_CONCEALMENT;
    mH264Params->temporalDirModePred = TRUE;
    mH264Params->detectCabacAlignErr = IH264VDEC_DISABLE_CABACALIGNERR_DETECTION;
    if (mDecoder->mIsAVCcType && !mDecoder->mForceByteStream)
        mH264Params->bitStreamFormat = IH264VDEC_NAL_UNIT_FORMAT;
    else
        mH264Params->bitStreamFormat = IH264VDEC_BYTE_STREAM_FORMAT;

    DEBUG("dce_alloc VIDDEC3_Params successful h264_params=%p", mH264Params);
    // err = msync((Ptr)mH264Params, sizeof(IH264VDEC_Params), MS_CACHE_ONLY | MS_SYNC);
    mCodec = VIDDEC3_create(mEngine, (char*)"ivahd_h264dec", (VIDDEC3_Params *)mH264Params);

    if (!mCodec) {
        ERROR("fail to create libdce codec");
        return false;
    }
    DEBUG("create libdce codec ok");

    // xxx, other codec support
    mDynParams = (IVIDDEC3_DynamicParams*)dce_alloc(sizeof(IH264VDEC_DynamicParams));
    mDynParams->size = sizeof(IH264VDEC_DynamicParams);

    // common parameters
    mDynParams->decodeHeader  = XDM_DECODE_AU;
    /*Not Supported: Set default*/
    mDynParams->displayWidth  = 0;
    mDynParams->frameSkipMode = IVIDEO_NO_SKIP;
    mDynParams->newFrameFlag  = XDAS_TRUE;

    // Allocating memory to store the Codec version information from Codec.
    char *codec_version = NULL;
    codec_version = (char*)dce_alloc(VERSION_SIZE);
    DEBUG("codec_version 0x%x", codec_version);

    // xxx other codec support
    mH264DynParams = (IH264VDEC_DynamicParams *) mDynParams;
    DEBUG("Allocating IH264VDEC_DynamicParams successful h264_dynParams=%p", mH264DynParams);
    // xxx other codec support
    mStatus = (IVIDDEC3_Status*)dce_alloc(sizeof(IH264VDEC_Status));
    mStatus->size = sizeof(IH264VDEC_Status);
    DEBUG("dce_alloc IH264VDEC_Status successful mStatus=%p", mStatus);

    // XDM_GETVERSION & XDM_SETPARAMS
    mStatus->data.buf = (XDAS_Int8 *) codec_version;
    mStatus->data.bufSize = VERSION_SIZE;

    // xxx other codec
    mH264Status = (IH264VDEC_Status *) mStatus;
    DEBUG("IH264VDEC_Status successful h264_status=%p", mH264Status);

    err = VIDDEC3_control(mCodec, XDM_GETVERSION, (VIDDEC3_DynamicParams *) mH264DynParams, (VIDDEC3_Status *) mH264Status);
    DEBUG("VIDDEC3_control IH264VDEC_Status XDM_GETVERSION h264_status->data.buf = %s", (((VIDDEC3_Status *)mH264Status)->data.buf));

    err = VIDDEC3_control(mCodec, XDM_SETPARAMS, (VIDDEC3_DynamicParams *) mH264DynParams, (VIDDEC3_Status *) mH264Status);

    if( codec_version ) {
        dce_free(codec_version);
    }

    if( err ) {
        ERROR("DCE: codec control failed  %d",err);
        return false;
    }

    // allocate one input buffer
    DEBUG("input buffer configuration width %d height %d", mDecoder->mWidth, mDecoder->mHeight);
    mInBufs = (XDM2_BufDesc*)dce_alloc(sizeof(XDM2_BufDesc));
    mInBufs->numBufs = 1;
    mInBufs->descs[0].buf = (XDAS_Int8*) omap_bo_dmabuf(mInputBo);
    dce_buf_lock(1, (size_t*)(&(mInBufs->descs[0].buf)));
    mInBufs->descs[0].bufSize.bytes = omap_bo_size(mInputBo);
    mInBufs->descs[0].memType = XDM_MEMTYPE_RAW;
    DEBUG("mInBufs->descs[0].buf %p mInputBoBuffer %p", mInBufs->descs[0].buf, mInputBoBuffer);

    // allocate multiple output buffer parameter
    DEBUG("output buffer configuration num_buffers %d padded_width %d padded_height %d", mNumBuffers, mPaddedWidth, mPaddedHeight);
    mOutBufs = (XDM2_BufDesc*)dce_alloc(sizeof(XDM2_BufDesc));
    mOutBufs->numBufs = 2;
    mOutBufs->descs[0].memType = XDM_MEMTYPE_RAW;
    mOutBufs->descs[1].memType = XDM_MEMTYPE_RAW;

    // in/out Args
    mInArgs = (IVIDDEC3_InArgs*)dce_alloc(sizeof(IVIDDEC3_InArgs));
    mInArgs->size = sizeof(IVIDDEC3_InArgs);
    mOutArgs = (IVIDDEC3_OutArgs*)dce_alloc(sizeof(IVIDDEC3_OutArgs));
    mOutArgs->size = sizeof(IVIDDEC3_OutArgs);

    return true;
}

void VideoDecodeDucati::WorkerThread::deinitCodec()
{
    FUNC_TRACK();
    freeOutputBuffers();

    if( mStatus ) {
        dce_free(mStatus);
    }
    if( mParams ) {
        dce_free(mParams);
    }
    if( mDynParams ) {
        dce_free(mDynParams);
    }
    if (mInBufs) {
        if(mInBufs->descs[0].buf) {
            dce_buf_unlock(1, (size_t*)(&(mInBufs->descs[0].buf)));
            close((int)(mInBufs->descs[0].buf));
        }
        dce_free(mInBufs);
    }

    if( mOutBufs ) {
        dce_free(mOutBufs);
    }
    if( mInArgs ) {
        dce_free(mInArgs);
    }
    if( mOutArgs ) {
        dce_free(mOutArgs);
    }

    VIDDEC3_delete(mCodec);
    if( mEngine ) {
        Engine_close(mEngine);
    }

    if (mInputBo) {
        // no omap_bo_unmap() required?
        omap_bo_del(mInputBo);
    }
    if (mTempOutBufFds)
        delete mTempOutBufFds;

    dce_deinit(mDevice);
}

void VideoDecodeDucati::WorkerThread::dbgOutputBufferStatus()
{
    FUNC_TRACK();
    uint32_t i=0, pos=0;
    const int k = 12;
    char temp[40*12];
    for (i=0; i<mOutputBuffers.size(); i++) {
        sprintf(temp+pos,  "(%02d, %d, %d), ", i, mOutputBuffers[i]->usedByCodec, mOutputBuffers[i]->usedByClient);
        pos +=k;
        if (i%8 == 0) {
            sprintf(temp+pos, "\n");
            pos++;
        }
    }
    DEBUG("%s", temp);
}

void VideoDecodeDucati::WorkerThread::updateOutputBufferAge()
{
    FUNC_TRACK();
    uint32_t i=0;
    for (i=0; i<mOutputBuffers.size(); i++) {
        if (mOutputBuffers[i]->usedByCodec || mOutputBuffers[i]->usedByClient)
            continue;
        mOutputBuffers[i]->age++;
    }
}

bool VideoDecodeDucati::WorkerThread::releaseOutMediaBuffer(MediaBuffer* mediaBuffer)
{
    FUNC_TRACK();
    OutputBufferType *outBuf = NULL;
    VideoDecodeDucati* decoder = NULL;
    void* ptr = NULL;

    if (!mediaBuffer)
        return false;

    MediaMetaSP meta = mediaBuffer->getMediaMeta();
    if (!meta)
        return false;

    if (meta->getPointer("ducati-out-buffer", ptr))
        outBuf = (OutputBufferType*)ptr;

    if (meta->getPointer("ducati-decoder", ptr))
        decoder = (VideoDecodeDucati*)ptr;

    if (decoder && outBuf)
        decoder->recycleOutputBuffer(outBuf);

    return true;
}

MediaBufferSP VideoDecodeDucati::WorkerThread::createOutputMediaBuffer(OutputBufferType *outBuf)
{
    FUNC_TRACK();
    MediaBufferSP mediaBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_DrmBufName);
    if (mEosState == kOutputEOS){ // in fact, it is late here. since KOutputEOS is set after processBuffer
        mediaBuffer->setFlag(MediaBuffer::MBFT_EOS);
    }
    if (!outBuf) // EOS may be sent with empty MediaBuffer
        return mediaBuffer;

    DEBUG("outBuf: %p, offsets: %p, pitches: %p pts %" PRId64, outBuf, outBuf->offsets, outBuf->pitches,outBuf->pts);
    MediaMetaSP meta = MediaMeta::create();
    meta->setPointer("ducati-decoder", mDecoder);
    meta->setPointer("ducati-out-buffer", outBuf);
    // outBuf->sequence = mGeneration;

    uintptr_t data[4] = {outBuf->drmName, outBuf->drmName, 0, 0};
    mediaBuffer->setBufferInfo(data, outBuf->offsets, outBuf->pitches, 2);
    mediaBuffer->setPts(outBuf->pts);
    mediaBuffer->setMediaMeta(meta);
    mediaBuffer->addReleaseBufferFunc(releaseOutMediaBuffer);
    outBuf->usedByClient = true;

    dbgOutputBufferStatus();
    return mediaBuffer;
}
bool VideoDecodeDucati::WorkerThread::prepareCodecOutBuffer()
{
    int i=0;
    while (!mOutBufsInUse) {
        mCurrOutBuf = getOneOutputBuffer();
        if (mCurrOutBuf)
            break;
        i++;
        if (i>20) // xxx, input buffer is skipped
            return false;
        usleep(15000);
    }

    DEBUG("mCurrOutBuf: %p", mCurrOutBuf);
    mInArgs->inputID = (XDAS_Int32)mCurrOutBuf;

    mOutBufs->descs[0].buf = (XDAS_Int8*)mCurrOutBuf->fd[0];
    if (mCurrOutBuf->multiBo)
        mOutBufs->descs[1].buf = (XDAS_Int8*)mCurrOutBuf->fd[1];
    else
        mOutBufs->descs[1].buf = mOutBufs->descs[0].buf;

    if(mCurrOutBuf->multiBo){
        mTempOutBufFds[0] = mCurrOutBuf->fd[0];
        mTempOutBufFds[1] = mCurrOutBuf->fd[1];
        dce_buf_lock(2, mTempOutBufFds);
    }
    else{
        mTempOutBufFds[0] = mCurrOutBuf->fd[0];
        dce_buf_lock(1, mTempOutBufFds);
    }
    mOutBufs->descs[0].bufSize.bytes = mPaddedWidth * mPaddedHeight;
    mOutBufs->descs[1].bufSize.bytes = mPaddedWidth* (mPaddedHeight/2);
    mCurrOutBuf->pts = mCurrentPts;
    mCurrOutBuf->usedByCodec = true;
    // mCurrOutBuf->age = 0;
    return true;
}
int32_t VideoDecodeDucati::WorkerThread::processBuffer()
{
    FUNC_TRACK();
    XDAS_Int32       err = DCE_EXDM_FAIL;
    int i = 0;

    DEBUG("Calling VIDDEC3_process mInArgs->inputID=0x%x descs[0].buf %p descs.bufSize %d mInput %p",
          mInArgs->inputID, mInBufs->descs[0].buf, (int) mInBufs->descs[0].bufSize.bytes, mInputBoBuffer);
    err = VIDDEC3_process(mCodec, mInBufs, mOutBufs, mInArgs, mOutArgs);

#if 0 // FIXME
    if(err == DCE_EXDM_UNSUPPORTED  ||err == DCE_EIPC_CALL_FAIL  ||err == DCE_EINVALID_INPUT
         || ( err == DCE_EXDM_FAIL && XDM_ISFATALERROR(mOutArgs->extendedError)) ) {
                 ERROR("DCE_TEST_FAIL: VIDDEC3_process return err %d, extendedError: %08x", err, mOutArgs->extendedError);
                 return err;
    }
#endif
     if (err != DCE_EXDM_FAIL) {
          // handle output frame from codec
          OutputBufferType* outBuf = NULL;

          for( i = 0; mOutArgs->outputID[i]; i++ ) {
            outBuf = (OutputBufferType *)mOutArgs->outputID[i];
            DEBUG("outBuf %p\n",outBuf);
              if (mFlush) {
                  outBuf->usedByClient = true;
                  continue;
              }
            MediaBufferSP mediaBuffer = createOutputMediaBuffer(outBuf);

            if (mEosState == kInputEOS && (i+1 == IVIDEO2_MAX_IO_BUFFERS-1 || !mOutArgs->outputID[i+1])) {
                DEBUG("create EOS output buffer, i=%d", i); // i is 0; seems libdec doesn't drain buffers in dpb
                mediaBuffer->setFlag(MediaBuffer::MBFT_EOS);
            }

            mDecoder->putOneOutputBuffer(mediaBuffer);
            DEBUG("get output frame, mOutCount: %03d, outBuf: %p, drmName: %d", mOutCount, outBuf, outBuf->drmName);
            mOutCount++;
          }
     }
    // handle buffers not used by codec
    uint32_t freeBufCount = 0;
    for( i = 0; mOutArgs->freeBufID[i]; i++ ) {
        DEBUG("dce release buf: (%02d, 0x%x)", i, mOutArgs->freeBufID[i]);
        OutputBufferType *outBuf = (OutputBufferType *)mOutArgs->freeBufID[i];
        outBuf->usedByCodec = false;

        if(outBuf->multiBo){
            mTempOutBufFds[freeBufCount++] = outBuf->fd[0];
            mTempOutBufFds[freeBufCount++] = outBuf->fd[1];
        }
        else{
            mTempOutBufFds[freeBufCount++] = outBuf->fd[0];
            DEBUG("FreeBufID: %p fd:%d\n", mOutArgs->freeBufID[i], outBuf->fd[0]);
        }
    }

    if(freeBufCount){
        // if(mEosState == kNoneEOS) // why add such condition?
        dce_buf_unlock(freeBufCount, mTempOutBufFds);
        freeBufCount =0;
    }

    mOutBufsInUse = mOutArgs->outBufsInUseFlag ;
    if( mOutBufsInUse ) {
        DEBUG("outBufsInUseFlag is SET. Not sending a new output buffer to codec on the next Codec_process ");
    }

    return err;
}

bool VideoDecodeDucati::WorkerThread::flushCodec()
{
    FUNC_TRACK();
    XDAS_Int32 err = DCE_EXDM_FAIL;
    int32_t retryCount = 0, i = 0;
    int32_t numInBuf = 0, numOutBuf = 0;

    // other codec support
    DEBUG("Calling VIDDEC3_control XDM_FLUSH h264_dynParams %p h264_status %p", mH264DynParams, mH264Status);
    err = VIDDEC3_control(mCodec, XDM_FLUSH, (VIDDEC3_DynamicParams *) mH264DynParams, (VIDDEC3_Status *) mH264Status);

    numOutBuf = mOutBufs->numBufs;
    numInBuf = mInBufs->numBufs;
    // prepare empty input buffer
    mInArgs->inputID = 0;
    mInArgs->numBytes = 0;
    // mInBufs->descs[0].buf = NULL;
    mInBufs->descs[0].bufSize.bytes = 0;
    mOutBufs->numBufs = 0;


    // for eos
    mInBufs->numBufs = 0;

    while (err == DCE_EOK && retryCount++<100) {
        DEBUG("retryCount: %d", retryCount);
        err = processBuffer();
        #if 0 // example code from libdce, but not work well
        // xxx other codec flush complete check
        if((!((mOutArgs->extendedError) >> 18) & 0x1)) {
            INFO("libdce reach EOS");
            break;
        }

        if (XDM_ISFATALERROR(mOutArgs->extendedError))
            break;
        #endif
    }

    DEBUG("err: %d", err);
    mOutBufs->numBufs = numOutBuf;
    mInBufs->numBufs = numInBuf;
    memset(mOutArgs->outputID,0,sizeof(mOutArgs->outputID));
    memset(mOutArgs->freeBufID,0,sizeof(mOutArgs->freeBufID));

    mInCount = 0;
    mOutCount = 0;
    mOutCount2 = 0;
    for (i=0; i<mOutputBuffers.size(); i++) {
        mOutputBuffers[i]->usedByCodec = false;
        mOutputBuffers[i]->usedByClient = false;
    }

    return true;
}

// decoder worker thread
void VideoDecodeDucati::WorkerThread::main()
{
    FUNC_TRACK();

    if (!mInited)
        mInited = initCodec();
    ASSERT(mInited);

    while(1) {
        {
            MMAutoLock locker(mDecoder->mLock);
            if (!mContinue || mEosState == kOutputEOS) {
                break;
            }
        }

        if (mDecoder->mState != kStatePlaying && !(mFlush && kStatePaused == mDecoder->mState)) {
            // neither playing nor flush (usually flush happends in paused state)
            INFO("WorkerThread waitting_sem ...");
            sem_wait(&mSem);
            INFO("WorkerThread wakeup_sem");
            continue;
        }

        if (mFlush || mEosState == kInputEOS) {
            flushCodec();
            mFlush = false;
            if(mEosState == kInputEOS)
                mEosState = kOutputEOS;
            continue;
        }

        // get one input buffer
        MediaBufferSP srcMediaBuffer = mDecoder->getOneInputBuffer();
        if (!srcMediaBuffer) {
            usleep(10000);
            continue;
        }
        if (srcMediaBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
            mEosState = kInputEOS;
        }
        if (!srcMediaBuffer->size())
            continue;

        mCurrentPts = srcMediaBuffer->pts();
        DEBUG("srcMediaBuffer, size: %" PRId64 ", pts: %" PRId64, srcMediaBuffer->size(), srcMediaBuffer->pts());
        uintptr_t buffers[1];
        int32_t offsets[1], strides[1];

        mInBufs->numBufs = 1;
        uint32_t offset = 0;

        if (mDecoder->mPrefixCSD) {
            int32_t i = 0;
            DEBUG("mDecoder->mStreamCSDMaker->getCSDCount() : %d", mDecoder->mStreamCSDMaker->getCSDCount());
            for (i=0; i< mDecoder->mStreamCSDMaker->getCSDCount(); i++) {
                MediaBufferSP csdBuf = mDecoder->mStreamCSDMaker->getMediaBufferFromCSD(i);
                csdBuf->getBufferInfo((uintptr_t *)buffers, offsets, strides, 1);
                DEBUG("i: %d, buffer: %p, offset: %d, stride: %d", i, buffers[0], offsets[0], strides[0]);
                memcpy(mInputBoBuffer+offset, (void*)buffers[0], csdBuf->size());
                offset += csdBuf->size();
            }
            mDecoder->mPrefixCSD = false;
        }
        DEBUG();
        srcMediaBuffer->getBufferInfo((uintptr_t *)buffers, offsets, strides, 1);
        memcpy(mInputBoBuffer+offset, (void*)buffers[0], srcMediaBuffer->size());
        if (mDecoder->mIsAVCcType && mDecoder->mForceByteStream) {
            h264_avcC2ByteStream(NULL, (uint8_t*)mInputBoBuffer, offset + srcMediaBuffer->size());
        }
        if (mDecoder->mDumpInput)
            mDecoder->mInputDataDump.dump(mInputBoBuffer, offset + srcMediaBuffer->size());
        mInBufs->descs[0].bufSize.bytes = offset + srcMediaBuffer->size();
        mInArgs->numBytes = mInBufs->descs[0].bufSize.bytes;
        mInCount++;
        DEBUG("mInCount %03d: (%" PRId64 ", %p)", mInCount, srcMediaBuffer->size(), (void*)buffers[0]);

        if (prepareCodecOutBuffer()) // FIXME, input buffer is skipped when fail to get on output buffer
            processBuffer();
    }

    deinitCodec();
    INFO("Ducati codec thread exited");
}

// ///////////////////////////////////// VideoDecodeDucati main class
VideoDecodeDucati::VideoDecodeDucati(const char* mimeType, bool isEncoder)
    : MMMsgThread(MMSGTHREAD_NAME)
    , mMime(mimeType)
    , mComponentName(COMPONENT_NAME)
    , mState(kStateNull)
    , mSource(NULL)
    , mSink(NULL)
    , mFormat(0)
    , mRotationDegrees(0)
    , mIsAVCcType(false)
    , mForceByteStream(true)
    , mCSDBufferIndex(0)
    , mPrefixCSD(true)
    , mWidth(0)
    , mHeight(0)
    , mConfigured(false)
    , mGeneration(1)
    , mDumpInput(false)
    , mInputDataDump("/data/input.h264")
{
    FUNC_TRACK();

    ASSERT(!isEncoder);
    // check mimeType

    if (mm_check_env_str("mm.vdducati.dump.input","MM_VDDUCATI_DUMP_INTPUT", "1"))
        mDumpInput = true;

    mOutputFormat = MediaMeta::create();
}

VideoDecodeDucati::~VideoDecodeDucati()
{
    FUNC_TRACK();
    // make sure worker thread exited
}

void VideoDecodeDucati::updateBufferGeneration()
{
    FUNC_TRACK();

    // 0 & UINT32_MAX are invalid generation
    mGeneration++;
    if (mGeneration == UINT32_MAX)
        mGeneration = 1;

    DEBUG("update mGeneration: %d", mGeneration);

}

mm_status_t VideoDecodeDucati::prepare()
{
    FUNC_TRACK();
    postMsg(VDDUCATI_MSG_prepare, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeDucati::start()
{
    FUNC_TRACK();
    if (mConfigured) {
        postMsg(VDDUCATI_MSG_resume, 0, NULL);
    } else {
        postMsg(VDDUCATI_MSG_start, 0, NULL);
    }

    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeDucati::stop()
{
    FUNC_TRACK();
    postMsg(VDDUCATI_MSG_stop, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoDecodeDucati::pause()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(VDDUCATI_MSG_pause, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeDucati::resume()
{
    FUNC_TRACK();
    // FIXME, is it necessary to post to another thread
    postMsg(VDDUCATI_MSG_resume, 0, NULL);
    return MM_ERROR_ASYNC;
}

mm_status_t VideoDecodeDucati::seek(int msec, int seekSequence)
{
    FUNC_TRACK();
    return MM_ERROR_SUCCESS;
}
mm_status_t VideoDecodeDucati::reset()
{
    FUNC_TRACK();
    postMsg(VDDUCATI_MSG_reset, 0, NULL);
    return MM_ERROR_ASYNC;
}
mm_status_t VideoDecodeDucati::flush()
{
    FUNC_TRACK();
    postMsg(VDDUCATI_MSG_flush, 0, NULL);
    return MM_ERROR_ASYNC;
}

void VideoDecodeDucati::recycleOutputBuffer(OutputBufferType* outBuf)
{
    if (outBuf) {
        mRecycleBuffers.push(outBuf);
    }
    postMsg(VDDUCATI_MSG_recycleOutBuffer, 0, NULL); // always schedule task, may use NULL outBuf to recycle all previous buffer
}

mm_status_t VideoDecodeDucati::addSource(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            mInputFormat = mReader->getMetaData();
            if (mInputFormat) {
                if (mInputFormat->getInt32(MEDIA_ATTR_WIDTH, mWidth)) {
                    DEBUG("get meta data, width is %d\n", mWidth);
                    mOutputFormat->setInt32(MEDIA_ATTR_WIDTH, mWidth);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_HEIGHT, mHeight)) {
                    DEBUG("get meta data, height is %d\n", mHeight);
                    mOutputFormat->setInt32(MEDIA_ATTR_HEIGHT, mHeight);
                }

                const char* mime = NULL;
                if (mInputFormat->getString(MEDIA_ATTR_MIME, mime)) {
                    DEBUG("get meta data, mime is %s\n", mime);
                }

                int32_t codecId = -1;
                if (mInputFormat->getInt32(MEDIA_ATTR_CODECID, codecId)) {
                    DEBUG("get meta data, codec ID is %d\n", codecId);
                }

                if (mInputFormat->getInt32(MEDIA_ATTR_ROTATION, mRotationDegrees)) {
                    VERBOSE("get meta data, rotation degrees is %d\n", mRotationDegrees);
                }

                mStreamCSDMaker = createStreamCSDMaker(mInputFormat);
            }

#if 0
            mWidth  = ALIGN2(mWidth, 4);         /* round up to MB */
            mHeight = ALIGN2(mHeight, 4);        /* round up to MB */
#endif
            // xxx seems gst-ducati always uses XDM_MEMTYPE_TILEDPAGE with mStride = 4096;

            return MM_ERROR_SUCCESS;
        }
    }

    return MM_ERROR_OP_FAILED;
}

mm_status_t VideoDecodeDucati::addSink(Component * component, MediaType mediaType)
{
    FUNC_TRACK();
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);

        if (mWriter && mOutputFormat) {
            mWriter->setMetaData(mOutputFormat);
        }
    }
    return MM_ERROR_SUCCESS;
}

void VideoDecodeDucati::setState(StateType state)
{
    FUNC_TRACK();
    // FIXME, seem Lock isn't necessary
    // caller thread update state to kStatePaused,  other state are updated by MMMsgThread
    // MMAutoLock locker(mLock);
    mState = state;
}

StreamMakeCSDSP VideoDecodeDucati::createStreamCSDMaker(MediaMetaSP &meta)
{
    FUNC_TRACK();
    StreamMakeCSDSP csd;
    int32_t codec_id = 0;
    uint8_t *data = NULL;
    int32_t size = 0;

    if (!meta->getByteBuffer(MEDIA_ATTR_CODEC_DATA, data, size))
        return csd;
    if (mInputFormat->getInt32(MEDIA_ATTR_CODECID, codec_id)) {
        DEBUG("get meta data, codec ID is %d\n", codec_id);
    }

    if (codec_id == kCodecIDH264) {
        DEBUG();
        AVCCSDMaker *maker = new AVCCSDMaker(meta);
        if (maker == NULL) {
            ERROR("no memory\n");
            return csd;
        }
        csd.reset(maker);
        maker->getCSDCount();//make isAVCc valid
        mIsAVCcType = maker->isAVCc();
        DEBUG("mIsAVCcType %d, cds count: %d", mIsAVCcType, maker->getCSDCount());
    } else {
        csd.reset(new StreamCSDMaker(meta));
    }

    INFO("codec 0x%0x", codec_id);
    return csd;
}

MediaBufferSP VideoDecodeDucati::getOneInputBuffer()
{
    FUNC_TRACK();
    MediaBufferSP sourceBuf;
    mm_status_t status = MM_ERROR_AGAIN;
    int32_t retryCount = 0;
    while (status == MM_ERROR_AGAIN) {
        status = mReader->read(sourceBuf);
        retryCount++;
        if (retryCount>10)
            break;
    }

    if (status != MM_ERROR_SUCCESS)
        sourceBuf.reset();

    return sourceBuf;
}
bool VideoDecodeDucati::putOneOutputBuffer(MediaBufferSP buffer)
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_AGAIN;

    while (status == MM_ERROR_AGAIN)
        status = mWriter->write(buffer);
    if (status != MM_ERROR_SUCCESS)
        return false;

    return true;
}

void VideoDecodeDucati::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock); // big lock

    mState = kStatePreparing;
    ASSERT(mInputFormat && mMime.c_str());

    mState = kStatePrepared;
    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoDecodeDucati::onHandleResolutionChange(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    // xxx
    notify(kEventGotVideoFormat, mWidth, mHeight, nilParam);
    DEBUG("mWidth %d, mHeight %d", mWidth, mHeight);

    return;
}

void VideoDecodeDucati::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    MMAutoLock locker(mLock); // big locker

    if (!mConfigured) {
        mState = kStatePlay; // setState(kStatePlay);
        mWorkerThread.reset(new WorkerThread(this), MMThread::releaseHelper);
        // FIXME, add SP to handle create and destroy
        mWorkerThread->create();
        mConfigured = true;
    }

    mState = kStatePlaying;
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mWorkerThread->signalContinue();
}


void VideoDecodeDucati::onPause(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState != kStatePlaying) {
        WARNING("already paused, just return");
        notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
    }

    setState(kStatePaused);
    notify(kEventPaused, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoDecodeDucati::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (mState != kStatePaused) {
        ERROR("invalid resume command, not in kStatePaused: mState: %d", mState);
        // notify(kEventResumed, MM_ERROR_OP_FAILED, 0, nilParam);
    }

    setState(kStatePlaying);
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);

    mPrefixCSD = true;
    mWorkerThread->signalContinue();
}

void VideoDecodeDucati::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();
    // MMAutoLock locker(mLock);

    DEBUG("mConfigured=%d, mState=%d\n", mConfigured, mState);
    if (!mConfigured || mState == kStateStopped || mState == kStateStopping){
        mReader.reset();
        mWriter.reset();
        notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    setState(kStateStopping);

    if (mWorkerThread) {
        mWorkerThread->signalExit();
        mWorkerThread.reset(); // it will trigger MMThread::destroy() to wait until the exit of mWorkerThread
    }
    updateBufferGeneration();

    setState(kStateStopped);
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);

    mReader.reset();
    mWriter.reset();
    mConfigured = false;
}

void VideoDecodeDucati::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    if (!mConfigured) {
        notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
        return;
    }

    if (!mWorkerThread) {
        DEBUG("codec hasn't been started yet, return");
        return;
    }

    mWorkerThread->signalFlush();
    while(1) {
        if (mWorkerThread->isFlushDone())
            break;
    }

    updateBufferGeneration();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    mPrefixCSD = true;
}

void VideoDecodeDucati::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    // FIXME, onStop wait until the pthread_join of mWorkerThread
    onStop(param1, param2, rspId);

    // close device
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
}

void VideoDecodeDucati::onRecycleOutBuffer(param1_type param1, param2_type param2, uint32_t rspId)
{
    FUNC_TRACK();

    while (mRecycleBuffers.size()) {
        OutputBufferType * outBuf = mRecycleBuffers.front();
        if (mState == kStatePlaying || mState == kStatePaused) {
            DEBUG("output_release: %p", outBuf);
            outBuf->usedByClient = false;
            outBuf->age = 0;

            mWorkerThread->mOutCount2++;
            DEBUG("mOutCount2: %d", mWorkerThread->mOutCount2);
            mWorkerThread->updateOutputBufferAge();
        }
        mRecycleBuffers.pop();
    }
}

// boilplate for MMMsgThread and Component
mm_status_t VideoDecodeDucati::init()
{
    FUNC_TRACK();
    int ret = MMMsgThread::run();
    if (ret)
        return MM_ERROR_OP_FAILED;

    return MM_ERROR_SUCCESS;
}

void VideoDecodeDucati::uninit()
{
    FUNC_TRACK();
    MMMsgThread::exit();
}

const char * VideoDecodeDucati::name() const
{
    return mComponentName.c_str();
}

mm_status_t VideoDecodeDucati::setParameter(const MediaMetaSP & meta)
{
    FUNC_TRACK();

    WARNING("setParameter isn't supported yet");
    return MM_ERROR_SUCCESS;
}

mm_status_t VideoDecodeDucati::getParameter(MediaMetaSP & meta) const
{
    FUNC_TRACK();
    WARNING("getParameter isn't supported yet");
    return MM_ERROR_SUCCESS;
}

// //////// for component factory
extern "C" {
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    FUNC_TRACK();
    YUNOS_MM::VideoDecodeDucati *VideoDecodeDucatiComponent = new YUNOS_MM::VideoDecodeDucati(mimeType, isEncoder);
    if (VideoDecodeDucatiComponent == NULL) {
        return NULL;
    }

    return static_cast<YUNOS_MM::Component*>(VideoDecodeDucatiComponent);
}


void releaseComponent(YUNOS_MM::Component *component) {
    FUNC_TRACK();
    delete component;
}
} // extern "C"

} // YUNOS_MM

