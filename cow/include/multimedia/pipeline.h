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
#ifndef pipeline_h
#define pipeline_h
#include "multimedia/component.h"
#include "multimedia/mmmsgthread.h"
#include "multimedia/media_meta.h"
#include <multimedia/mm_cpp_utils.h>
#include "multimedia/codec.h"
#include "multimedia/elapsedtimer.h"
#include "multimedia/mm_debug.h"


namespace YUNOS_MM {
class Pipeline;
typedef MMSharedPtr<Pipeline> PipelineSP;


class PipelineParamRefBase : public MMRefBase {
  public:
    explicit PipelineParamRefBase(int msg, const Component* sender, const MMParamSP param)
        : mMsg(msg)
        , mSender(sender)
        , mParam(param) {}
    //virtual ~PipelineParamRefBase() {INFO();}
    int mMsg;
    const Component* mSender;
    MMParamSP mParam;
};

class Pipeline : public MMMsgThread {
  public:
    class Listener{
      public:
        Listener(){}
        virtual ~Listener(){}

      public:
        virtual void onMessage(int msg, int param1, int param2, const MMParamSP param) = 0;

        MM_DISALLOW_COPY(Listener)
    };
    typedef MMSharedPtr<Listener> ListenerSP;

    enum ComponentStateType{
        kComponentStateNull = 0,    // 0 default state after creation
        // FIXME, remove the the state for Loading/Loaded
        kComponentStateLoading,     // 1 in the process of load media file
        kComponentStateLoaded,      // 2 loaded media file
        kComponentStatePreparing,   // 3 in the process of  construct pipeline (prepare)
        kComponentStatePrepared,    // 4 after pipeline is created, initialized with necessary information
        kComponentStatePausing,     // 5 goint into pause
        kComponentStatePaused,      // 6 paused
        kComponentStatePlay,        // 7 going into play
        kComponentStatePlaying,     // 8 is playing
        kComponentStateStop,        // 9 goint into stop
        kComponentStateStopped,     // 10 stopped
        kComponentStateInvalid,     // 11
        // NOT component state, but help to do seek/reset/flush
        kComponentSeekComplete,     // 12
        kComponentResetComplete,    // 13
        kComponentFlushComplete,    // 14
        kComponentEOSComplete       // 15
    };

    static PipelineSP create(Pipeline *pipeline, ListenerSP listenerReceive = ListenerSP((Listener*)NULL));
    // postMsg is protected
    int postMsgBridge(msg_type what, param1_type param1, param2_type param2, param3_type param3, int64_t timeoutUs = 0);
    // player doesn't know the event of Pipeline, so we have to create a bridge for it
    int postCowMsgBridge(Component::Event event, param1_type param1, param2_type param2, int64_t timeoutUs=0);

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    virtual mm_status_t prepare() = 0;

    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventStartResult.
    //              others: error.
    virtual mm_status_t start() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventStopped.
    //              others: error.
    virtual mm_status_t stop() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventPaused.
    //              others: error.
    virtual mm_status_t pause() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResetComplete.
    //              others: error.
    virtual mm_status_t resume() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventResumed.
    //              others: error.
    virtual mm_status_t reset() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventFlushComplete.
    //              others: error.
    virtual mm_status_t flush() = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error.
    virtual mm_status_t getState(ComponentStateType& state) = 0;
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventSeekComplete.
    //              others: error.
    //virtual mm_status_t seek(SeekEventParamSP param) { return MM_ERROR_UNSUPPORTED; }
    // process all pending message ?
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the action will done by send event kEventDrainComplete.
    //              others: error.
    virtual mm_status_t drain() { return MM_ERROR_UNSUPPORTED; }
    mm_status_t setListener(ListenerSP listener)  { mListenerSend = listener; return MM_ERROR_SUCCESS; }
        // load the given media resource
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              MM_ERROR_ASYNC: execute async, the caller should determine the result by the later event kEventPrepareResult.
    //              others: error.
    virtual mm_status_t load(const char * uri,
                              const std::map<std::string, std::string> * headers = NULL) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t load(int fd, int64_t offset, int64_t length) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t loadSubtitleUri(const char * uri) { return MM_ERROR_UNSUPPORTED; }

