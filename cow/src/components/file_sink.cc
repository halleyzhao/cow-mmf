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
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include <multimedia/mmparam.h>
#include <multimedia/media_meta.h>
#include <multimedia/media_buffer.h>

#include "multimedia/media_attr_str.h"
#include "file_sink.h"

#ifndef MM_LOG_OUTPUT_V
//#define MM_LOG_OUTPUT_V
#endif
#include <multimedia/mm_debug.h>


namespace YUNOS_MM {

DEFINE_LOGTAG(FileSink)
DEFINE_LOGTAG(FileSink::FileSinkWriter)


#define ENTER() INFO(">>>\n")
#define FLEAVE() do {INFO(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() VERBOSE(">>>\n")
#define FLEAVE1() do {VERBOSE(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE1(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define DEFAULT_PREFIX "VID"
#define DEFAULT_EXTENSION "mp4"
#define IMAGE_PREFIX "IMG"
#define IMAGE_EXTENSION "jpg"
#define AMR_EXTENSION "amr"
#define M4A_EXTENSION "m4a"
#define AUDIO_PREFIX "AUD"
#define OPUS_EXTENSION "opus"

/*static*/ std::string FileSink::getMediaFileName(const char*url, const char*prefix, const char*extension)
{
    struct stat statbuf;
    if (stat(url, &statbuf) == 0) {
        if (S_ISDIR(statbuf.st_mode)) {
            char             filePath[256] = {0};
            struct timeval   tv;
            struct timezone  tz;
            struct tm       *p;

            gettimeofday(&tv, &tz);
            p = localtime(&tv.tv_sec);
            snprintf(filePath, sizeof(filePath), "%s%s_%02d%02d%02d_%02d%02d%02d_%03d.%s",
                               url,
                               prefix,
                               1900+p->tm_year,
                               1+p->tm_mon,
                               p->tm_mday,
                               p->tm_hour,
                               p->tm_min,
                               p->tm_sec,
                               (int)(tv.tv_usec/1000),
                               extension);
            return filePath;
        }
    }
    // consider as file path
    return url;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
mm_status_t FileSink::FileSinkWriter::write(const MediaBufferSP &buffer) {
    ENTER1();
    MMAutoLock locker(mSink->mLock);
    mm_status_t status = MM_ERROR_UNKNOWN;

    if (!buffer) {
        FLEAVE_WITH_CODE(status);
    }

    MediaMetaSP meta = buffer->getMediaMeta();
    int64_t fileOffset = 0;
    int32_t fileWhence = 0;

    //check the seeking operate
    if (meta->getInt64(MEDIA_ATTR_SEEK_OFFSET, fileOffset) &&
         meta->getInt32(MEDIA_ATTR_SEEK_WHENCE, fileWhence)) {
        if (mSink->mWriteMode != WMT_Record) {
            ERROR("in the snapshot mode, fseek is invalid\n");
            return MM_ERROR_IVALID_OPERATION;
        }

        if (mSink->mFd >= 0){
            int64_t result = ::lseek(mSink->mFd, fileOffset, fileWhence);
            if (result == -1) {
                ERROR("seek to %" PRId64 " failed, whence %d", fileOffset, fileWhence);
                FLEAVE_WITH_CODE(MM_ERROR_UNKNOWN);
            }
        } else {
            WARNING("invalid fd");
        }
    }

   //get the eos
   if (buffer->isFlagSet(MediaBuffer::MBFT_EOS)) {
        INFO("Notify kEventEOS");

        if (mSink->mWriteMode == WMT_Record) {
            if (mSink->mFd >= 0) {
                ::close(mSink->mFd);
                mSink->mFd = -1;
            }
            if (mSink->mFrameCount == 0) {
                INFO("record file %s is empty, delete it", mSink->mFilePath.c_str());
                unlink(mSink->mFilePath.c_str());
            }
        }
        mSink->notify(kEventEOS, 0, 0, nilParam);
        FLEAVE_WITH_CODE(MM_ERROR_EOS);
    }

    //write the file
    uint8_t *sourceBuf = NULL;
    int32_t offset = 0, length = 0;
    buffer->getBufferInfo((uintptr_t *)&sourceBuf, &offset, &length, 1);
    if ( !sourceBuf ) {
        ERROR("sourceBuf is null\n");
        FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
    }

    length = buffer->size();
    if (mSink->mWriteMode == WMT_Image) {
#ifdef DUMP_FILE_SINK_DATA
        char   filePath[128];
        //sprintf(filePath,"%s%d.dat", getMediaFileName(mUrl.c_str(), mPrefix, mExtension).c_str(), mFileIdx);
        DataDump outputDumper(filePath);
        mOutputDumper.dump(sourceBuf+offset, length);
#endif
        status = mSink->writeSingleFile(sourceBuf, length);
        mSink->mFileIdx++;
    } else {
        int count = -1;

        if (mSink->mFd >= 0 && sourceBuf && length) {
            count = ::write(mSink->mFd, sourceBuf, length);
            mSink->mFrameCount++;

            if (count == length) {
                status = MM_ERROR_SUCCESS;
            } else {
                ERROR("ERROR occur");
            }

            if (mSink->mCurrentPosition < buffer->dts()) {
                mSink->mCurrentPosition = buffer->dts();
            }

            VERBOSE("sourceBuf %p, length %d, count:%d, pts %0.3f s\n", sourceBuf, length, count, mSink->mCurrentPosition/1000000.0f);
        }
    }

    FLEAVE_WITH_CODE1(status);
}


mm_status_t FileSink::FileSinkWriter::setMetaData(const MediaMetaSP & metaData) {
    ENTER1();
    mSink->mMediaMeta = metaData->copy();
    FLEAVE_WITH_CODE1(MM_ERROR_SUCCESS);
}

//////////////////////////////////////////////////////////////////////////////////
FileSink::FileSink(const char *mimeType, bool isEncoder)
                     : mFd(-1)
                     , mWriteMode(WMT_Record)
                     , mFileIdx(1)
                     , mFrameCount(0)
                     , mPrefix(DEFAULT_PREFIX)
                     , mExtension(DEFAULT_EXTENSION)
                     , mCurrentPosition(-1ll)
{
    ENTER();
    FLEAVE();
}

FileSink::~FileSink() {
    ENTER();
#ifdef DUMP_FILE_SINK_DATA
    mOutputDumper.dump(NULL, 0);
#endif
    FLEAVE();
}

mm_status_t FileSink::start()
{
    ENTER();

    MMAutoLock locker(mLock);
    if (mWriteMode == WMT_Record) {
        if (!mUrl.empty() && mFd < 0) {
            int32_t count = 0;
            mFilePath = getMediaFileName(mUrl.c_str(), mPrefix, mExtension);
            do {
                mFd = ::open(mFilePath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
                // try if errno is 2/11/30
                if (mFd < 0 && (errno == EAGAIN || errno == ENOENT || errno == EROFS)) {
                    usleep(10*1000);
                    ERROR("open failed and try, errno %d(%s), count %d", errno, strerror(errno), count);
                } else {
                    break;
                }
            } while(count++ < 50);

            if (mFd < 0) {
                ERROR("fail to open file (%s, %s) to record, errno %d(%s)",
                    mUrl.c_str(), mFilePath.c_str(), errno, strerror(errno));
                FLEAVE_WITH_CODE(MM_ERROR_NO_SUCH_FILE);
            }
        }
    }
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t FileSink::prepare()
{
    ENTER();
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t FileSink::stop() {
    ENTER();

    MMAutoLock locker(mLock);
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}


mm_status_t FileSink::reset() {
    ENTER();
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}



mm_status_t FileSink::flush() {
    ENTER();

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

const char *FileSink::name() const{
    return "FileSink";
}

mm_status_t FileSink::setParameter(const MediaMetaSP & meta) {
    ENTER();

    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_FILE_HANDLE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            {
                MMAutoLock locker(mLock);
                if (mFd >= 0) {
                    ::close(mFd);
                }
                mFd = dup(item.mValue.ii);
                DEBUG("key: %s, value: %d\n", item.mName, mFd);
            }
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_FILE_PATH) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }

            mUrl = item.mValue.str;
            DEBUG("key: %s, value: %s\n", item.mName, mUrl.c_str());
            continue;
        } else if ( !strcmp(item.mName, MEDIA_ATTR_OUTPUT_FORMAT) ) {
            if ( item.mType != MediaMeta::MT_String ) {
                WARNING("invalid type for %s\n", item.mName);
                continue;
            }
            DEBUG("key: %s, value: %s\n", item.mName, item.mValue.str);
            if (!strcmp(item.mValue.str, IMAGE_EXTENSION)) {
                mWriteMode = WMT_Image;
                mPrefix = IMAGE_PREFIX;
            } else if (!strcmp(item.mValue.str, AMR_EXTENSION) ||
                !strcmp(item.mValue.str, M4A_EXTENSION) ||
                !strcmp(item.mValue.str, OPUS_EXTENSION)) {
                mWriteMode = WMT_Record;
                mPrefix = AUDIO_PREFIX;
            }

            mExtension = item.mValue.str;
            continue;
        }
    }

    if (!mUrl.empty()) {
        struct stat statbuf;
        if (stat(mUrl.c_str(), &statbuf) == 0) {
            DEBUG("%d", statbuf.st_mode);
            if (!S_ISDIR(statbuf.st_mode) && !S_ISREG(statbuf.st_mode)) {
                ERROR("%s is not dir or file\n", mUrl.c_str());
                FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
            }
        }
        if (mUrl.empty() && (mFd < 0)) {
            ERROR("you must input correct parameters");
            FLEAVE_WITH_CODE(MM_ERROR_IVALID_OPERATION);
        }
        if (S_ISDIR(statbuf.st_mode) && mUrl.rfind('/',0) != 0) {
            mUrl.append("/");
        }
    }

    DEBUG("Fd:%d, file path:%s, write mode:%d\n",
             mFd, mUrl.c_str(),(int)mWriteMode);

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

Component::WriterSP FileSink::getWriter(MediaType mediaType) {
    ENTER();
    if ( (int)mediaType != Component::kMediaTypeVideo &&
        (int)mediaType != Component::kMediaTypeAudio &&
        (int)mediaType != Component::kMediaTypeImage) {
           ERROR("not supported mediatype: %d\n", mediaType);
           return Component::WriterSP((Component::Writer*)NULL);
       }

   return Component::WriterSP(new FileSink::FileSinkWriter(this));
}

mm_status_t FileSink::writeSingleFile(uint8_t *buffer,int32_t bufferSize)
{
    ENTER();
    if (!mUrl.empty()) {
        std::string filePath = getMediaFileName(mUrl.c_str(), mPrefix, mExtension);
        INFO("%s created, %dth\n", filePath.c_str(), mFileIdx);

        mFd = ::open(filePath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (mFd < 0){
            ERROR("open %s failed\n", filePath.c_str());
            return MM_ERROR_IVALID_OPERATION;
        }
    }

    if (mFd >= 0) {
        int32_t size = ::write(mFd, buffer, bufferSize);
        if (size < bufferSize) {
            ERROR("written size %d, bufferSize %d", size, bufferSize);
        }
        ::close(mFd);
        mFd = -1;
    }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

int64_t FileSink::getCurrentPosition()
{
    return mCurrentPosition;
}

} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {
MM_LOG_DEFINE_MODULE_NAME("FileSink");

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::FileSink *sinkComponent = new YUNOS_MM::FileSink(mimeType, isEncoder);
    if (sinkComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sinkComponent);
}


void releaseComponent(YUNOS_MM::Component *component) {
    ENTER();
    delete component;
    FLEAVE();
}

}
