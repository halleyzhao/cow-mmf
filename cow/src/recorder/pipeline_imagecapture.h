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
#ifndef pipeline_imagecapture_h
#define pipeline_imagecapture_h
#include "pipeline_recorder_base.h"
#include "multimedia/codec.h"


namespace YUNOS_MM {
class PipelineImageCapture;
typedef MMSharedPtr<PipelineImageCapture> PipelineImageCaptureSP;

class PipelineImageCapture : public PipelineRecorderBase {
  public:

    bool setPhotoCount(int32_t count) { mPhotoCount = count; return true; };
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);
    PipelineImageCapture();
    virtual ~PipelineImageCapture();

    mm_status_t getCamera(void *&camera, void *&recordingProxy);

  protected:

        // returns: MM_ERROR_SUCCESS: successfuly executed.
        //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
        //              others: error.
    virtual mm_status_t prepareInternal();
    virtual mm_status_t notify(int msg, int param1, int param2, const MediaMetaSP obj);

  private:
    int32_t mPhotoCount;
    int32_t mJpegQuality;
    int32_t mCameraId;

    MM_DISALLOW_COPY(PipelineImageCapture)


}; // PipelineImageCapture

} // YUNOS_MM

#endif // pipeline_imagecapture_h

