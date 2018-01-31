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

#ifndef __mediaplayer_H
#define __mediaplayer_H

#include <map>
#include <string>
#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mmlistener.h>
#include <multimedia/media_meta.h>
#include <multimedia/media_buffer.h>

namespace YUNOS_MM {
class Pipeline;
typedef MMSharedPtr<Pipeline> PipelineSP;

class MediaPlayer {
public:
    enum PlayerType {
        PlayerType_DEFAULT,
        PlayerType_ADO,
        PlayerType_COMPAT,
        PlayerType_COW,
        PlayerType_COWAudio,
        PlayerType_LPA,
        PlayerType_PROXY,
        PlayerType_PROXY_Audio,
        PlayerType_LOCAL_Audio,
    };

    static MediaPlayer * create(PlayerType type = PlayerType_DEFAULT, const void * userDefinedData = NULL);
    static void destroy(MediaPlayer * player);

public:
    class Listener : public MMListener {
    public:
        Listener(){}
        virtual ~Listener(){}

        enum message_type {
            // only for test
            MSG_NOP               = 0,
            // player has prepared over.
            // params:
            //     param1: prepare result
            MSG_PREPARED          = 1,
            // player has playback completed.
            // params: none
            MSG_PLAYBACK_COMPLETE = 2,
            // buffering percentage
            // params:
            //     param1: percent(0~100)
            MSG_BUFFERING_UPDATE  = 3,
            // player has seek complted
            // params:
            //     param1: seek result
            MSG_SEEK_COMPLETE     = 4,
            // set video size
            // params:
            //     param1: int width
            //     param2: int height
            MSG_SET_VIDEO_SIZE    = 5,
            // player started
            // params:
            //     param1: start result
            MSG_STARTED           = 6,
            // player paused
            // params:
            //     param1: pause result
            MSG_PAUSED            = 7,
            // player stopped
            // params:
            //     param1: stop result
            MSG_STOPPED           = 8,
            // progressive download started
            // params:
            //     none
            MSG_PD_STARTED = 9,
            // progressive download completed
            // params:
            //     none
            MSG_PD_COMPLETED = 10,
            // video is captured
            // params:
            //     param1: undefined
            //     param2: undefined
            //     obj: int32_t: width
            //            int32_t: height
            //            int32_t: buffer size
            //
            MSG_VIDEO_CAPTURED = 11,
            MSG_VIDEO_REQUEST_IDR = 12,
            MSG_SKIPPED = 20,
            MSG_TIMED_TEXT        = 99,
            // an error occured
            // params:
            //     param1: erro code, see mm_errors_t
            MSG_ERROR             = 100,
            // information
            // params:
            //     param1: see info_type
            MSG_INFO              = 200,
            // extended information, by manufacture
            // params:
            //     param1: see manufacture's document
            MSG_INFO_EXT              = 401,
            // subtitle is updated
            // params:
            //     param1: unsigned long duration: The duration of the updated subtitle
            //     param2: not defined
            //     obj: const char *, The text of the updated subtitle.
            MSG_SUBTITLE_UPDATED = 202,
            //MSG_SUBTITLE_DATA     = 201,
            MSG_DURATION_UPDATE   = 300,

            // video frame to update texture image
            // params:
            //     param1: buffer index
            //     param2: not defined
            //     obj: not defined
            MSG_UPDATE_TEXTURE_IMAGE = 501,
            // video rotation degree
            // params:
            //     param1: rotation degree
            //     param2: not defined
            //     obj: not defined
            MSG_VIDEO_ROTATION_DEGREE = 502,
        };

        enum info_type {
                INFO_UNKNOWN = 1,
                //The player was started as the next player when another player playbak complete
                INFO_STARTED_AS_NEXT = 2,
                //The first video frame is pushed for rendering
                INFO_RENDERING_START = 3,
                // 7xx
                //The video frame can't be deocded fast enough because
                //the video is too complex for the decoder.At this stage, perhaps only the audio plays fine
                INFO_VIDEO_TRACK_LAGGING = 700,
                //In order to buffer more data, the player is internally paused temporarily.
                INFO_BUFFERING_START = 701,
                //The player resume playback after bufferring enough data.
                INFO_BUFFERING_END = 702,
                // The network bandwidth in recent past
                INFO_NETWORK_BANDWIDTH = 703,

                // 8xx
                //If a media has been improperly interleaved or not interleaved at all,
                //e.g has all the video samples first then all the audio
                // ones.This situation is referred to as Bad interleaving.
                //A lot of disk seek may be happening when the video is playing.
                INFO_BAD_INTERLEAVING = 800,
                // The media is not seekable (e.g  the live stream).
                INFO_NOT_SEEKABLE = 801,
                // The new media metadata is available.
                INFO_METADATA_UPDATE = 802,
                // Error occured when seeking
                INFO_SEEK_ERROR = 803,

