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
#ifndef __HAL_AUDIO_COMMON_H_
#define __HAL_AUDIO_COMMON_H_
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/cdefs.h>
#include <sys/types.h>

/**
* @file Audio.h
* @brief This file contains the audio device modeling utilities defined by YunOS,
* and is shared by the platform independent audio framework and the vendor specific HAL.
***/


__BEGIN_DECLS

typedef int snd_unique_id_t;

/** @cond AUDIO_YUNHAL */
/**
* @addtogroup YUNHAL_AUDIO
* @section YUNOS_AUDIO_HEADER Required Header
*      \#include <yunhal/Audio.h>
* @{
*/
/** @endcond */

/** @cond NAPI_AUDIO */
/**
* @ingroup NAPI_AUDIO
* @addtogroup NAPI_AUDIO_UTILS
* @section YUNOS_AUDIO_HEADER Required Header
*      \#include <yunhal/Audio.h>
* @{
*/
/** @endcond */


/**
* @brief Enumerations for audio stream type.
*/
typedef enum {
    /** The default audio stream */
    AS_TYPE_DEFAULT          = -1,
    /** @cond AUDIO_YUNHAL */
    AS_TYPE_MIN              = 0,
    /** @endcond */
    /** The audio stream for phone calls */
    AS_TYPE_VOICE_CALL       = 0,
    /** The audio stream for system sounds */
    AS_TYPE_SYSTEM           = 1,
    /** The audio stream for the phone ring and message alerts */
    AS_TYPE_RING             = 2,
    /** The audio stream for music playback */
    AS_TYPE_MUSIC            = 3,
    /** The audio stream for alarms */
    AS_TYPE_ALARM            = 4,
    /** The audio stream for notifications */
    AS_TYPE_NOTIFICATION     = 5,
    /** The audio stream for phone calls when connected on bluetooth */
    AS_TYPE_BT_SCO           = 6,
    /** The audio stream for enforced system sounds */
    AS_TYPE_ENFORCED_AUDIBLE = 7,
    /** The audio stream for DTMF tones */
    AS_TYPE_DTMF             = 8,
    /**
    * The audio stream for text to speech (TTS).
    * Transmitted Through Speaker. Plays over speaker only
    */
    AS_TYPE_TTS              = 9,

/** @cond AUDIO_PRIVATE */
//HTC_AUD_START
    AS_TYPE_FM               = 10,
    AS_TYPE_MIRRORLINK_RECORD= 11,
    AS_TYPE_ACCESSIBILITY    = 12,
    AS_TYPE_REROUTING        = 13,
    AS_TYPE_PATCH            = 14,
    AS_TYPE_PUBLIC_CNT       = AS_TYPE_MIRRORLINK_RECORD + 1,
//HTC_AUD_END
    AS_TYPE_FOR_POLICY_CNT   = AS_TYPE_PATCH, /* number of streams considered by
                                           audio policy for volume and routing */
//HTC_AUD_END
    AS_TYPE_CNT              = AS_TYPE_PATCH + 1,
/** @endcond */
} as_type_t;

/**
 *  @brief Enumerations for audio source type
 */
typedef enum {
    /** Default audio source */
    ADEV_SOURCE_DEFAULT             = 0,
    /** Microphone audio source */
    ADEV_SOURCE_MIC                 = 1,
    /** Voice call uplink audio source. */
    ADEV_SOURCE_VOICE_UPLINK        = 2,
    /** Voice call downlink audio source. */
    ADEV_SOURCE_VOICE_DOWNLINK      = 3,
    /** Voice call uplink + downlink audio source */
    ADEV_SOURCE_VOICE_CALL          = 4,
    /** Microphone audio source with same orientation as camera if available */
    ADEV_SOURCE_CAMCORDER           = 5,
    /** Microphone audio source tuned for voice recognition if available */
    ADEV_SOURCE_VOICE_RECOGNITION   = 6,
    /** Microphone audio source tuned for voice communications such as VoIP. */
    ADEV_SOURCE_VOICE_COMMUNICATION = 7,
    /**
    * Audio source for a submix of audio streams to be presented remotely.
    * Wifi Display is an example of remote presentation. where a dongle
    * attached to a TV can be used to play the mix audio data captured by
    * this audio source.
    */
    ADEV_SOURCE_REMOTE_SUBMIX       = 8,
    /** Microphone audio source tuned for unprocessed (raw) audio if available */
    /*  Usage examples include level measurement and raw signal analysis. */
    ADEV_SOURCE_UNPROCESSED         = 9,
    /** @cond AUDIO_YUNHAL */
    //HTC_AUD_START
    ADEV_SOURCE_MIRRORLINK          = 11,
    //HTC_AUD_END
    ADEV_SOURCE_CNT,
    ADEV_SOURCE_MAX                 = ADEV_SOURCE_CNT - 1,
    //HTC_AUD_START
    ADEV_SOURCE_FM_RX               = 1996,
    ADEV_SOURCE_FM_RX_A2DP          = 1997,
    //HTC_AUD_END
    /** @endcond */
    /** FM audio source */
    ADEV_SOURCE_FM_TUNER            = 1998,
    /**
    * @cond AUDIO_YUNHAL
    * A low-priority, preemptible audio source for
    * for background software hotword detection.
    * Same tuning as AUDIO_SOURCE_VOICE_RECOGNITION.
    */
    ADEV_SOURCE_HOTWORD             = 1999,
    /** @endcond */
} adev_source_t;

/** @cond AUDIO_YUNHAL */

/**
*   @brief global handle used by AudioStreamer and AudioRouteStrategyManager to identify
*   audio input/output endpoints
*/
typedef int adev_endpoint_handle_t; // corresponding to audio_io_handle_t
#define ADEV_ENDPOINT_HANDLE_NONE    0

typedef enum adev_handle_val {
    ADEV_ENDPOINT_HANDLE_INVALID = -1,  /**< the invalid endpoint handle */
} adev_handle_val_t;

/**
*   @brief audio hw module handle functions or structures referencing a module
*/
typedef int adev_handle_t; //corresponding to audio_module_handle_t

/**
 *  @brief Enumerations for audio IO device type
 */
typedef enum {
    ADEV_NONE = 0,  /**< */
    ADEV_PRIMARY,   /**< Primary audio IO device */
    ADEV_HDMI,      /**< */
    ADEV_USB,       /**< */
    ADEV_BT,        /**< Bluetooth audio IO device */
    ADEV_R_SUBMIX,  /**< Remote submix auido IO device */
    ADEV_MIRRORLINK,/**< */
    ADEV_ALL_TYPE,  /**< */
    ADEV_MAX        /**< */
} adev_type_t;
/** @endcond */

/**
 *  @brief Enumerations for audio phone state mode
 */
typedef enum {
    /** @cond AUDIO_YUNHAL */
    ADEV_MODE_INVALID          = -2,
    ADEV_MODE_CURRENT          = -1,
    /** @endcond */
    /** Normal mode */
    ADEV_MODE_NORMAL           = 0,
    /** Ringtone playing mode */
    ADEV_MODE_RINGTONE         = 1,
    /** In voice call mode */
    ADEV_MODE_IN_CALL          = 2,
    /** Voice communications such as VoIP mode */
    ADEV_MODE_IN_COMMUNICATION = 3,
    /** @cond AUDIO_YUNHAL */
    ADEV_MODE_CNT,
    ADEV_MODE_MAX              = ADEV_MODE_CNT - 1,
    /** @endcond */
} adev_mode_t;

/** @cond AUDIO_YUNHAL */

/**
 *  @brief Directions of audio device operation: Input or Output
 */
typedef enum {
    ADEV_OP_OUTPUT = 0, /**< */
    ADEV_OP_INPUT,      /**< */
} adev_op_direction_t;

/**
 *  @brief Reserved values for audio session
 */
typedef enum {
    ADEV_SESSION_OUTPUT_STAGE = -1,
    ADEV_SESSION_OUTPUT_MIX = 0,
    ADEV_SESSION_ALLOCATE = 0,
    ADEV_SESSION_NONE = 0,
} adev_session_t;

/** @endcond */

/**
 *  @brief unique id for adev
 */
typedef int adev_unique_id_t;

