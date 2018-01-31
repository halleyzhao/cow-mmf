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

#ifndef __UBUS_HELPER_H__
#define __UBUS_HELPER_H__

#include <dbus/DError.h>

namespace YUNOS_MM {
#define UBUS_TIME_OUT -1

/*
 * handle obtain message
 */

#define CHECK_OBTAIN_MSG_RETURN1(METHOD, INIT_VALUE)                                        \
    yunos::SharedPtr<yunos::DMessage> msg = obtainMethodCallMessage(yunos::String(METHOD)); \
    if (msg == NULL) {                                                                      \
        ERROR("msg is null");                                                               \
        return INIT_VALUE;                                                                  \
    }

#define CHECK_OBTAIN_MSG_RETURN0(METHOD)                                                    \
    yunos::SharedPtr<yunos::DMessage> msg = obtainMethodCallMessage(yunos::String(METHOD)); \
    if (msg == NULL) {                                                                      \
        ERROR("msg is null");                                                               \
        return;                                                                             \
    }


/*
 * handle reply message
 */

#define HANDLE_REPLY_MSG_RETURN0() do {                                                               \
    DError err;                                                                                       \
    yunos::SharedPtr<yunos::DMessage> reply = sendMessageWithReplyBlocking(msg, UBUS_TIME_OUT, &err); \
    if (!reply) {                                                                                     \
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());   \
        if (err.type() == DError::ErrorType::BUS_ERROR_ACCESS_DENIED) {                               \
            ERROR("permission is not allowed");                                                       \
            return;                                                                                   \
        }                                                                                             \
    }                                                                                                 \
} while(0)

#define HANDLE_INVALID_REPLY_RETURN1(INIT_VALUE)                                                      \
    DError err;                                                                                       \
    yunos::SharedPtr<yunos::DMessage> reply = sendMessageWithReplyBlocking(msg, UBUS_TIME_OUT, &err); \
    if (!reply) {                                                                                     \
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());   \
        return INIT_VALUE;                                                                            \
    }

// spcific for int, because permission result should return to client
#define HANDLE_INVALID_REPLY_RETURN_INT()                                                             \
    DError err;                                                                                       \
    yunos::SharedPtr<yunos::DMessage> reply = sendMessageWithReplyBlocking(msg, mTimeOut, &err); \
    if (!reply) {                                                                                     \
        ERROR("DError(%d): %s, detail: %s", err.type(), err.name().c_str(), err.message().c_str());   \
        if (err.type() == DError::ErrorType::BUS_ERROR_ACCESS_DENIED) {                               \
            ERROR("permission is not allowed");                                                       \
            return MM_ERROR_PERMISSION_DENIED;                                                        \
        } else {                                                                                      \
            return MM_ERROR_UNKNOWN;                                                                  \
        }                                                                                             \
    }


#define CHECK_REPLY_MSG_RETURN_INT() do {              \
    HANDLE_INVALID_REPLY_RETURN_INT();                 \
    int result = reply->readInt32();                   \
    VERBOSE("ret: %d", result);                        \
    return (mm_status_t)result;                        \
} while(0)


#define CHECK_REPLY_MSG_RETURN_BOOL() do {             \
    HANDLE_INVALID_REPLY_RETURN1(false);               \
    bool result = reply->readBool();                   \
    VERBOSE("ret: %d", result);                        \
    return result;                                     \
} while(0)

#define CHECK_REPLY_MSG_RETURN_STRING(INIT_VALUE) do { \
    HANDLE_INVALID_REPLY_RETURN1(INIT_VALUE);          \
    std::string result = reply->readString().c_str();  \
    VERBOSE("ret: %s", result.c_str());                  \
    return result;                                     \
} while(0)


// specific for string
#define SEND_UBUS_MESSAGE_PARAM0_RETURNSTR(METHOD)  do{              \
    std::string str;                                                 \
    CHECK_OBTAIN_MSG_RETURN1(METHOD, str);                           \
    HANDLE_INVALID_REPLY_RETURN1(str);                               \
    str = reply->readString().c_str();                               \
    return str;                                                      \
} while(0)


#define SEND_UBUS_MESSAGE_PARAM0_RETURNOBJ(METHOD, OBJECT_TYPE)  do{ \
    OBJECT_TYPE##SP object;                                          \
    CHECK_OBTAIN_MSG_RETURN1(METHOD, object);                        \
    HANDLE_INVALID_REPLY_RETURN1(object);                            \
    int32_t result = reply->readInt32();                             \
    if (result == MM_ERROR_SUCCESS) {                                \
        object = OBJECT_TYPE::create();                              \
        object->readFromMsg(reply);                                  \
    }                                                                \
    VERBOSE("ret: %d", result);                                      \
    return object;                                                   \
} while(0)



#define SEND_SIGNAL_MESSAGE_PARAMOBJ_RETURN0(CALLBACKNAME, NODE, MSGTYPE, OBJ) do {       \
    SharedPtr<DMessage> signal = NODE->obtainSignalMessage(CALLBACKNAME);                 \
    if (!signal) {                                                                        \
        ERROR("fail to create signal for callback, name %s", CALLBACKNAME.c_str());       \
        return;                                                                           \
    }                                                                                     \
    signal->writeInt32(MSGTYPE);                                                          \
    OBJ->writeToMsg(signal);                                                              \
    NODE->sendMessage(signal);                                                            \
}while(0)

#define SEND_SIGNAL_MESSAGE_PARAM0_RETURN0(CALLBACKNAME, NODE, MSGTYPE) do {              \
    SharedPtr<DMessage> signal = NODE->obtainSignalMessage(CALLBACKNAME);                 \
    if (!signal) {                                                                        \
        ERROR("fail to create signal for callback, name %s", CALLBACKNAME.c_str());       \
        return;                                                                           \
    }                                                                                     \
    signal->writeInt32(MSGTYPE);                                                          \
    NODE->sendMessage(signal);                                                            \
}while(0)

#define SEND_SIGNAL_MESSAGE_PARAM1_RETURN0(CALLBACKNAME, NODE, MSGTYPE, TYPE, value) do { \
    SharedPtr<DMessage> signal = NODE->obtainSignalMessage(CALLBACKNAME);                 \
    if (!signal) {                                                                        \
        ERROR("fail to create signal for callback, name %s", CALLBACKNAME.c_str());       \
        return;                                                                           \
    }                                                                                     \
    signal->writeInt32(MSGTYPE);                                                          \
    signal->write##TYPE(value);                                                           \
    NODE->sendMessage(signal);                                                            \
}while(0)

}

#endif //__UBUS_HELPER_H__
