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

#include <map>
#include <multimedia/mm_debug.h>
#include <multimedia/media_attr_str.h>
#include <string>

MM_LOG_DEFINE_MODULE_NAME("media-attr");

namespace YUNOS_MM {
    #define MEDIA_MIMETYPE(TYPE, str) const char* MEDIA_MIMETYPE_##TYPE = str;
    #define MEDIA_ATTR(TYPE, str)  const char* MEDIA_ATTR_##TYPE = str;
    #define MEDIA_META(TYPE, str)  const char* MEDIA_META_##TYPE = str;
    // mimetype
    MEDIA_MIMETYPE(IMAGE_JPEG, "image/jpeg")
    MEDIA_MIMETYPE(IMAGE_PNG, "image/png")
    MEDIA_MIMETYPE(IMAGE_BMP, "image/bmp")
    MEDIA_MIMETYPE(IMAGE_GIF, "image/gif")
    MEDIA_MIMETYPE(IMAGE_WEBP, "image/webp")
    MEDIA_MIMETYPE(IMAGE_WBMP, "image/wbmp")

    MEDIA_MIMETYPE(VIDEO_VP3, "video/x-vnd.on2.vp3")
    MEDIA_MIMETYPE(VIDEO_VP5, "video/x-vnd.on2.vp5")
    MEDIA_MIMETYPE(VIDEO_VP6,  "video/x-vnd.on2.vp6")
    MEDIA_MIMETYPE(VIDEO_VP6A,  "video/x-vnd.on2.vp6-a")
    MEDIA_MIMETYPE(VIDEO_VP6F,  "video/x-vnd.on2.vp6-f")
    MEDIA_MIMETYPE(VIDEO_VP7, "video/x-vnd.on2.vp7")
    MEDIA_MIMETYPE(VIDEO_VP8, "video/x-vnd.on2.vp8")
    MEDIA_MIMETYPE(VIDEO_VP9, "video/x-vnd.on2.vp9")
    MEDIA_MIMETYPE(VIDEO_AVC, "video/avc")
    MEDIA_MIMETYPE(VIDEO_HEVC, "video/hevc")
    MEDIA_MIMETYPE(VIDEO_MPEG4,"video/mp4v-es")
    MEDIA_MIMETYPE(VIDEO_MPEG4V1,"video/mp4v-es-v1")
    MEDIA_MIMETYPE(VIDEO_MPEG4V2,"video/mp4v-es-v2")
    MEDIA_MIMETYPE(VIDEO_MPEG4V3,"video/mp4v-es-v3")
    MEDIA_MIMETYPE(VIDEO_H263, "video/3gpp")
    MEDIA_MIMETYPE(VIDEO_H263_P, "video/3gpp-p")
    MEDIA_MIMETYPE(VIDEO_H263_I, "video/3gpp-i")
    MEDIA_MIMETYPE(VIDEO_MPEG2,"video/mpeg2")
    MEDIA_MIMETYPE(VIDEO_MPEG2_XVMC,"video/mpeg2-xvmc")
    MEDIA_MIMETYPE(VIDEO_MPEG1, "video/mpeg1")
    MEDIA_MIMETYPE(VIDEO_H261, "video/H261")
    MEDIA_MIMETYPE(VIDEO_RV10, "video/rv10")
    MEDIA_MIMETYPE(VIDEO_RV20, "video/rv20")
    MEDIA_MIMETYPE(VIDEO_RV30,"video/rv30")
    MEDIA_MIMETYPE(VIDEO_RV40, "video/rv40")
    MEDIA_MIMETYPE(VIDEO_MJPEG,"video/mjpeg")
    MEDIA_MIMETYPE(VIDEO_MJPEGB,"video/mjpeg-b")
    MEDIA_MIMETYPE(VIDEO_LJPEG , "video/ljpeg")
    MEDIA_MIMETYPE(VIDEO_JPEGGLS, "video/jpeggls")
    MEDIA_MIMETYPE(VIDEO_SP5X, "video/sp5x")
    MEDIA_MIMETYPE(VIDEO_WMV, "video/wmv")
    MEDIA_MIMETYPE(VIDEO_WMV1, "video/wmv1")
    MEDIA_MIMETYPE(VIDEO_WMV2, "video/wmv2")
    MEDIA_MIMETYPE(VIDEO_WMV3,"video/wmv3")
    MEDIA_MIMETYPE(VIDEO_WMV3IMAGE,"video/wmv3image")
    MEDIA_MIMETYPE(VIDEO_VC1, "video/vc1")
    MEDIA_MIMETYPE(VIDEO_FLV,  "video/flv")
    MEDIA_MIMETYPE(VIDEO_DV,  "video/dv")
    MEDIA_MIMETYPE(VIDEO_HUFFYUV, "video/huffyuv")
    MEDIA_MIMETYPE(VIDEO_CYUV, "video/cyuv")
    MEDIA_MIMETYPE(VIDEO_INDEOX, "video/indeox")
    MEDIA_MIMETYPE(VIDEO_THEORA, "video/theora")
    MEDIA_MIMETYPE(VIDEO_TIFF, "video/tiff")
    MEDIA_MIMETYPE(VIDEO_GIF, "video/gif")
    MEDIA_MIMETYPE(VIDEO_DVB,  "video/dvb")
    MEDIA_MIMETYPE(VIDEO_FLASH,  "video/flash")
    MEDIA_MIMETYPE(VIDEO_RAW, "video/raw")