/** @cond AUDIO_YUNHAL */

#define ADEV_UNIQUE_ID_ALLOCATE ADEV_SESSION_ALLOCATE

/**
 *  @brief PCM sub formats
 */
typedef enum {
    SND_FORMAT_PCM_SUB_16_BIT = 1,      /**< Signed 16 bits */
    SND_FORMAT_PCM_SUB_8_BIT,           /**< Unsigned 8 bits */
    SND_FORMAT_PCM_SUB_32_BIT,          /**< Signed .31 fixed point */
    SND_FORMAT_PCM_SUB_8_24_BIT,        /**< Signed 8.23 fixed point */
    SND_FORMAT_PCM_SUB_FLOAT,           /**< Single-precision floating point */
    SND_FORMAT_PCM_SUB_24_BIT_PACKED,   /**< Signed .23 fixed point packed in 3 bytes */
} snd_format_pcm_sub_fmt_t;

/**
 *  @brief MP3 sub formats
 */
typedef enum {
    SND_FORMAT_MP3_SUB_NONE            = 0  /**< */
} snd_format_mp3_sub_fmt_t;

/**
 *  @brief AMR NB/WB sub formats
 */
typedef enum {
    SND_FORMAT_AMR_SUB_NONE            = 0, /**< */
} snd_format_amr_sub_fmt_t;

/**
 *  @brief AAC sub formats
 */
typedef enum {
    SND_FORMAT_AAC_SUB_MAIN            = 0x1 << 0,  /**< AAC Main */
    SND_FORMAT_AAC_SUB_LC              = 0x1 << 1,  /**< AAC Low-Complexity */
    SND_FORMAT_AAC_SUB_SSR             = 0x1 << 2,  /**< AAC Scalable Sampling Rate */
    SND_FORMAT_AAC_SUB_LTP             = 0x1 << 3,  /**< AAC Long Term Predictor */
    SND_FORMAT_AAC_SUB_HE_V1           = 0x1 << 4,  /**< High Efficiency AAC V1 */
    SND_FORMAT_AAC_SUB_SCALABLE        = 0x1 << 5,  /**< AAC Scalable */
    SND_FORMAT_AAC_SUB_ERLC            = 0x1 << 6,  /**< */
    SND_FORMAT_AAC_SUB_LD              = 0x1 << 7,  /**< AAC Low Delay */
    SND_FORMAT_AAC_SUB_HE_V2           = 0x1 << 8,  /**< High Efficiency AAC V2 */
    SND_FORMAT_AAC_SUB_ELD             = 0x1 << 9,  /**< AAC Enhanced Low Delay */
} snd_format_aac_sub_fmt_t;

/**
 *  @brief VORBIS sub formats
 */
typedef enum {
    SND_FORMAT_VORBIS_SUB_NONE         = 0, /**< */
} snd_format_vorbis_sub_fmt_t;

/** @endcond */

/**
 *  @brief Sound formats in bitmap
 *  @details
 *  <pre>
 *  31     24                      0
 *  |------|-----------------------|
 *  | main |       sub format      |
 *  </pre>
 *  The main format determines the sound codec type.
 *  The sub format includes the information on the options
 *  and parameters for each sound format kind.
 */
typedef enum {
    /** @cond AUDIO_YUNHAL */
    // Main format field
    SND_FORMAT_INVALID             = 0xFFFFFFFFUL, /**< invalid format */
    SND_FORMAT_DEFAULT             = 0x00000000UL, /**< */
    SND_FORMAT_PCM                 = SND_FORMAT_DEFAULT, /**< */
    SND_FORMAT_MP3                 = 0x01000000UL, /**< mp3 audio format*/
    SND_FORMAT_AMR_NB              = 0x02000000UL, /**< */
    SND_FORMAT_AMR_WB              = 0x03000000UL, /**< */
    SND_FORMAT_AAC                 = 0x04000000UL, /**< */
    SND_FORMAT_HE_AAC_V1           = 0x05000000UL, /**< Replaced by SND_FORMAT_AAC_HE_V1 */
    SND_FORMAT_HE_AAC_V2           = 0x06000000UL, /**< Replaced by SND_FORMAT_AAC_HE_V2 */
    SND_FORMAT_VORBIS              = 0x07000000UL, /**< */
    SND_FORMAT_OPUS                = 0x08000000UL, /**< */
    SND_FORMAT_AC3                 = 0x09000000UL, /**< */
    SND_FORMAT_E_AC3               = 0x0A000000UL, /**< */
    SND_FORMAT_DTS                 = 0x0B000000UL,
    SND_FORMAT_DTS_HD              = 0x0C000000UL,
    SND_FORMAT_IEC61937            = 0x0D000000UL,
    SND_FORMAT_EVRC                = 0x10000000UL,
    SND_FORMAT_QCELP               = 0x11000000UL,
    SND_FORMAT_WMA                 = 0x12000000UL,
    SND_FORMAT_WMA_PRO             = 0x13000000UL,
    SND_FORMAT_AAC_ADIF            = 0x14000000UL,
    SND_FORMAT_EVRCB               = 0x15000000UL,
    SND_FORMAT_EVRCWB              = 0x16000000UL,
    SND_FORMAT_AMR_WB_PLUS         = 0x17000000UL,
    SND_FORMAT_MP2                 = 0x18000000UL,
    SND_FORMAT_EVRCNW              = 0x19000000UL,
    SND_FORMAT_PCM_OFFLOAD         = 0x1A000000UL,
    SND_FORMAT_FLAC                = 0x1B000000UL,
    SND_FORMAT_ALAC                = 0x1C000000UL,
    SND_FORMAT_APE                 = 0x1D000000UL,
    SND_FORMAT_AAC_ADTS            = 0x1E000000UL,
    SND_FORMAT_DSD                 = 0x1F000000UL,
    SND_FORMAT_SBC                 = 0x20000000UL,
    SND_FORMAT_APTX                = 0x21000000UL,
    SND_FORMAT_APTX_HD             = 0x22000000UL,
    SND_FORMAT_MAIN_MASK           = 0xFF000000UL, /**< */
    SND_FORMAT_SUB_MASK            = 0x00FFFFFFUL, /**< */
    /** @endcond */

    // Combinations with sub format field
    SND_FORMAT_PCM_16_BIT          = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_16_BIT),      /**< Signed 16 bits */
    SND_FORMAT_PCM_8_BIT           = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_8_BIT),       /**< Unsigned 8 bits */
    SND_FORMAT_PCM_32_BIT          = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_32_BIT),      /**< Signed .31 fixed point */
    SND_FORMAT_PCM_8_24_BIT        = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_8_24_BIT),    /**< Signed 8.23 fixed point */
    SND_FORMAT_PCM_FLOAT           = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_FLOAT),       /**< Single-precision floating point */
    SND_FORMAT_PCM_24_BIT_PACKED   = (SND_FORMAT_PCM | SND_FORMAT_PCM_SUB_24_BIT_PACKED), /**< Signed .23 fixed point packed in 3 bytes */

    /** @cond AUDIO_YUNHAL */
    SND_FORMAT_AAC_MAIN            = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_MAIN),        /**< AAC Main */
    SND_FORMAT_AAC_LC              = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_LC),          /**< AAC Low-Complexity */
    SND_FORMAT_AAC_SSR             = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_SSR),         /**< AAC Scalable Sampling Rate */
    SND_FORMAT_AAC_LTP             = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_LTP),         /**< AAC Long Term Predictor */
    SND_FORMAT_AAC_HE_V1           = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_HE_V1),       /**< High Efficiency AAC V1 */
    SND_FORMAT_AAC_SCALABLE        = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_SCALABLE),    /**< AAC Scalable */
    SND_FORMAT_AAC_ERLC            = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_ERLC),        /**< AAC erlc */
    SND_FORMAT_AAC_LD              = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_LD),          /**< AAC Low Delay */
    SND_FORMAT_AAC_HE_V2           = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_HE_V2),       /**< High Efficiency AAC V2 */
    SND_FORMAT_AAC_ELD             = (SND_FORMAT_AAC | SND_FORMAT_AAC_SUB_ELD),         /**< AAC Enhanced Low Delay */

    SND_FORMAT_AAC_ADTS_MAIN       = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_MAIN),   /**< AAC ADTS Main */
    SND_FORMAT_AAC_ADTS_LC         = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_LC),     /**< AAC ADTS Low-Complexity */
    SND_FORMAT_AAC_ADTS_SSR        = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_SSR),    /**< AAC ADTS Scalable Sampling Rate */
    SND_FORMAT_AAC_ADTS_LTP        = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_LTP),    /**< AAC ADTS Long Term Predictor */
    SND_FORMAT_AAC_ADTS_HE_V1      = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_HE_V1),  /**< High Efficiency AAC ADTS V1 */
    SND_FORMAT_AAC_ADTS_SCALABLE   = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_SCALABLE), /**< AAC ADTS Scalable */
    SND_FORMAT_AAC_ADTS_ERLC       = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_ERLC),   /**< AAC ADTS erlc */
    SND_FORMAT_AAC_ADTS_LD         = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_LD),     /**< AAC ADTS Low Delay */
    SND_FORMAT_AAC_ADTS_HE_V2      = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_HE_V2),  /**< High Efficiency AAC ADTS V2 */
    SND_FORMAT_AAC_ADTS_ELD        = (SND_FORMAT_AAC_ADTS | SND_FORMAT_AAC_SUB_ELD),    /**< AAC ADTS Enhanced Low Delay */
    SND_FORMAT_PCM_16_BIT_OFFLOAD  = (SND_FORMAT_PCM_OFFLOAD | SND_FORMAT_PCM_SUB_16_BIT),
    SND_FORMAT_PCM_24_BIT_OFFLOAD  = (SND_FORMAT_PCM_OFFLOAD | SND_FORMAT_PCM_SUB_8_24_BIT),
    /** @endcond */
} snd_format_t;

