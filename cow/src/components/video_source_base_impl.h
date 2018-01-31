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

#include "multimedia/mm_debug.h"
#include "multimedia/media_buffer.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_cpp_utils.h"

#include <unistd.h>

MM_LOG_DEFINE_MODULE_NAME("VideoSource");

namespace YUNOS_MM {
//#define VERBOSE INFO

#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() INFO(">>>\n")
#define EXIT1() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

/*
template <typename frameGenerator>
BEGIN_MSG_LOOP(VideoSourceBase<frameGenerator>)
    MSG_ITEM(CAMERA_MSG_prepare, onPrepare)
END_MSG_LOOP()
*/

template <typename frameGenerator>
VideoSourceBase<frameGenerator>::VideoSourceBase(const char *threadName, bool async, bool asyncStop)
        : MMMsgThread(threadName),
          mState(UNINITIALIZED),
          mType(NONE),
          mStreamEOS(false),
          mAsync(async),
          mAsyncStop(asyncStop),
          mReaderExist(false),
          mCondition(mLock),
          mRequestIDR(false) {
    ENTER();

    mSourceFormat = MediaMeta::create();

    // FIXME, video size/format is hard coded here
    // set default parameter
    mSourceFormat->setInt32(MEDIA_ATTR_WIDTH, VIDEO_SOURCE_DEFAULT_WIDTH);
    mSourceFormat->setInt32(MEDIA_ATTR_HEIGHT, VIDEO_SOURCE_DEFAULT_HEIGHT);
    mSourceFormat->setFloat(MEDIA_ATTR_FRAME_RATE, VIDEO_SOURCE_DEFAULT_FPS);
    mSourceFormat->setInt32(MEDIA_ATTR_COLOR_FOURCC, VIDEO_SOURCE_DEFAULT_CFORMAT);
    mSourceFormat->setInt32(MEDIA_META_BUFFER_TYPE, MediaBuffer::MBT_RawVideo);

    EXIT();
}

template <typename frameGenerator>
VideoSourceBase<frameGenerator>::~VideoSourceBase() {
    ENTER();
    EXIT();
}

template <typename frameGenerator>
const char *VideoSourceBase<frameGenerator>::name() const {
    return mComponentName.c_str();
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::init() {
    ENTER();
    int ret;

    if (mState != UNINITIALIZED)
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);

    ret = MMMsgThread::run();

    if (!ret)
        mState = INITIALIZED;

    EXIT_AND_RETURN(ret == 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

template <typename frameGenerator>
void VideoSourceBase<frameGenerator>::uninit() {
    ENTER();
    MMMsgThread::exit();
    mState = UNINITIALIZED;
    EXIT();
}

#define CAMERA_MSG_BRIDGE(msg, param1, param2, async) \
    mm_status_t status;                                 \
    if (async) {                                       \
        status = MM_ERROR_ASYNC;                        \
        postMsg(msg, param1, param2);                   \
    } else {                                            \
        status = processMsg(msg, param1, param2);       \
    }                                                   \
    INFO("%s return status %d", __func__, status);

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::processMsg(uint32_t msg, param1_type param1, param2_type param2, param3_type param3) {
    param1_type rsp_param1;
    param2_type rsp_param2;

    if (sendMsg(msg, param1, param2, param3, &rsp_param1, &rsp_param2, NULL)) {
        EXIT_AND_RETURN(MM_ERROR_OP_FAILED);
    }

    if (rsp_param1)
        EXIT_AND_RETURN(rsp_param1);

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::prepare() {

    ENTER();

    CAMERA_MSG_BRIDGE(CAMERA_MSG_prepare, 0, NULL, mAsync);


    // TODO remove
    EXIT_AND_RETURN(status);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::start() {
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock locker(mLock);

    if (mState == STARTED) {
        INFO("already in state %d, start return success", mState);
        EXIT_AND_RETURN(status);
    }

    if (mState != PREPARED) {
        ERROR("cannot start in state %d", mState);
        status = MM_ERROR_INVALID_STATE;
        EXIT_AND_RETURN(status);
    }

    // TODO should we do that with mLock locked?
    status = mFrameSourceSP->start();

    if (status == MM_ERROR_SUCCESS)
        mState = STARTED;

    mStreamEOS = false;

    INFO("%s return status %d", __func__, status);
    EXIT_AND_RETURN(status);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::stopInternal() {
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;

    if (mState == STOPED) {
        INFO("already in state %d, stop return success", mState);
    } else if (mState != STARTED) {
        ERROR("cannot stop in state %d", mState);
        status = MM_ERROR_INVALID_STATE;
    }

    mStreamEOS = true;

    // TODO should we do that with mLock locked?
    status = mFrameSourceSP->stop();

    EXIT_AND_RETURN(status);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::stop() {
    ENTER();
    //mm_status_t status;
    //MMAutoLock locker(mLock);
    CAMERA_MSG_BRIDGE(CAMERA_MSG_stop, 0, NULL, mAsyncStop);
    EXIT_AND_RETURN(status);
}




template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::reset() {
    ENTER();
    mm_status_t status;
    MMAutoLock locker(mLock);

    status = stopInternal();
    if (status == MM_ERROR_SUCCESS)
        mState = STOPED;

    mFrameSourceSP->reset();
    mType = NONE;
    mStreamEOS = false;
    mReaderExist = false;

    INFO("%s return status %d", __func__, status);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::flush() {
    ENTER();
    mm_status_t status;
    MMAutoLock locker(mLock);

    if (mState != STARTED) {
        WARNING("flush in state %d, return success", mState);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    status = mFrameSourceSP->flush();
    mStreamEOS = false;

    INFO("%s return status %d", __func__, status);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::signalEOS() {
    ENTER();
    mm_status_t status = MM_ERROR_SUCCESS;
    MMAutoLock locker(mLock);

    if (mState != STARTED) {
        WARNING("flush in state %d, return success", mState);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    //status = mFrameSourceSP->signalEOS();
    mStreamEOS = true;

    INFO("%s return status %d", __func__, status);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

#define SETPARAM_BEGIN()\
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {\
        const MediaMeta::MetaItem & item = *i;

#define CHECK_TYPE(_val_type)\
        if ( item.mType != MediaMeta::_val_type ) {\
            WARNING("invalid type for %s, expect: %s, real: %d\n", item.mName, #_val_type, item.mType);\
            continue;\
        }

#define SETPARAM_I32(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int32);\
        mSourceFormat->setInt32(_key_name, item.mValue.ii);\
        INFO("setparam key: %s, val: %d\n", item.mName, item.mValue.ii);\
        continue;\
    }

#define SETPARAM_I64(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Int64);\
        mSourceFormat->setInt64(_key_name, item.mValue.ld);\
        INFO("setparam key: %s, val: %" PRId64 "\n", item.mName, item.mValue.ld);\
        continue;\
    }

#define SETPARAM_F(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Float);\
        mSourceFormat->setFloat(_key_name, item.mValue.f);\
        INFO("setparam key: %s, val: %f\n", item.mName, item.mValue.f);\
        continue;\
    }

#define SETPARAM_STRING(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_String);\
        mSourceFormat->setString(_key_name, item.mValue.str);\
        INFO("setparam key: %s, val: %s\n", item.mName, item.mValue.str);\
        continue;\
    }

#define SETPARAM_POINTER(_key_name)\
    if ( !strcmp(item.mName, _key_name) ) {\
        CHECK_TYPE(MT_Pointer);\
        mSourceFormat->setPointer(_key_name, item.mValue.ptr);\
        INFO("setparam key: %s, val: %p\n", item.mName, item.mValue.ptr);\
        continue;\
    }

#define SETPARAM_END() }

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::setParameter(const MediaMetaSP & meta) {
    ENTER();

    // FIXME, "uri" is a must to have parameter, it's better set in setUri.
    // FIXME,  mType can be deduced from uri
    SETPARAM_BEGIN()
#if 1 // remove it, use setUri
        SETPARAM_STRING("uri")
        {
            if ( !strcmp(item.mName, "type") ) {
                CHECK_TYPE(MT_String);
                const char * s = item.mValue.str;
                INFO("setparam key: %s, val: %s\n", item.mName, s);
                if (!strcmp(s,"file"))
                    mType = YUVFILE;
                else if (!strcmp(s,"auto"))
                    mType = AUTO;
                else if (!strcmp(s,"camera://0"))
                    mType = CAMERA_BACK;
                else if (!strcmp(s,"camera://1"))
                    mType = CAMERA_FRONT;
                else if (!strcmp(s,"camera"))
                    mType = CAMERA_BACK;
                else {
                    ERROR("you should choose correctly input type:%s\n",s);
                    EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
                }
                continue;
            }
        }
#endif
        SETPARAM_I32(MEDIA_ATTR_WIDTH)
        SETPARAM_I32(MEDIA_ATTR_HEIGHT)
        SETPARAM_F(MEDIA_ATTR_FRAME_RATE)
        SETPARAM_I32(MEDIA_ATTR_COLOR_FORMAT)
        SETPARAM_I32(MEDIA_ATTR_COLOR_FOURCC)
        SETPARAM_I32(MEDIA_ATTR_TIME_LAPSE_ENABLE)
        SETPARAM_F(MEDIA_ATTR_TIME_LAPSE_FPS)
        SETPARAM_POINTER(MEDIA_ATTR_VIDEO_SURFACE)
        SETPARAM_POINTER(MEDIA_ATTR_CAMERA_OBJECT)
        SETPARAM_POINTER(MEDIA_ATTR_RECORDING_PROXY_OBJECT)
        SETPARAM_I64(MEDIA_ATTR_START_TIME)
        SETPARAM_I32("raw-data")
    SETPARAM_END()

    MMAutoLock locker(mLock);
    if (mFrameSourceSP) {
        mm_status_t status = mFrameSourceSP->setParameters(mSourceFormat);
        if (status != MM_ERROR_SUCCESS) {
            EXIT_AND_RETURN(status);
        }
    }

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::getParameter(MediaMetaSP & meta) const {
    ENTER();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

#if defined (__MM_YUNOS_CNTRHAL_BUILD__) || defined (__MM_YUNOS_YUNHAL_BUILD__) || defined (__MM_YUNOS_LINUX_BSP_BUILD__)
template <typename frameGenerator>
void VideoSourceBase<frameGenerator>::signalBufferReturned(void* anb, int64_t pts) {
    ENTER();

    // need lock?
    if (mFrameSourceSP)
        mFrameSourceSP->signalBufferReturned(anb, pts);
    EXIT();
}
#endif

template <typename frameGenerator>
MediaBufferSP VideoSourceBase<frameGenerator>::getMediaBuffer() {
    ENTER();
    MediaBufferSP mediaBufferSP;

    if (mState != STARTED) {
        WARNING("invalid state, state %d, sleep half media duration before return\n", mState);
        usleep(1000 * (1000 / VIDEO_SOURCE_DEFAULT_FPS) / 2);
        return mediaBufferSP;
    }

    // need lock?
    if (mFrameSourceSP)
        mediaBufferSP = mFrameSourceSP->getMediaBuffer();

    if (mediaBufferSP && mRequestIDR) {
        // request i frame
        DEBUG("request idr frame");
        mediaBufferSP->setFlag(MediaBuffer::MBFT_KeyFrame);
        mRequestIDR = false;
    }
    //EXIT_AND_RETURN(mediaBufferSP);
    return mediaBufferSP;
}

template <typename frameGenerator>
void VideoSourceBase<frameGenerator>::asyncFuncExit(Component::Event event, int param1, int param2, uint32_t rspId) {
    ENTER();
    if (rspId) {
        postReponse(rspId, param1, NULL);
        EXIT();
    }

    MMParamSP param;

    notify(event, param1, param2, param);
    EXIT();
}

template <typename frameGenerator>
void VideoSourceBase<frameGenerator>::onStop(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    mm_status_t status;

    MMAutoLock locker(mLock);
    status = stopInternal();
    if (status == MM_ERROR_SUCCESS)
        mState = STOPED;

    INFO("%s return status %d", __func__, status);
    //EXIT_AND_RETURN(status);
    asyncFuncExit(kEventStopped, status, 0, rspId);
    EXIT();

}

template <typename frameGenerator>
void VideoSourceBase<frameGenerator>::onPrepare(param1_type param1, param2_type param2, uint32_t rspId) {
    ENTER();
    mm_status_t status;
    MMAutoLock locker(mLock);

    if (mState != INITIALIZED) {
        ERROR("cannot prepare in state %d\n", mState);
        asyncFuncExit(kEventPrepareResult, MM_ERROR_IVALID_OPERATION, 0, rspId);
        EXIT();
    }

    mFrameSourceSP.reset(new frameGenerator(this));

    int32_t width;
    int32_t height;
    int32_t fourcc;
    float fps;
    float captureFps = 0;
    int32_t lapseEnable = false;

    // the get always success since we init these parameter in construct func
    mSourceFormat->getInt32(MEDIA_ATTR_WIDTH, width);
    mSourceFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
    mSourceFormat->getFloat(MEDIA_ATTR_FRAME_RATE, fps);
    mSourceFormat->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
    mSourceFormat->getInt32(MEDIA_ATTR_TIME_LAPSE_ENABLE, lapseEnable);
    mSourceFormat->getFloat(MEDIA_ATTR_TIME_LAPSE_FPS, captureFps);

    if (mType == YUVFILE) {
        const char *uri = NULL;
        mSourceFormat->getString("uri",uri);
        if (uri) {
            mUri = uri;
        } else if (mUri.empty())
            mUri = "none";
    }
    INFO("VideoSourceBase<frameGenerator>::onPrepare w:%d,h:%d,fps:%f,fourcc:%d,uri:%s, captureFps: %0.2f, lapseEnable: %d\n",
            width,height,fps,fourcc,mUri.c_str(), captureFps, lapseEnable);

    status = mFrameSourceSP->setParameters(mSourceFormat);
    if (status != MM_ERROR_SUCCESS) {
        asyncFuncExit(kEventPrepareResult, status, 0, rspId);
        EXIT();
    }

    status = mFrameSourceSP->configure(mType, mUri.c_str(),
                                     width, height, fps, fourcc);

    if (status == MM_ERROR_SUCCESS)
        mState = PREPARED;

    asyncFuncExit(kEventPrepareResult, status, 0, rspId);
    EXIT();
}

template <typename frameGenerator>
Component::ReaderSP VideoSourceBase<frameGenerator>::getReader(MediaType mediaType) {
    ENTER();
    ReaderSP readerSP;

    if (mediaType != kMediaTypeVideo) {
        ERROR("mediaType(%d) is not supported", mediaType);
        //EXIT_AND_RETURN(readerSP);
        return readerSP;
    }

    if (mReaderExist) {
        ERROR("cannot get reader, alread exist");
        //EXIT_AND_RETURN(readerSP);

        return readerSP;
    }

    readerSP.reset(new GraphicBufferReader(this));
    mReaderExist = true;

    //EXIT_AND_RETURN(readerSP);

    return readerSP;
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::setUri(const char * uri,
            const std::map<std::string, std::string> * headers) {
    ENTER();

    mUri = uri;
    if (!strncmp(uri,"file", 4))
        mType = YUVFILE;
    else if (!strncmp(uri,"auto", 4))
        mType = AUTO;
    else if (!strncmp(uri,"camera://0", 10))
        mType = CAMERA_BACK;
    else if (!strncmp(uri,"camera://1", 10))
        mType = CAMERA_FRONT;
    else if (!strncmp(uri,"camera", 6))
        mType = CAMERA_BACK;
    else if (!strncmp(uri,"wfd", 3))
        mType = WFD;
    else {
        ERROR("you should choose correctly input type:%s\n",uri);
        EXIT_AND_RETURN(MM_ERROR_INVALID_PARAM);
    }

    DEBUG("mUri: %s, type %d\n", mUri.c_str(), mType);
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::setUri(int fd, int64_t offset, int64_t length) {
    ENTER();

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
VideoSourceBase<frameGenerator>::GraphicBufferReader::GraphicBufferReader(VideoSourceBase *comp)
    : mComponent(comp) {
    ENTER();

    EXIT();
}

template <typename frameGenerator>
VideoSourceBase<frameGenerator>::GraphicBufferReader::~GraphicBufferReader() {
    ENTER();

    EXIT();
}

template <typename frameGenerator>
mm_status_t VideoSourceBase<frameGenerator>::GraphicBufferReader::read(MediaBufferSP & buffer) {
    ENTER();

    MediaBufferSP mediaBufferSP;

    mediaBufferSP =  mComponent->getMediaBuffer();

    if (!mediaBufferSP) {
        EXIT_AND_RETURN(MM_ERROR_AGAIN);
    }

    buffer = mediaBufferSP;

    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}

template <typename frameGenerator>
MediaMetaSP VideoSourceBase<frameGenerator>::GraphicBufferReader::getMetaData() {
    ENTER();
    MediaMetaSP metaSP = MediaMeta::create();

    metaSP->setString(MEDIA_ATTR_MIME, MEDIA_MIMETYPE_VIDEO_RAW);

    int32_t width, height, format, fourcc, bufferType;
    int32_t repeat = 0;
    float fps;

    mComponent->mSourceFormat->getInt32(MEDIA_ATTR_WIDTH, width);
    mComponent->mSourceFormat->getInt32(MEDIA_ATTR_HEIGHT, height);
    mComponent->mSourceFormat->getInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
    mComponent->mSourceFormat->getInt32(MEDIA_META_BUFFER_TYPE, bufferType);
    mComponent->mSourceFormat->getFloat(MEDIA_ATTR_FRAME_RATE, fps);

    INFO("get meta data: %dx%d, forcc %x, bufferType %d, fps %f",
         width, height, fourcc, bufferType, fps);

    metaSP->setInt32(MEDIA_ATTR_WIDTH, width);
    metaSP->setInt32(MEDIA_ATTR_HEIGHT, height);
    metaSP->setInt32(MEDIA_ATTR_COLOR_FOURCC, fourcc);
    metaSP->setInt32(MEDIA_META_BUFFER_TYPE, bufferType);
    metaSP->setFloat(MEDIA_ATTR_FRAME_RATE, fps);

    if (mComponent->mSourceFormat->getInt32(MEDIA_ATTR_COLOR_FORMAT, format))
        metaSP->setInt32(MEDIA_ATTR_COLOR_FORMAT, format);

    if (mComponent->mSourceFormat->getInt32("repeat", repeat)) {
        metaSP->setInt32("repeat", repeat);
        DEBUG("get meta data: repeat %d", repeat);
    }

    // TODO add fps meta

    //EXIT_AND_RETURN(metaSP);
    return metaSP;
}
} // YUNOS_MM

