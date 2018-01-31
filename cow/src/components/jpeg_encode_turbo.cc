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

#include "jpeg_encode_turbo.h"

namespace YUNOS_MM
{

MM_LOG_DEFINE_MODULE_NAME("JPEG_ENCODE_TURBO")

static const char * COMPONENT_NAME = "JpegEncodeTurbo";
static const char * MMTHREAD_NAME = "JpegEncodeTurbo::EncoderThread";

const int TrafficControlLowBar = 1;
const int TrafficControlHighBar = 10;

#define ENTER() INFO(">>>\n")
#define EXIT() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define JET_MSG_prepare (msg_type)1
#define JET_MSG_start (msg_type)2
#define JET_MSG_resume (msg_type)3
#define JET_MSG_pause (msg_type)4
#define JET_MSG_stop (msg_type)5
//#define JET_MSG_flush (msg_type)6
//#define JET_MSG_seek (msg_type)7
#define JET_MSG_reset (msg_type)8
#define JET_MSG_setParameters (msg_type)9
#define JET_MSG_getParameters (msg_type)10

#define JET_MAX_WIDTH    4096
#define JET_MAX_HEIGHT   4096

BEGIN_MSG_LOOP(JpegEncodeTurbo)
    MSG_ITEM(JET_MSG_prepare, onPrepare)
    MSG_ITEM(JET_MSG_start, onStart)
    MSG_ITEM(JET_MSG_stop, onStop)
    MSG_ITEM(JET_MSG_reset, onReset)
//    MSG_ITEM(JET_MSG_setParameters, onSetParameters)
//    MSG_ITEM(JET_MSG_getParameters, onGetParameters)
END_MSG_LOOP()

// ////////////////////// EncoderThread
JpegEncodeTurbo::EncoderThread::EncoderThread(JpegEncodeTurbo* encoder)
    : MMThread(MMTHREAD_NAME),
      mEncoder(encoder),
      mContinue(true)
{
    ENTER();
    EXIT();
}

JpegEncodeTurbo::EncoderThread::~EncoderThread()
{
    ENTER();
    EXIT();
}

void JpegEncodeTurbo::EncoderThread::signalExit()
{
    ENTER();
    MMAutoLock locker(mEncoder->mLock);
    mContinue = false;
    mEncoder->mCondition.signal();
    EXIT();
}

void JpegEncodeTurbo::EncoderThread::signalContinue()
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

mm_status_t JpegEncodeTurbo::EncoderThread::createJPEGFromYV12(uint8_t* yuvBuffer,int yuvSize,int pad,
                                                                        unsigned char** jpegBuffer, unsigned long* jpegSize)
{
    int flags = TJFLAG_FASTUPSAMPLE;
    int needSize = 0,ret = 0;

    needSize = tjBufSizeYUV2(mEncoder->mWidth, pad, mEncoder->mHeight, TJSAMP_420);
    if (needSize != yuvSize) {
        ERROR("we detect yuv size: %d, but you give: %d, check again.\n",needSize,yuvSize);
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    ret = tjCompressFromYUV(mEncoder->mHandle, yuvBuffer,
                            mEncoder->mWidth, pad, mEncoder->mHeight, TJSAMP_420,
                            jpegBuffer, jpegSize, mEncoder->mQuality,flags);
    INFO("jpeg sink create one frame:ret:%d,jpeg_size:%d\n",ret,*jpegSize);
    if (ret < 0) {
        ERROR("compress to jpeg failed: %s\n", tjGetErrorStr());
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

/*static*/
mm_status_t JpegEncodeTurbo::EncoderThread::YUYV2YV12(uint8_t *yuyv, uint8_t *yv12,
                                                            uint32_t width, uint32_t height)
{
    uint8_t *py;
    uint8_t *pu;
    uint8_t *pv;
    //bytes per line of YUYV format
    uint32_t yuyv_linesize = width * 2;
    //bytes of uv per line of YV12 format
    uint32_t uv_lineszie = width / 2;
    uint32_t line_offset = 0;
    uint32_t next_line_offset = 0;
    uint32_t y_line_offset = 0;
    uint32_t y_next_line_offset = 0;
    uint32_t uv_offset = 0;

    py = yv12;//Y component's address of YV12
    pv = py + (width*height);//V component's address of YV12
    pu = pv + ((width*height) / 4); //U component's address of YV12

    uint32_t h = 0;
    uint32_t w = 0;

    //for YV12
    uint32_t y_row_offset = 0;//indicate current row of y component, [0, width]
    uint32_t uv_line_offset = 0;//indicate the current line of uv component, [0, width/2]
    uint32_t uv_row_offset = 0;//indicate the current row of uv component, between [0, height/2]

    //YUYV to  YV12,
    //keep all the Y value
    //resample each 1 pixel of u/v every 4 pixels
    for (h = 0; h<height; h += 2)
    {
        y_row_offset = 0;
        uv_row_offset = 0;
        line_offset = h * yuyv_linesize;//offset of Hth line
        next_line_offset = (h + 1) * yuyv_linesize;
        y_line_offset = h * width;//y component's offset of YV12
        y_next_line_offset = (h + 1) * width;
        uv_offset = uv_line_offset * uv_lineszie;//uv component's offset of YV12

        for (w = 0; w<yuyv_linesize; w += 4)
        {
            /*y00*/
            py[y_row_offset + y_line_offset] = yuyv[w + line_offset];
            /*y01*/
            py[(y_row_offset + 1) + y_line_offset] = yuyv[(w + 2) + line_offset];
            /*y10*/
            py[y_row_offset + y_next_line_offset] = yuyv[w + next_line_offset];
            /*y11*/
            py[(y_row_offset + 1) + y_next_line_offset] = yuyv[(w + 2) + next_line_offset];
            /*u0*/
            pu[uv_row_offset + uv_offset] = yuyv[(w + 3) + line_offset];
            /*v0*/
            pv[uv_row_offset + uv_offset] = yuyv[(w + 1) + line_offset];

            uv_row_offset++;
            y_row_offset += 2;
        }
        uv_line_offset++;
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


// decode Buffer
void JpegEncodeTurbo::EncoderThread::main()
{
    ENTER();
    int32_t photoCount = mEncoder->mPhotoCount;
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
            int32_t offset = 0, length = 0;
            unsigned long jpegSize;
            mediaInputBuffer->getBufferInfo((uintptr_t *)&yuvBuffer, &offset, &length, 1);
            length = mediaInputBuffer->size();
            INFO("read from source filter: buffer:%p, %d\n", yuvBuffer,length);

            //create the media buffer for output
            MediaBufferSP mediaOutputBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
            if (mediaInputBuffer->isFlagSet(MediaBuffer::MBFT_EOS)) {// EOS frame
                mediaOutputBuffer->setFlag(MediaBuffer::MBFT_EOS);
                if ((!yuvBuffer) || (length <= 0)) { //only send "eos"
                    mediaOutputBuffer->setSize(0);
                    mm_status_t ret = mEncoder->mWriter->write(mediaOutputBuffer);
                    if ((ret != MM_ERROR_SUCCESS) && (ret != MM_ERROR_EOS)) {
                        ERROR("encoder fail to write Sink:ret:%d",ret);
                        EXIT();
                    }
                    continue;
                }
            }
            uint8_t *jpegBuffer = mEncoder->mJpegBuffer.get();
            uint8_t *convertBuffer = mEncoder->mYUVConvertBuffer.get();
            //set the output media buffer pointer
            mm_status_t ret = MM_ERROR_UNKNOWN;
            if (mEncoder->mFourcc == 'YV12') {//YUV420P
                ret = createJPEGFromYV12(yuvBuffer, length, 1,
                                       &jpegBuffer, &jpegSize);
            } else if (mEncoder->mFourcc == 'YUYV') {
                //Y0 U0 Y1 V0 Y2 U1 Y3 V1
                YUYV2YV12(yuvBuffer, convertBuffer, mEncoder->mWidth, mEncoder->mHeight);
                length = mEncoder->mWidth * mEncoder->mHeight * 3 / 2;
                ret = createJPEGFromYV12(convertBuffer, length, 1,
                                       &jpegBuffer, &jpegSize);
            }
            if(ret != MM_ERROR_SUCCESS) {
                DEBUG_FOURCC("unsupport", mEncoder->mFourcc);
                mEncoder->notify(kEventError, MM_ERROR_UNKNOWN, 0, nilParam);
                EXIT();
            }

            if (jpegSize > 0) {

                uint8_t *midPtr = new uint8_t [jpegSize];
                if (!midPtr) {
                   ERROR("malloc midPtr failed\n");
                   mEncoder->notify(kEventError, MM_ERROR_UNKNOWN, 0, nilParam);
                   EXIT();
                }
                memcpy(midPtr, jpegBuffer, jpegSize);

                mediaOutputBuffer->setPts(0);
                mediaOutputBuffer->setDuration(1);
                mediaOutputBuffer->addReleaseBufferFunc(releaseOutputBuffer);
                int32_t t = (int32_t)jpegSize;
                mediaOutputBuffer->setBufferInfo((uintptr_t *)&midPtr, NULL, &t, 1);
                mediaOutputBuffer->setSize(t);
                if (mEncoder->mWriter->write(mediaOutputBuffer) != MM_ERROR_SUCCESS) {
                    ERROR("encoder fail to write Sink");
                    mEncoder->notify(kEventError, MM_ERROR_UNKNOWN, 0, nilParam);
                    EXIT();
                }


                photoCount--;
                if (!photoCount)
                   break;
            }//if (jpegSize > 0) {
         }else { //if (mediaInputBuffer) {
            VERBOSE("read NULL buffer from demuxer\n");
            usleep(10*1000);
         }

    }

    INFO("Output thread exited\n");
    EXIT();
}

// /////////////////////////////////////
JpegEncodeTurbo::JpegEncodeTurbo(const char *mimeType, bool isEncoder)
                                        : MMMsgThread(COMPONENT_NAME),
                                          mHandle(NULL),
                                          mWidth(0),
                                          mHeight(0),
                                          mFourcc(0),
                                          mQuality(100),
                                          mPhotoCount(1),
                                          mComponentName(COMPONENT_NAME),
                                          mIsPaused(true),
                                          mCondition(mLock)
{
}

JpegEncodeTurbo::~JpegEncodeTurbo()
{
    ENTER();

    EXIT();
}

mm_status_t JpegEncodeTurbo::release()
{
    ENTER();

    if (mHandle) {
        tjDestroy(mHandle);
        mHandle = NULL;
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t JpegEncodeTurbo::init()
{
    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret)
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void JpegEncodeTurbo::uninit()
{
    ENTER();

    MMMsgThread::exit();

    EXIT();
}

const char * JpegEncodeTurbo::name() const
{
    ENTER();
    return mComponentName.c_str();
}

mm_status_t JpegEncodeTurbo::addSource(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeVideo) {
        mReader = component->getReader(kMediaTypeVideo);
        if (mReader) {
            MediaMetaSP metaData;
            metaData = mReader->getMetaData();
            if (metaData) {
                mInputMetaData = metaData->copy();
                mOutputMetaData = metaData->copy();
                EXIT_AND_RETURN(MM_ERROR_SUCCESS);
            }
        }
    }
    EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
}

mm_status_t JpegEncodeTurbo::addSink(Component * component, MediaType mediaType)
{
    ENTER();
    if (component && mediaType == kMediaTypeVideo) {
        mWriter = component->getWriter(kMediaTypeVideo);
        if (mWriter && mOutputMetaData) {
            mWriter->setMetaData(mOutputMetaData);
        }
    }
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t JpegEncodeTurbo::prepare()
{
    ENTER();
    postMsg(JET_MSG_prepare, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t JpegEncodeTurbo::start()
{
    ENTER();
    postMsg(JET_MSG_start, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t JpegEncodeTurbo::stop()
{
    ENTER();
    postMsg(JET_MSG_stop, 0, NULL);
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t JpegEncodeTurbo::reset()
{
    ENTER();
    postMsg(JET_MSG_reset, 0, NULL);
    mIsPaused = true;
    EXIT_AND_RETURN(MM_ERROR_ASYNC);
}

mm_status_t JpegEncodeTurbo::setParameter(const MediaMetaSP & meta)
{
    ENTER();

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_JPEG_QUALITY) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mQuality = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mQuality);
            continue;
        }

        if ( !strcmp(item.mName, MEDIA_ATTR_PHOTO_COUNT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mPhotoCount = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mPhotoCount);
            continue;
        }
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

void JpegEncodeTurbo::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    ASSERT(mOutputMetaData);

    //#0 get the parameters width && height && format
    int ret = mOutputMetaData->getInt32(MEDIA_ATTR_WIDTH, mWidth);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_WIDTH);
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    ret = mOutputMetaData->getInt32(MEDIA_ATTR_HEIGHT, mHeight);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_HEIGHT);
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }
    ret = mOutputMetaData->getInt32(MEDIA_ATTR_COLOR_FOURCC, mFourcc);
    if (!ret) {
        ERROR("fail to get int32_t data %s\n", MEDIA_ATTR_COLOR_FOURCC);
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    INFO("prepare width:%d, height:%d\n",mWidth, mHeight);
    DEBUG_FOURCC("format: ", mFourcc);
    if ((mWidth <= 0)||
         (mWidth >= JET_MAX_WIDTH) ||
         (mHeight <= 0) ||
         (mHeight >= JET_MAX_HEIGHT) ) {
        ERROR("get width or height is unaviable\n",mWidth,mHeight);
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    //#1 malloc some variables && open jpeg encoder
    mJpegBuffer.reset(new uint8_t [mWidth * mHeight * 4]);
    mYUVConvertBuffer.reset(new uint8_t[(mWidth+15) * (mHeight+15) * 3]);
    if (!mJpegBuffer || !mYUVConvertBuffer) {
        ERROR("malloc jpeg buffer failed\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    mHandle = tjInitCompress();
    if (!mHandle) {
        ERROR("unable to open the jpeg compress\n");
        notify(kEventPrepareResult, MM_ERROR_OP_FAILED, 0, nilParam);
        EXIT();
    }

    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void JpegEncodeTurbo::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    MMAutoLock locker(mLock);
    if (!mEncoderThread) {
        // create thread to decode buffer
        mEncoderThread.reset (new EncoderThread(this), MMThread::releaseHelper);
        mEncoderThread->create();
    }
    mIsPaused = false;
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    mEncoderThread->signalContinue();
    EXIT();
}

void JpegEncodeTurbo::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mEncoderThread) {
            mEncoderThread->signalExit();
            mEncoderThread.reset();
        }
    }
    notify(kEventStopped, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}

void JpegEncodeTurbo::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    {
        mIsPaused = true;
        if (mEncoderThread) {
            mEncoderThread->signalExit();
            mEncoderThread.reset();
        }
    }
    MMAutoLock locker(mLock);
    release();
    mReader.reset();
    mWriter.reset();
    notify(kEventResetComplete, MM_ERROR_SUCCESS, 0, nilParam);
    EXIT();
}
}

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::JpegEncodeTurbo *codecComponent = new YUNOS_MM::JpegEncodeTurbo(mimeType, isEncoder);
    if (codecComponent == NULL) {
          return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(codecComponent);
}

void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}

}