/**
 *  @brief Audio channel mask definitions
 */
enum {

    // General channel mask values
    ADEV_CHANNEL_NONE                      = 0x0,                       /**< */
    ADEV_CHANNEL_INVALID                   = 0xC0000000,                /**< */

    /** @cond AUDIO_YUNHAL */
    // Channel mask bits definitions for output
    ADEV_CHANNEL_OUT_FRONT_LEFT            = 0x1,           /**< */
    ADEV_CHANNEL_OUT_FRONT_RIGHT           = 0x2,           /**< */
    ADEV_CHANNEL_OUT_FRONT_CENTER          = 0x4,           /**< */
    ADEV_CHANNEL_OUT_LOW_FREQUENCY         = 0x8,           /**< */
    ADEV_CHANNEL_OUT_BACK_LEFT             = 0x10,          /**< */
    ADEV_CHANNEL_OUT_BACK_RIGHT            = 0x20,          /**< */
    ADEV_CHANNEL_OUT_FRONT_LEFT_OF_CENTER  = 0x40,          /**< */
    ADEV_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER = 0x80,          /**< */
    ADEV_CHANNEL_OUT_BACK_CENTER           = 0x100,         /**< */
    ADEV_CHANNEL_OUT_SIDE_LEFT             = 0x200,         /**< */
    ADEV_CHANNEL_OUT_SIDE_RIGHT            = 0x400,         /**< */
    ADEV_CHANNEL_OUT_TOP_CENTER            = 0x800,         /**< */
    ADEV_CHANNEL_OUT_TOP_FRONT_LEFT        = 0x1000,        /**< */
    ADEV_CHANNEL_OUT_TOP_FRONT_CENTER      = 0x2000,        /**< */
    ADEV_CHANNEL_OUT_TOP_FRONT_RIGHT       = 0x4000,        /**< */
    ADEV_CHANNEL_OUT_TOP_BACK_LEFT         = 0x8000,        /**< */
    ADEV_CHANNEL_OUT_TOP_BACK_CENTER       = 0x10000,       /**< */
    ADEV_CHANNEL_OUT_TOP_BACK_RIGHT        = 0x20000,       /**< */
    /** @endcond */

    ADEV_CHANNEL_OUT_MONO     = ADEV_CHANNEL_OUT_FRONT_LEFT,        /**< Single front left channel*/

