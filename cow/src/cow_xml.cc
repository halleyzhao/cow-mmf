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
#include "expat.h"
#include <string>
#include "cow_xml.h"
#include <multimedia/component_factory.h>

#include <multimedia/mm_debug.h>

MM_LOG_DEFINE_MODULE_NAME("COWXML");

namespace YUNOS_MM {

const char * xmlPath = "Path";
const char * xmlCompLibPath = "libpath";
const char * xmlComponents = "Components";
const char * xmlComponent = "Component";
const char * xmlCompLibName = "libComponentName";
const char * xmlCompName = "ComponentName";
const char * xmlMime = "mime";
const char * xmlMimeType = "MimeType";
const char * xmlPriority = "Priority";
const char * xmlPriorityDisable = "disable";
const char * xmlPriorityLow = "low";
const char * xmlPriorityNormal = "normal";
const char * xmlPriorityHigh = "high";
const char * xmlCap = "Cap";
const char * xmlDecoder = "decoder";
const char * xmlEncoder = "encoder";
const char * xmlEnAndDecoder = "codec";
const char * xmlGeneric = "generic";

/*static*/  CowComponentXMLSP CowComponentXML::create(std::string fileName)
{
    //#0 new a XML file
    CowComponentXML *pXML = new CowComponentXML();
    if (!pXML){
       ERROR("new CowComponentXML() is error\n");
       return CowComponentXMLSP((CowComponentXML*)NULL);
    }
    if(pXML->Init(fileName)==false){
       ERROR("initiate the xml file is error\n");
       delete pXML;
       return CowComponentXMLSP((CowComponentXML*)NULL);
    }

    //#1 set the shared pointer
    CowComponentXMLSP pCCowCompXMLSP(pXML,&CowComponentXML::Release);
    return pCCowCompXMLSP;
}

CowComponentXML::CowComponentXML()
                  :mbInited(false)
{
    INFO("now start to parse cow xml file\n");
    return;
}

CowComponentXML::~CowComponentXML()
{
    unInit();
}

bool CowComponentXML::Init(std::string fileName)
{
    FILE    *file = NULL;
    if(true == mbInited){
        INFO("CAUTION:cow xml has been initiated\n");
        return true;
    }

    //#1 open the xml file for parsing
    INFO("the cow xml file:%s\n",fileName.c_str());
    if (fileName.empty())
        return false;

    file = fopen(fileName.c_str(),"r");
    if (file == NULL) {
        ERROR("unable to open cow plugins xml file:%s\n", fileName.c_str());
        return false;
    }

    if (parseXMLFile(file) == false){
        ERROR("unable to parse the xml file\n");
        fclose(file);
        return false;
    }

    fclose(file);
    file = NULL;

    mbInited = true;
    return true;
}

void CowComponentXML::unInit()
{
    if(false == mbInited){
        INFO("cow xml hasn't been initiated\n");
        return ;
    }

    mVComNameLibName.clear();
    mVMimeTypeComName.clear();
    mNodeStack.clear();

    mbInited = false;
    return;
}

bool CowComponentXML::parseXMLFile(FILE *file)
{
    int InitCheck = 0;

    XML_Parser parser = ::XML_ParserCreate(NULL);
    if (parser == NULL) {
        ERROR("fail to create XML parser");
        return false;
    }

    ::XML_SetUserData(parser, this);
    ::XML_SetElementHandler(parser,
        StartElementHandlerWrapper, EndElementHandlerWrapper);

    const int BUFF_SIZE = 512;
    while (InitCheck == 0) {
        void *buff = ::XML_GetBuffer(parser, BUFF_SIZE);
        if (buff == NULL) {
            ERROR("failed in call to XML_GetBuffer()");
            InitCheck = -1;
            break;
        }

        int bytes_read = ::fread(buff, 1, BUFF_SIZE, file);
        if (bytes_read < 0) {
            ERROR("failed in call to fread");
            InitCheck = -1;
            break;
        }

        int ret = ::XML_ParseBuffer(parser, bytes_read, bytes_read == 0);
        if (ret == 0) {//OK==1,ERROR==0
            InitCheck = -1;
            break;
        }

        if (bytes_read == 0) {
            break;
        }
    }

    ::XML_ParserFree(parser);

    return true;
}

// static
void CowComponentXML::StartElementHandlerWrapper(void *me, const char *name, const char **attrs)
{
    static_cast<CowComponentXML *>(me)->startElementHandler(name, attrs);
}

// static
void CowComponentXML::EndElementHandlerWrapper(void *me, const char *name)
{
    static_cast<CowComponentXML *>(me)->endElementHandler(name);
}

void CowComponentXML::startElementHandler(const char *name, const char **attrs)
{
    //#0 check it with "Components"
    if (!strcmp(name, xmlComponents)){
        if (!mNodeStack.empty()){
            ERROR("the cow plugins xml file has error format(Components)\n");
            return;
        }
        //#0.0 push the components into stack
        std::string StackName = xmlComponents;
        mNodeStack.push_back(StackName);
        return;
    }

    //check it with "libpath".
    // it is the default 'libpath' of this xml, each Component can override it
    if (!strcmp(name, xmlPath)){
        DEBUG("libpath: (%s, %s)", attrs[0], attrs[1]);
        if (!strcmp(attrs[0], xmlCompLibPath) && attrs[1]) {
            mLibPath = attrs[1];
        }
        return;
    }

    //#1 check it with "Component"
    if (!strcmp(name, xmlComponent)){
        if (mNodeStack.empty()){
            ERROR("the cow plugins xml file has error format(Component)\n");
            return;
        }
        if (mNodeStack[0] != xmlComponents)
        {
            ERROR("find the xml file header is error\n");
            return;
        }
        //#1.0 save the attribute into list
        addOneComponent(attrs);
        //#1.1 push the current component into stack
        std::string str = xmlComponent;
        mNodeStack.push_back(str);
        return;
    }

    //#2 check it with "MimeType"
    if (!strcmp(name, xmlMime)){
        if (mNodeStack.empty()){
            ERROR("the cow plugins xml file has error format(mime)\n");
            return;
        }
        if (mNodeStack[0] != xmlComponents)
        {
            ERROR("find the xml file header is error2\n");
            return;
        }
        if (mNodeStack[1] != xmlComponent)
        {
            ERROR("find the xml file header is error3\n");
            return;
        }
        addOneMime(attrs);
        return;
    }

    ERROR("find UNKNOWN block in the xml file header: %s", name);
    return;
}

void CowComponentXML::endElementHandler(const char *name)
{
    if (!strcmp(name, xmlPath)){
        return;
    }

    if (!strcmp(name, xmlComponents)){
        if ((mNodeStack.size() != 1) || (mNodeStack[0] != xmlComponents)){
            ERROR("End Element Components is error\n");
            return;
        }
        mNodeStack.pop_back();
        return;
    }

    //#1 check it with "</Component>"
    if (!strcmp(name, xmlComponent)){
        if ((mNodeStack[mNodeStack.size() - 1] != xmlComponent)){
            ERROR("End Element Component is error\n");
            return;
        }
        mNodeStack.pop_back();
        return;
    }

    if (!strcmp(name, xmlMime)){
        return;
    }

    ERROR("End Element Component:unknown block:%s", name);
    return;
}

bool CowComponentXML::addOneComponent(const char **attrs)
{
    const char *pLibComponentName = NULL;
    const char *pComponentName = NULL;
    const char *pLibPath = mLibPath.c_str();

    size_t i = 0;
    //#0 find the LibComponentName && ComponentName
    while (attrs[i] != NULL) {
        if (!strcmp(attrs[i], xmlCompLibName)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            pLibComponentName = attrs[i + 1];
            ++i;
        } else if (!strcmp(attrs[i], xmlCompName)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            pComponentName = attrs[i + 1];
            ++i;
        } else if (!strcmp(attrs[i], xmlCompLibPath)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            pLibPath = attrs[i + 1];
            ++i;
        } else
            return false;
        ++i;
    }//while (attrs[i] != NULL)

    if (!pLibComponentName || !pComponentName || !pLibPath){
        return false;
    }

    //#1 save the LibComponentName && ComponentName
    ComNameLibName cnlc;
    cnlc.mLibName = pLibPath;
    cnlc.mLibName.append("/lib");
    cnlc.mLibName.append(pLibComponentName);
    cnlc.mLibName.append(".so");
    cnlc.mComName = pComponentName;

    DEBUG("got lib %s support component: %s\n", pLibComponentName, pComponentName);
    mVComNameLibName.push_back(cnlc);

    return true;
}

bool CowComponentXML::addOneMime(const char **attrs)
{
    const char *MimeType = NULL;
    const char *Priority = NULL;
    const char *Cap = NULL;

    size_t i = 0;
    //#0 find the LibComponentName && ComponentName
    while (attrs[i] != NULL) {
        if (!strcmp(attrs[i], xmlMimeType)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            MimeType = attrs[i + 1];
            ++i;
        } else if (!strcmp(attrs[i], xmlPriority)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            Priority = attrs[i + 1];
            ++i;
        } else if (!strcmp(attrs[i], xmlCap)) {
            if (attrs[i + 1] == NULL) {
                return false;
            }
            Cap = attrs[i + 1];
            ++i;
        } else
            return false;
        ++i;
    }//while (attrs[i] != NULL)

    if ((!MimeType) || (!Priority) || (Cap == NULL))
        return false;

    //#1 push the MimeType && ComponentName input the number
    MimeTypeComName mtcn;

    mtcn.mMimeType = MimeType;

    if (!strcmp(Priority, xmlPriorityLow))
        mtcn.mPriority = LOW_PRIORITY;
    else if (!strcmp(Priority, xmlPriorityNormal))
         mtcn.mPriority = DEFAULT_PRIORITY;
    else if (!strcmp(Priority, xmlPriorityHigh))
         mtcn.mPriority = HIGH_PRIORITY;
    else if (!strcmp(Priority, xmlPriorityDisable))
         mtcn.mPriority = DISABLE_PRIORITY;
    else {
         ERROR("xml file priority error:%s",Priority);
         return false;
    }

    if(!strcmp(Cap, xmlEnAndDecoder))
        mtcn.mCap = XML_COMP_CODECS;
    else if(!strcmp(Cap, xmlEncoder))
         mtcn.mCap = XML_COMP_ENCODER;
    else if(!strcmp(Cap, xmlDecoder))
         mtcn.mCap = XML_COMP_DECODER;
    else if(!strcmp(Cap,xmlGeneric))
         mtcn.mCap = XML_COMP_GENERIC;
    else {
         ERROR("xml file codec info error:%s\n",Cap);
         return false;
    }

    mtcn.mComName = mVComNameLibName.back().mComName;

    mVMimeTypeComName.push_back(mtcn);

    return true;
}

std::vector<ComNameLibName>   &CowComponentXML::getComNameLibNameTable()
{
    return mVComNameLibName;
}

std::vector<MimeTypeComName>     &CowComponentXML::getMimeTypeComNameTable()
{
    return mVMimeTypeComName;
}

/*static*/void CowComponentXML::Release(CowComponentXML * pXML)
{
    if (pXML){
      INFO("now begin to unload xml file\n");
      pXML->unInit();
    }
    delete pXML;
}

}
