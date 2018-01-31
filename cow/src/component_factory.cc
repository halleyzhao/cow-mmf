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

#include <string.h>
#include <string>
#include <dlfcn.h>
#include <vector>
#include <algorithm>


#include <multimedia/component.h>
#include <multimedia/mm_errors.h>
#include <multimedia/media_attr_str.h>
#include <multimedia/mm_debug.h>

#include <multimedia/component_factory.h>
#include "cow_xml.h"


namespace YUNOS_MM {

MM_LOG_DEFINE_MODULE_NAME("cowfactory")

ComponentFactory::MComponentMap ComponentFactory::mComponentMap;

Lock ComponentFactory::mLock;

static const char* gXmlFileList[] = {
    "cow_plugins.xml",
#ifdef __MM_NATIVE_BUILD__
    "cow_plugins_ubt.xml",
#else
    "cow_plugins_vendor.xml"
#endif
};

static std::vector<ComNameLibName> kComponentsNameToLibMap;
static std::vector<MimeTypeComName> kMimeTypeToComName;

// C++ issue, it can't be declared inside ::create when it is used for STL
struct ComWithPriority {
    ComWithPriority(const char *name, int32_t pri)
        : comName(name)
        , priority(pri)
        {}
    ComWithPriority() : comName(NULL), priority(LOW_PRIORITY-1) {}

    void operator()(ComWithPriority &com) {
        DEBUG("comName:%s, priority:%d\n", PRINTABLE_STR(com.comName), com.priority);
    }

    bool operator < (const ComWithPriority &m)const {
        return priority > m.priority;
    }