    ADEV_CHANNEL_OUT_STEREO   = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT),     /**< front left + front right output channels*/

    ADEV_CHANNEL_OUT_2POINT1  = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER),    /**<  front left + front right +
                                                                     * front center output channels*/

    ADEV_CHANNEL_OUT_QUAD     = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_BACK_LEFT |
                                 ADEV_CHANNEL_OUT_BACK_RIGHT),      /**<  front left + front right +
                                                                     * back left + back right output channels*/

    ADEV_CHANNEL_OUT_QUAD_BACK = ADEV_CHANNEL_OUT_QUAD,             /**<  front left + front right +
                                                                     * back left + back right output channels*/

    ADEV_CHANNEL_OUT_QUAD_SIDE = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                  ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                  ADEV_CHANNEL_OUT_SIDE_LEFT |
                                  ADEV_CHANNEL_OUT_SIDE_RIGHT),     /**<  front left + front right +
                                                                     * side left + side right output channels*/

    ADEV_CHANNEL_OUT_SURROUND = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER |
                                 ADEV_CHANNEL_OUT_BACK_CENTER),     /**<  front left + front right +
                                                                     * front center + back center output channels*/

    ADEV_CHANNEL_OUT_PENTA =    (ADEV_CHANNEL_OUT_QUAD |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER),    /**<  front left + front right + front center +
                                                                     * back left + back right output channels*/

    ADEV_CHANNEL_OUT_5POINT1  = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER |
                                 ADEV_CHANNEL_OUT_LOW_FREQUENCY |
                                 ADEV_CHANNEL_OUT_BACK_LEFT |
                                 ADEV_CHANNEL_OUT_BACK_RIGHT),     /**<  front left + front right + front center +
                                                                     * low frequency + back left + back right
                                                                     * output channels*/

    ADEV_CHANNEL_OUT_5POINT1_BACK = ADEV_CHANNEL_OUT_5POINT1,       /**<  front left + front right + front center +
                                                                     * low frequency + back left + back right
                                                                     * output channels*/

    ADEV_CHANNEL_OUT_5POINT1_SIDE = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                     ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                     ADEV_CHANNEL_OUT_FRONT_CENTER |
                                     ADEV_CHANNEL_OUT_LOW_FREQUENCY |
                                     ADEV_CHANNEL_OUT_SIDE_LEFT |
                                     ADEV_CHANNEL_OUT_SIDE_RIGHT),   /**<  front left + front right + front center +
                                                                     * low frequency + side left + side right
                                                                     * output channels*/

    ADEV_CHANNEL_OUT_6POINT1  = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER |
                                 ADEV_CHANNEL_OUT_LOW_FREQUENCY |
                                 ADEV_CHANNEL_OUT_BACK_LEFT |
                                 ADEV_CHANNEL_OUT_BACK_RIGHT |
                                 ADEV_CHANNEL_OUT_BACK_CENTER),     /**<  front left + front right + front center +
                                                                     * low frequency + back left + back right +
                                                                     * back center output channels*/

    ADEV_CHANNEL_OUT_7POINT1  = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER |
                                 ADEV_CHANNEL_OUT_LOW_FREQUENCY |
                                 ADEV_CHANNEL_OUT_BACK_LEFT |
                                 ADEV_CHANNEL_OUT_BACK_RIGHT |
                                 ADEV_CHANNEL_OUT_SIDE_LEFT |
                                 ADEV_CHANNEL_OUT_SIDE_RIGHT),      /**<  front left + front right + front center +
                                                                     * low frequency + back left + back right +
                                                                     * ide left + side right output channels*/
    /** @cond AUDIO_YUNHAL */

    ADEV_CHANNEL_OUT_ALL      = (ADEV_CHANNEL_OUT_FRONT_LEFT |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_CENTER |
                                 ADEV_CHANNEL_OUT_LOW_FREQUENCY |
                                 ADEV_CHANNEL_OUT_BACK_LEFT |
                                 ADEV_CHANNEL_OUT_BACK_RIGHT |
                                 ADEV_CHANNEL_OUT_FRONT_LEFT_OF_CENTER |
                                 ADEV_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER |
                                 ADEV_CHANNEL_OUT_BACK_CENTER|
                                 ADEV_CHANNEL_OUT_SIDE_LEFT|
                                 ADEV_CHANNEL_OUT_SIDE_RIGHT|
                                 ADEV_CHANNEL_OUT_TOP_CENTER|
                                 ADEV_CHANNEL_OUT_TOP_FRONT_LEFT|
                                 ADEV_CHANNEL_OUT_TOP_FRONT_CENTER|
                                 ADEV_CHANNEL_OUT_TOP_FRONT_RIGHT|
                                 ADEV_CHANNEL_OUT_TOP_BACK_LEFT|
                                 ADEV_CHANNEL_OUT_TOP_BACK_CENTER|
                                 ADEV_CHANNEL_OUT_TOP_BACK_RIGHT),     /**< */

    // Channel mask bits definitions for input
    ADEV_CHANNEL_IN_LEFT            = 0x4,          /**< */
    ADEV_CHANNEL_IN_RIGHT           = 0x8,          /**< */
    ADEV_CHANNEL_IN_FRONT           = 0x10,         /**< */
    ADEV_CHANNEL_IN_BACK            = 0x20,         /**< */
    ADEV_CHANNEL_IN_LEFT_PROCESSED  = 0x40,         /**< */
    ADEV_CHANNEL_IN_RIGHT_PROCESSED = 0x80,         /**< */
    ADEV_CHANNEL_IN_FRONT_PROCESSED = 0x100,        /**< */
    ADEV_CHANNEL_IN_BACK_PROCESSED  = 0x200,        /**< */
    ADEV_CHANNEL_IN_PRESSURE        = 0x400,        /**< */
    ADEV_CHANNEL_IN_X_AXIS          = 0x800,        /**< */
    ADEV_CHANNEL_IN_Y_AXIS          = 0x1000,       /**< */
    ADEV_CHANNEL_IN_Z_AXIS          = 0x2000,       /**< */
    ADEV_CHANNEL_IN_VOICE_UPLINK    = 0x4000,       /**< */
    ADEV_CHANNEL_IN_VOICE_DNLINK    = 0x8000,       /**< */
    /** @endcond */

    ADEV_CHANNEL_IN_MONO   = ADEV_CHANNEL_IN_FRONT,         /**< Single front input channel*/

    ADEV_CHANNEL_IN_STEREO = (ADEV_CHANNEL_IN_LEFT |
                              ADEV_CHANNEL_IN_RIGHT),       /**< left + right input channels*/

    ADEV_CHANNEL_IN_FRONT_BACK = (ADEV_CHANNEL_IN_FRONT |
                                  ADEV_CHANNEL_IN_BACK),    /**< front + back input channels*/

    ADEV_CHANNEL_IN_5POINT1 = (ADEV_CHANNEL_IN_LEFT |
                               ADEV_CHANNEL_IN_RIGHT |
                               ADEV_CHANNEL_IN_FRONT |
                               ADEV_CHANNEL_IN_BACK |
                               ADEV_CHANNEL_IN_LEFT_PROCESSED |
                               ADEV_CHANNEL_IN_RIGHT_PROCESSED), /**< left + right + front + back +
                                                                  * left processed + right processed input channels*/

    /** Single voice uplink input channel */
    ADEV_CHANNEL_IN_VOICE_UPLINK_MONO = (ADEV_CHANNEL_IN_VOICE_UPLINK | ADEV_CHANNEL_IN_MONO),

    /** Single voice downlink input channel */
    ADEV_CHANNEL_IN_VOICE_DNLINK_MONO = (ADEV_CHANNEL_IN_VOICE_DNLINK | ADEV_CHANNEL_IN_MONO),

    /** Voice uplink + Voice downlink input channels */
    ADEV_CHANNEL_IN_VOICE_CALL_MONO   = (ADEV_CHANNEL_IN_VOICE_UPLINK_MONO |
                                         ADEV_CHANNEL_IN_VOICE_DNLINK_MONO),

    /** @cond AUDIO_YUNHAL */
    ADEV_CHANNEL_IN_ALL    = (ADEV_CHANNEL_IN_LEFT |
                              ADEV_CHANNEL_IN_RIGHT |
                              ADEV_CHANNEL_IN_FRONT |
                              ADEV_CHANNEL_IN_BACK|
                              ADEV_CHANNEL_IN_LEFT_PROCESSED |
                              ADEV_CHANNEL_IN_RIGHT_PROCESSED |
                              ADEV_CHANNEL_IN_FRONT_PROCESSED |
                              ADEV_CHANNEL_IN_BACK_PROCESSED|
                              ADEV_CHANNEL_IN_PRESSURE |
                              ADEV_CHANNEL_IN_X_AXIS |
                              ADEV_CHANNEL_IN_Y_AXIS |
                              ADEV_CHANNEL_IN_Z_AXIS |
                              ADEV_CHANNEL_IN_VOICE_UPLINK |
                              ADEV_CHANNEL_IN_VOICE_DNLINK),   /**< */
    /** @endcond */
};

/**
 *  @brief Audio channel mask
 */
typedef uint32_t adev_channel_mask_t;

/** @cond AUDIO_YUNHAL */

/**< Maximum number of adev_channel for all descriptions */
#define ADEV_CHANNEL_COUNT_MAX          30

/**< Maximum number of descriptions in log(2) */
#define ADEV_CHANNEL_DESCRIPTION_LOG2   2

/**
 *  @brief Audio channel mask definitions
 */
typedef enum {
    ADEV_CHANNEL_DESCRIPTION_POSITION    = 0,
    /**< @note must be zero for compatibility */
    ADEV_CHANNEL_DESCRIPTION_INDEX       = 2,
    /**< @note must be 2 for compatibility */
    /**< @note 1 and 3 are reserved for future use */
} adev_channel_description_t;
/** @endcond */

/* @brief The channel index masks for 1 to 8 channel endpoints and apply to both source and sink.
 */
enum {
    /** @cond AUDIO_YUNHAL */
    ADEV_CHANNEL_INDEX_HDR  = ADEV_CHANNEL_DESCRIPTION_INDEX << ADEV_CHANNEL_COUNT_MAX,
    /** @endcond */
    ADEV_CHANNEL_INDEX_MASK_1 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 1) - 1),   /**< One channel*/
    ADEV_CHANNEL_INDEX_MASK_2 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 2) - 1),   /**< Two channels*/
    ADEV_CHANNEL_INDEX_MASK_3 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 3) - 1),   /**< Three channels*/
    ADEV_CHANNEL_INDEX_MASK_4 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 4) - 1),   /**< Four channels*/
    ADEV_CHANNEL_INDEX_MASK_5 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 5) - 1),   /**< Five channels*/
    ADEV_CHANNEL_INDEX_MASK_6 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 6) - 1),   /**< Six channels*/
    ADEV_CHANNEL_INDEX_MASK_7 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 7) - 1),   /**< Seven channels*/
    ADEV_CHANNEL_INDEX_MASK_8 =  ADEV_CHANNEL_INDEX_HDR | ((1 << 8) - 1),   /**< Eight channels*/
};

/** @cond AUDIO_YUNHAL */

/**
 *  @brief Get low bit values from the channel mask
 *  @details The return value is undefined if the channel mask is invalid.
 *  @return uint32_t
 */
static inline uint32_t adev_channel_mask_get_bits(adev_channel_mask_t channel) {
    return channel & ((1 << ADEV_CHANNEL_COUNT_MAX) - 1);
}

/**
 *  @brief Get description from the channel mask
 *  @details The return value is undefined if the channel mask is invalid.
 *  @return adev_channel_description_t
 */
static inline adev_channel_description_t adev_channel_mask_get_description (
        adev_channel_mask_t channel)
{
    return (adev_channel_description_t)
            ((channel >> ADEV_CHANNEL_COUNT_MAX) & ((1 << ADEV_CHANNEL_DESCRIPTION_LOG2) - 1));
}

/**
 *  @brief Verify the adev_channel_mast value
 *  @return bool
 *  @retval true for valid channel mask,
 *  @retval false for ADEV_CHANNEL_NONE, ADEV_CHANNEL_INVALID, and all invalid values.
 */
