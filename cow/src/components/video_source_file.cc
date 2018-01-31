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

#include "video_source_file.h"
#include "video_source_base_impl.h"

namespace YUNOS_MM {
static const char * COMPONENT_NAME = "VideoSource";
// #define DUMP_DATA_DEBUG

#undef DEBUG_PERF
//#define DEBUG_PERF

#ifdef DEBUG_PERF
#define TIME_TAG(str) printTimeMs(str)
void printTimeMs(const char *str) {

    uint64_t nowMs;
    struct timeval t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    nowMs = t.tv_sec * 1000LL + t.tv_nsec / 1000000LL;

    INFO("%s at %" PRId64 "ms\n", str, (nowMs % 10000));
}
#else
#define TIME_TAG(str)
#endif

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

BEGIN_MSG_LOOP(VideoSource)
    MSG_ITEM(CAMERA_MSG_prepare, onPrepare)
    MSG_ITEM(CAMERA_MSG_stop, onStop)
END_MSG_LOOP()

VideoSource::VideoSource() : VideoSourceBase(COMPONENT_NAME) {
    ENTER();

    mComponentName = COMPONENT_NAME;

    EXIT();
}

VideoSource::~VideoSource() {
    ENTER();

    EXIT();
}

void VideoSource::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();

    // TODO add specific prepare

    VideoSourceBase<FrameGenerator>::onPrepare(param1, param2, rspId);

    EXIT();
}

#define FRAMEGENERATOR_CREATEED   1
#define FRAMEGENERATOR_CONFIGED   2
#define FRAMEGENERATOR_STARTED    4
FrameGenerator::FrameGenerator(SourceComponent *comp)
    : mTag(FRAMEGENERATOR_CREATEED),
      mYUVFile(NULL),
#if FRAMEGENERATOR_DUMP_FILE
      mDumpYUVFile(NULL),
#endif
      mType(NONE),
      mWidth(0),
      mHeight(0),
      mFrameRate(30.0f),
      mFrameSize(0),
      mPlaneCount(0),
      mTargetFrameCount(100),
      mFrameNum(0),
      mFramePts(0),
      mFrameCondition(mFrameLock) {
    ENTER();

    mComponent = DYNAMIC_CAST<VideoSource *>(comp);

    EXIT();
}

FrameGenerator::~FrameGenerator()
{
    ENTER();
    mFrameCondition.signal();
    EXIT();
}

mm_status_t FrameGenerator::configure(SourceType type,  const char *fileName,
                                         int width, int height,
                                         float fps, uint32_t fourcc)
{
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    MMAutoLock locker(mFrameLock);

    //#0 check the available parameters
    if ((type != YUVFILE) &&
        (type != CAMERA_BACK) &&
        (type != CAMERA_FRONT)){
        ERROR("FrameGenerator::configure unavailable type:%d\n",type);
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }
    if((type == YUVFILE)&&(!fileName)){
        ERROR("FrameGenerator::configure your file is null\n");
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    // FIXME, either filesrc or camerasrc is decided in setUri()
    INFO("FrameGenerator::configure type:%d,%s,width:%d,height:%d,fps:%f,fourcc:0x%x,status:%d\n",
         type,fileName,width,height,fps,fourcc,mTag);

    mType = type;
    mUrl = fileName;
    mWidth = width;
    mHeight = height;
    mFrameRate = fps;
    mFourcc = fourcc;

    mComponent->mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, MediaBuffer::MBT_RawVideo);

    switch(fourcc) {
    case VIDEO_SOURCE_CORLOR_FMT_YV12:
    case VIDEO_SOURCE_CORLOR_FMT_YV21:
    case VIDEO_SOURCE_CORLOR_FMT_I420:
        mStrides[0] = mWidth;
        mStrides[1] = (mWidth+1)/2;
        mStrides[2] = (mWidth+1)/2;
        mPlaneSize[0] = mStrides[0] * mHeight;
        mPlaneSize[1] = mStrides[1] * ((mHeight+1)/2);
        mPlaneSize[2] = mStrides[2] * ((mHeight+1)/2);
        mFrameSize = mPlaneSize[0] + mPlaneSize[1] + mPlaneSize[2];
        mPlaneCount = 3;
        break;
    case VIDEO_SOURCE_CORLOR_FMT_NV12:
    case VIDEO_SOURCE_CORLOR_FMT_NV21:
        mStrides[0] = mWidth;
        mStrides[1] = mWidth + (mWidth&0x1) ;
        mStrides[2] = 0;
        mPlaneSize[0] = mStrides[0] * mHeight;
        mPlaneSize[1] = mStrides[1] * ((mHeight+1)/2);
        mPlaneSize[2] = 0;
        mFrameSize = mPlaneSize[0] + mPlaneSize[1];
        mPlaneCount = 2;
        break;
    case VIDEO_SOURCE_CORLOR_FMT_YUY2:
    case VIDEO_SOURCE_CORLOR_FMT_YUYV:
    case VIDEO_SOURCE_CORLOR_FMT_YVYU:
        mStrides[0] = (mWidth + (mWidth&0x1)) * 2;
        mPlaneSize[0] = mStrides[0] * mHeight;
        mPlaneSize[1]  = mPlaneSize[2] = 0;
        mFrameSize = mPlaneSize[0];
        mPlaneCount = 1;
        break;
    default:
        ERROR("unsupported fourcc: 0x%x\n", mFourcc);
        status = MM_ERROR_INVALID_PARAM;
        break;
    }

    // TODO retrive Camera from mComponent and do some configure
    if(status == MM_ERROR_INVALID_PARAM){
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    mTag |= FRAMEGENERATOR_CONFIGED;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);

}