    MEDIA_MIMETYPE(AUDIO_AMR_NB,  "audio/amr")
    MEDIA_MIMETYPE(AUDIO_3GPP,  "audio/3gpp")
    MEDIA_MIMETYPE(AUDIO_AMR_WB, "audio/amr-wb")
    MEDIA_MIMETYPE(AUDIO_MPEG,  "audio/mpeg")
    MEDIA_MIMETYPE(AUDIO_MPEG_ADU,  "audio/mpeg-adu")
    MEDIA_MIMETYPE(AUDIO_MPEG_ON4,  "audio/mpeg-on4")
    MEDIA_MIMETYPE(AUDIO_MPEG_LAYER_I,  "audio/mpeg-L1")
    MEDIA_MIMETYPE(AUDIO_MPEG_LAYER_II, "audio/mpeg-L2")
    MEDIA_MIMETYPE(AUDIO_AAC,  "audio/mp4a-latm")
    MEDIA_MIMETYPE(AUDIO_ADTS_PROFILE,  "audio/adts")
    MEDIA_MIMETYPE(AUDIO_QCELP,  "audio/qcelp")
    MEDIA_MIMETYPE(AUDIO_VORBIS,  "audio/vorbis")
    MEDIA_MIMETYPE(AUDIO_OPUS, "audio/opus")
    MEDIA_MIMETYPE(AUDIO_G711_ALAW,  "audio/g711-alaw")
    MEDIA_MIMETYPE(AUDIO_G711_MLAW,  "audio/g711-mlaw")
    MEDIA_MIMETYPE(AUDIO_RAW,  "audio/raw")
    MEDIA_MIMETYPE(AUDIO_FLAC,  "audio/flac")
    MEDIA_MIMETYPE(AUDIO_AAC_ADTS,  "audio/aac-adts")
    MEDIA_MIMETYPE(AUDIO_AAC_LATM,  "audio/aac-latm")
    MEDIA_MIMETYPE(AUDIO_MSGSM,  "audio/gsm")
    MEDIA_MIMETYPE(AUDIO_AC3, "audio/ac3")
    MEDIA_MIMETYPE(AUDIO_EC3,  "audio/eac3")
    MEDIA_MIMETYPE(AUDIO_DTSHD, "audio/dtshd")
    MEDIA_MIMETYPE(AUDIO_DRA144,  "audio/dra144")
    MEDIA_MIMETYPE(AUDIO_DRA288,  "audio/dra288")
    MEDIA_MIMETYPE(AUDIO_MP2,  "audio/mp2")
    MEDIA_MIMETYPE(AUDIO_MP3, "audio/mp3")
    MEDIA_MIMETYPE(AUDIO_WMV, "audio/wmv")
    MEDIA_MIMETYPE(AUDIO_WAV, "audio/wav")
    MEDIA_MIMETYPE(AUDIO_WMA, "audio/wma")
    MEDIA_MIMETYPE(AUDIO_WMAPRO, "audio/wmapro")
    MEDIA_MIMETYPE(AUDIO_WMALOSSLESS,  "audio/wmalossless")
    MEDIA_MIMETYPE(AUDIO_DTS,  "audio/vnd.dts")

    MEDIA_MIMETYPE(CONTAINER_MPEG4, "video/mp4")
    MEDIA_MIMETYPE(CONTAINER_WAV, "audio/x-wav")
    MEDIA_MIMETYPE(CONTAINER_OGG,  "application/ogg")
    MEDIA_MIMETYPE(CONTAINER_MATROSKA, "video/x-matroska")
    MEDIA_MIMETYPE(CONTAINER_MPEG2TS, "video/mp2ts")
    MEDIA_MIMETYPE(CONTAINER_AVI, "video/avi")
    MEDIA_MIMETYPE(CONTAINER_MPEG2PS ,"video/mp2p")

    MEDIA_MIMETYPE(CONTAINER_WVM,  "video/wvm")

    MEDIA_MIMETYPE(TEXT_3GPP, "text/3gpp-tt")
    MEDIA_MIMETYPE(TEXT_SUBRIP, "application/x-subrip")
    MEDIA_MIMETYPE(TEXT_VTT, "text/vtt")
    MEDIA_MIMETYPE(TEXT_CEA_608,"text/cea-608")

    MEDIA_ATTR(FILE_MAJOR_BAND, "major_brand")
    MEDIA_ATTR(FILE_MINOR_VERSION, "minor_version")
    MEDIA_ATTR(FILE_COMPATIBLE_BRANDS,  "compatible_brands")
    MEDIA_ATTR(FILE_CREATION_TIME,"creation_time")

    MEDIA_ATTR(CODECID, "codec-id")
    MEDIA_ATTR(CODECTAG, "codec-tag")
    MEDIA_ATTR(STREAMCODECTAG,  "stream-codec-tag")
    MEDIA_ATTR(CODECPROFILE, "codec-profile")
    MEDIA_ATTR(CODEC_CONTEXT, "codec-context")
    MEDIA_ATTR(CODEC_CONTEXT_MUTEX, "codec-context-mutex")
    MEDIA_ATTR(TARGET_TIME, "target-time")