static inline bool adev_channel_mask_is_valid(adev_channel_mask_t channel) {
    uint32_t bits = adev_channel_mask_get_bits(channel);
    adev_channel_description_t description = adev_channel_mask_get_description(channel);
    switch (description) {
    case ADEV_CHANNEL_DESCRIPTION_POSITION:
    case ADEV_CHANNEL_DESCRIPTION_INDEX:
        break;
    default:
        bits = 0;
        break;
    }
    return bits != 0;
}

/**
 *  @brief Retrieve channel mask according to the description and bit values
 *  @return adev_channel_mask_t
 */
static inline adev_channel_mask_t adev_channel_mask_from_description_and_bits(
        adev_channel_description_t description, uint32_t bits)
{
    return (adev_channel_mask_t) ((description << ADEV_CHANNEL_COUNT_MAX) | bits);
}
/** @endcond */

/**
 *  @brief Audio output flags
 *  @details
 *  1. Indicates an AudioRender intends to attach a output stream with attributes
 *  described by the flags
 *  2. Described the output stream with certain attributes
 */
typedef enum {
    ADEV_OUTPUT_FLAG_NONE                   = 0x0,      /**< no attributes*/
    ADEV_OUTPUT_FLAG_DIRECT                 = 0x1,      /**< directly connects an AudioRender
                                                        * to one hal output stream: no format convert
                                                        * and software mixer
                                                        */
    ADEV_OUTPUT_FLAG_PRIMARY                = 0x2,      /**< this is the primary output of the audio
                                                        * device. It must be present and is unique.
                                                        * And receives routing, audio mode and volume
                                                        * controls related to voice calls.
                                                        */
    ADEV_OUTPUT_FLAG_FAST                   = 0x4,      /**< this output supports "fast streams"*/
    ADEV_OUTPUT_FLAG_DEEP_BUFFER            = 0x8,      /**< this output use deep audio buffers,
                                                        * usually used for long time music or video playbacks*/
    ADEV_OUTPUT_FLAG_COMPRESS_OFFLOAD       = 0x10,     /**< this output connects compressed streams
                                                        * to hardware codec*/
    ADEV_OUTPUT_FLAG_NON_BLOCKING           = 0x20,     /**< this output use non-blocking write*/
    ADEV_OUTPUT_FLAG_HW_AV_SYNC             = 0x40,     /**< this output uses a hardware A/V synchronization source*/
    ADEV_OUTPUT_FLAG_TTS                    = 0x80,     /**< this output is used for streams which will transmitted
                                                        *  through speaker at a sample rate high enough to accommodate
                                                        * ower-range ultrasonic playback
                                                        */
    /** @cond AUDIO_YUNHAL */

    ADEV_OUTPUT_FLAG_RAW                    = 0x100,    /**< */
    ADEV_OUTPUT_FLAG_SYNC                   = 0x200,    /**< */
    ADEV_OUTPUT_FLAG_IEC958_NONAUDIO        = 0x400,    /**< */
    ADEV_OUTPUT_FLAG_VOIP_RX                = 0x800,    /**< */
    ADEV_OUTPUT_FLAG_COMPRESS_PASSTHROUGH   = 0x1000,   /**< */
    ADEV_OUTPUT_FLAG_DIRECT_PCM             = 0x2000,   /**< */
    ADEV_OUTPUT_FLAG_INCALL_MUSIC           = 0x8000,   /**< */
    ADEV_OUTPUT_FLAG_16BIT                  = 0x10000   /**< */
    /** @endcond */
} adev_output_flags_t;

/**
 *  @brief Audio input flags
 *  @details
 *  Indicates an AudioCapture intends to attach a input stream with attributes
 *  described by the flags
 */
typedef enum {
    ADEV_INPUT_FLAG_NONE       = 0x0,   /**< no attributes*/
    ADEV_INPUT_FLAG_FAST       = 0x1,   /**< an input that supports "fast streams"*/
    ADEV_INPUT_FLAG_HW_HOTWORD = 0x2,   /**< an input that captures from hw hotword source*/
    /** @cond AUDIO_YUNHAL */
    ADEV_INPUT_FLAG_RAW        = 0x4,   /**< */
    ADEV_INPUT_FLAG_SYNC       = 0x8,   /**< */
    /** @endcond */
} adev_input_flags_t;

/**
 *  @brief Enumerations for audio endpoint
 */
enum {
    ADEV_ENDPOINT_NONE                          = 0x0,          /**< */
    /** @cond AUDIO_YUNHAL */
    ADEV_ENDPOINT_BIT_IN                        = 0x80000000,   /**< */
    ADEV_ENDPOINT_BIT_DEFAULT                   = 0x40000000,   /**< */
    /** @endcond */
    ADEV_ENDPOINT_OUT_EARPIECE                  = 0x1,          /**< earpiece endpoint*/
    ADEV_ENDPOINT_OUT_SPEAKER                   = 0x2,          /**< speaker endpoint*/
    ADEV_ENDPOINT_OUT_WIRED_HEADSET             = 0x4,          /**< wired headset with a mic on it*/
    ADEV_ENDPOINT_OUT_WIRED_HEADPHONE           = 0x8,          /**< wired headphone with no mic*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_SCO             = 0x10,         /**< bluetooth sco*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_HEADSET     = 0x20,         /**< bluetooth sco headset*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_CARKIT      = 0x40,         /**< bluetooth sco carkit*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP            = 0x80,         /**< bluetooth a2dp*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_HEADPHONES = 0x100,        /**< bluetooth a2dp headphones*/
    ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_SPEAKER    = 0x200,        /**< bluetooth a2dp speaker*/
    ADEV_ENDPOINT_OUT_AUX_DIGITAL               = 0x400,        /**< */
    ADEV_ENDPOINT_OUT_HDMI                      = ADEV_ENDPOINT_OUT_AUX_DIGITAL, /**< HDMI endpoint*/
    ADEV_ENDPOINT_OUT_ANLG_DOCK_HEADSET         = 0x800,        /**< */
    ADEV_ENDPOINT_OUT_DGTL_DOCK_HEADSET         = 0x1000,       /**< */
    ADEV_ENDPOINT_OUT_USB_ACCESSORY             = 0x2000,       /**< USB accessory mode: the dock is a USB host
                                                                * and your device is a USB device
                                                                */
    ADEV_ENDPOINT_OUT_USB_DEVICE                = 0x4000,       /**< USB host mode: the dock is a USB device
                                                                * and your plugin device is a USB host*/
    ADEV_ENDPOINT_OUT_REMOTE_SUBMIX             = 0x8000,       /**< remot submix endpoint*/
    ADEV_ENDPOINT_OUT_TELEPHONY_TX              = 0x10000,      /**< Telephony voice TX endpoint*/
    ADEV_ENDPOINT_OUT_LINE                      = 0x20000,      /**< */
    ADEV_ENDPOINT_OUT_HDMI_ARC                  = 0x40000,      /**< */
    ADEV_ENDPOINT_OUT_SPDIF                     = 0x80000,      /**< */
    ADEV_ENDPOINT_OUT_FM                        = 0x100000,     /**< FM transmitter out endpoint*/
    ADEV_ENDPOINT_OUT_AUX_LINE                  = 0x200000,     /**< */
    ADEV_ENDPOINT_OUT_SPEAKER_SAFE              = 0x400000,     /**< */
    ADEV_ENDPOINT_OUT_IP                        = 0x800000,     /**< */
    ADEV_ENDPOINT_OUT_BUS                       = 0x1000000,    /**< */
    ADEV_ENDPOINT_OUT_PROXY                     = 0x2000000,    /**< */
/** @cond AUDIO_PRIVATE */
//HTC_AUD_START -Mirrorlink
    ADEV_ENDPOINT_OUT_MIRRORLINK                = 0x4000000,    /**< */
//HTC_AUD_END
/** @endcond */