mm_status_t FrameGenerator::start() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    // TODO start Camera
    //#0 check the state
    if(!(mTag & FRAMEGENERATOR_CONFIGED)){
        ERROR("FrameGenerator::start you should call config:%d",mTag);
        EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
    }

    //#1 open raw video data file if mType ==0
    if(mType == YUVFILE){
        mYUVFile = fopen (mUrl.c_str()+7,"rb"); // skip the "file://" prefix
        if(!mYUVFile){
            ERROR("FrameGenerator::start unable to open yuv file:%s, path:%s", mUrl.c_str(), mUrl.c_str()+7);
            EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
        }
    }

    mTag |= FRAMEGENERATOR_STARTED;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t FrameGenerator::stop() {
    ENTER();
    MMAutoLock locker(mFrameLock);
    // TODO stop Camera
    //#0 check the status
    if (!(mTag & FRAMEGENERATOR_STARTED)){
        ERROR("FrameGenerator::stop you should call start at first:%d\n",mTag);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    //#1 wake up the thread
    mFrameCondition.signal();

    //#2 close yuv file if need
    if( mType == YUVFILE){
        if(mYUVFile)
            fclose(mYUVFile);
        mYUVFile = NULL;
    }
#if FRAMEGENERATOR_DUMP_FILE
    if (mDumpYUVFile)
        fclose(mDumpYUVFile);
    mDumpYUVFile = NULL;
#endif

    //#3 reset some variables
    mFrameNum = 0;
    mFramePts = 0;

    mTag &= (~(FRAMEGENERATOR_STARTED));
    INFO("FrameGenerator::stop you call stop:ok:%d\n",mTag);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t FrameGenerator::flush() {
    ENTER();
    MMAutoLock locker(mFrameLock);

    // TODO flush Camera, maybe not needed

    mFrameCondition.signal();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

mm_status_t FrameGenerator::reset() {
    ENTER();

    INFO("FrameGenerator::reset mTag:%d\n",mTag);

    //#0 check the state
    if(mTag & FRAMEGENERATOR_STARTED){
       if(stop() != MM_ERROR_SUCCESS){
         EXIT_AND_RETURN(MM_ERROR_IVALID_OPERATION);
       }
    }

    MMAutoLock locker(mFrameLock);
    // TODO reset Camera
    mFrameCondition.signal();

    //#1 clean some parameters
    mWidth = 0;
    mHeight = 0;
    mFourcc = 0;
    mFrameRate = 0.0;
    mUrl = "";//clean it ???

    mTag = FRAMEGENERATOR_CREATEED;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

#if defined (__MM_YUNOS_CNTRHAL_BUILD__)
void FrameGenerator::signalBufferReturned(void* anb, int64_t pts) {
    ENTER();
    /*
    {
        MMAutoLock locker(mFrameLock);
        for (i = 0; i < mInputBufferCount; i++) {
            if (anb == mBufferInfo[i].anb)
                break;
        }

        if (i == mInputBufferCount) {
            WARNING("cannot find anb to release\n");
            EXIT();
        }

        mBufferInfo[i].state = OWNED_BY_US;
        mBufferInfo[i].pts = pts;
        VERBOSE("signal buffer return %d %" PRId64 "\n", i, pts);
    }
    */
    mFrameCondition.broadcast();
    EXIT();
}
#else
#endif

MediaBufferSP FrameGenerator::getMediaBuffer()
{
    ENTER();
    uint8_t *buf;
    MediaBufferSP mediaBuf;
    MediaMetaSP meta;

    INFO("FrameGenerator::getMediaBuffer mTag:%d,frameNum:%d,TargetNum:%d\n",
         mTag,mFrameNum,mTargetFrameCount);

    if (mFrameNum > mTargetFrameCount)  return MediaBufferSP((MediaBuffer*)NULL);

    // early return after reach user specified buffer count
    if (mFrameNum == mTargetFrameCount) {
        if (mType == YUVFILE)
            mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
        else {
            ERROR("don't know to do what\n");
            return mediaBuf;
        }

        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
        mediaBuf->setSize(0);
        INFO("FrameGenerator::getMediaBuffer video source send eos\n");
        mFrameNum ++;
        return mediaBuf;
    }

    //mFramePts += 1.0f / mFrameRate * 1000000; this is a bug!!!
    mFramePts = ((mFrameNum * 1.0f) / mFrameRate) * 1000000;

    if ( mType ==  YUVFILE){
        if (mYUVFile) {
            // #0 malloc the space to read the file
            buf = (uint8_t*)malloc(mFrameSize);
            if (!buf) {
                ERROR("FrameGenerator::getMediaBuffer fail to alloc buffer for new video frame\n");
                return mediaBuf;
            }

            //#1 read the file from file
            uint32_t readSize = fread(buf, 1, mFrameSize, mYUVFile);
            if (readSize < mFrameSize) {
                fseek(mYUVFile, 0, SEEK_SET);
                readSize = fread(buf, 1, mFrameSize, mYUVFile);
            }

            //#2 create the media buffer
            uintptr_t data[3];
            int32_t offset[3];
            int32_t stride[3];
            uint32_t i = 0;

            mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_RawVideo);
            for (i=0; i< mPlaneCount; i++) {
                offset[i] = 0;
                stride[i] = mStrides[i];
                data[i] = (uintptr_t)buf;
                if (i>0) {
                    data[i] += mPlaneSize[i-1];
                }
            }

            mediaBuf->setBufferInfo((uintptr_t *)data, offset, stride, mPlaneCount);
            mediaBuf->setSize(mFrameSize);
            mediaBuf->addReleaseBufferFunc(releaseMediaBuffer);

#ifdef DUMP_DATA_DEBUG
            hexDump(buf, mFrameSize, 32);
#endif
       }//if (mYUVFile) {
       else {
            ERROR("FrameGenerator::getMediaBuffer yuv file hasn't been opened\n");
            return mediaBuf;
        }
    }

    mediaBuf->setPts(mFramePts);

    //save the mType into releaseMediaBuffer
    meta = mediaBuf->getMediaMeta();
    meta->setInt32("source-type", (int)mType);

    mFrameNum++;
    return mediaBuf;
}

/* static */ bool FrameGenerator::releaseMediaBuffer(MediaBuffer *mediaBuf) {
    ENTER();
    uint8_t *buf = NULL;
    MediaMetaSP meta;
    int i;
    SourceType type;

    meta = mediaBuf->getMediaMeta();
    meta->getInt32("source-type",i);
    type = (SourceType)i;
    INFO("FrameGenerator::releaseMediaBuffer type:%d\n",type);

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&buf, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        EXIT_AND_RETURN(false);
    }

    delete buf;

    EXIT_AND_RETURN(true);

}

} // YUNOS_MM

extern "C" {

YUNOS_MM::Component *createComponent(const char* mime, bool encode) {
    YUNOS_MM::Component *comp;

    comp = new YUNOS_MM::VideoSource();

    INFO("Video Source Component is created");

    return comp;
}

void releaseComponent(YUNOS_MM::Component *component) {
    INFO("Video Source Component is deleted");
    delete component;
}

}
