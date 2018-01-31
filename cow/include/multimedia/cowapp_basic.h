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
#ifndef cowapp_basic_h
#define cowapp_basic_h

#include "multimedia/mm_cpp_utils.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/mm_errors.h"
#include "multimedia/mmparam.h"
#include "multimedia/media_meta.h"
#include "multimedia/pipeline.h" // give client opportunity to customize pipeline

namespace YUNOS_MM {

class CowAppBasic;
typedef MMSharedPtr<CowAppBasic> CowAppBasicSP;
class CowAppBasic : public MMMsgThread {
/* most action (setDataSource, prepare, start/stop etc) are handled in async mode,
 * and nofity upper layer (Listener) after it has been executed.
 */
public:
    CowAppBasic();
    virtual ~CowAppBasic();
    static CowAppBasicSP create(CowAppBasic* app);
    // listener for app
    class Listener{
      public:
        Listener(){}
        virtual ~Listener(){}
        virtual void onMessage(int msg, int param1, int param2, const MMParamSP param) = 0;
        MM_DISALLOW_COPY(Listener)
    };
    typedef MMSharedPtr<Listener> ListenerSP;

public:
    virtual mm_status_t setPipeline(PipelineSP pipeline);
    virtual mm_status_t setListener(Listener * listener);
    virtual void removeListener();
    virtual mm_status_t setVideoSurface(void * handle, bool isTexture);
    virtual mm_status_t prepare();
    virtual mm_status_t prepareAsync();
    virtual mm_status_t start();
    virtual mm_status_t stop();
    virtual mm_status_t pause();
    virtual mm_status_t reset();
    virtual bool isPlaying() const;
    virtual mm_status_t getVideoSize(int& width, int& height) const;
    virtual mm_status_t getCurrentPosition(int64_t& msec) const;
    virtual mm_status_t getDuration(int64_t& msec) const;
    virtual mm_status_t setParameter(const MediaMetaSP & meta);
    virtual mm_status_t getParameter(MediaMetaSP & meta);

  private:
    class PipelineListener;
     mm_status_t notify(int msg, int param1, int param2, const MMParamSP obj);
     Lock mLock;
     PipelineSP mPipeline;
     Pipeline::ListenerSP mListenerReceive;
     Listener* mListenderSend;

     DECLARE_MSG_LOOP()
     DECLARE_MSG_HANDLER(onPrepare)
     DECLARE_MSG_HANDLER(onPrepareAsync)
     DECLARE_MSG_HANDLER(onStart)
     DECLARE_MSG_HANDLER(onStop)
     DECLARE_MSG_HANDLER(onPause)
     DECLARE_MSG_HANDLER(onReset)

    MM_DISALLOW_COPY(CowAppBasic)
   };

} // YUNOS_MM

#endif // cowapp_basic_h
