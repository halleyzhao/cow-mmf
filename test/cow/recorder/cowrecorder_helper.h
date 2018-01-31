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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>


#include "multimedia/media_meta.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/cowrecorder.h"
#include "multimedia/component.h"
#include "multimedia/mediarecorder.h"
#include <multimedia/mmthread.h>

#include "multimedia/mm_debug.h"

namespace YUNOS_MM {

#define FRONT_CAMERA "camera://1"
#define BACK_CAMERA "camera://0"
#define CAMERA "camera://"
#define WFD "wfd://"

#ifdef __MM_NATIVE_BUILD__
    #define DEFAULT_FILE_OUTPUT_PATH  "/home/cow/"
#elif defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__) || defined(__MM_YUNOS_LINUX_BSP_BUILD__)
    #define DEFAULT_FILE_OUTPUT_PATH "/tmp/"
#else
    #define DEFAULT_FILE_OUTPUT_PATH "/data/"
#endif

#ifdef __MM_NATIVE_BUILD__
    #define DEFAULT_VIDEO_INPUT_PATH  BACK_CAMERA
#elif defined(__MM_YUNOS_CNTRHAL_BUILD__) || defined(__MM_YUNOS_YUNHAL_BUILD__)
    #define DEFAULT_VIDEO_INPUT_PATH "auto://"
    //#define DEFAULT_VIDEO_INPUT_PATH "wfd://"
#elif defined (__MM_YUNOS_LINUX_BSP_BUILD__)
    #define DEFAULT_VIDEO_INPUT_PATH "external camera"
#endif


#define VIDEO_EXTENSION "mp4"
#define IMAGE_EXTENSION "jpg"

#ifdef __MM_NATIVE_BUILD__
#define FOURCC_FORMAT 'YUYV'
#else
#define FOURCC_FORMAT 'YV12'
#endif
#define DEFAULT_PREVIEW_FORMAT FOURCC_FORMAT
#define DEFAULT_RECORD_FORMAT FOURCC_FORMAT

#define DEFAULT_IMAGE_WIDTH 1600
#define DEFAULT_IAMGE_HEIGHT 1200
#define DEFAULT_PREVIEW_WIDTH 720
#define DEFAULT_PREVIEW_HEIGHT 544
#ifdef __PHONE_BOARD_SPRD__
#define DEFAULT_VIDEO_WIDTH 640
#define DEFAULT_VIDEO_HEIGHT 480
#else
#define DEFAULT_VIDEO_WIDTH 1280
#define DEFAULT_VIDEO_HEIGHT 720
#endif

#define DEFAULT_PHOTO_COUNT 20
#define DEFAULT_PHOTO_QUALITY 100

#define DEFAULT_AUDIO_SOURCE_URL "cras://mic"
//#define DEFAULT_AUDIO_SOURCE_URL "cras://remote_submix"


struct RecordParam {
public:
    RecordParam() :
          mPreviewWidth(DEFAULT_PREVIEW_WIDTH)
        , mPreviewHeight(DEFAULT_PREVIEW_HEIGHT)
        , mPreviewFormat(DEFAULT_PREVIEW_FORMAT)
        , mAudioMime(MEDIA_MIMETYPE_AUDIO_AAC)
        , mVideoMime(MEDIA_MIMETYPE_VIDEO_AVC)
        , mAudioInputFilePath(DEFAULT_AUDIO_SOURCE_URL)
        , mVideoFormat(DEFAULT_RECORD_FORMAT)
        , mVideoWidth(DEFAULT_VIDEO_WIDTH)
        , mVideoHeight(DEFAULT_VIDEO_HEIGHT)
        , mVideoBitRate(4000000)
        , mRecorderOutputFilePath(DEFAULT_FILE_OUTPUT_PATH)
        , mVideoExtension(VIDEO_EXTENSION)
        , mVideoInputFilePath(DEFAULT_VIDEO_INPUT_PATH)
        , mPhotoCount(DEFAULT_PHOTO_COUNT)
        , mQuality(DEFAULT_PHOTO_QUALITY)
        , mImageWidth(DEFAULT_IMAGE_WIDTH)
        , mImageHeight(DEFAULT_IAMGE_HEIGHT) //not support 1920X1080, should align to 16 bytes, such as 1920*1088
        , mImageOutputFilePath(DEFAULT_FILE_OUTPUT_PATH)
        , mImageExtension(IMAGE_EXTENSION)
        , mImageInputFilePath(DEFAULT_VIDEO_INPUT_PATH)
        , mSampleRate(48000)
        , mChannels(2)
        , mAudioBitRate(128000)
        {
        }

    ~RecordParam () {}

    void dump()
    {
        PRINTF("******************************************************************************\n");
        PRINTF("preview size %dX%d, video size %dX%d, image size %dX%d\n",
            mPreviewWidth, mPreviewHeight, mVideoWidth, mVideoHeight, mImageWidth, mImageHeight);

        PRINTF("mImageInputFilePath: %s, mImageOutputFilePath %s, mImageExtension %s\n",
            mImageInputFilePath.c_str(), mImageOutputFilePath.c_str(), mImageExtension.c_str());
        PRINTF("mPhotoCount: %d, mQuality: %d\n", mPhotoCount, mQuality);

        PRINTF("mAudioInputFilePath %s, mVideoInputFilePath: %s, mRecorderOutputFilePath: %s, mVideoExtension: %s\n",
            mAudioInputFilePath.c_str(), mVideoInputFilePath.c_str(), mRecorderOutputFilePath.c_str(), mVideoExtension.c_str());

        char *ptr = (char*)(&(mPreviewFormat));
        PRINTF("mPreviewFormat: 0x%x, %c%c%c%c\n", mPreviewFormat, *(ptr), *(ptr+1), *(ptr+2), *(ptr+3));
        ptr = (char*)(&(mVideoFormat));
        PRINTF("mVideoFormat: 0x%x, %c%c%c%c\n", mVideoFormat, *(ptr), *(ptr+1), *(ptr+2), *(ptr+3));

        PRINTF("mAudioMime: %s, mVideoMime: %s\n", mAudioMime.c_str(), mVideoMime.c_str());
        PRINTF("******************************************************************************\n\n\n");
    }

public:
    //both for image and video
    uint32_t mPreviewWidth;
    uint32_t mPreviewHeight;
    uint32_t mPreviewFormat;

    //for video
    std::string mAudioMime;
    std::string mVideoMime;
    std::string mAudioInputFilePath;

    uint32_t mVideoFormat;
    uint32_t mVideoWidth;
    uint32_t mVideoHeight;
    int32_t mVideoBitRate;

    std::string mRecorderOutputFilePath;
    std::string mVideoExtension;
    std::string mVideoInputFilePath;

    //for image
    uint32_t mPhotoCount;
    uint32_t mQuality;//only used for takepicture
    uint32_t mImageWidth;
    uint32_t mImageHeight;

    std::string mImageOutputFilePath;
    std::string mImageExtension;
    std::string mImageInputFilePath;

    //for audio
    int32_t mSampleRate;
    int32_t mChannels;
    int32_t mAudioBitRate;
};

}