    // set display name (for example X11 display name as hostname:protocol.port or Wayland nested display name)
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setDisplayName(const char* name) { return MM_ERROR_UNSUPPORTED; }

    // set native display (for example X11 display or Wayland display)
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setNativeDisplay(void * display) { return MM_ERROR_UNSUPPORTED; }

    // set video rendering surface
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setVideoSurface(void * handle, bool isTexture = false) { return MM_ERROR_UNSUPPORTED; }

    // set preview surface
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others:error
    virtual mm_status_t setPreviewSurface(void * handle) { return MM_ERROR_UNSUPPORTED; }

    // get current playback position
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t getCurrentPosition(int64_t& position) const { return MM_ERROR_UNSUPPORTED; }
    // get media content duration
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t getDuration(int64_t& msec) { return MM_ERROR_UNSUPPORTED; }
    // select track index of given media type
    // returns: MM_ERROR_SUCCESS: successfuly executed.
    //              others: error
    virtual mm_status_t selectTrack(Component::MediaType mediaType, int index) { return MM_ERROR_UNSUPPORTED; }
    // get selected track index of give media type
    // returns: -1 no track selected (no such mediaType).
    virtual int getSelectedTrack(Component::MediaType mediaType) { return MM_ERROR_UNSUPPORTED; }

    virtual MMParamSP getTrackInfo(){ return MMParamSP((MMParam*)NULL); }

