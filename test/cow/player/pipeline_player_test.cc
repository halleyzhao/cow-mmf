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
#include "pipeline_player_test.h"
#include "multimedia/component_factory.h"
#include "multimedia/media_attr_str.h"
#include "multimedia/mm_debug.h"

namespace YUNOS_MM {
MM_LOG_DEFINE_MODULE_NAME("COW-PLPT")
#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

typedef struct  {
    int32_t index;
    MediaMetaSP meta;
} ComponentAttr;

class ComponentInfo{
  public:
    ComponentInfo() : isEncoder(false) {}
    virtual ~ComponentInfo() { index = -1; }
    int32_t index;      // unique index in pipeline
    std::string name;   // component name
    std::string mime;   // mimetype
    bool isEncoder;
    std::vector<int32_t> pulls;       // pull data from this componentm: addSource
    std::vector<int32_t> pushes;       // push data to this component, addSink
};
static std::vector<ComponentAttr> compAttrs;

static const char *pipeGraphFile = "/data/pipeline.cfg";
static std::vector<ComponentInfo> compInfos;

void parseLinkedComps(char* comps, std::vector<int32_t> &components)
{
    char tmp[20];
    char *ptr = comps, *saveptr = NULL, *gotptr = NULL;
    memset(tmp, 0, sizeof(tmp));
    int ret = sscanf(comps, "(%[^)])", tmp);
    if (ret == 1)
        ptr = tmp;
    gotptr = strtok_r(ptr, ":", &saveptr);
    while (gotptr) {
        int pull = atoi(gotptr);
        if (pull >=0)
            components.push_back(pull);
        gotptr = strtok_r(NULL, ":", &saveptr);
    }
}

static bool parsePipelineInfo()
{
    FILE *fp = NULL;

    fp = fopen(pipeGraphFile, "r");
    if (!fp)
        return false;

    #define BUFFER_SIZE     1023
    #define MAX_LINE_SIZE   128
    char buffer[BUFFER_SIZE+1]; // plus one char for '\0'
    char* bufferFillStart = buffer;
    uint32_t bufferEmptySize = BUFFER_SIZE;
    bool readToEOF = false;
    enum {
        GRAPH_SECTION,
        ATTRIBUTES_SECTION,
    } sectionMode = GRAPH_SECTION;

    while(1) {
        // read file to buffer
        uint32_t readSize = fread(bufferFillStart, 1, bufferEmptySize, fp);
        if (readSize < bufferEmptySize)
            readToEOF = true;
        ASSERT(bufferFillStart+readSize <= &buffer[BUFFER_SIZE+1]);
        *(bufferFillStart+readSize) = 0; // set '\0'

        // parse the data in buffer
        char *pos = NULL;
        char *line = strtok_r(buffer, "\n", &pos);
        while(1) {
            DEBUG("one line data: %s", line);
            if (!line)
                break;
            if (*line == '\r') // ignore '\r'
                line++;

            do { // make it easier to continue next line reading, without goto
                if ((line[0] == '/' && line[1] == '/'))
                    break; // comment, ignore

                if (!strcmp(line, "GRAPH:")) {
                    sectionMode = GRAPH_SECTION;
                    break;
                } else if (!strcmp(line, "ATTRIBUTES:")) {
                    sectionMode = ATTRIBUTES_SECTION;
                    break;
                }
                #define BLANK "%*[ ]"
                int ret = -1;
                if (sectionMode == GRAPH_SECTION) {
                    int index = -1;
                    char compName[20];
                    char mimeType[20];
                    char isEncoder[10];
                    char pulls[20];
                    char pushes[20];

                    memset(compName, 0, sizeof(compName));
                    memset(mimeType, 0, sizeof(mimeType));
                    memset(isEncoder, 0, sizeof(isEncoder));
                    memset(pulls, 0, sizeof(pulls));
                    memset(pushes, 0, sizeof(pushes));
                    ret = sscanf(line, "{" BLANK "%d," BLANK "%[^,]," BLANK "%[^,]," BLANK "%[^,]," BLANK "%[^,]," BLANK "%[^}]}",
                        &index, compName, mimeType, isEncoder, pulls, pushes);
                    DEBUG("ret: %d, index: %d, compName: %s, mimeType: %s, isEncoder: %s, pulls: %s, pushes: %s",
                        ret, index, compName, mimeType, isEncoder, pulls, pushes);
                    if (ret < 6) {
                        ERROR("incorrect line data: %s", line);
                        fclose(fp);
                        return false;
                    }

                    ComponentInfo compInfo;
                    compInfo.index = index;
                    compInfo.name = compName;
                    compInfo.mime = mimeType;
                    compInfo.isEncoder = !strcmp(isEncoder, "true");

                    // parse uplink/downlink components (self in master mode)
                    parseLinkedComps(pulls, compInfo.pulls);
                    parseLinkedComps(pushes, compInfo.pushes);

                    compInfos.push_back(compInfo);
                }else if (sectionMode == ATTRIBUTES_SECTION) {
                    int index = -1;
                    // FIXME, boundary check
                    char name[20];
                    char type[20];
                    char value[20];
                    memset(name, 0, sizeof(name));
                    memset(type, 0, sizeof(type));
                    memset(value, 0, sizeof(value));

                    ret = sscanf(line, "{" BLANK "%d," BLANK "%[^,]," BLANK "%[^,]," BLANK "%[^}],", &index, name, type, value);
                    DEBUG("ret: %d, index: %d, name: %s, type: %s, value: %s", ret, index, name, type, value);
                    if (ret < 4) {
                        ERROR("incorrect line data: %s", line);
                        break;
                    }
                    ComponentAttr attr;
                    attr.index = index;
                    MediaMetaSP meta = MediaMeta::create();
                    if (!strcasecmp(type, "STRING")) {
                        meta->setString(name, value);
                    } else if (!strcasecmp(type, "INT32")) {
                        meta->setInt32(name, atoi(value));
                    } else if (!strcasecmp(type, "INT64")) {
                        meta->setInt32(name, atoi(value));
                    }
                    attr.meta = meta;
                    meta->dump();
                    compAttrs.push_back(attr);
                }
                #undef BLANK
            }while (0);

            if (!readToEOF) {
                if (pos + MAX_LINE_SIZE > &buffer[BUFFER_SIZE]) // may not enough data for one complete line
                    break;
            }

            line = strtok_r(NULL, "\n", &pos);
        }


        if (readToEOF)
            break;

        bufferEmptySize = pos - buffer;
        uint32_t remainingDataSize = BUFFER_SIZE - bufferEmptySize;
        memcpy(buffer, pos, remainingDataSize);
        bufferFillStart = buffer + remainingDataSize;
    }

    fclose(fp);
    return true;
}

mm_status_t PipelinePlayerTest::prepareInternal()
{
    FUNC_TRACK();
    mm_status_t status = MM_ERROR_SUCCESS;

    compInfos.reserve(5);
    if (!parsePipelineInfo()) {
        ERROR("fail to parse pipeline config file: %s", pipeGraphFile);
        return MM_ERROR_NO_PIPELINE;
    }

    // MMAutoLock locker(mLock); NO big lock
    setState(mState, kComponentStatePreparing);
    PlaySourceComponent* source = getSourceComponent();
    ASSERT_RET(source, MM_ERROR_NO_COMPONENT);

    status = source->prepare();
    ASSERT_RET(status == MM_ERROR_SUCCESS || status == MM_ERROR_ASYNC, status);
    if (status == MM_ERROR_ASYNC) {
        status = waitUntilCondition(mComponents[mDemuxIndex].state, kComponentStatePrepared, false/*pipeline state*/);
    }
    if (status != MM_ERROR_SUCCESS)
        return status;

    status = updateTrackInfo();
    ASSERT_RET(status == MM_ERROR_SUCCESS, status);

    std::map<int32_t, int32_t> compIndexMap;
    uint32_t i = 0;

    // AVDemuxer index
    compIndexMap[0] = 0;
    for (i=0; i<compInfos.size(); i++) {
        ComponentSP comp = createComponentHelper(compInfos[i].name.c_str(), compInfos[i].mime.c_str());
        ASSERT(comp);
        mComponents.push_back(ComponentInfo(comp, ComponentInfo::kComponentTypeSource));
        compIndexMap[compInfos[i].index] = i+1;
    }

    for (i=0; i<compInfos.size(); i++) {
        Component::MediaType mediaType = Component::kMediaTypeVideo;
        DEBUG("config component: %s", mComponents[i+1].component->name());
        if (!strncmp(compInfos[i].mime.c_str(), "audio/", strlen("audio/")))
            mediaType = Component::kMediaTypeAudio;
        uint32_t j=0;
        for (j=0; j<compInfos[i].pulls.size(); j++) {
            std::map<int32_t, int32_t>::iterator it = compIndexMap.find(compInfos[i].pulls[j]);
            ASSERT(it != compIndexMap.end());
            DEBUG("i+1: %d, compInfos[i].pulls: %d, source data from: %d, %s", i+1, compInfos[i].pulls[j], it->second, mComponents[it->second].component->name());
            status = mComponents[i+1].component->addSource(mComponents[it->second].component.get(), mediaType);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }
        for (j=0; j<compInfos[i].pushes.size(); j++) {
            std::map<int32_t, int32_t>::iterator it = compIndexMap.find(compInfos[i].pushes[j]);
            ASSERT(it != compIndexMap.end());
            DEBUG("i+1: %d, compInfos[i].pushes: %d, sink data to: %d", i+1, compInfos[i].pushes[j], it->second);
            status = mComponents[i+1].component->addSink(mComponents[it->second].component.get(), mediaType);
            ASSERT_RET(status == MM_ERROR_SUCCESS, MM_ERROR_COMPONENT_CONNECT_FAILED);
        }
    }

    for (i=0; i<compAttrs.size(); i++) {
        std::map<int32_t, int32_t>::iterator it = compIndexMap.find(compAttrs[i].index);
        ASSERT(it != compIndexMap.end());
        mComponents[it->second].component->setParameter(compAttrs[i].meta);
    }

    mMediaMeta->setInt32(MEDIA_ATTR_VARIABLE_RATE_SUPPORT, false);
    if (mSurface) {
        if (mIsSurfaceTexture)
            mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE_TEXTURE, mSurface);
        else
            mMediaMeta->setPointer(MEDIA_ATTR_VIDEO_SURFACE, mSurface);
    }

    MMAutoLock locker(mLock);
    //protect mSurface
    for (i=0; i<compInfos.size(); i++) {
            mComponents[i].component->setParameter(mMediaMeta);
    }

    // FIXME
#if 0
    if (videoSink) {
        ClockSP clock = audioSink->provideClock();
        videoSink->setClock(clock);
    }
#endif

    compAttrs.clear();
    compInfos.clear();

    return status;
}

} // YUNOS_MM
