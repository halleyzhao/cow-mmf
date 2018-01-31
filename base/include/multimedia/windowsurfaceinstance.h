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

#ifndef __windowsurfaceinstance_H
#define __windowsurfaceinstance_H



#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mmlistener.h>
#include <WindowSurface.h>
#include "WindowSurfaceTestWindow.h"
#define WindowSurface Surface

namespace YUNOS_MM {


class WindowSurfaceInstance {
public:
    WindowSurfaceInstance();
    virtual ~WindowSurfaceInstance();
    void create(uint32_t width = 1280, uint32_t height = 720);
    void destroy();
    void setOffset(int x, int y);
    void setSurfaceResize(int width, int height);
    WindowSurface * get();

protected:
    Surface* mSurfaceInstance;

    MM_DISALLOW_COPY(WindowSurfaceInstance)
    DECLARE_LOGTAG()
};

}

#endif /* __windowsurfaceinstance_H */

