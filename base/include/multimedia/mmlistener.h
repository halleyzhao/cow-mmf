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

#ifndef __mmlistener_H
#define __mmlistener_H

#include <multimedia/mm_cpp_utils.h>
#include <multimedia/mmparam.h>

namespace YUNOS_MM {

class MMListener {
public:
    MMListener(){}
    virtual ~MMListener(){}

public:
    virtual void onMessage(int msg, int param1, int param2, const MMParam *obj) = 0;

    MM_DISALLOW_COPY(MMListener)
};

}

#endif // __mmlistener_H