    virtual mm_status_t setParameter(const MediaMetaSP & meta) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getParameter(MediaMetaSP & meta) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getVideoSize(int& width, int& height) const { return MM_ERROR_UNSUPPORTED; }
    // unblock the wait of current command execution
    virtual mm_status_t  unblock() { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setAudioStreamType(int type) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getAudioStreamType(int *type) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t setAudioConnectionId(const char * connectionId) { return MM_ERROR_UNSUPPORTED; }
    virtual const char * getAudioConnectionId() const { return ""; }
    virtual mm_status_t setVolume(const float left, const float right) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t getVolume(float& left, float& right) const{ return MM_ERROR_UNSUPPORTED; }
    ComponentSP createComponentHelper(const char* componentName, const char* mimeType, bool isEncoder=false);
    static ComponentSP createComponentHelper(const char* componentName, const char* mimeType, Component::ListenerSP listener, bool isEncoder=true);

    static void printMsgInfo(int event, int param1, const char* _sender=NULL);

    static std::string getSourceUri(const char * uri, bool isAudio = false);
    virtual ~Pipeline();
    virtual mm_status_t init();
    virtual mm_status_t uninit();

    virtual mm_status_t pushData(MediaBufferSP & buffer) { return MM_ERROR_UNSUPPORTED; }
    virtual mm_status_t enableExternalSubtitleSupport(bool enable) { return MM_ERROR_UNSUPPORTED; }

  protected:

     //prepareInternal should implemented by each derived class
     virtual mm_status_t prepareInternal() { return MM_ERROR_UNSUPPORTED; }

     //stopInternal can be implemented by each derived class if needed
     virtual mm_status_t stopInternal(){return MM_ERROR_SUCCESS;};

     //resetInternal can be implemented by each derived class if needed
     virtual mm_status_t resetInternal(){return MM_ERROR_SUCCESS;};

     virtual mm_status_t notify(int msg, int param1, int param2, const MMParamSP obj);

     void dump();

  // FIXME: private
  protected:

    #define PL_MSG_componentMessage (MMMsgThread::msg_type)1
    #define PL_MSG_cowMessage (MMMsgThread::msg_type)2
    class ComponentInfo
    {
      public:
        enum ComponentType {
            kComponentTypeSource,
            kComponentTypeFilter,
            kComponentTypeSink
        };
        explicit ComponentInfo(ComponentSP& comp, ComponentType type)
          : component(comp)
          , state(kComponentStateNull)
          , state2(kComponentStateInvalid)
          , mType(type) {
        }
        ComponentSP component;
        ComponentStateType state, state2;
        ComponentType mType;
      private:
        ComponentInfo();
    };

    typedef std::vector<ComponentInfo> ComponentsInfos;
    ComponentsInfos mComponents;

    struct TrackInfo {
        uint32_t id;
        CowCodecID codecId;
        std::string codecName;
        std::string mime;
        std::string title;
        std::string language;
    };
    typedef std::vector<TrackInfo> StreamInfo;

    Component::ListenerSP mListenerReceive;
    ListenerSP mListenerSend;

    mutable Lock mLock;
    Condition mCondition;

    bool mEscapeWait; // escape conditionly wait upon new command (stop/flush/reset etc)
    uint32_t mErrorCode; // setup the error code from msg handler thread

    static const char*sInternalStateStr[];

    Pipeline();
    virtual mm_status_t updateTrackInfo() { return MM_ERROR_UNSUPPORTED; }

    enum UriType {
        kUriDefault,
        kUriRtpIF,
        kUriMPD,
        kUriExternal
    };

    virtual mm_status_t loadPreparation(UriType type) { return MM_ERROR_UNSUPPORTED; }
    virtual void setState(ComponentStateType& state, ComponentStateType s) {
        MMAutoLock locker(mLock);
        state = s;
    }

    virtual void setMemberUint32(uint32_t& member, uint32_t val) {
        //MMAutoLock locker(mLock);//no need to lock here?
        member = val;
    }

    template <typename T>
    mm_status_t waitUntilCondition(T& value, T targetValue, bool pipelineState = true, bool state2 = false, int64_t timeoutInUs=30000000000)
    {
        mm_status_t status = MM_ERROR_SUCCESS;
        ElapsedTimer timer;
        const int64_t tolerance = 5000;     // 5 ms

        // setMemberUint32(mErrorCode, 0);
        timer.start(timeoutInUs-tolerance);

        while (1) {
            MMAutoLock locker(mLock);
            if (value == targetValue) {
                mm_log(MM_LOG_INFO, "COW-PLB", "[MM] mState is already %s\n", sInternalStateStr[value]);
                break;
            }

            if (pipelineState) {
                bool isAllStateChanged = true;
                if (state2) {
                    for (uint32_t i = 0; i < mComponents.size(); i++) {
                        if (mComponents[i].state2 != targetValue) {
                            isAllStateChanged = false;
                            mm_log(MM_LOG_INFO, "COW-PLB", "[MM] %s target state2 is %s, cur state2 is %s\n",
                                mComponents[i].component->name(),
                                sInternalStateStr[targetValue],
                                sInternalStateStr[mComponents[i].state2]);
                            break;
                        }
                    }
                } else {
                    for (uint32_t i = 0; i < mComponents.size(); i++) {
                        if (mComponents[i].state != targetValue) {
                            isAllStateChanged = false;
                            mm_log(MM_LOG_INFO, "COW-PLB", "[MM] %s target state is %s, cur state is %s\n",
                                mComponents[i].component->name(),
                                sInternalStateStr[targetValue],
                                sInternalStateStr[mComponents[i].state]);
                            break;
                        }
                    }
                }
                if (isAllStateChanged) {
                    value = targetValue;
                    mm_log(MM_LOG_INFO, "COW-PLB", "[MM] all components is aleady %s\n", sInternalStateStr[targetValue]);
                    break;
                }
            }

            if (mEscapeWait) {
                mm_log(MM_LOG_WARN, "COW-PLB", "[MM] state change to %s isn't complete, interrupted by client\n", sInternalStateStr[targetValue]);
                status = MM_ERROR_INTERRUPTED;
                break;
            }
            if (timer.isExpired()) {
                // ERROR("pipeline state change timeout, some component fail\n");
                mm_log(MM_LOG_ERROR, "COW-PLB", "[MM] pipeline state change timeout, some component fail\n");
                status = MM_ERROR_TIMED_OUT;
                break;
            }

            mCondition.timedWait(timer.remaining());
        }


        // FIXME, set state to invalid?
        if (status == MM_ERROR_SUCCESS && mErrorCode) {
            mm_log(MM_LOG_ERROR, "COW-PLB", "[MM] state change failed mErrorCode = %d status = %d\n", mErrorCode, status);
            status = mErrorCode;
        }

        setMemberUint32(mErrorCode, 0);

        return status;
    }


    MM_DISALLOW_COPY(Pipeline)

    DECLARE_MSG_LOOP_PURE_VIRTUAL()
    DECLARE_MSG_HANDLER2_PURE_VIRTUAL(onComponentMessage)
    DECLARE_MSG_HANDLER2_PURE_VIRTUAL(onCowMessage)

}; // Pipeline

#define CHECK_PIPELINE_STATE(TARGET_STATE, NOTIFY_EVENT) do{                      \
    MMAutoLock locker(mLock);                                                     \
    if (mState == TARGET_STATE) {                                                 \
        INFO("pipeline is already in state %s", sInternalStateStr[TARGET_STATE]); \
        notify(NOTIFY_EVENT, MM_ERROR_SUCCESS, 0, nilParam);                      \
        return MM_ERROR_SUCCESS;                                                  \
    }                                                                             \
}while(0)


#define SET_PIPELINE_STATE(RET_VALUE, OP_FUNC, STATE_ASYNC, TARGET_STATE, NOTIFY_EVENT) do { \
    uint32_t i=0;                                                                            \
    for (i=0; i<mComponents.size(); i++) {                                                   \
        if (mComponents[i].state == TARGET_STATE)                                            \
            continue;                                                                        \
        setState(mComponents[i].state, STATE_ASYNC);                                         \
        mm_status_t ret = mComponents[i].component->OP_FUNC();                               \
                                                                                             \
        if ( ret == MM_ERROR_SUCCESS)                                                        \
            setState(mComponents[i].state, TARGET_STATE);                                    \
        else if (ret == MM_ERROR_ASYNC) {                                                    \
            /* to avoid race condition, we init component state to */                        \
            /* STATE_ASYNC before call ->OP_FUNC() */                                        \
        } else {                                                                             \
            ERROR("component %s status change failed", mComponents[i].component->name());    \
            setState(mComponents[i].state, TARGET_STATE);                                    \
            setState(mState, TARGET_STATE);                                                  \
            notify(NOTIFY_EVENT, MM_ERROR_OP_FAILED, 0, nilParam);                           \
            return ret;                                                                      \
        }                                                                                    \
        INFO("%s %s: %s", mComponents[i].component->name(),                                  \
            ret == MM_ERROR_SUCCESS ? "reached internal state" :                             \
            "async to internal state", sInternalStateStr[TARGET_STATE]);                     \
                                                                                             \
        if (ret == MM_ERROR_ASYNC)                                                           \
            RET_VALUE = MM_ERROR_ASYNC;                                                      \
    }                                                                                        \
                                                                                             \
    if (RET_VALUE == MM_ERROR_ASYNC) {                                                       \
        RET_VALUE = waitUntilCondition(mState, TARGET_STATE);                                \
    } else if (RET_VALUE == MM_ERROR_SUCCESS) {                                              \
        INFO("pipeline reach state: %s", sInternalStateStr[TARGET_STATE]);                   \
        setState(mState, TARGET_STATE);                                                      \
        notify(NOTIFY_EVENT, MM_ERROR_SUCCESS, 0, nilParam);                                 \
    } else if (RET_VALUE == MM_ERROR_INTERRUPTED) {                                          \
        notify(NOTIFY_EVENT, MM_ERROR_INTERRUPTED, 0, nilParam);                             \
    }                                                                                        \
}while (0)

// If RET_VALUE is not MM_ERROR_ASYNC/MM_ERROR_SUCCESS/MM_ERROR_INTERRUPTED, consider in future


} // YUNOS_MM

#endif // pipeline_h

