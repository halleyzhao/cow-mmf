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

#include <multimedia/mm_cpp_utils.h>
#include <string/String.h>

#ifndef __media_client_helper_h
#define __media_client_helper_h

namespace YUNOS_MM {

using namespace yunos;

class MediaServiceLooper;
class MediaPlayerClient;
class MediaRecorderClient;

class MediaClientHelper {

public:
    static bool connect(int playType,
                        const char *mediaName,
                        String &serviceName,
                        String &objectPath,
                        String &iface);

    static void disconnect(const char* mediaName,
                           MediaServiceLooper *client);

    static MediaPlayerClient* getMediaPlayerClient(
                                  const String &serviceName,
                                  const String &objectPath,
                                  const String &iface);

    static MediaRecorderClient* getMediaRecorderClient(
                                    const String &serviceName,
                                    const String &objectPath,
                                    const String &iface);

    static String dumpsys();
private:
    static bool connect_l(int playType,
                        const char *mediaName,
                        String &serviceName,
                        String &objectPath,
                        String &iface);

    static void disconnect_l(const char* mediaName,
                           MediaServiceLooper *client);

    static String dumpsys_l();
private:
    static const char * MM_LOG_TAG;
};

} // end of YUNOS_MM
#endif
