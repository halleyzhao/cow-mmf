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

#ifndef component_factory_h
#define component_factory_h

#include <map>
#include <list>

#include <multimedia/mm_types.h>
#include <multimedia/mm_errors.h>
#include <multimedia/mm_cpp_utils.h>

namespace YUNOS_MM {

#define DISABLE_PRIORITY    -512
#define LOW_PRIORITY        0
#define DEFAULT_PRIORITY    256
#define HIGH_PRIORITY      512
#ifdef __MM_NATIVE_BUILD__
#define NATIVE_PRIORITY     HIGH_PRIORITY
#else
#define NATIVE_PRIORITY     LOW_PRIORITY
#endif
#define XML_COMP_DECODER    0x1
#define XML_COMP_ENCODER    0x2
#define XML_COMP_CODECS     (XML_COMP_ENCODER|XML_COMP_DECODER)
#define XML_COMP_GENERIC    0x4

class Component;
typedef MMSharedPtr<Component> ComponentSP;

class ComponentFactory {

public:
    static ComponentSP create(const char* componentName, const char* mimeType, bool isEncoder);
    static bool appendPluginsXml(const char* xmlFile);

private:
    struct ComponentInfo {
        typedef Component *(*CreateComponentFunc)(const char *, bool);
        typedef void (*ReleaseComponentFunc)(Component *);

        CreateComponentFunc mCreate;
        ReleaseComponentFunc mRelease;
        void *mLibHandle;
        const char* mLibComName;
        std::list<Component *> mComponent;
    };

    ComponentFactory() {}
    virtual ~ComponentFactory(){}

    static void release(Component * component);

    static ComponentSP loadComponent_l(const char* libComponentName, const char* mimeType, bool isEncoder);
    static bool appendPluginsXml_l(const char* xmlFile);
    static bool loadXmlFile_l(const char* xmlFile);
    static bool loadDefaultXmlIfNeeded_l();

private:
    typedef MMSharedPtr<ComponentInfo> ComponentInfoSP;

    typedef std::map<std::string, ComponentInfoSP> MComponentMap;
    typedef std::pair<std::string, ComponentInfoSP> MComponentMapPair;

    static MComponentMap mComponentMap;
    static Lock mLock;

    MM_DISALLOW_COPY(ComponentFactory);
};

}

#endif // component_factory_h