    const char *comName;
    int32_t priority;
};

/*static*/
ComponentSP ComponentFactory::loadComponent_l(const char* libNameWithPath, const char* mimeType, bool isEncoder)
{
    void *libHandle = NULL;
    ComponentInfo::CreateComponentFunc create = NULL;

    INFO("[%s]: mComponentMap.size(): %d, libNameWithPath: %s, isEncoder: %d",
        PRINTABLE_STR(mimeType), mComponentMap.size(), PRINTABLE_STR(libNameWithPath), isEncoder);

    ComponentInfoSP info;
    MComponentMap::iterator ite = mComponentMap.find(std::string(libNameWithPath));
    if ( ite == mComponentMap.end() ) {

        libHandle = dlopen(libNameWithPath, RTLD_NOW);
        if (libHandle == NULL) {
            ERROR("unable to dlopen %s, error: %s", libNameWithPath, dlerror());
            return ComponentSP((Component*)NULL);
        }

        //Use command "readelf -s -W libXXX.so" to find the right symbols
        create = (ComponentInfo::CreateComponentFunc)dlsym(libHandle, "createComponent");
        if (create == NULL) {
            create = (ComponentInfo::CreateComponentFunc)dlsym(libHandle, "_Z15createComponentPKcb");
        }

        ComponentInfo::ReleaseComponentFunc release =
            (ComponentInfo::ReleaseComponentFunc)dlsym(libHandle, "releaseComponent");
        if (release == NULL) {
            release =
                (ComponentInfo::ReleaseComponentFunc)dlsym(libHandle, "_Z16releaseComponentPN8YUNOS_MM9ComponentE");
        }

        if (create == NULL || release == NULL) {
            dlclose(libHandle);
            libHandle = NULL;
            ERROR("load create OR release method failed, error %s", dlerror());
            return ComponentSP((Component*)NULL);
        }

        info.reset(new ComponentInfo);
        //skip libMediaCodecComponent.so
        if (!info && strstr(libNameWithPath, "MediaCodecComponent") == NULL) {
            ERROR("no memory, size %zu", sizeof(ComponentInfo));
            dlclose(libHandle);
            libHandle = NULL;
            return ComponentSP((Component*)NULL);
        }

        info->mCreate = create;
        info->mRelease = release;
        info->mLibHandle = libHandle;
        info->mLibComName = libNameWithPath;
        mComponentMap.insert(MComponentMapPair(std::string(libNameWithPath), info));

        INFO("[%s]: open %s success, handle %p", mimeType, libNameWithPath, libHandle);
    }else {
        INFO("%s is already opened, components.size: %zu",
            ite->first.c_str(), ite->second->mComponent.size());
        info = ite->second;
    }

    Component* com = NULL;
    create = info->mCreate;
    com = (*create)(mimeType, isEncoder);
    if (!com) {
        //skip libMediaCodecComponent.so
        if(libHandle && strstr(libNameWithPath, "MediaCodecComponent") == NULL){
            dlclose(libHandle);
            libHandle = NULL;
        }
        ERROR("com create method failed");
        return ComponentSP((Component*)NULL);
    }

    mm_status_t status = com->init();
    VERBOSE("%s's version:%s\n",libNameWithPath,com->version());
    if (status != MM_ERROR_SUCCESS) {
        ERROR("com %s init failed %d", com->name(), status);
        delete com;
        return ComponentSP((Component*)NULL);
    }

    if (com != NULL) {
        info->mComponent.push_back(com);
        INFO("%p added to %s,  mComponent.size %zu",
            com, libNameWithPath, info->mComponent.size());
    }

    ComponentSP componentSP(com, &ComponentFactory::release);
    return componentSP;
}

/*static*/
bool ComponentFactory::loadXmlFile_l(const char* xmlFile)
{
    if (!xmlFile)
        return false;

    CowComponentXMLSP cowCompXML= CowComponentXML::create(xmlFile);
     if(!cowCompXML){
        ERROR("factory doesn't get the xml instance\n");
        return false;
    }

    INFO("now begin to load xml file: %s", xmlFile);
    std::vector<ComNameLibName> comNameToLibMap;
    std::vector<MimeTypeComName>   mimeToComName;
    comNameToLibMap = cowCompXML->getComNameLibNameTable();
    mimeToComName = cowCompXML->getMimeTypeComNameTable();
    kComponentsNameToLibMap.insert(kComponentsNameToLibMap.end(), comNameToLibMap.begin(), comNameToLibMap.end());
    if (kMimeTypeToComName.empty()) {
        kMimeTypeToComName.insert(kMimeTypeToComName.end(), mimeToComName.begin(), mimeToComName.end());
    } else { // the platform cow_plugins_xxx.xml override (the priority in) default cow_plugins.xml
        uint32_t ii, jj;
        for (ii=0; ii<mimeToComName.size(); ii++) {
            bool override = false;
            VERBOSE("####[%s]: mimeToComName[%d], mComName: %s, mCap: %d, mPriority: %d",
                mimeToComName[ii].mMimeType.c_str(), ii, mimeToComName[ii].mComName.c_str(), mimeToComName[ii].mCap, mimeToComName[ii].mPriority);
            for (jj = 0; jj <kMimeTypeToComName.size(); jj++) {
                VERBOSE("---- [%s]: kMimeTypeToComName[%d], mComName: %s, mCap: %d, mPriority: %d",
                    kMimeTypeToComName[jj].mMimeType.c_str(), jj, kMimeTypeToComName[jj].mComName.c_str(), kMimeTypeToComName[jj].mCap, kMimeTypeToComName[jj].mPriority);
                if (kMimeTypeToComName[jj].mComName == mimeToComName[ii].mComName &&
                    kMimeTypeToComName[jj].mMimeType == mimeToComName[ii].mMimeType &&
                    kMimeTypeToComName[jj].mCap == mimeToComName[ii].mCap) {
                    DEBUG("[%s]: override (%s, %d) priority from %d to %d",
                        kMimeTypeToComName[jj].mMimeType.c_str(), kMimeTypeToComName[jj].mComName.c_str(), kMimeTypeToComName[jj].mCap, kMimeTypeToComName[jj].mPriority,  mimeToComName[ii].mPriority);
                    kMimeTypeToComName[jj].mPriority = mimeToComName[ii].mPriority;
                    override = true;
                }
            }
            if (!override) {
                kMimeTypeToComName.push_back(mimeToComName[ii]);
            }
        }
    }

    INFO("parse xml file result:%zu,%zu\n", kComponentsNameToLibMap.size(), kMimeTypeToComName.size());
    return true;
}

/*static*/
bool ComponentFactory::loadDefaultXmlIfNeeded_l()
{
    uint32_t i=0;

    if(kComponentsNameToLibMap.size() && kMimeTypeToComName.size())
        return true;

    // parse xml file
    // FIXME, when there is additional xml from user, update here
    std::string xmlFilePath = mm_get_env_str(NULL, "COW_XML_PATH");
    if (xmlFilePath.empty())
        xmlFilePath = _COW_XML_PATH;
    for (i=0; i<sizeof(gXmlFileList)/sizeof(gXmlFileList[0]); i++) {
        std::string xmlFileName =  xmlFilePath;
        xmlFileName.append("/");
        xmlFileName.append(gXmlFileList[i]);

        if (!loadXmlFile_l(xmlFileName.c_str())) {
            WARNING("fail to load %s", xmlFileName.c_str());
            if (i == 0)
                return false;
        }
    }

    return true;
}

/*static */bool ComponentFactory::appendPluginsXml(const char* xmlFile) {
    MMAutoLock locker(mLock);
    return appendPluginsXml_l(xmlFile);
}


/*static */bool ComponentFactory::appendPluginsXml_l(const char* xmlFile) {
    if (!xmlFile)
        return false;

    // make sure to load default xml first
    loadDefaultXmlIfNeeded_l();
    if (!loadXmlFile_l(xmlFile))
        return false;

    return true;
}

/*static*/ComponentSP ComponentFactory::create(const char* componentName,
    const char* mimeType, bool isEncoder){

    INFO("[%s]: componentName:%s, isEncoder:%d", mimeType, componentName, isEncoder);
    MMAutoLock locker(mLock);
    uint32_t i=0, j=0;

    if (componentName == NULL && mimeType == NULL) {
        ERROR("invalid param");
        return ComponentSP((Component*)NULL);
    }

    if (!loadDefaultXmlIfNeeded_l()) {
        ERROR("fail to load plugins xml file");
        return ComponentSP((Component*)NULL);
    }

    // filter suitable components to a vector, sort by priority
    int32_t cap = isEncoder ? XML_COMP_ENCODER : XML_COMP_DECODER;
    std::vector<ComWithPriority>comNames;

    if (componentName) {
        comNames.push_back(ComWithPriority(componentName, DEFAULT_PRIORITY));
    } else {
        ASSERT(mimeType);

        for (i = 0; i < kMimeTypeToComName.size(); ++i) {
            if (strcmp(mimeType, kMimeTypeToComName[i].mMimeType.c_str())){
                continue;
            }
            if ((kMimeTypeToComName[i].mCap == XML_COMP_GENERIC) || (kMimeTypeToComName[i].mCap & cap)){
                if (kMimeTypeToComName[i].mPriority >= 0 ) {
                    comNames.push_back(ComWithPriority(kMimeTypeToComName[i].mComName.c_str(),
                        kMimeTypeToComName[i].mPriority));
                }
            }
        }

        std::sort(comNames.begin(), comNames.end());
    }

    DEBUG("find %d components\n", comNames.size());
    for_each(comNames.begin(), comNames.end(), ComWithPriority());

    // try to create suitable com in priority order
    for (j=0; j< comNames.size(); j++) {
        for (i = 0; i < kComponentsNameToLibMap.size(); ++i) {
            if (strcmp(comNames[j].comName, kComponentsNameToLibMap[i].mComName.c_str())) {
                continue;
            }

            ComponentSP com = loadComponent_l(kComponentsNameToLibMap[i].mLibName.c_str(), mimeType, isEncoder);
            if (com) {
                INFO("use %s com finally\n", comNames[j].comName);
                return com;
            } else
                break;
        }
    }

    ERROR("[%s]: fail to find com for (componentName: %s)",
        PRINTABLE_STR(mimeType), PRINTABLE_STR(componentName));
    return ComponentSP((Component*)NULL);
}

/*static*/void ComponentFactory::release(Component * com){
    MMAutoLock locker(mLock);

    if (com == NULL) {
        WARNING("invalid param\n");
        return;
    }

    MComponentMap::iterator itMap;
    std::list<Component *>::iterator itList;
    bool isFind = false;
    for (itMap = mComponentMap.begin(); itMap != mComponentMap.end(); itMap++) {
        std::list<Component *> &list = itMap->second->mComponent;
        for( itList = list.begin(); itList != list.end(); itList++) {
            if (*itList == com) {
                list.erase(itList); //itList will be invalid after erase
                DEBUG("after erase, %s has %d clients", itMap->first.c_str(), list.size());
                isFind = true;
                break;
            }
        }

        if (isFind) {
            if (itMap->second->mRelease) {
                com->uninit();
                (*(itMap->second->mRelease))(com);
                INFO("com %p, %s uninit and released", com, itMap->first.c_str());
            }
            if (list.empty()) {
                if (strstr(itMap->first.c_str(), "MediaCodecComponent") == NULL) {
                    DEBUG("close %s begin, handle %p", itMap->first.c_str(), itMap->second->mLibHandle);
                    dlclose(itMap->second->mLibHandle);
                    INFO("close %s end", itMap->first.c_str());
                    mComponentMap.erase(itMap);
                }
            }
            break;
        }
    }

    return;
}

}