    //component mimeType
    MEDIA_MIMETYPE(AUDIO_RENDER, "audio/render")
    MEDIA_MIMETYPE(VIDEO_RENDER, "video/render")
    MEDIA_MIMETYPE(MEDIA_DEMUXER, "media/demuxer")
    MEDIA_MIMETYPE(MEDIA_APP_SOURCE, "media/app-source")
    MEDIA_MIMETYPE(MEDIA_RTP_DEMUXER,"media/rtp-demuxer")
    MEDIA_MIMETYPE(MEDIA_DASH_DEMUXER,"application/dash+xml")
    MEDIA_MIMETYPE(MEDIA_MUXER, "media/muxer")
    MEDIA_MIMETYPE(MEDIA_RTP_MUXER, "media/rtp-muxer")
    MEDIA_MIMETYPE(VIDEO_SOURCE, "video/source")
    MEDIA_MIMETYPE(AUDIO_SOURCE ,"audio/source")
    MEDIA_MIMETYPE(AUDIO_SOURCE_FILE, "audio/file-source")
    MEDIA_MIMETYPE(VIDEO_TEST_SOURCE, "video/test-source")
    MEDIA_MIMETYPE(VIDEO_FILE_SOURCE,"video/file-source")
    MEDIA_MIMETYPE(VIDEO_WFD_SOURCE, "video/wfd-source")
    MEDIA_MIMETYPE(VIDEO_SCREEN_SOURCE, "video/screen-source")
    MEDIA_MIMETYPE(VIDEO_SURFACE_SOURCE, "video/surface-source")
    MEDIA_MIMETYPE(VIDEO_UVC_SOURCE, "video/UVC-source")
    MEDIA_MIMETYPE(MEDIA_FILE_SINK, "media/file-sink")
    MEDIA_MIMETYPE(IMAGE_CAMERA_SOURCE, "image/camera-source")
    MEDIA_MIMETYPE(MEDIA_CAMERA_SOURCE, "video/camera-source")
    MEDIA_MIMETYPE(SUBTITLE_SOURCE, "subtitle/source")
    MEDIA_MIMETYPE(SUBTITLE_SINK, "subtitle/sink")
    MEDIA_MIMETYPE(MEDIA_EXTERNAL_SOURCE, "source/external")


    //  media attributes
    MEDIA_ATTR(AAC_PROFILE, "aac-profile")
    MEDIA_ATTR(BIT_RATE, "bitrate")
    MEDIA_ATTR(BIT_RATE_AUDIO, "bitrate-audio")
    MEDIA_ATTR(BIT_RATE_VIDEO,"bitrate-video")
    MEDIA_ATTR(CHANNEL_COUNT,"channel-count")
    MEDIA_ATTR(CHANNEL_MASK, "channel-mask")
    MEDIA_ATTR(SAMPLE_FORMAT, "sample-format")
    MEDIA_ATTR(COLOR_FORMAT,"color-format")
    MEDIA_ATTR(COLOR_FOURCC, "color-fourcc")
    MEDIA_ATTR(DURATION, "durationUs")
    MEDIA_ATTR(FLAC_COMPRESSION_LEVEL,"flac-compression-level")
    MEDIA_ATTR(FRAME_RATE, "frame-rate")
    MEDIA_ATTR(HEIGHT,"height")
    MEDIA_ATTR(IS_ADTS, "is-adts")
    MEDIA_ATTR(IS_AUTOSELECT,"is-autoselect")
    MEDIA_ATTR(IS_DEFAULT, "is-default")
    MEDIA_ATTR(IS_FORCED_SUBTITLE, "is-forced-subtitle")
    MEDIA_ATTR(I_FRAME_INTERVAL, "i-frame-interval")
    MEDIA_ATTR(LANGUAGE, "language")
    MEDIA_ATTR(MAX_HEIGHT,"max-height")
    MEDIA_ATTR(SLICE_HEIGHT, "slice-height")
    MEDIA_ATTR(MAX_INPUT_SIZE, "max-input-size")
    MEDIA_ATTR(MAX_WIDTH,"max-width")
    MEDIA_ATTR(MAX_FILE_SIZE,"max-filesize")
    MEDIA_ATTR(MAX_DURATION,"max-duration")
    MEDIA_ATTR(MIME, "mime")
    MEDIA_ATTR(CONTAINER, "container")
    MEDIA_ATTR(PUSH_BLANK_BUFFERS_ON_STOP,"push-blank-buffers-on-shutdown")
    MEDIA_ATTR(REPEAT_PREVIOUS_FRAME_AFTER,"repeat-previous-frame-after")
    MEDIA_ATTR(SAMPLE_RATE, "sample-rate")
    MEDIA_ATTR(WIDTH,"width")
    MEDIA_ATTR(PREVIEW_WIDTH,"preview-width")
    MEDIA_ATTR(PREVIEW_HEIGHT,"preview-height")
    MEDIA_ATTR(IMAGE_WIDTH, "image-width")
    MEDIA_ATTR(IMAGE_HEIGHT, "image-height")
    MEDIA_ATTR(STRIDE,"stride")
    MEDIA_ATTR(STRIDE_X,"stride-x")
    MEDIA_ATTR(STRIDE_Y,"stride-y")
    MEDIA_ATTR(CROP_RECT,"crop-rect")
    MEDIA_ATTR(VOLUME,"volume")
    MEDIA_ATTR(MUTE,"mute")
    MEDIA_ATTR(AVG_FRAMERATE,"avg-framerate")
    MEDIA_ATTR(AVC_PROFILE,"avc-profile")
    MEDIA_ATTR(AVC_LEVEL,"avc-level")
    MEDIA_ATTR(HEVC_PROFILE,"hevc-profile")
    MEDIA_ATTR(HEVC_LEVEL,"hevc-level")
    MEDIA_ATTR(IDR_FRAME, "request-idr")
    MEDIA_ATTR(AUDIO_SOURCE,"audio-source")

    // additional
    MEDIA_ATTR(STREAM_INDEX, "stream-index")
    MEDIA_ATTR(AUDIO_SAMPLES, "audio_samples")
    MEDIA_ATTR(SAMPLE_ASPECT_RATION,"sample-aspect-ratio")
    MEDIA_ATTR(CHANNEL_LAYOUT, "channel_layout")

