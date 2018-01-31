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


#ifndef __mm_errors_h
#define __mm_errors_h

#ifdef __cplusplus
extern "C" {
#endif

typedef int mm_status_t;

typedef  enum {
    MM_ERROR_SUCCESS = 0,
    MM_ERROR_NO_MEM, /** < No mem */
    MM_ERROR_INVALID_PARAM,  /** < Invalid param */
    MM_ERROR_IVALID_OPERATION,  /** < Invalid operation */
    MM_ERROR_NOT_INITED,  /** < not inited */
    MM_ERROR_UNSUPPORTED,  /** < unsupported */
    MM_ERROR_NO_SUCH_FILE,                 /**< No such file or directory */
    MM_ERROR_FILE_NO_SPACE_ON_DEVICE,  /**< No space left on the device */
    MM_ERROR_RESOURCE_LIMIT,            /**< Resource limit */
    MM_ERROR_PERMISSION_DENIED,        /**< Permission denied */
    MM_ERROR_INVALID_STATE,                /**< Invalid state */
    MM_ERROR_INVALID_URI,                /**< Invalid URI */
    MM_ERROR_CONNECTION_FAILED,            /**< Streaming connection failed */
    MM_ERROR_OP_FAILED = 13, /* operation failed */
    MM_ERROR_NO_MORE = 14, /* no more */
    MM_ERROR_BUFFER_TOO_SMALL = 15,
    MM_ERROR_SERVER_DIED = 16,
    MM_ERROR_NOT_VALID_FOR_PROGRESSIVE_PLAYBACK = 17,
    MM_ERROR_SOURCE = 18,
    MM_ERROR_IO = 19,
    MM_ERROR_MALFORMED = 20,
    MM_ERROR_TIMED_OUT = 21,
    MM_ERROR_FATAL_ERROR = 22,
    MM_ERROR_NO_COMPONENT = 23,
    MM_ERROR_NO_PIPELINE = 24,
    MM_ERROR_NO_MEDIA_TYPE = 25,
    MM_ERROR_NO_MEDIA_TRACK = 26,
    MM_ERROR_FULL = 27,
    MM_ERROR_EOS = 28,
    MM_ERROR_COMPONENT_CONNECT_FAILED = 29,
    MM_ERROR_INTERRUPTED = 30,                  /** current operation is terminate before finished, interupted by other operation*/
    MM_ERROR_AGAIN = 31, /* try again */
    MM_ERROR_SKIPPED = 32, /* operation skipped */
    MM_ERROR_NO_AUDIOMANAGER = 33, /* no audio manager */
    MM_ERROR_NO_AUDIORENDER = 34, /* no audio render */
    MM_ERROR_NO_AUDIODECODER = 35, /* no audio decoder */
    MM_ERROR_NO_VIDEODECODER = 36, /* no video decoder */
    MM_ERROR_EXISTS = 37, /* already exists */
    MM_ERROR_NOTEXISTS = 38, /* not exists */
    MM_ERROR_LPA_NOT_SUPPORT = 39, /* LPA not support */
    MM_ERROR_CODEC_ERROR = 40, /* hardware codec error */
    MM_ERROR_SUBTITLE_TYPE_UNSUPPORTED = 41, /*subtitle type unsupported */
    MM_ERROR_NOT_HANDLED = 42, /*key not handled*/



    MM_ERROR_SOUND_POLICY = 100,                /**< Sound policy error */

    MM_ERROR_DRM_EXPIRED = 120,                /**< Expired license */
    MM_ERROR_DRM_NO_LICENSE,            /**< No license */
    MM_ERROR_DRM_FUTURE_USE,            /**< License for future use */
    MM_ERROR_DRM_NOT_PERMITTED,            /**< Format not permitted */

    MM_ERROR_SEEK_FAILED = 140,                /**< Seek operation failure */
    MM_ERROR_NOT_SUPPORTED_FILE,            /**< File format not supported */
    MM_ERROR_VIDEO_CAPTURE_FAILED,        /**< Video capture failed */

    MM_ERROR_ASYNC = 210,

    MM_ERROR_UNKNOWN = 0XFF /* unknown error */
}mm_errors_t;

enum {
    MM_ERROR_FATAL_SUB_HWUNRELEASE = -880,
    MM_ERROR_FATAL_SUBERROR_FFMPEGINIT = -881,
    MM_ERROR_FATAL_SUBERROR_OMXERROR = -882,
};
#ifdef __cplusplus
}
#endif


#endif // __mm_errors_h