    ADEV_ENDPOINT_OUT_DEFAULT                   = ADEV_ENDPOINT_BIT_DEFAULT,
    /** @cond AUDIO_YUNHAL */
    ADEV_ENDPOINT_OUT_ALL      = (ADEV_ENDPOINT_OUT_EARPIECE |
                                  ADEV_ENDPOINT_OUT_SPEAKER |
                                  ADEV_ENDPOINT_OUT_WIRED_HEADSET |
                                  ADEV_ENDPOINT_OUT_WIRED_HEADPHONE |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_SCO |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_HEADSET |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_CARKIT |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_HEADPHONES |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_SPEAKER |
                                  ADEV_ENDPOINT_OUT_HDMI |
                                  ADEV_ENDPOINT_OUT_ANLG_DOCK_HEADSET |
                                  ADEV_ENDPOINT_OUT_DGTL_DOCK_HEADSET |
                                  ADEV_ENDPOINT_OUT_USB_ACCESSORY |
                                  ADEV_ENDPOINT_OUT_USB_DEVICE |
                                  ADEV_ENDPOINT_OUT_REMOTE_SUBMIX |

/** @cond AUDIO_PRIVATE */
//HTC_AUD_START - Mirrorlink
                                  ADEV_ENDPOINT_OUT_MIRRORLINK |
//HTC_AUD_END
/** @endcond */

                                  ADEV_ENDPOINT_OUT_TELEPHONY_TX |
                                  ADEV_ENDPOINT_OUT_LINE |
                                  ADEV_ENDPOINT_OUT_HDMI_ARC |
                                  ADEV_ENDPOINT_OUT_SPDIF |
                                  ADEV_ENDPOINT_OUT_FM |
                                  ADEV_ENDPOINT_OUT_AUX_LINE |
                                  ADEV_ENDPOINT_OUT_SPEAKER_SAFE |
                                  ADEV_ENDPOINT_OUT_IP |
                                  ADEV_ENDPOINT_OUT_BUS |
                                  ADEV_ENDPOINT_OUT_PROXY |
                                  ADEV_ENDPOINT_OUT_DEFAULT),
    ADEV_ENDPOINT_OUT_ALL_A2DP = (ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP |
                                ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_HEADPHONES |
                                ADEV_ENDPOINT_OUT_BLUETOOTH_A2DP_SPEAKER),
    ADEV_ENDPOINT_OUT_ALL_SCO  = (ADEV_ENDPOINT_OUT_BLUETOOTH_SCO |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_HEADSET |
                                  ADEV_ENDPOINT_OUT_BLUETOOTH_SCO_CARKIT),
    ADEV_ENDPOINT_OUT_ALL_USB  = (ADEV_ENDPOINT_OUT_USB_ACCESSORY |
                                  ADEV_ENDPOINT_OUT_USB_DEVICE),
    /** @endcond */

    ADEV_ENDPOINT_IN_COMMUNICATION         = ADEV_ENDPOINT_BIT_IN | 0x1,    /**< */
    ADEV_ENDPOINT_IN_AMBIENT               = ADEV_ENDPOINT_BIT_IN | 0x2,    /**< */
    ADEV_ENDPOINT_IN_BUILTIN_MIC           = ADEV_ENDPOINT_BIT_IN | 0x4,    /**< */
    ADEV_ENDPOINT_IN_BLUETOOTH_SCO_HEADSET = ADEV_ENDPOINT_BIT_IN | 0x8,    /**< */
    ADEV_ENDPOINT_IN_WIRED_HEADSET         = ADEV_ENDPOINT_BIT_IN | 0x10,   /**< */
    ADEV_ENDPOINT_IN_AUX_DIGITAL           = ADEV_ENDPOINT_BIT_IN | 0x20,   /**< */
    ADEV_ENDPOINT_IN_HDMI                  = ADEV_ENDPOINT_IN_AUX_DIGITAL,  /**< */
    ADEV_ENDPOINT_IN_VOICE_CALL            = ADEV_ENDPOINT_BIT_IN | 0x40,   /**< */
    ADEV_ENDPOINT_IN_TELEPHONY_RX          = ADEV_ENDPOINT_IN_VOICE_CALL,   /**< */
    /** @cond AUDIO_PRIVATE */
    ADEV_ENDPOINT_IN_BUILTIN_MIC2          = ADEV_ENDPOINT_BIT_IN | 0x80, //HTC_AUDIO
    /** @endcond */
    ADEV_ENDPOINT_IN_BACK_MIC              = ADEV_ENDPOINT_BIT_IN | 0x80,   /**< */
    ADEV_ENDPOINT_IN_REMOTE_SUBMIX         = ADEV_ENDPOINT_BIT_IN | 0x100,  /**< */
    ADEV_ENDPOINT_IN_ANLG_DOCK_HEADSET     = ADEV_ENDPOINT_BIT_IN | 0x200,  /**< */
    ADEV_ENDPOINT_IN_DGTL_DOCK_HEADSET     = ADEV_ENDPOINT_BIT_IN | 0x400,  /**< */
    ADEV_ENDPOINT_IN_USB_ACCESSORY         = ADEV_ENDPOINT_BIT_IN | 0x800,  /**< */
    ADEV_ENDPOINT_IN_USB_DEVICE            = ADEV_ENDPOINT_BIT_IN | 0x1000, /**< */
    ADEV_ENDPOINT_IN_FM_TUNER              = ADEV_ENDPOINT_BIT_IN | 0x2000, /**< */
    ADEV_ENDPOINT_IN_TV_TUNER              = ADEV_ENDPOINT_BIT_IN | 0x4000, /**< */
    ADEV_ENDPOINT_IN_LINE                  = ADEV_ENDPOINT_BIT_IN | 0x8000, /**< */
    ADEV_ENDPOINT_IN_SPDIF                 = ADEV_ENDPOINT_BIT_IN | 0x10000, /**< */
    ADEV_ENDPOINT_IN_BLUETOOTH_A2DP        = ADEV_ENDPOINT_BIT_IN | 0x20000, /**< */
    ADEV_ENDPOINT_IN_LOOPBACK              = ADEV_ENDPOINT_BIT_IN | 0x40000, /**< */
    ADEV_ENDPOINT_IN_IP                    = ADEV_ENDPOINT_BIT_IN | 0x80000, /**< */
    ADEV_ENDPOINT_IN_BUS                   = ADEV_ENDPOINT_BIT_IN | 0x100000,/**< */
/** @cond AUDIO_PRIVATE */
//HTC_AUD_START
    ADEV_ENDPOINT_IN_FM_RX                 = ADEV_ENDPOINT_BIT_IN | 0x200000,/**< */
    ADEV_ENDPOINT_IN_FM_RX_A2DP            = ADEV_ENDPOINT_BIT_IN | 0x400000,/**< */
    ADEV_ENDPOINT_IN_MIRRORLINK            = ADEV_ENDPOINT_BIT_IN | 0x800000,/**< */
//HTC_AUD_END
/** @endcond */

    ADEV_ENDPOINT_IN_PROXY                 = ADEV_ENDPOINT_BIT_IN | 0x1000000,/**< */
/** @cond AUDIO_PRIVATE */
//HTC_AUD_START
    ADEV_ENDPOINT_IN_BUILTIN_MIC3          = ADEV_ENDPOINT_BIT_IN | 0x2000000,/**< */
//HTC_AUD_END
/** @endcond */

    ADEV_ENDPOINT_IN_DEFAULT               = ADEV_ENDPOINT_BIT_IN | ADEV_ENDPOINT_BIT_DEFAULT,/**< */