    MEDIA_ATTR(DECODE_MODE,"decode-mode")
    MEDIA_ATTR(DECODE_THUMBNAIL,"thumbnail")
    MEDIA_ATTR(THUMB_PATH,"thumb-path")
    MEDIA_ATTR(THUMB_WIDTH, "thumb-width")
    MEDIA_ATTR(THUMB_HEIGHT, "thumb-height")
    MEDIA_ATTR(MICROTHUMB_PATH, "micro-thumb-path")
    MEDIA_ATTR(MICROTHUMB_WIDTH, "micro-thumb-width")
    MEDIA_ATTR(MICROTHUMB_HEIGHT,"micro-thumb-height")
    MEDIA_ATTR(COVER_PATH, "cover-path")
    MEDIA_ATTR(MIME_AUDIO_3GPP,"audio/3gpp")
    MEDIA_ATTR(MIME_AUDIO_QUICKTIME,"audio/quicktime")
    MEDIA_ATTR(MIME_AUDIO_MP4, "audio/mp4")
    MEDIA_ATTR(MIME_VIDEO_3GPP,"video/3gpp")
    MEDIA_ATTR(MIME_VIDEO_QUICKTIME, "video/quicktime")
    MEDIA_ATTR(MIME_VIDEO_MP4,"video/mp4")
    MEDIA_ATTR(ALBUM, "album")
    MEDIA_ATTR(HAS_AUDIO, "has-audio")
    MEDIA_ATTR(HAS_VIDEO, "has-video")
    MEDIA_ATTR(HAS_COVER, "has-cover")
    MEDIA_ATTR(ATTACHEDPIC, "attached-pic")
    MEDIA_ATTR(ATTACHEDPIC_SIZE, "attached-pic-size")
    MEDIA_ATTR(ATTACHEDPIC_CODECID, "attached-pic-codecid")
    MEDIA_ATTR(ATTACHEDPIC_MIME, "attached-pic-mime")
    MEDIA_ATTR(YES, "y")
    MEDIA_ATTR(NO, "n")
    MEDIA_ATTR(TITLE, "title")
    MEDIA_ATTR(SUBTITLE, "subtitle")
    MEDIA_ATTR(AUTHOR, "author")
    MEDIA_ATTR(COMMENT, "comment")
    MEDIA_ATTR(DESCRIPTION, "description")
    MEDIA_ATTR(CATEGORY, "category")
    MEDIA_ATTR(GENRE, "genre")
    MEDIA_ATTR(YEAR, "year")
    MEDIA_ATTR(DATE, "date")
    MEDIA_ATTR(USER_RATING, "user-rating")
    MEDIA_ATTR(KEYWORDS, "keywords")
    MEDIA_ATTR(PUBLISHER,"publisher")
    MEDIA_ATTR(COPYRIGHT, "copyright")
    MEDIA_ATTR(PARENTAL_RATING, "parental-rating")
    MEDIA_ATTR(RATING_ORGANIZATION, "rating-organization")
    MEDIA_ATTR(SIZE, "size")
    MEDIA_ATTR(BUFFER_INDEX, "buffer-index")
    MEDIA_ATTR(IS_VIDEO_RENDER, "is-video-render")
    MEDIA_ATTR(VIDEO_FORCE_RENDER, "video-force-render")
    MEDIA_ATTR(MEDIA_TYPE,"media-type")
    MEDIA_ATTR(AUDIO_CODEC, "audio-codec")
    MEDIA_ATTR(AVERAGE_LEVEL, "average-level")
    MEDIA_ATTR(BLOCK_ALIGN, "block-align")
    MEDIA_ATTR(PEAK_VALUE,"peak-value")
    MEDIA_ATTR(ALBUM_TITLE, "album-title")
    MEDIA_ATTR(ALBUM_ARTIST, "album-artist")
    MEDIA_ATTR(CONTRIBUTING_ARTIST, "contributing-artist")
    MEDIA_ATTR(COMPOSER, "composer")
    MEDIA_ATTR(CONDUCTOR, "conductor")
    MEDIA_ATTR(LYRICS, "lyrics")
    MEDIA_ATTR(MOOD, "mood")
    MEDIA_ATTR(TRACK_NUMBER,"track-number")
    MEDIA_ATTR(TRACK_COUNT, "track-count")
    MEDIA_ATTR(COVER_ART_URL_SMALL, "cover-art-url-small")
    MEDIA_ATTR(COVER_ART_URL_LARGE,"cover-art-url-large")
    MEDIA_ATTR(RESOLUTION, "resolution")
    MEDIA_ATTR(VIDEO_CODEC, "video-codec")
    MEDIA_ATTR(POSTER_URL, "poster-url")
    MEDIA_ATTR(CHAPTER_NUMBER, "chapter-number")
    MEDIA_ATTR(DIRECTOR, "director")
    MEDIA_ATTR(LEAD_PERFORMER,"lead-performer")
    MEDIA_ATTR(WRITER, "writer")
    MEDIA_ATTR(FILE_PATH, "file-path")
    MEDIA_ATTR(FILE_HANDLE, "file-handle")
    MEDIA_ATTR(OUTPUT_FORMAT, "output-format")
    MEDIA_ATTR(JPEG_QUALITY, "jpeg-quality")

    MEDIA_ATTR(EXTRADATA0, "csd0")
    MEDIA_ATTR(EXTRADATA1, "csd1")
    MEDIA_ATTR(ROTATION, "rotation")   // in degree

    MEDIA_ATTR(CODEC_DATA, "codec-data")
    MEDIA_ATTR(LOCATION, "location")   // latitude and longitude
    MEDIA_ATTR(LOCATION_LATITUDE, "latitude")
    MEDIA_ATTR(LOCATION_LONGITUDE, "longitude")
    MEDIA_ATTR(DATETAKEN,"date-taken")
    MEDIA_ATTR(ORIENTATION, "orientation")

    MEDIA_ATTR(TIMEBASE, "timebase")
    MEDIA_ATTR(TIMEBASE2,"timebase2")

