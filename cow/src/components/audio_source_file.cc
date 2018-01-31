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

#include <multimedia/mm_debug.h>
#include <multimedia/media_attr_str.h>
#include "multimedia/mm_audio.h"

#include "audio_source_file.h"

namespace YUNOS_MM {
/////////////////////////////////////////////////////////////////////
DEFINE_LOGTAG(AudioSourceFile)

#define ENTER() INFO(">>>\n")
#define EXIT() do {INFO(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {INFO("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() VERBOSE(">>>\n")
#define EXIT1() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)


#define MCD_MSG_START (msg_type)1
#define MCD_MSG_STOP (msg_type)2
#define MCD_MSG_PREPARE (msg_type)3
#define MCD_MSG_RESET (msg_type)4
#define MCD_MSG_READ_AUDIO_SOURCE (msg_type)5





BEGIN_MSG_LOOP(AudioSourceFile)
    MSG_ITEM(MCD_MSG_READ_AUDIO_SOURCE, onReadAudioSource)
END_MSG_LOOP()




#define g_input_url_data "/data/audio_sink_pulse.pcm"
#define g_input_url_size "/data/audio_sink_pulse.size"
//#define g_input_url_data "/data/audio_sink_pulse-nb.pcm"
//#define g_input_url_size "/data/audio_sink_pulse-nb.size"
static const char * MMMSGTHREAD_NAME = "AudioSourceFile";


AudioSourceFile::AudioSourceFile(const char *mimeType, bool isEncoder) :
      MMMsgThread(MMMSGTHREAD_NAME),
      mChannels(-1),
      mSampleRate(-1),
      mSampleFormat(SND_FORMAT_PCM_16_BIT),
      mBitrate(-1),
      mTotalFrameSize(0),
      mMaxBufferCount(1000),
      mEos(false),
      mIsContinue(true),
      mBufferAvailableCondition(mLock),
      mInputDataFile(NULL),
      mInputSizeFile(NULL),
      mReadSourceGeneration(0),
      mPts(0){
      mInputFormat = MediaMeta::create();

}

mm_status_t AudioSourceFile::init() {
    ENTER();

    int ret = MMMsgThread::run();
    if (ret != 0) {
        ERROR("init failed, ret %d", ret);
    }
    return (ret == 0 ? MM_ERROR_SUCCESS : MM_ERROR_OP_FAILED);
}

void AudioSourceFile::uninit() {
    ENTER();
    MMMsgThread::exit();
}

mm_status_t AudioSourceFile::prepare() {
    ENTER();
    mInputDataFile = fopen(g_input_url_data, "rb");
    if (mInputDataFile == NULL) {
        ERROR("fail to open file (%s) to record\n", g_input_url_data);
        return MM_ERROR_UNKNOWN;
    }


    mInputSizeFile = fopen(g_input_url_size, "rb");
    if (mInputSizeFile == NULL) {
        ERROR("fail to open file (%s) to record\n", g_input_url_size);
        return MM_ERROR_UNKNOWN;
    }



    mEos = false;

    return MM_ERROR_SUCCESS;
}


mm_status_t AudioSourceFile::start() {
    ENTER();
    mEos = false;
    postMsg(MCD_MSG_READ_AUDIO_SOURCE, mReadSourceGeneration, NULL, 0);

    return MM_ERROR_SUCCESS;
}

mm_status_t AudioSourceFile::stop() {
    ENTER();

    MMAutoLock locker(mLock);

    mReadSourceGeneration++;

    mMediaBufferList.clear();
    mIsContinue = false;
    mBufferAvailableCondition.broadcast();
    return MM_ERROR_SUCCESS;
}


mm_status_t AudioSourceFile::reset() {
    ENTER();

    MMAutoLock locker(mLock);
    if (mInputDataFile) {
        fclose(mInputDataFile);
        mInputDataFile = NULL;
    }

    if (mInputSizeFile) {
        fclose(mInputSizeFile);
        mInputSizeFile = NULL;
    }

    mEos = false;

    mReadSourceGeneration++;

    mMediaBufferList.clear();

    mIsContinue = false;
    mBufferAvailableCondition.broadcast();

    return MM_ERROR_SUCCESS;
}

mm_status_t AudioSourceFile::signalEOS() {
    ENTER();
    mEos = true;
    return MM_ERROR_SUCCESS;
}

void AudioSourceFile::onReadAudioSource(param1_type param1, param2_type param2, uint32_t rspId) {
    if (param1 != (param1_type)mReadSourceGeneration) {
        WARNING("obsolete, param1 %d, mReadSourceGeneration %d\n",
                param1, mReadSourceGeneration);
        return;
    }

    uint8_t *buf;
    int32_t bufOffset, bufStride;
    MediaBufferSP mediaBuf;
    MediaMetaSP meta;

    mediaBuf = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);