                //9xx
                INFO_TIMED_TEXT_ERROR = 900,
        };

    };

    enum parameter_keys {
        // Parameters for Streaming or buffering
        KEY_PARAM_CACHE_STAT_COLLECT_FREQ_MS = 1000,

        KEY_PARAM_AUDIO_CHANNEL_COUNT = 1100,
        KEY_PARAM_PLAYBACK_RATE_PERMILLE = 1200,
        //bitrate for call active
        KEY_PARAM_SOURCE_BITRATE = 1500,
        //the network url
        KEY_PARAM_NETSOURCE_URL = 1501,

        KEY_PARAM_NETWORK_TIMEOUT = 1505,

        KEY_PARAM_DISPLAY_ASPECT_RATIO = 1506,

        KEY_PARAM_NETSOURCE_URL_RESPHEADER = 1507,   //HTTP response header

        KEY_PARAMETER_HTTPDNS = 1508,   // http dns request url & method
        KEY_PARAMETER_NETADAPTION = 1509, //net adaption policy

        //Ado CCADD
        KEY_PARAM_AUDIO_HDMI_TRANSPARENT = 1888,
        KEY_PARAM_AUDIO_DOBLY_NEIGHTMODE = 1889,
        KEY_PARAM_LOADSUBTITLE_AUTO = 1990,

        //autumn add code
        KEY_PARAM_SUBTITLE_OFFSET_TIME = 1991,
        //for smb net source
        KEY_PARAM_SET_APP_PACKAGENAME = 1992,


        // get only
        // the name of audio codec
        // value type: std::string *, caller must call "delete" for delete the param
        KEY_PARAM_AUDIO_CODEC_NAME = 257,
        // get only
        // the name of video codec
        // value type: std::string *, caller must call "delete" for delete the param
        KEY_PARAM_VIDEO_CODEC_NAME,
        // get only
        // audio stream information
        // value type: AudioStreamInfo*, caller must call "delete" for delete the param
        KEY_PARAM_AUDIO_STREAM_INFO,
        // get only
        // video stream information
        // value type: AudioStreamInfo*, caller must call "delete" for delete the param
        KEY_PARAM_VIDEO_STREAM_INFO,
        // set only
        // Sets the playback rate.
        // MM_ERROR_IVALIDOPERATION occurs when streaming playback.
        // No operation is performed, if rate is 0.
        // The sound is muted, when playback rate is under 0.0 and over 2.0.
        // value type: float &, the playback rate (-5.0x ~ 5.0x)
        KEY_PARAM_PLAYBACK_RATE,
    };

    enum EInvokeKey {
        INVOKE_ID_GET_TRACK_INFO = 1,
        INVOKE_ID_ADD_EXTERNAL_SOURCE = 2,
        INVOKE_ID_ADD_EXTERNAL_SOURCE_FD = 3,
        INVOKE_ID_SELECT_TRACK = 4,
        INVOKE_ID_UNSELECT_TRACK = 5,
        INVOKE_ID_SET_VIDEO_SCALING_MODE = 6,
        INVOKE_ID_PRINT_DUMP_INFO = 7,
        INVOKE_ID_RESOURCE_PRIORITY = 8,
    };

    enum ETrackType {
        TRACK_TYPE_UNKNOWN = 0,
        TRACK_TYPE_VIDEO = 1,
        TRACK_TYPE_AUDIO = 2,
        TRACK_TYPE_TIMEDTEXT = 3,
        TRACK_TYPE_SUBTITLE = 4,
    };

    /* Audio stream types */
    typedef enum {
        AS_TYPE_DEFAULT             = -1,       /**< default audio stream type */
        AS_TYPE_MIN                 = 0,        /**< start index of valid stream index */
        AS_TYPE_VOICE_CALL          = 0,        /**< voice call */
        AS_TYPE_SYSTEM,                         /**< system sound*/
        AS_TYPE_RING,                           /**< Ring*/
        AS_TYPE_MUSIC,                          /**< Music*/
        AS_TYPE_ALARM,                          /**< Alarm*/
        AS_TYPE_NOTIFICATION,                   /**< Notification*/
        AS_TYPE_BT_SCO,                         /**< BT SCO*/
        AS_TYPE_ENFORCED_AUDIBLE,               /**< Sounds that cannot be muted by user and must be routed to speaker */
        AS_TYPE_DTMF,                           /**< DTMF*/
        AS_TYPE_TTS,                            /**< Transmitted Through Speaker.Plays over speaker only, silent on other devices.*/
        AS_TYPE_FM,                             /**< FM */
        AS_TYPE_ACCESSIBILITY,                  /**< accessibility talk back prompts */
        AS_TYPE_REROUTING,                      /**< For dynamic policy output mixes */
        AS_TYPE_PATCH,                          /**< For internal audio flinger tracks. Fixed volume */
        AS_TYPE_PUBLIC_CNT = AS_TYPE_FM + 1,    /**< end index for the stream type visible to app developer*/
        AS_TYPE_CNT = AS_TYPE_PATCH + 1,        /**< Total count of audio stream type*/
    } as_type_t;