    MEDIA_ATTR(SEEK_OFFSET, "seek-offset")
    MEDIA_ATTR(SEEK_WHENCE, "seek-whence")


    MEDIA_ATTR(PALY_RATE,"play-rate")
    MEDIA_ATTR(VARIABLE_RATE_SUPPORT, "is-variable-rate-support")
    MEDIA_ATTR(STREAM_IS_SEEKABLE, "is-seekable")

    MEDIA_ATTR(START_TIME, "start-time-us")
    MEDIA_ATTR(START_DELAY_TIME, "start-delay-time-us")

    // avc profile and level
    MEDIA_ATTR(AVC_PROFILE_BASELINE, "avc-baseline")
    MEDIA_ATTR(AVC_PROFILE_MAIN, "avc-main")
    MEDIA_ATTR(AVC_PROFILE_HIGH, "avc-high")
    MEDIA_ATTR(AVC_PROFILE_CONSTRAINEDBASELINE, "avc-cbp")
    MEDIA_ATTR(AVC_LEVEL1, "avc-level1")
    MEDIA_ATTR(AVC_LEVEL1b, "avc-level1b")
    MEDIA_ATTR(AVC_LEVEL11, "avc-level11")
    MEDIA_ATTR(AVC_LEVEL12, "avc-level12")
    MEDIA_ATTR(AVC_LEVEL13, "avc-level13")
    MEDIA_ATTR(AVC_LEVEL2, "avc-level2")
    MEDIA_ATTR(AVC_LEVEL21, "avc-level21")
    MEDIA_ATTR(AVC_LEVEL22, "avc-level22")
    MEDIA_ATTR(AVC_LEVEL3, "avc-level3")
    MEDIA_ATTR(AVC_LEVEL31, "avc-level31")
    MEDIA_ATTR(AVC_LEVEL32, "avc-level32")
    MEDIA_ATTR(AVC_LEVEL4, "avc-level4")
    MEDIA_ATTR(AVC_LEVEL41, "avc-level41")
    MEDIA_ATTR(AVC_LEVEL42, "avc-level42")
    MEDIA_ATTR(AVC_LEVEL5, "avc-level5")
    MEDIA_ATTR(GOP_SIZE, "gop-size")
    MEDIA_META(BUFFER_TYPE,"media-buffer-type")   // specify MediaBuffer::MediaBufferType in MediaMeta

    //for camera
    MEDIA_ATTR(PHOTO_COUNT, "photo-count")
    MEDIA_ATTR(CAMERA_OBJECT, "camera")
    MEDIA_ATTR(RECORDING_PROXY_OBJECT,  "recording-proxy")
    MEDIA_ATTR(TIME_LAPSE_ENABLE,  "time-lapse-enable")
    MEDIA_ATTR(TIME_LAPSE_FPS,  "time-lapse-fps")

     //audio device
    MEDIA_ATTR(AUDIO_SOURCE_MIC, "mic")
    MEDIA_ATTR(AUDIO_SOURCE_VOICE_UPLINK, "voice-uplink")
    MEDIA_ATTR(AUDIO_SOURCE_VOICE_DOWNLINK, "voice-downlink")
    MEDIA_ATTR(AUDIO_SOURCE_VOICE_CALL, "voice-call")
    MEDIA_ATTR(AUDIO_SOURCE_CAMCORDER, "camcorder")
    MEDIA_ATTR(AUDIO_SOURCE_VOICE_RECOGNITION, "voice-recognition")
    MEDIA_ATTR(AUDIO_SOURCE_VOICE_COMMUNICATION,"voice-communication")
    MEDIA_ATTR(AUDIO_SOURCE_REMOTE_SUBMIX, "remote_submix")
    MEDIA_ATTR(AUDIO_SOURCE_FM_TUNER, "fm-tuner")
    MEDIA_ATTR(AUDIO_SOURCE_CNT, "cnt")

    MEDIA_ATTR(VIDEO_RAW_DATA,"video-raw-data")
    MEDIA_ATTR(VIDEO_SURFACE, "surface")
    MEDIA_ATTR(VIDEO_SURFACE_TEXTURE, "surfacetexture")
    MEDIA_ATTR(VIDEO_BQ_PRODUCER, "bufferqueue-producer")
    MEDIA_ATTR(NATIVE_WINDOW_BUFFER, "native-window-buffer")
    MEDIA_ATTR(NATIVE_DISPLAY, "native-display")
    MEDIA_ATTR(INPUT_BUFFER_NUM,"input-buffer-num")
    MEDIA_ATTR(INPUT_BUFFER_SIZE, "input-buffer-size")
    MEDIA_ATTR(STORE_META_INPUT, "store-meta-input")
    MEDIA_ATTR(PREPEND_SPS_PPS, "prepend-sps-pps")
    MEDIA_ATTR(STORE_META_OUTPUT, "store-meta-output")
    MEDIA_ATTR(REPEAT_FRAME_DELAY_US, "repeat-frame-delay-us")
    MEDIA_ATTR(BITRATE_MODE, "bitrate-mode")
    MEDIA_ATTR(MUSIC_SPECTRUM, "music-spectrum")
    MEDIA_ATTR(MUXER_STREAM_DRIFT_MAX, "muxer-stream-drift-max")
    MEDIA_ATTR(BUFFER_LIST, "buffer-list")

    MEDIA_ATTR(CODEC_MEDIA_DECRYPT, "codec-media-decrypt")
    MEDIA_ATTR(CODEC_RESOURCE_ID, "codec-resource-id")
    MEDIA_ATTR(CODEC_LOW_DELAY, "codec-low-delay")
    MEDIA_ATTR(CODEC_DISABLE_HW_RENDER, "codec-disable-hw-render")
    MEDIA_ATTR(CODEC_DROP_ERROR_FRAME, "codec-drop-error-frame")

