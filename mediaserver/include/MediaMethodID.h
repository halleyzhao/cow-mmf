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

#ifndef __media_method_id_h
#define __media_method_id_h

#include <multimedia/mmparam.h>

namespace YUNOS_MM {

// TODO use ThreadMux to switch dynamically
#define SINGLE_THREAD_PROXY
//#undef SINGLE_THREAD_PROXY

#ifdef SINGLE_THREAD_PROXY

#define INIT_MEDIA_METHOD(val)                  \
    MMParam param;                              \
    param.writeInt32(val);

enum MediaMethodID {
    MM_METHOD_BEGIN = 0,

    // player and common
    MM_METHOD_SET_LISTENER,       //1
    MM_METHOD_SET_DATA_SOURCE_FD, //2
    MM_METHOD_SET_DATA_SOURCE_URI,//3
    MM_METHOD_SET_DATA_SOURCE_MEM,//4
    MM_METHOD_SET_DISPLAY_NAME,   //5
    MM_METHOD_SET_VIDEO_DISPLAY,  //6
    MM_METHOD_SET_VIDEO_SURFACE_TEXTURE,//7
    MM_METHOD_PREPARE,       //8
    MM_METHOD_PREPARE_ASYNC, //9
    MM_METHOD_RESET,         //10
    MM_METHOD_SET_VOLUME,    //11
    MM_METHOD_GET_VOLUME,    //12
    MM_METHOD_SET_MUTE,      //13
    MM_METHOD_GET_MUTE,      //14
    MM_METHOD_START,         //15
    MM_METHOD_STOP,          //16
    MM_METHOD_stopSync,     //17
    MM_METHOD_PAUSE,         //18
    MM_METHOD_SEEK,          //19
    MM_METHOD_IS_PLAYING,    //20
    MM_METHOD_GET_VIDEO_SIZE,//21
    MM_METHOD_GET_CURRENT_POSITION, //22
    MM_METHOD_GET_DURATION,  //23
    MM_METHOD_SET_AUDIO_STREAM_TYPE,//24
    MM_METHOD_GET_AUDIO_STREAM_TYPE,//25
    MM_METHOD_SET_LOOP,      //26
    MM_METHOD_IS_LOOPING,    //27
    MM_METHOD_GET_PARAMETER, //28
    MM_METHOD_SET_PARAMETER, //29
    MM_METHOD_INVOKE,        //30
    MM_METHOD_CAPTURE_VIDEO, //31
    MM_METHOD_RELEASE,       //32

    // recorder
    MM_METHOD_SET_CAMERA = 100,    //100
    MM_METHOD_SET_VIDEO_SOURCE_URI,    //101
    MM_METHOD_SET_AUDIO_SOURCE_URI,    //102
    MM_METHOD_SET_VIDEO_SOURCE_FORMAT, //103
    MM_METHOD_SET_VIDEO_ENCODER,       //104
    MM_METHOD_SET_AUDIO_ENCODER,       //105
    MM_METHOD_SET_OUTPUT_FORMAT,       //106
    MM_METHOD_SET_OUTPUT_FILE_PATH,    //107
    MM_METHOD_SET_OUTPUT_FILE_FD,      //108
    MM_METHOD_SET_RECORDER_USAGE,      //109
    MM_METHOD_GET_RECORDER_USAGE,      //110
    MM_METHOD_SET_PREVIEW_SURFACE,     //111
    MM_METHOD_IS_RECORDING,            //112
    MM_METHOD_SET_MAX_DURATION,        //113
    MM_METHOD_SET_MAX_FILE_SIZE,       //114

    // mst
    MM_METHOD_RETURN_ACQUIRED_BUFFERS = 200, //200
    MM_METHOD_ACQUIRE_BUFFER,          //201
    MM_METHOD_RETURN_BUFFER,           //202
    MM_METHOD_SET_WINDOW_SURFACE,      //203
    MM_METHOD_SET_SHOW_FLAG,           //204
    MM_METHOD_GET_SHOW_FLAG,           //205
    MM_METHOD_SET_MST_LISTENER,        //206

    // subtitle
    MM_METHOD_SET_SUBTITLE_URI = 300,        //300
    MM_METHOD_ENABLE_EXTERNAL_SUBTITLE //301
};
#endif

} // end of YUNOS_MM
#endif
