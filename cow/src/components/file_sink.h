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

#ifndef file_sink_h
#define file_sink_h

#include <string>

#include <multimedia/mm_cpp_utils.h>
#include <multimedia/component.h>


namespace YUNOS_MM {

//#define DUMP_FILE_SINK_DATA
//#define WRITE_AMR_FILE_HEADER

class MediaBuffer;
class MediaMeta;
class FileSink : public SinkComponent {

public:
    FileSink(const char *mimeType = NULL, bool isEncoder = false);

    virtual const char * name() const;
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType);
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return  MM_ERROR_IVALID_OPERATION;}
    virtual mm_status_t prepare();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t seek(int msec, int seekSequence) { return MM_ERROR_SUCCESS;}
    virtual mm_status_t reset();
    virtual mm_status_t flush();
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual int64_t getCurrentPosition();

protected:
    virtual ~FileSink();

private:
    static std::string getMediaFileName(const char*url, const char*prefix, const char*extension);

    mm_status_t writeSingleFile(uint8_t *buffer,int32_t bufferSize);

    class FileSinkWriter : public Writer {
    public:
        FileSinkWriter(FileSink *sink) : mSink(sink)
        {
        }
        virtual ~FileSinkWriter(){}
        virtual mm_status_t write(const MediaBufferSP &buffer);
        virtual mm_status_t setMetaData(const MediaMetaSP & metaData);
    private:
        FileSink *mSink;

        DECLARE_LOGTAG()
    };

    enum WriteModeType
    {
        WMT_Record = 0,
        WMT_Image
    };

    WriterSP mWriter;

    Lock mLock;
    int mFd;
    std::string mUrl;
    std::string mFilePath;
    WriteModeType mWriteMode;
    int mFileIdx;
    uint32_t mFrameCount;

    const char*mPrefix;
    const char*mExtension;

    int64_t mCurrentPosition;
    MediaMetaSP mMediaMeta;

private:
    MM_DISALLOW_COPY(FileSink);

    DECLARE_LOGTAG()
};
typedef MMSharedPtr<FileSink> FileSinkSP;

} // end of namespace YUNOS_MM
#endif//file_sink_h

