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

#ifndef __webrtc_video_decode_plugin_imp_H
#define __webrtc_video_decode_plugin_imp_H

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_buffer.h>
#include <multimedia/cowapp_basic.h>
#include <multimedia/webrtc/video_decode_plugin.h>

namespace YUNOS_MM {

class VideoDecodePluginImp : public VideoDecodePlugin {
public:
    explicit VideoDecodePluginImp(void * surface, int surfaceType = 0/* platform dependent surface type */);
   virtual  ~VideoDecodePluginImp();

public:
    // FIXME, not match with interface defined in woogeen videoencoderinterface.h
    virtual bool InitDecodeContext(woogeen::base::MediaCodec::VideoCodec video_codec);
    virtual bool Release();
    virtual bool OnFrame(woogeen::base::VideoFrame* frame);

private:
    MediaBufferSP createMediaBuffer(woogeen::base::VideoFrame* frame);
    static bool releaseMediaBuffer(MediaBuffer* mediaBuffer);

private:
    woogeen::base::MediaCodec::VideoCodec mCodecType;
    CowAppBasicSP mPlayer;
    PipelineSP mPipeline;
    void *mSurface;
    void *mSelfSurface;
    bool mGotKeyFrame;

    MediaMetaSP mBufferMeta;

    MM_DISALLOW_COPY(VideoDecodePluginImp)
    DECLARE_LOGTAG()
};

}
#endif
