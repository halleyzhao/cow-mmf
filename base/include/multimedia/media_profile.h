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

#ifndef __profile_H
#define __profile_H

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_PROFILE_UNKNOWN -99
#define MEDIA_PROFILE_RESERVED -100

#define MEDIA_PROFILE_AAC_MAIN 0
#define MEDIA_PROFILE_AAC_LOW  1
#define MEDIA_PROFILE_AAC_SSR  2
#define MEDIA_PROFILE_AAC_LTP  3
#define MEDIA_PROFILE_AAC_HE   4
#define MEDIA_PROFILE_AAC_HE_V2 28
#define MEDIA_PROFILE_AAC_LD   22
#define MEDIA_PROFILE_AAC_ELD  38
#define MEDIA_PROFILE_MPEG2_AAC_LOW 128
#define MEDIA_PROFILE_MPEG2_AAC_HE  131

#define MEDIA_PROFILE_DTS         20
#define MEDIA_PROFILE_DTS_ES      30
#define MEDIA_PROFILE_DTS_96_24   40
#define MEDIA_PROFILE_DTS_HD_HRA  50
#define MEDIA_PROFILE_DTS_HD_MA   60

#define MEDIA_PROFILE_MPEG2_422    0
#define MEDIA_PROFILE_MPEG2_HIGH   1
#define MEDIA_PROFILE_MPEG2_SS     2
#define MEDIA_PROFILE_MPEG2_SNR_SCALABLE  3
#define MEDIA_PROFILE_MPEG2_MAIN   4
#define MEDIA_PROFILE_MPEG2_SIMPLE 5

#define MEDIA_PROFILE_H264_CONSTRAINED  (1<<9)  // 8+1; constraint_set1_flag
#define MEDIA_PROFILE_H264_INTRA        (1<<11) // 8+3; constraint_set3_flag

#define MEDIA_PROFILE_H264_BASELINE             66
#define MEDIA_PROFILE_H264_CONSTRAINED_BASELINE (66|MEDIA_PROFILE_H264_CONSTRAINED)
#define MEDIA_PROFILE_H264_MAIN                 77
#define MEDIA_PROFILE_H264_EXTENDED             88
#define MEDIA_PROFILE_H264_HIGH                 100
#define MEDIA_PROFILE_H264_HIGH_10              110
#define MEDIA_PROFILE_H264_HIGH_10_INTRA        (110|MEDIA_PROFILE_H264_INTRA)
#define MEDIA_PROFILE_H264_HIGH_422             122
#define MEDIA_PROFILE_H264_HIGH_422_INTRA       (122|MEDIA_PROFILE_H264_INTRA)
#define MEDIA_PROFILE_H264_HIGH_444             144
#define MEDIA_PROFILE_H264_HIGH_444_PREDICTIVE  244
#define MEDIA_PROFILE_H264_HIGH_444_INTRA       (244|MEDIA_PROFILE_H264_INTRA)
#define MEDIA_PROFILE_H264_CAVLC_444            44

#define MEDIA_PROFILE_VC1_SIMPLE   0
#define MEDIA_PROFILE_VC1_MAIN     1
#define MEDIA_PROFILE_VC1_COMPLEX  2
#define MEDIA_PROFILE_VC1_ADVANCED 3

#define MEDIA_PROFILE_MPEG4_SIMPLE                     0
#define MEDIA_PROFILE_MPEG4_SIMPLE_SCALABLE            1
#define MEDIA_PROFILE_MPEG4_CORE                       2
#define MEDIA_PROFILE_MPEG4_MAIN                       3
#define MEDIA_PROFILE_MPEG4_N_BIT                      4
#define MEDIA_PROFILE_MPEG4_SCALABLE_TEXTURE           5
#define MEDIA_PROFILE_MPEG4_SIMPLE_FACE_ANIMATION      6
#define MEDIA_PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE     7
#define MEDIA_PROFILE_MPEG4_HYBRID                     8
#define MEDIA_PROFILE_MPEG4_ADVANCED_REAL_TIME         9
#define MEDIA_PROFILE_MPEG4_CORE_SCALABLE             10
#define MEDIA_PROFILE_MPEG4_ADVANCED_CODING           11
#define MEDIA_PROFILE_MPEG4_ADVANCED_CORE             12
#define MEDIA_PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE 13
#define MEDIA_PROFILE_MPEG4_SIMPLE_STUDIO             14
#define MEDIA_PROFILE_MPEG4_ADVANCED_SIMPLE           15

#define MEDIA_PROFILE_JPEG2000_CSTREAM_RESTRICTION_0   0
#define MEDIA_PROFILE_JPEG2000_CSTREAM_RESTRICTION_1   1
#define MEDIA_PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION  2
#define MEDIA_PROFILE_JPEG2000_DCINEMA_2K              3
#define MEDIA_PROFILE_JPEG2000_DCINEMA_4K              4


#define MEDIA_PROFILE_HEVC_MAIN                        1
#define MEDIA_PROFILE_HEVC_MAIN_10                     2
#define MEDIA_PROFILE_HEVC_MAIN_STILL_PICTURE          3


#ifdef __cplusplus
}
#endif

#endif // __profile_H
