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

#include <multimedia/component.h>

namespace YUNOS_MM {
const char* Component::sEventStr[] = {
    "kEventPrepareResult",      // 0
    "kEventStartResult",        // 1
    "kEventStopped",            // 2
    "kEventPaused",             // 3
    "kEventResumed",            // 4
    "kEventSeekComplete",       // 5
    "kEventResetComplete",      // 6
    "kEventFlushComplete",      // 7
    "kEventDrainComplete",      // 8
    "kEventEOS",                // 9
    "kEventGotVideoFormat",     // 10
    "kEventMediaInfo",          // 11
    "kEventInfoDuration",       // 12
    "kEventInfoBufferingUpdate",// 13
    "kEventInfoSubtitleData",   // 14
    "kEventError",              // 15
    "kEventInfo",               // 16
    "kEventInfoExt",            // 17
    "kEventUpdateTextureImage", // 18
    "kEventRequestIDR",         // 19
    "kEventVideoRotationDegree",// 20
    "kEventMusicSpectrum",      // 21
    "kEventSeekRequire",        // 22
    "kEventInfoGetTrackCompleted", // 23
    "unknown event",               // 24
};

const char* Component::sEventInfoStr[] = {
    "kEventInfoDiscontinuety",
    "kEventInfoVideoRenderStart",
    "kEventInfoMediaRenderStarted",
    /*
    "kEventInfoSourceStart",
    "kEventInfoSourceMax",
    "kEventInfoFilterStart",
    "kEventInfoFilterMax",
    "kEventInfoSinkStart",
    "kEventInfoSinkMax",
    */
};

const char * SourceComponent::PARAM_KEY_PREPARE_TIMEOUT = "prepare-timeout";
const char * SourceComponent::PARAM_KEY_READ_TIMEOUT = "read-timeout";

const char * PlaySourceComponent::PARAM_KEY_BUFFERING_TIME = "buffering-time";
const char * DashSourceComponent::PARAM_KEY_SEGMENT_BUFFER = "segment-buffer";

MMParamSP nilParam;

}

