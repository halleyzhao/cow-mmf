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

#ifndef audio_source_file_h
#define audio_source_file_h

#include <multimedia/component.h>
#include <multimedia/mmmsgthread.h>
#include <multimedia/media_buffer.h>

namespace YUNOS_MM {

////////////////////////////////////////////////////////////////////////////////
class AudioSourceFile : public RecordSourceComponent, public MMMsgThread {
public:
    AudioSourceFile(const char *mimeType = NULL, bool isEncoder = false);

    virtual ~AudioSourceFile() {}

    mm_status_t init();

    void uninit();
    virtual mm_status_t prepare();
    virtual mm_status_t reset();
    virtual mm_status_t start();

    virtual mm_status_t stop();

    virtual mm_status_t signalEOS();

    virtual mm_status_t setParameter(const MediaMetaSP & meta);

    virtual mm_status_t setUri(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t setUri(int fd, int64_t offset, int64_t length) {return MM_ERROR_UNSUPPORTED;}

    virtual ReaderSP getReader(MediaType mediaType);


public:

    struct StubReader : public Reader {
        StubReader(AudioSourceFile *comp)
            : mComponent(comp), mReadCount(1) {}

        virtual ~StubReader() {}

        virtual mm_status_t read(MediaBufferSP & buffer);
        virtual MediaMetaSP getMetaData();

        private:
            AudioSourceFile *mComponent;
            int32_t mReadCount;
    };

    friend class StubReader;

public:
    virtual const char * name() const {return "FakeAudiosource";}
    COMPONENT_VERSION;
    virtual WriterSP getWriter(MediaType mediaType) { return WriterSP((Writer*)NULL); }
    virtual mm_status_t addSource(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
    virtual mm_status_t addSink(Component * component, MediaType mediaType) {return MM_ERROR_UNSUPPORTED;}
private:
    static bool releaseMediaBuffer(MediaBuffer *mediaBuf);

private:

    int32_t mChannels;
    int32_t mSampleRate;
    int32_t mSampleFormat;
    int32_t mBitrate;
    int32_t mTotalFrameSize;
    int32_t mInputBufferCount;
    int32_t mMaxBufferCount;
    bool mEos;
    bool mIsContinue;

    Lock mLock;
    Condition mBufferAvailableCondition;

    FILE *mInputDataFile;
    FILE *mInputSizeFile;

    int32_t mReadSourceGeneration;
    std::list<MediaBufferSP> mMediaBufferList;

    int64_t mPts;
    MediaMetaSP mInputFormat;


    DECLARE_MSG_LOOP()
    DECLARE_MSG_HANDLER(onReadAudioSource)

    DECLARE_LOGTAG()


    MM_DISALLOW_COPY(AudioSourceFile);

};


} // end of namespace YUNOS_MM
#endif// audio_source_file_h