    int32_t frameSize = 0;
    int32_t readSize = fread(&frameSize, 4, 1, mInputSizeFile);
    if (readSize == 0) {
        INFO("EOS\n");
        mEos = true;
    } else {
        buf = new uint8_t[frameSize];
        if(!buf){
             ERROR("buf new fail \n");
             return;
        }
        memset(buf, 0, frameSize);

        readSize = fread(buf, frameSize, 1, mInputDataFile);
        if (readSize == 0) {
            INFO("EOS\n");

            mEos = true;
           if(buf){
              delete []buf;
           }
        } else {

            mediaBuf->setPts(mPts);
            mediaBuf->setSize(frameSize);

            bufOffset = 0;
            bufStride = frameSize;
            mediaBuf->setBufferInfo((uintptr_t *)&buf, &bufOffset, &bufStride, 1);
            mediaBuf->setSize(bufStride);
            mediaBuf->addReleaseBufferFunc(AudioSourceFile::releaseMediaBuffer);
        }

        mTotalFrameSize += frameSize;
        mPts = (int64_t)mTotalFrameSize * (1000000 / mSampleRate / mChannels / 2);//int64_t
    }

    if (mEos) {
        INFO("source signal eos\n");
        mediaBuf->setFlag(MediaBuffer::MBFT_EOS);
    }

    MMAutoLock locker(mLock);
    mMediaBufferList.push_back(mediaBuf);

    mBufferAvailableCondition.broadcast();
    if (!mEos)
        postMsg(MCD_MSG_READ_AUDIO_SOURCE, mReadSourceGeneration, NULL, 20*1000);

}

bool AudioSourceFile::releaseMediaBuffer(MediaBuffer *mediaBuf) {

    uint8_t *buf = NULL;

    if (!(mediaBuf->getBufferInfo((uintptr_t *)&buf, NULL, NULL, 1))) {
        ERROR("error in release mediabuffer\n");
        return false;
    }

    delete buf;

    return true;
}

mm_status_t AudioSourceFile::setParameter(const MediaMetaSP & meta) {
    for ( MediaMeta::iterator i = meta->begin(); i != meta->end(); ++i ) {
        const MediaMeta::MetaItem & item = *i;
        if ( !strcmp(item.mName, MEDIA_ATTR_BIT_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mBitrate = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mBitrate);
            mInputFormat->setInt32(MEDIA_ATTR_BIT_RATE, mBitrate);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_CHANNEL_COUNT) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mChannels = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mChannels);
            mInputFormat->setInt32(MEDIA_ATTR_CHANNEL_COUNT, mChannels);
            continue;
        }
        if ( !strcmp(item.mName, MEDIA_ATTR_SAMPLE_RATE) ) {
            if ( item.mType != MediaMeta::MT_Int32 ) {
                MMLOGW("invalid type for %s\n", item.mName);
                continue;
            }
            mSampleRate = item.mValue.ii;
            MMLOGI("key: %s, value: %d\n", item.mName, mSampleRate);
            mInputFormat->setInt32(MEDIA_ATTR_SAMPLE_RATE, mSampleRate);
            continue;
        }
    }
    return MM_ERROR_SUCCESS;
}


Component::ReaderSP AudioSourceFile::getReader(MediaType mediaType) {
    ReaderSP rsp;

    if (mediaType != kMediaTypeAudio) {
        ERROR("mediaType(%d) is not supported\n", mediaType);
        return rsp;
    }

    Reader *reader = new StubReader(this);
    if (!reader)
        return rsp;
    rsp.reset(reader);

    return rsp;
}

/////////////////////////////////////////////////////////////////////////////////
////StubReader
mm_status_t AudioSourceFile::StubReader::read(MediaBufferSP & buffer) {

    MMAutoLock locker(mComponent->mLock);
    while (mComponent->mMediaBufferList.empty() && mComponent->mIsContinue) {
        mComponent->mBufferAvailableCondition.wait();
    }

    if (!mComponent->mMediaBufferList.empty()) {
        buffer = *mComponent->mMediaBufferList.begin();
        mComponent->mMediaBufferList.pop_front();

        if (mReadCount++ % 30 == 0)
            DEBUG("StubReader: read mediabuffer, mMediaBufferList size %zu, mReadCount %d\n",
                mComponent->mMediaBufferList.size(), mReadCount-1);

        return MM_ERROR_SUCCESS;
    } else if (mComponent->mEos){
        return MM_ERROR_EOS;
    } else {
        return MM_ERROR_AGAIN;
    }
}


MediaMetaSP AudioSourceFile::StubReader::getMetaData() {

    mComponent->mInputFormat->setInt32(MEDIA_ATTR_SAMPLE_FORMAT, SND_FORMAT_PCM_16_BIT);
    //source packet pts should be based on us.
    mComponent->mInputFormat->setFraction(MEDIA_ATTR_TIMEBASE, 1, 1000000);
    return mComponent->mInputFormat;
}


extern "C" {
YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::AudioSourceFile *component = new YUNOS_MM::AudioSourceFile(mimeType, isEncoder);
    if (component == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(component);
}


void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}
}

}