    MEDIA_ATTR(FILE_DOWNLOAD_PATH, "file-download-path")

/////////////////////////////////////////////////////////
// codeId and mimetype mapping
typedef struct {
    int id;
    const char * mime;
} CodecIdMimeType ;

static CodecIdMimeType s_codecId2Mime [] = {
    { kCodecIDMPEG1VIDEO, MEDIA_MIMETYPE_VIDEO_MPEG1 },
    { kCodecIDMPEG2VIDEO, MEDIA_MIMETYPE_VIDEO_MPEG2 },
    { kCodecIDMPEG2VIDEO_XVMC, MEDIA_MIMETYPE_VIDEO_MPEG2_XVMC },
    { kCodecIDH261, MEDIA_MIMETYPE_VIDEO_H261 },
    { kCodecIDH263, MEDIA_MIMETYPE_VIDEO_H263 },
    { kCodecIDRV10, MEDIA_MIMETYPE_VIDEO_RV10 },
    { kCodecIDRV20, MEDIA_MIMETYPE_VIDEO_RV20 },
    { kCodecIDMJPEG, MEDIA_MIMETYPE_VIDEO_MJPEG },
    { kCodecIDMJPEGB, MEDIA_MIMETYPE_VIDEO_MJPEGB },
    { kCodecIDLJPEG, MEDIA_MIMETYPE_VIDEO_LJPEG },
    { kCodecIDSP5X, MEDIA_MIMETYPE_VIDEO_SP5X },
    { kCodecIDJPEGLS, MEDIA_MIMETYPE_VIDEO_JPEGGLS },
    { kCodecIDMPEG4, MEDIA_MIMETYPE_VIDEO_MPEG4 },
    { kCodecIDRAWVIDEO, MEDIA_MIMETYPE_VIDEO_RAW },
    { kCodecIDMSMPEG4V1, MEDIA_MIMETYPE_VIDEO_MPEG4V1 },
    { kCodecIDMSMPEG4V2, MEDIA_MIMETYPE_VIDEO_MPEG4V2 },
    { kCodecIDMSMPEG4V3, MEDIA_MIMETYPE_VIDEO_MPEG4V3 },
    { kCodecIDWMV1, MEDIA_MIMETYPE_VIDEO_WMV1 },
    { kCodecIDWMV2, MEDIA_MIMETYPE_VIDEO_WMV2 },
    { kCodecIDH263P, MEDIA_MIMETYPE_VIDEO_H263_P },
    { kCodecIDH263I, MEDIA_MIMETYPE_VIDEO_H263_I },
    { kCodecIDFLV1, MEDIA_MIMETYPE_VIDEO_FLV },
    { kCodecIDDVVIDEO, MEDIA_MIMETYPE_VIDEO_DV },
    { kCodecIDHUFFYUV, MEDIA_MIMETYPE_VIDEO_HUFFYUV },
    { kCodecIDCYUV, MEDIA_MIMETYPE_VIDEO_CYUV },
    { kCodecIDH264, MEDIA_MIMETYPE_VIDEO_AVC },
    { kCodecIDHEVC, MEDIA_MIMETYPE_VIDEO_HEVC },
    { kCodecIDINDEO3, MEDIA_MIMETYPE_VIDEO_INDEOX },
    { kCodecIDVP3, MEDIA_MIMETYPE_VIDEO_VP3 },
    { kCodecIDTHEORA, MEDIA_MIMETYPE_VIDEO_THEORA },
    { kCodecIDPNG, MEDIA_MIMETYPE_IMAGE_PNG },
    { kCodecIDRV30, MEDIA_MIMETYPE_VIDEO_RV30 },
    { kCodecIDRV40, MEDIA_MIMETYPE_VIDEO_RV40 },
    { kCodecIDWMV3, MEDIA_MIMETYPE_VIDEO_WMV3 },
    { kCodecIDBMP, MEDIA_MIMETYPE_IMAGE_BMP },
    { kCodecIDJPEG2000, MEDIA_MIMETYPE_IMAGE_JPEG },
    { kCodecIDVP5, MEDIA_MIMETYPE_VIDEO_VP5 },
    { kCodecIDVP6, MEDIA_MIMETYPE_VIDEO_VP6 },
    { kCodecIDVP6F, MEDIA_MIMETYPE_VIDEO_VP6F },
    { kCodecIDTIFF, MEDIA_MIMETYPE_VIDEO_TIFF },
    { kCodecIDGIF, MEDIA_MIMETYPE_VIDEO_GIF },
    { kCodecIDVP6A, MEDIA_MIMETYPE_VIDEO_VP6A },
    { kCodecIDVB, MEDIA_MIMETYPE_VIDEO_DVB },
    { kCodecIDFLASHSV2, MEDIA_MIMETYPE_VIDEO_FLASH },
    { kCodecIDVP8, MEDIA_MIMETYPE_VIDEO_VP8 },
    { kCodecIDWMV3IMAGE, MEDIA_MIMETYPE_VIDEO_WMV3IMAGE },
    { kCodecIDVP9, MEDIA_MIMETYPE_VIDEO_VP9 },
    { kCodecIDVP7, MEDIA_MIMETYPE_VIDEO_VP7 },

    /* audio codecs */
    // kCodecIDPCM_S16LE to kCodecIDADPCM_VIMA, to raw(not in map)
    { kCodecIDAMR_NB, MEDIA_MIMETYPE_AUDIO_AMR_NB },
    { kCodecIDAMR_WB, MEDIA_MIMETYPE_AUDIO_AMR_WB },
    { kCodecIDRA_144, MEDIA_MIMETYPE_AUDIO_DRA144 },
    { kCodecIDRA_288, MEDIA_MIMETYPE_AUDIO_DRA288 },
    { kCodecIDMP1, MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I },
    { kCodecIDMP2, MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II },
    { kCodecIDMP3, MEDIA_MIMETYPE_AUDIO_MPEG },
    { kCodecIDAAC, MEDIA_MIMETYPE_AUDIO_AAC },
    { kCodecIDVORBIS, MEDIA_MIMETYPE_AUDIO_VORBIS },
    { kCodecIDWMAV1, MEDIA_MIMETYPE_AUDIO_WMA },
    { kCodecIDWMAV2, MEDIA_MIMETYPE_AUDIO_WMA },
    { kCodecIDWMAVOICE, MEDIA_MIMETYPE_AUDIO_WMA },
    { kCodecIDWMAPRO, MEDIA_MIMETYPE_AUDIO_WMAPRO },
    { kCodecIDWMALOSSLESS, MEDIA_MIMETYPE_AUDIO_WMALOSSLESS },
    { kCodecIDFLAC, MEDIA_MIMETYPE_AUDIO_FLAC },
    { kCodecIDMP3ADU, MEDIA_MIMETYPE_AUDIO_MPEG_ADU },
    { kCodecIDMP3ON4, MEDIA_MIMETYPE_AUDIO_MPEG_ON4 },
    { kCodecIDAC3, MEDIA_MIMETYPE_AUDIO_AC3 },
    { kCodecIDEAC3, MEDIA_MIMETYPE_AUDIO_EC3 },
    { kCodecIDGSM, MEDIA_MIMETYPE_AUDIO_MSGSM },
    { kCodecIDQCELP, MEDIA_MIMETYPE_AUDIO_QCELP },
    { kCodecIDOPUS, MEDIA_MIMETYPE_AUDIO_OPUS },
    { kCodecIDWAVPACK, MEDIA_MIMETYPE_AUDIO_WAV },
    { kCodecIDAAC_LATM, MEDIA_MIMETYPE_AUDIO_AAC_LATM },
    { kCodecIDPCM_MULAW, MEDIA_MIMETYPE_AUDIO_G711_MLAW },
    { kCodecIDPCM_ALAW, MEDIA_MIMETYPE_AUDIO_G711_ALAW },
//    kCodecIDSVQ1,
//    kCodecIDSVQ3,
//    kCodecIDASV1,
//    kCodecIDASV2,
//    kCodecIDFFV1,
//    kCodecID4XM,
//    kCodecIDVCR1,
//    kCodecIDCLJR,
//    kCodecIDMDEC,
//    kCodecIDROQ,
//    kCodecIDINTERPLAY_VIDEO,
//    kCodecIDXAN_WC3,
//    kCodecIDXAN_WC4,
//    kCodecIDRPZA,
//    kCodecIDCINEPAK,
//    kCodecIDWS_VQA,
//    kCodecIDMSRLE,
//    kCodecIDMSVIDEO1,
//    kCodecIDIDCIN,
//    kCodecID8BPS,
//    kCodecIDSMC,
//    kCodecIDFLIC,
//    kCodecIDTRUEMOTION1,
//    kCodecIDVMDVIDEO,
//    kCodecIDMSZH,
//    kCodecIDZLIB,
//    kCodecIDQTRLE,
//    kCodecIDTSCC,
//    kCodecIDULTI,
//    kCodecIDQDRAW,
//    kCodecIDVIXL,
//    kCodecIDQPEG,
//    kCodecIDPPM,
//    kCodecIDPBM,
//    kCodecIDPGM,
//    kCodecIDPGMYUV,
//    kCodecIDPAM,
//    kCodecIDFFVHUFF,
    { kCodecIDVC1, MEDIA_MIMETYPE_VIDEO_VC1 },
//    kCodecIDLOCO,
//    kCodecIDWNV1,
//    kCodecIDAASC,
//    kCodecIDINDEO2,
//    kCodecIDFRAPS,
//    kCodecIDTRUEMOTION2,
//    kCodecIDCSCD,
//    kCodecIDMMVIDEO,
//    kCodecIDZMBV,
//    kCodecIDAVS,
//    kCodecIDSMACKVIDEO,
//    kCodecIDNUV,
//    kCodecIDKMVC,
//    kCodecIDFLASHSV,
//    kCodecIDCAVS,
//    kCodecIDVMNC,
//    kCodecIDTARGA,
//    kCodecIDDSICINVIDEO,
//    kCodecIDTIERTEXSEQVIDEO,
//    kCodecIDDXA,
//    kCodecIDDNXHD,
//    kCodecIDTHP,
//    kCodecIDSGI,
//    kCodecIDC93,
//    kCodecIDBETHSOFTVID,
//    kCodecIDPTX,
//    kCodecIDTXD,
//    kCodecIDAMV,
//    kCodecIDPCX,
//    kCodecIDSUNRAST,
//    kCodecIDINDEO4,
//    kCodecIDINDEO5,
//    kCodecIDMIMIC,
//    kCodecIDRL2,
//    kCodecIDESCAPE124,
//    kCodecIDDIRAC,
//    kCodecIDBFI,
//    kCodecIDCMV,
//    kCodecIDMOTIONPIXELS,
//    kCodecIDTGV,
//    kCodecIDTGQ,
//    kCodecIDTQI,
//    kCodecIDAURA,
//    kCodecIDAURA2,
//    kCodecIDV210X,
//    kCodecIDTMV,
//    kCodecIDV210,
//    kCodecIDDPX,
//    kCodecIDMAD,
//    kCodecIDFRWU,
//    kCodecIDCDGRAPHICS,
//    kCodecIDR210,
//    kCodecIDANM,
//    kCodecIDBINKVIDEO,
//    kCodecIDIFF_ILBM,
//    kCodecIDIFF_BYTERUN1,
//    kCodecIDKGV1,
//    kCodecIDYOP,
//    kCodecIDPICTOR,
//    kCodecIDANSI,
//    kCodecIDA64_MULTI,
//    kCodecIDA64_MULTI5,
//    kCodecIDR10K,
//    kCodecIDMXPEG,
//    kCodecIDLAGARITH,
//    kCodecIDPRORES,
//    kCodecIDJV,
//    kCodecIDDFA,
//    kCodecIDVC1IMAGE,
//    kCodecIDUTVIDEO,
//    kCodecIDBMV_VIDEO,
//    kCodecIDVBLE,
//    kCodecIDDXTORY,
//    kCodecIDV410,
//    kCodecIDXWD,
//    kCodecIDCDXL,
//    kCodecIDXBM,
//    kCodecIDZEROCODEC,
//    kCodecIDMSS1,
//    kCodecIDMSA1,
//    kCodecIDTSCC2,
//    kCodecIDMTS2,
//    kCodecIDCLLC,
//    kCodecIDMSS2,
//    kCodecIDAIC,
//    kCodecIDESCAPE130,
//    kCodecIDG2M,
//    kCodecIDWEBP,
//    kCodecIDHNM4_VIDEO,
//    kCodecIDHEVC,
//    kCodecIDFIC,
//    kCodecIDALIAS_PIX,
//    kCodecIDBRENDER_PIX,
//    kCodecIDPAF_VIDEO,
//    kCodecIDEXR,
//    kCodecIDSANM,
//    kCodecIDSGIRLE,
//    kCodecIDMVC1,
//    kCodecIDMVC2,



    /* various DPCM codecs */
//    kCodecIDROQ_DPCM = 0x14000,
//    kCodecIDINTERPLAY_DPCM,
//    kCodecIDXAN_DPCM,
//    kCodecIDSOL_DPCM,

    /* audio codecs */
//    ,
//    kCodecIDDTS,
    { kCodecIDDTS, MEDIA_MIMETYPE_AUDIO_DTS },
//    kCodecIDDVAUDIO,
//    kCodecIDMACE3,
//    kCodecIDMACE6,
//    kCodecIDVMDAUDIO,
//    kCodecIDSHORTEN,
//    kCodecIDALAC,
//    kCodecIDWESTWOOD_SND1,
//    kCodecIDQDM2,
//    kCodecIDCOOK,
//    kCodecIDTRUESPEECH,
//    kCodecIDTTA,
//    kCodecIDSMACKAUDIO,
//    kCodecIDDSICINAUDIO,
//    kCodecIDIMC,
//    kCodecIDMUSEPACK7,
//    kCodecIDMLP,
//    kCodecIDGSM_MS, /* as found in WAV */
//    kCodecIDATRAC3,
//    kCodecIDVOXWARE,
//    kCodecIDAPE,
//    kCodecIDNELLYMOSER,
//    kCodecIDMUSEPACK8,
//    kCodecIDSPEEX,
//    kCodecIDATRAC3P,
//    kCodecIDEAC3,
//    kCodecIDSIPR,
//    kCodecIDMP1,
//    kCodecIDTWINVQ,
//    kCodecIDTRUEHD,
//    kCodecIDMP4ALS,
//    kCodecIDATRAC1,
//    kCodecIDBINKAUDIO_RDFT,
//    kCodecIDBINKAUDIO_DCT,
//    kCodecIDQDMC,
//    kCodecIDCELT,
//    kCodecIDG723_1,
//    kCodecIDG729,
//    kCodecID8SVX_EXP,
//    kCodecID8SVX_FIB,
//    kCodecIDBMV_AUDIO,
//    kCodecIDRALF,
//    kCodecIDIAC,
//    kCodecIDILBC,
//    kCodecIDOPUS,
//    kCodecIDCOMFORT_NOISE,
//    kCodecIDTAK,
//    kCodecIDMETASOUND,
//    kCodecIDPAF_AUDIO,
//    kCodecIDON2AVC,

    /* subtitle codecs */
//    kCodecIDFIRST_SUBTITLE = 0x17000,          ///< A dummy ID pointing at the start of subtitle codecs.
//    kCodecIDDVD_SUBTITLE = 0x17000,
//    kCodecIDDVB_SUBTITLE,
//    kCodecIDTEXT,  ///< raw UTF-8 text
//    kCodecIDXSUB,
//    kCodecIDSSA,
//    kCodecIDMOV_TEXT,
//    kCodecIDHDMV_PGS_SUBTITLE,
//    kCodecIDDVB_TELETEXT,
//    kCodecIDSRT,

};

const char * codecId2Mime(CowCodecID id)
{
    unsigned int i = 0;

    if (( id >= kCodecIDPCM_S16LE && id <= kCodecIDPCM_U8) ||
        (id >= kCodecIDPCM_S32LE && id <= kCodecIDADPCM_VIMA )) {
        INFO("id: %d, mime: %s\n", id, MEDIA_MIMETYPE_AUDIO_RAW);
        return MEDIA_MIMETYPE_AUDIO_RAW;
    }

    for (i=0; i< sizeof(s_codecId2Mime)/sizeof(CodecIdMimeType); i++) {
        if (s_codecId2Mime[i].id == id)
            return s_codecId2Mime[i].mime;
    }

    return NULL;
}

CowCodecID mime2CodecId(const char *mime)
{
    unsigned int i = 0;

    if (!mime)
        return kCodecIDNONE;

    for (i=0; i< sizeof(s_codecId2Mime)/sizeof(CodecIdMimeType); i++) {
        if (!strcmp(s_codecId2Mime[i].mime, mime))
            return (CowCodecID)s_codecId2Mime[i].id;
    }

    return kCodecIDNONE;
}

} // YUNOS_MM