    /** @cond AUDIO_YUNHAL */
    ADEV_ENDPOINT_IN_ALL     = (ADEV_ENDPOINT_IN_COMMUNICATION |
                                ADEV_ENDPOINT_IN_AMBIENT |
                                ADEV_ENDPOINT_IN_BUILTIN_MIC |
                                ADEV_ENDPOINT_IN_BLUETOOTH_SCO_HEADSET |
                                ADEV_ENDPOINT_IN_WIRED_HEADSET |
                                ADEV_ENDPOINT_IN_HDMI |
                                ADEV_ENDPOINT_IN_TELEPHONY_RX |
                                ADEV_ENDPOINT_IN_BACK_MIC |
                                ADEV_ENDPOINT_IN_REMOTE_SUBMIX |
                                ADEV_ENDPOINT_IN_ANLG_DOCK_HEADSET |
                                ADEV_ENDPOINT_IN_DGTL_DOCK_HEADSET |
                                ADEV_ENDPOINT_IN_USB_ACCESSORY |
                                ADEV_ENDPOINT_IN_USB_DEVICE |
                                ADEV_ENDPOINT_IN_FM_TUNER |
                                ADEV_ENDPOINT_IN_TV_TUNER |
                                ADEV_ENDPOINT_IN_LINE |
                                ADEV_ENDPOINT_IN_SPDIF |
                                ADEV_ENDPOINT_IN_BLUETOOTH_A2DP |
                                ADEV_ENDPOINT_IN_LOOPBACK |
                                ADEV_ENDPOINT_IN_IP |
                                ADEV_ENDPOINT_IN_BUS |
                                ADEV_ENDPOINT_IN_PROXY |
/** @cond AUDIO_PRIVATE */
//HTC_AUD_START
                                ADEV_ENDPOINT_IN_FM_RX |
                                ADEV_ENDPOINT_IN_FM_RX_A2DP |
                                ADEV_ENDPOINT_IN_MIRRORLINK |
                                ADEV_ENDPOINT_IN_BUILTIN_MIC2 |
                                ADEV_ENDPOINT_IN_BUILTIN_MIC3 |
//HTC_AUD_END
/** @endcond */

                                ADEV_ENDPOINT_IN_DEFAULT),

    ADEV_ENDPOINT_IN_ALL_SCO = ADEV_ENDPOINT_IN_BLUETOOTH_SCO_HEADSET,
    ADEV_ENDPOINT_IN_ALL_USB  = (ADEV_ENDPOINT_IN_USB_ACCESSORY |
                                 ADEV_ENDPOINT_IN_USB_DEVICE),
    /** @endcond */

};
/**
 *  @brief audio output and input endpoint type
 */
typedef uint32_t adev_endpoints_t;

/** @cond AUDIO_PRIVATE */
#define ADEV_ENDPOINT_OUT_DEFAULT_FOR_VOLUME    ADEV_ENDPOINT_OUT_DEFAULT
/** @endcond */

/** @cond AUDIO_YUNHAL */

typedef enum {
    ADEV_USAGE_UNKNOWN                            = 0,
    ADEV_USAGE_MEDIA                              = 1,
    ADEV_USAGE_VOICE_COMMUNICATION                = 2,
    ADEV_USAGE_VOICE_COMMUNICATION_SIGNALLING     = 3,
    ADEV_USAGE_ALARM                              = 4,
    ADEV_USAGE_NOTIFICATION                       = 5,
    ADEV_USAGE_NOTIFICATION_TELEPHONY_RINGTONE    = 6,
    ADEV_USAGE_NOTIFICATION_COMMUNICATION_REQUEST = 7,
    ADEV_USAGE_NOTIFICATION_COMMUNICATION_INSTANT = 8,
    ADEV_USAGE_NOTIFICATION_COMMUNICATION_DELAYED = 9,
    ADEV_USAGE_NOTIFICATION_EVENT                 = 10,
    ADEV_USAGE_ASSISTANCE_ACCESSIBILITY           = 11,
    ADEV_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE     = 12,
    ADEV_USAGE_ASSISTANCE_SONIFICATION            = 13,
    ADEV_USAGE_GAME                               = 14,
    ADEV_USAGE_VIRTUAL_SOURCE                     = 15,

    /** @cond AUDIO_PRIVATE */
//HTC_AUD_START - Mirrorlink
    ADEV_USAGE_MIRRORLINK_RECORD                  = 16,
//HTC_AUD_END
    /** @endcond */

    ADEV_USAGE_CNT,
    ADEV_USAGE_MAX                                = ADEV_USAGE_CNT - 1,
} adev_usage_t;
/** @endcond */

/**
 *  @brief The information on offloaded streams
 *  @details
 *  The information describes the compressed streams which is offloaded to hardware playback
 *  The version and size MUST be initialized by using the contants defined below.
 */
typedef struct {
    uint16_t version;                   /**< version of this structure*/
    uint16_t size;                      /**< size of this structure*/
    uint32_t sample_rate;               /**< sample rate in Hz of the compressed stream*/
    adev_channel_mask_t channel_mask;   /**< channel layout information*/
    snd_format_t format;                /**< sound format of the compressed stream*/
    as_type_t stream_type;              /**< stream type of the compressed stream*/
    uint32_t bit_rate;                  /**< bit rate in bits per second of the compressed stream*/
    int64_t duration_us;                /**< the whole duration in microseconds of the compressed stream,
                                         * set it with -1 if duration is unknown*/
    bool has_video;                     /**< true if this stream is from an video playback*/
    bool is_streaming;                  /**< true if this is an streaming playpack.
                                        * false if this is an local playback
                                        */
    /** @cond AUDIO_PRIVATE */
    uint32_t bit_width;                 /**< */
    uint32_t offload_buffer_size;       /**< */
    adev_usage_t usage;                 /**< */
    /** @endcond */
} adev_offload_info_t;

/** @cond AUDIO_PRIVATE */
#define ADEV_CREATE_OFFLOAD_VER(main,sub)   ((((main) & 0xFF) << 8) | ((sub) & 0xFF))
#define ADEV_OFFLOAD_VER_0_1                ADEV_CREATE_OFFLOAD_VER(0, 1)
/** @endcond */
/**
* @brief current offload info version
*/
#define ADEV_OFFLOAD_VER_CURRENT            ADEV_OFFLOAD_VER_0_1

/** @cond AUDIO_YUNHAL */
/**
*  @brief common audio stream configuration parameters
*/
typedef struct as_config {
    uint32_t sample_rate;               /**< sampling rate */
    adev_channel_mask_t channel_mask;   /**< channel mask */
    snd_format_t  format;               /**< sound format */
    adev_offload_info_t offload_info;   /**< offload information */
    size_t frame_count;                 /**< frame count */
} as_config_t;

/**
 *  @brief The connection state of endpoint in audio route manager
 */
typedef enum {
    ARS_ENDPOINT_STATE_UNAVAILABLE,     /**< */
    ARS_ENDPOINT_STATE_AVAILABLE,       /**< */
    ARS_ENDPOINT_STATE_CNT,             /**< */
    ARS_ENDPOINT_STATE_MAX = ARS_ENDPOINT_STATE_CNT - 1,    /**< */
    ARS_ENDPOINT_STATE_INVALID,         /**< */
} ars_endpoint_state_t;

/**
 *  @brief Enumerations for the configurations for force use in audio route manager
 */
typedef enum {
    ARS_FORCE_NONE,
    ARS_FORCE_SPEAKER,
    ARS_FORCE_HEADPHONES,
    ARS_FORCE_BT_SCO,
    ARS_FORCE_BT_A2DP,
    ARS_FORCE_WIRED_ACCESSORY,
    ARS_FORCE_BT_CAR_DOCK,
    ARS_FORCE_BT_DESK_DOCK,
    ARS_FORCE_ANALOG_DOCK,
    ARS_FORCE_DIGITAL_DOCK,
    ARS_FORCE_NO_BT_A2DP,
    ARS_FORCE_SYSTEM_ENFORCED,
    ARS_FORCE_HDMI_SYSTEM_AUDIO_ENFORCED,
    ARS_FORCE_ENCODED_SURROUND_NEVER,
    ARS_FORCE_ENCODED_SURROUND_ALWAYS,
    ARS_FORCE_CFG_CNT,
    ARS_FORCE_CFG_MAX = ARS_FORCE_CFG_CNT - 1,
    ARS_FORCE_DEFAULT = ARS_FORCE_NONE,
    ARS_FORCE_CFG_INVALID
} ars_forced_cfg_t;

/**
 *  @brief Enumerations for the usages for force use in audio route manager
 */
typedef enum {
    ARS_FORCE_FOR_COMMUNICATION,
    ARS_FORCE_FOR_MEDIA,
    ARS_FORCE_FOR_RECORD,
    ARS_FORCE_FOR_DOCK,
    ARS_FORCE_FOR_SYSTEM,
    ARS_FORCE_FOR_HDMI_SYSTEM_AUDIO,
    ARS_FORCE_FOR_ENCODED_SURROUND,
    ARS_FORCE_USE_CNT,
    ARS_FORCE_USE_MAX = ARS_FORCE_USE_CNT - 1,
} ars_force_use_t;