    /* Play rate */
    enum  play_rate {
        /** slow rate 1/32 */
        PLAYRATE_1_32 = 1,
        /** slow rate 1/16 */
        PLAYRATE_1_16 = 2,
        /** slow rate 1/8 */
        PLAYRATE_1_8 = 4,
        /** slow rate 1/4 */
        PLAYRATE_1_4 = 8,
        /** slow rate 1/2 */
        PLAYRATE_1_2 = 16,
        /** normal rate 1  */
        PLAYRATE_NORMAL = 32,
        /** fast rate 2*/
        PLAYRATE_2 = 64,
        /** fast rate 4 */
        PLAYRATE_4 = 128,
        /** fast rate 8 */
        PLAYRATE_8 = 256,
        /** fast rate 16 */
        PLAYRATE_16 = 512,
        /** fast rate 32 */
        PLAYRATE_32 = 1024
    };

public:
    struct VolumeInfo {
        float left;
        float right;
    };
    virtual mm_status_t setPipeline(PipelineSP pipeline) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setListener(Listener * listener);
    // headers: the headers to be sent together with the request for the data.
    virtual mm_status_t setDataSource(const char * uri,
                            const std::map<std::string, std::string> * headers = NULL) = 0;
    virtual mm_status_t setDataSource(const unsigned char * mem, size_t size) = 0;
    virtual mm_status_t setDataSource(int fd, int64_t offset, int64_t length) = 0;
    virtual mm_status_t setSubtitleSource(const char* uri) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setDisplayName(const char* name) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setNativeDisplay(void * display) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setVideoDisplay(void * handle) = 0;
    virtual mm_status_t setVideoSurfaceTexture(void * handle) = 0;
    virtual mm_status_t prepare() = 0;
    virtual mm_status_t prepareAsync() = 0;
    virtual mm_status_t reset() = 0;
    virtual mm_status_t setVolume(const VolumeInfo & volume) = 0;
    virtual mm_status_t getVolume(VolumeInfo * volume) const = 0;
    virtual mm_status_t setMute(bool mute) = 0;
    virtual mm_status_t getMute(bool * mute) const = 0;
    virtual mm_status_t start() = 0;
    virtual mm_status_t stop() = 0;
    virtual mm_status_t pause() = 0;
    virtual mm_status_t seek(int msec) = 0;
    virtual bool isPlaying() const = 0;
    virtual mm_status_t getVideoSize(int *width, int * height) const = 0;
    virtual mm_status_t getCurrentPosition(int * msec) const = 0;
    virtual mm_status_t getDuration(int * msec) const = 0;
    virtual mm_status_t setAudioStreamType(as_type_t type) = 0;
    virtual mm_status_t getAudioStreamType(as_type_t *type) = 0;
    virtual mm_status_t setAudioConnectionId(const char * connectionId);
    virtual const char * getAudioConnectionId() const;
    virtual mm_status_t setLoop(bool loop) = 0;
    virtual bool isLooping() const = 0;
    // A language code in either way of ISO-639-1 or ISO-639-2. When the language is unknown or could not be determined, MM_ERROR_UNSUPPORTED will returned
    virtual mm_status_t setParameter(const MediaMetaSP & meta) = 0;
    virtual mm_status_t getParameter(MediaMetaSP & meta) = 0;
    virtual mm_status_t invoke(const MMParam * request, MMParam * reply) = 0;

    virtual mm_status_t captureVideo() = 0;

    virtual mm_status_t pushData(MediaBufferSP & buffer) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t enableExternalSubtitleSupport(bool enable) { return MM_ERROR_UNSUPPORTED; }

protected:
    MediaPlayer();
    virtual ~MediaPlayer();

protected:
    Listener * mListener;

private:
    MM_DISALLOW_COPY(MediaPlayer)
    DECLARE_LOGTAG()
};

}

#endif /* __mediaplayer_H */

