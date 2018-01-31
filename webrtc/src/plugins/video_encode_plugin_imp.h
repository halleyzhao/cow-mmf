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

#ifndef __webrtc_video_encode_plugin_imp_H
#define __webrtc_video_encode_plugin_imp_H

#include <multimedia/webrtc/video_encode_plugin.h>
#include "multimedia/cowapp_basic.h"
#include "multimedia/mm_camera_compat.h"

namespace YUNOS_MM {

/* there is one single video encoding implementation/pipeline (VideoEncodePluginImp),
 * each webrtc peer connection has one VideoEncodeInterface (VideoEncoderWrapper),
 * all VideoEncoderWrapper share the single VideoEncodePluginImp, but each one makes
 * a copy of encoded data (EncodedBufferQueue).
 */
typedef struct {
    std::queue<MediaBufferSP> que;
    Lock lock;
} EncodedBufferQueue;

class VideoEncodePluginImp : public VideoEncodePlugin {
public:
    VideoEncodePluginImp();
   virtual  ~VideoEncodePluginImp();

public:
    virtual bool InitEncoderContext(
            woogeen::base::MediaCodec::VideoCodec video_codec,
            size_t width,
            size_t height,
            uint32_t bitrate,
            uint32_t framerate);
    virtual bool EncodeOneFrame();
    virtual VideoEncoderInterface* Copy();
    // remove one encoder instance, in fact, remove corresponding buffer queue
    virtual bool Remove(EncodedBufferQueue * queue);

private:
    virtual bool EncodeOneFrame(std::vector<uint8_t>& buffer,
            uint32_t timestamp,
            bool is_key_frame) {} // private to make it unusable
    virtual bool Release(); // make it private. only be called inside VideoEncodePluginImp when the last VideoEncoderWrapper is Released
    bool createCamera();
    static void destroyCamera(YunOS::VideoCapture* camera);

private:
    int32_t mCameraId;
    typedef MMSharedPtr<YunOS::VideoCapture> CameraCaptureSP;
    CameraCaptureSP mCamera;
    CowAppBasicSP mRecorder;
    PipelineSP mPipeline;

    bool mInited;
    woogeen::base::MediaCodec::VideoCodec mVideoCodec;
    int32_t mWidth;
    int32_t mHeight;
    float mFramerate;
    uint32_t mBitrate;
    mutable Lock mLock;

    std::list<EncodedBufferQueue*> mBufferQueues;
    bool mCopyH264Data; // make a copy of data from pipeline, (since hw codec have few buffer slots)
    void* mPreviewSurface;

    //dbg use
    int32_t mFrameCount;

    bool mUtDumpH264; // ffplay is able to play this H264 byte stream with extra info
    FILE * mFile;

    MM_DISALLOW_COPY(VideoEncodePluginImp)
    DECLARE_LOGTAG()
};

}
#endif // __webrtc_video_encode_plugin_imp_H