/**
 *  @brief The definition for beep usage
 */
typedef enum {
    ARS_BEEP_IN_CALL_NOTIFICATION = 0,  /**< Generate a beep as notification for user while in a call */
    ARS_BEEP_CNT,                       /**< */
    ARS_BEEP_MAX                  = ARS_BEEP_CNT - 1,   /**< */
} ars_beep_t;

/**
 *  @brief Audio error numbers
 */
typedef enum {
    ERR_NONE = 0,               /**< */
    ERR_UNKNOWN,                /**< */
    ERR_INVALID_PARAM,          /**< */
    ERR_INVALID_OPERATION,      /**< */
    ERR_OUT_OF_MEMORY,          /**< */
    ERR_BAD_VALUE,              /**< */
    ERR_PERMISSION_DENIED,      /**< */
    ERR_NO_ENOUGH_DATA,         /**< */
    ERR_WOULD_BLOCK,            /**< */
    ERR_UNSUPPORTED_FEATURE     /**< */
} error_code_t;
typedef int ret_t;

/**
*   @brief Audio Module Version
*/
enum am_version_t {
    HAL_AM_VER_1 = 0,   /**< Audio Module Version 1*/
    HAL_AM_VER_2,       /**< Audio Module Version 2*/
    HAL_AM_VER_3,       /**< Audio Module Version 3*/
};
#define HAL_AM_VER_CURRENT HAL_AM_VER_1

/**
*   @brief Audio Device Version
*/
enum ad_version_t {
    HAL_AD_VER_0 = 0,   /**< Audio Device Version 0*/
    HAL_AD_VER_1,       /**< Audio Device Version 1*/
    HAL_AD_VER_2,       /**< Audio Device Version 2*/
    HAL_AD_VER_3,       /**< Audio Device Version 3*/
};
#define HAL_AD_VER_CURRENT HAL_AD_VER_3

/* Minimal audio HAL version supported by AudioStreamer*/
#define HAL_AD_VER_MIN HAL_AD_VER_2

/*  Volume Control  */
enum snd_gain_mode_val {
    SND_GAIN_MODE_JOINT = 0x1,
    SND_GAIN_MODE_CHANNELS = 0x2,
    SND_GAIN_MODE_RAMP = 0x4,
};
typedef uint32_t snd_gain_mode_t;

//  An snd_gain struct is a representation of a gain stage.
typedef struct snd_gain {
    snd_gain_mode_t mode;
    adev_channel_mask_t channel_mask;
    int min;
    int max;
    int def;
    unsigned int step;
    unsigned int min_ramp_ms;
    unsigned int max_ramp_ms;
} snd_gain_t;

//  The gain configuration structure is used to get/set the gain
//  values of a given endpoint
typedef struct snd_gain_param {
    int idx;
    snd_gain_mode_t mode;
    adev_channel_mask_t channel_mask;
    int values[sizeof(adev_channel_mask_t) * 8];
    unsigned int ramp_duration_ms;
} snd_gain_param_t;

/*  Routing Control */

/*  Audio Endpoint Role*/
typedef enum {
    AER_NONE,
    AER_IN,
    AER_OUT,
} adev_endpoint_role_t;

/* Audio Endpoint Type*/
typedef enum {
    AET_NONE,
    AET_PHYS,
    AET_MIX,
    AET_TRANS, // transaction corresponding to session
} adev_endpoint_type_t;

/* Maximum length of device name */
#define ADEV_ENDPOINT_MAX_NAME_LEN 128
#define ADEV_ENDPOINT_MAX_ADDR_LEN 32

/* extension for audio endpoint type */
typedef union {
    //  extension for audio endpoint configuration structure
    //  when the endpoint is a physical one.
    struct {
        adev_handle_t devHandle; //
        adev_endpoints_t type;
        char addr[ADEV_ENDPOINT_MAX_ADDR_LEN];
    } phys;

    //  extension for audio endpoint configuration structure
    //  when the endpoint is a sub mix.
    struct {
        adev_handle_t devHandle;
        adev_endpoint_handle_t endpointHandle;
        union {
            as_type_t stream;
            adev_source_t source;
        } usecase;
    } submix;

    //  extension for audio endpoint configuration structure
    //  when the endpoint is an transaction
    struct {
        adev_session_t trans; /*audio transaction (corresponding to audio session)*/
    } trans;
} adev_endpoint_param_ext_t;

/* flags indicating which fields are to be considered in adev_endpoint_param*/
#define ADEV_ENDPOINT_PARAM_SAMPLE_RATE 0x1
#define ADEV_ENDPOINT_PARAM_CHANNEL_MASK 0x2
#define ADEV_ENDPOINT_PARAM_FORMAT 0x4
#define ADEV_ENDPOINT_PARAM_GAIN 0x8
#define ADEV_ENDPOINT_PARAM_ALL (ADEV_ENDPOINT_PARAM_SAMPLE_RATE | \
                                ADEV_ENDPOINT_PARAM_CHANNEL_MASK | \
                                ADEV_ENDPOINT_PARAM_FORMAT | \
                                ADEV_ENDPOINT_PARAM_GAIN)

// audio endpoint parameter used to specify a particular audio endpoint
typedef struct adev_endpoint_param {
    adev_endpoint_handle_t id;  /*  endpoint unique ID  */
    adev_endpoint_role_t role;
    adev_endpoint_type_t type;
    unsigned int    config_mask;
    unsigned int sample_rate;
    adev_channel_mask_t channel_mask;
    snd_format_t format;
    snd_gain_param_t gain;
    adev_endpoint_param_ext_t ext;
} adev_endpoint_param_t;

#define ADEV_ENDPOINT_MAX_PARAM_CNT 16
typedef enum {
    ADEV_LATENCY_LOW,
    ADEV_LATENCY_NORMAL
} adev_mix_latency_class_t;

typedef union adev_endpoint_ext {
    struct {
        adev_handle_t devHandle;
        adev_endpoints_t type;
        char addr[ADEV_ENDPOINT_MAX_ADDR_LEN];
    } phys;

    struct {
        adev_handle_t devHandle;
        adev_endpoint_handle_t handle;
        adev_mix_latency_class_t latencyClass;
    } mix;

    struct {
        adev_session_t trans; /*audio transaction (corresponding to audio session)*/
    } trans;
} adev_endpoint_ext_t;

typedef struct adev_endpoint {
    adev_endpoint_handle_t id;  /*  endpoint unique ID  */
    adev_endpoint_role_t role;
    adev_endpoint_type_t type;
    char name[ADEV_ENDPOINT_MAX_NAME_LEN];
    unsigned int num_sample_rates;
    unsigned int sample_rates[ADEV_ENDPOINT_MAX_PARAM_CNT];
    unsigned int num_channel_masks;
    adev_channel_mask_t channel_masks[ADEV_ENDPOINT_MAX_PARAM_CNT];
    unsigned int num_formats;
    snd_format_t formats[ADEV_ENDPOINT_MAX_PARAM_CNT];
    unsigned int num_gains;
    snd_gain_t gains[ADEV_ENDPOINT_MAX_PARAM_CNT];
    adev_endpoint_param_t active_param; //current audio endpoint's configuration
    adev_endpoint_ext_t ext;
} adev_endpoint_t;

/* Audio Tunnel represents the connection between one or more source endpoints and one or
more sink endpoints.*/
typedef enum {
    ADEV_TUNNEL_HANDLE_NONE = 0,
} adev_tunnel_handle_t;

#define ADEV_TUNNEL_MAX_ENDPOINTS_CNT 16
typedef struct adev_tunnel {
    adev_tunnel_handle_t id; /* tunnel unique id*/
    unsigned int num_sources;
    adev_endpoint_param_t sources[ADEV_TUNNEL_MAX_ENDPOINTS_CNT];
    unsigned int num_sinks;
    adev_endpoint_param_t sinks[ADEV_TUNNEL_MAX_ENDPOINTS_CNT];
} adev_tunnel_t;

/* a HW sync source handle returned by the audio HAL.*/
typedef uint32_t adev_hw_sync_handle_t;
/** @endcond */

/** @} */ // end of YUNOS_AUDIO

__END_DECLS

#endif

