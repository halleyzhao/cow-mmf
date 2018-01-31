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
#include <stdint.h>
#include "multimedia/mm_cpp_utils.h"
#include "multimedia/media_meta.h"
#include "multimedia/media_monitor.h"

#ifndef media_buffer_h
#define media_buffer_h

namespace YUNOS_MM {

class MediaBuffer;
typedef MMSharedPtr <MediaBuffer> MediaBufferSP;

class MediaBuffer {
  public:
    const static int64_t MediaBufferSizeUndefined;
    const static int64_t MediaBufferTimeInvalid;
    #define MediaBufferMaxDataPlane     8       // in fact, 3 for video, 8 for audio 7.1 channel
    #define MediaBufferMaxReleaseFunc   4
    #define MediaBufferMaxExtraData     8
    typedef bool (*releaseBufferFunc)(MediaBuffer* buffer); // when new resource is associated with MediaBuffer, register func for cleanup
    // typedef bool (*renderBufferFunc)(MediaBuffer* buffer, bool skip);  // register function to render the buffer.

    enum MediaBufferType {
        MBT_Undefined,
        MBT_ByteBuffer,     // compressed data
        MBT_RawVideo,       // raw video frame data
        MBT_BufferIndex,    // buffer index for MediaCodec
        MBT_BufferIndexV4L2, // buffer index for v4l2
        MBT_DmaBufHandle,   // Linux kernle dma_buf handle
        MBT_DrmBufName,     // the fd of drm buffer
        MBT_DrmBufBo,       // the dri_bo of drm buffer
        MBT_RawAudio,       // raw audio data
        MBT_GraphicBufferHandle, // graphic buffer_handle_t
        MBT_CameraSourceMetaData, // meta data from CameraSource
        MBT_LAST,
    };

    enum MediaBufferFlagType {
        MBFT_Live,
        MBFT_Discontinue,
        MBFT_Corrupt,
        MBFT_EOS,
        MBFT_KeyFrame,
        MBFT_CodecData, // for example, H264 SPS/PPS
        MBFT_AVPacket,  // it is initialized from ffmpeg AVPacket, it is possible to retrieve the AVPacket by AVBufferHelper
        MBFT_AVFrame,   // it is initialized from ffmpeg AVFrame, it is possible to retrieve the AVFrame by AVBufferHelper
        MBFT_AVCC,      // avcC format H264 stream which prefixed with nal size (versus 'bytestream' which prefix with start code)
        MBFT_BufferInited, // internal used only, it has been initialized with valid buffer
        MBFT_NonRef, // not used as ref frame. one usage example: during video conference, local encoded frame can be dropped w/o impact future frames (when decoding at remote side)
        MBFT_LAST,
    };

    ~MediaBuffer();
    static MediaBufferSP createMediaBuffer(MediaBufferType type = MBT_ByteBuffer);

    inline MediaBufferType type() const { return mType; };
    inline void setType(MediaBufferType type) { mType = type; };

    inline void setFlag(MediaBufferFlagType flag) { mFlags |= (1<<flag); };
    inline void unsetFlag(MediaBufferFlagType flag) { mFlags &= ~(1<<flag); };

    inline bool isFlagSet(MediaBufferFlagType flag) const { return mFlags & (1<<flag); };

    bool setBufferInfo(uintptr_t* buffers, int32_t* offsets, int32_t* strides, int dimension);
    bool getBufferInfo(uintptr_t* buffers, int32_t* offsets, int32_t* strides, int dimension) const;
    bool updateBufferOffset(int32_t* offsets, int dimension);

    inline void setSize(int64_t size) { mSize = size; };
    inline int64_t size() const { return mSize; };

    inline void setDts(int64_t dts) { mDts = dts; };
    inline int64_t dts() const { return mDts;};

    inline void setPts(int64_t pts) { mPts = pts;};
    inline int64_t pts() const { return mPts; };

    //based on us
    inline void setDuration(int64_t duration) { mDuration = duration; };
    inline int64_t duration() const { return mDuration; };

    // when one buffer want to continue the life of another one(usually reincarnation from former one by some processing), specify the born time for it
    inline void setBirthTimeInMs(int64_t birthTime) { mBirthTime = birthTime; }
    inline int64_t birthTimeInMs() const { return mBirthTime; }
    int32_t ageInMs() const;

    // after a buffer is processed, the releaseFunc may require to change
    bool addReleaseBufferFunc(releaseBufferFunc releaseFunc);
    // bool setRenderFunc(renderBufferFunc renderFunc);

    void setMediaMeta(MediaMetaSP &meta) { mMeta = meta; };
    MediaMetaSP getMediaMeta() const {return mMeta;};
    void setMonitor(MonitorSP monitor) { mTrackers.push_back(Tracker::create(monitor));};

  protected:

  private:
    MediaBuffer(MediaBufferType type);
    MediaBufferType mType;
    uint64_t    mFlags;
    std::vector<TrackerSP> mTrackers;

    /* data pointer/handle of data
     * for compressed audio/video data, usually buf[0] is used only
     * for uncompressed video data, up to 3 component is used depending on video format, for example:
     *    - YUY2 or RGBX use buf[0] only, NV12 use buf[0] and buf[1], YV12 uses buf[0]/buf[1]/buf[2]
     */
    uintptr_t mBuffers[MediaBufferMaxDataPlane];
    int32_t mOffsets[MediaBufferMaxDataPlane];              // TODO, offset_end
    int32_t mStrides[MediaBufferMaxDataPlane];

    /* total size of the buffer,
     * default -1 means undefined, for example, planar video data, the total size is not much interesting
     * 0 size can be used to send EOS buffer
     */
    int64_t mSize;
    int64_t mDts;
    int64_t mPts;
    int64_t mDuration;
    int64_t mBirthTime; // debug use
    MediaMetaSP mMeta;

    // renderBufferFunc mRenderFunc;
    releaseBufferFunc mReleaseFunc[MediaBufferMaxReleaseFunc];
    int32_t mReleaseFuncCount;

    // we don't support the copy function. it is too hard to do a deep copy in most case, shared_ptr can be treated as light copy
    MediaBuffer();
    MM_DISALLOW_COPY(MediaBuffer)
};

} // end of namespace YUNOS_MM

#endif // media_buffer_h
