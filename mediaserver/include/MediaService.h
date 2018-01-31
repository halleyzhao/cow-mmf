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

#include <multimedia/mmthread.h>

#ifndef __media_service_h
#define __media_service_h

namespace YUNOS_MM {

// TODO to inherit from MeidaServiceLooper instead of MMThread
class MediaService : public MMThread {

public:
    static bool createService(bool blocked = false);

protected:
    virtual void main();

    MediaService();
    virtual ~MediaService();

private:
    static const char * MM_LOG_TAG;

private:
    MediaService(const MediaService &);
    MediaService & operator=(const MediaService &);

};

}
#endif
